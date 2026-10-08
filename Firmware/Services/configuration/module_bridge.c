#include "module_bridge.h"

#include "configuration_service.h"
#include "reader_settings.h"
#include "../../Application/Silion/sillion_application.h"
#include "../../Application/Silion/sillion_application_port.h"
#include "../../BSP/industrial_io.h"
#include "../../Config/product_config.h"
#include "../../Devices/w5500/w5500.h"
#include "../../Protocols/silion/silion_protocol.h"
#include "../../Services/GPIO/gpio_service.h"
#include "../../Services/host/host_transport.h"
#include "../../Services/Time/time_service.h"

#include <stddef.h>
#include <string.h>

/*
 * Vendor "ARM7 mainboard" emulation on the configuration port, as used
 * by ModuleAPI.dll (Reader Manager, ReaderSense):
 *
 *   Module frame (forwarded):   FF LEN CMD DATA[LEN] CRC16
 *   Module reply (relayed):     FF LEN CMD ST_HI ST_LO DATA[LEN] CRC16
 *
 * Mainboard commands answered locally:
 *   "IOGET"                     -> "IOGET" + GPI bitmap (bit0 = GPI1)
 *   "IOSET" 01 pin state        -> "IOSETOK"
 *   "GIO"   (during async read) -> FF 04 AA 00 00 'G' 'I' 'O' bitmap CRC
 *   "SIO"  01 pin state (async) -> FF 03 AA 00 00 'S' 'I' 'O' CRC
 *   "VER"                       -> 4 bytes mainboard version a.b.c.d
 *   "IPGET"                     -> "IPGET" ip mask gw mac port(BE16)
 *   "IPSET" ip mask gw mac port -> no reply, applied after 1.5 s
 *   32 x FF (module reset)      -> discarded
 *
 * Local replies are inserted between module frames only, so a reply
 * never splits a tag frame of a running async inventory.
 *
 * The first module frame of a session taken from local use is the
 * "Moduletech" Stop Async Inventory frame. The module is already
 * stopped at that point, so that frame is consumed locally (no reply)
 * to avoid a late module reply being read as the next answer.
 */

#define MODULE_BRIDGE_HOST_BUFFER_SIZE   1024U
#define MODULE_BRIDGE_RELAY_CHUNK_SIZE   1024U
#define MODULE_BRIDGE_RELEASE_DELAY_MS   5000U
#define MODULE_BRIDGE_FRAME_OVERHEAD     5U
#define MODULE_BRIDGE_INJECT_SIZE        32U
#define MODULE_BRIDGE_INJECT_TIMEOUT_MS  100U
#define MODULE_BRIDGE_IPSET_LENGTH       25U
#define MODULE_BRIDGE_IPSET_APPLY_MS     1500U

/* A COM port gives no "closed" event (the tools do not use DTR), so a
 * session on a serial link (USB CDC or RS232/RS485) ends after this long
 * without traffic in either direction. The next module frame or board
 * command simply starts a new one. */
#define MODULE_BRIDGE_LINK_IDLE_MS       30000U

/* Only one session (TCP or a serial link) owns the module. A session on
 * another transport takes it over once the PC of the current session has
 * sent nothing for this long: the tools poll continuously while
 * connected, so only an abandoned session goes quiet. Module output is
 * not counted, so a tag stream left running does not hold the session. */
#define MODULE_BRIDGE_TAKEOVER_IDLE_MS   3000U

/* g_transport holds this for the TCP configuration port, otherwise the
 * HOST_TRANSPORT_USB / HOST_TRANSPORT_SERIAL link of the session. */
#define MODULE_BRIDGE_TRANSPORT_TCP      0xFFU

static const uint8_t g_stopAsyncFrame[19] =
{
    0xFFU, 0x0EU, 0xAAU, 0x4DU, 0x6FU, 0x64U, 0x75U, 0x6CU, 0x65U, 0x74U,
    0x65U, 0x63U, 0x68U, 0xAAU, 0x49U, 0xF3U, 0xBBU, 0x03U, 0x91U
};

static uint8_t g_hostRx[MODULE_BRIDGE_HOST_BUFFER_SIZE];
static uint16_t g_hostLength = 0U;
static uint8_t g_relay[MODULE_BRIDGE_RELAY_CHUNK_SIZE];

static uint8_t g_sessionActive = 0U;
static uint8_t g_moduleOwned = 0U;
static uint8_t g_consumeFirstStop = 0U;
static uint32_t g_sessionEndMs = 0U;
static uint8_t g_transport = MODULE_BRIDGE_TRANSPORT_TCP;
static uint32_t g_lastActivityMs = 0U;
static uint32_t g_lastHostByteMs = 0U;

/* Module -> PC frame tracker (where frame boundaries are). */
typedef enum
{
    MODULE_TRACK_IDLE = 0,
    MODULE_TRACK_LENGTH,
    MODULE_TRACK_BODY
} Module_Bridge_Track_t;

static Module_Bridge_Track_t g_trackState = MODULE_TRACK_IDLE;
static uint16_t g_trackRemaining = 0U;
static uint32_t g_lastModuleByteMs = 0U;

/* Pending local reply waiting for a frame boundary. */
static uint8_t g_inject[MODULE_BRIDGE_INJECT_SIZE];
static uint8_t g_injectLength = 0U;
static uint32_t g_injectSinceMs = 0U;

static void Module_Bridge_SendToHost(const uint8_t *data, uint16_t length)
{
    uint16_t sent;
    uint8_t attempt;

    g_lastActivityMs = Time_GetTickMs();

    if(g_transport != MODULE_BRIDGE_TRANSPORT_TCP)
    {
        /* Queued on the link and sent in the background. The relay
         * never reads more than the queue can take. */
        (void)HOST_Transport_LinkSend(g_transport, data, length);
        return;
    }

    for(attempt = 0U; (attempt < 20U) && (length > 0U); attempt++)
    {
        sent = W5500_Socket1_Send(data, length);
        data = &data[sent];
        length = (uint16_t)(length - sent);
    }
}

static uint16_t Module_Bridge_HostTxFree(void)
{
    if(g_transport != MODULE_BRIDGE_TRANSPORT_TCP)
    {
        return HOST_Transport_LinkGetFreeSpace(g_transport);
    }

    return W5500_Socket1_GetTxFreeSize();
}

static void Module_Bridge_TrackByte(uint8_t byte)
{
    switch(g_trackState)
    {
        case MODULE_TRACK_IDLE:
            if(byte == 0xFFU)
            {
                g_trackState = MODULE_TRACK_LENGTH;
            }
            break;

        case MODULE_TRACK_LENGTH:
            /* CMD + STATUS(2) + DATA[LEN] + CRC(2) */
            g_trackRemaining = (uint16_t)(byte + 5U);
            g_trackState = MODULE_TRACK_BODY;
            break;

        default:
            g_trackRemaining--;
            if(g_trackRemaining == 0U)
            {
                g_trackState = MODULE_TRACK_IDLE;
            }
            break;
    }
}

static void Module_Bridge_FlushInject(void)
{
    if(g_injectLength != 0U)
    {
        Module_Bridge_SendToHost(g_inject, g_injectLength);
        g_injectLength = 0U;
    }
}

/* Queue a local reply; it is sent at the next module frame boundary. */
static void Module_Bridge_Reply(const uint8_t *data, uint8_t length)
{
    if(length > sizeof(g_inject))
    {
        return;
    }

    /* A previous reply still waiting is sent first (rare). */
    Module_Bridge_FlushInject();

    memcpy(g_inject, data, length);
    g_injectLength = length;
    g_injectSinceMs = Time_GetTickMs();

    if(g_trackState == MODULE_TRACK_IDLE)
    {
        Module_Bridge_FlushInject();
    }
}

/* Async reply frame: FF LEN AA 00 00 payload CRC (ModuleAPI parser). */
static void Module_Bridge_ReplyAsyncFrame(const uint8_t *payload, uint8_t payloadLength)
{
    uint8_t frame[MODULE_BRIDGE_INJECT_SIZE];
    uint16_t crc;

    if((uint16_t)(payloadLength + 7U) > sizeof(frame))
    {
        return;
    }

    frame[0] = 0xFFU;
    frame[1] = payloadLength;
    frame[2] = 0xAAU;
    frame[3] = 0x00U;
    frame[4] = 0x00U;
    memcpy(&frame[5], payload, payloadLength);

    crc = SILION_CalculateCRC(&frame[1], (uint16_t)(payloadLength + 4U));
    frame[5U + payloadLength] = (uint8_t)(crc >> 8U);
    frame[6U + payloadLength] = (uint8_t)(crc & 0xFFU);

    Module_Bridge_Reply(frame, (uint8_t)(payloadLength + 7U));
}

static void Module_Bridge_Consume(uint16_t count)
{
    if(count >= g_hostLength)
    {
        g_hostLength = 0U;
        return;
    }

    memmove(g_hostRx, &g_hostRx[count], (size_t)(g_hostLength - count));
    g_hostLength = (uint16_t)(g_hostLength - count);
}

/* Returns 2 = full match, 1 = buffer is a prefix (wait), 0 = no match. */
static uint8_t Module_Bridge_MatchAscii(const char *command)
{
    uint16_t commandLength = (uint16_t)strlen(command);
    uint16_t compareLength;

    compareLength = (g_hostLength < commandLength) ? g_hostLength : commandLength;

    if(memcmp(g_hostRx, command, compareLength) != 0)
    {
        return 0U;
    }

    return (g_hostLength >= commandLength) ? 2U : 1U;
}

static uint8_t Module_Bridge_GpiBitmap(void)
{
    uint8_t states[INDUSTRIAL_IO_COUNT];
    uint8_t bitmap = 0U;
    uint8_t i;

    Industrial_IO_GetAllInputs(states);

    for(i = 0U; i < INDUSTRIAL_IO_COUNT; i++)
    {
        if(states[i] != 0U)
        {
            bitmap |= (uint8_t)(1U << i);
        }
    }

    /* Never 0x80: ModuleAPI4 reads that as "reader busy in active mode". */
    return (uint8_t)(bitmap & 0x0FU);
}

static void Module_Bridge_SetGpo(uint8_t pin, uint8_t state)
{
    uint8_t outputs[1];
    uint8_t states[1];
    uint32_t durations[1] = { 0U };
    uint32_t errorCode = 0U;
    const char *errorString = NULL;

    /* GPO 5 (buzzer on reference readers) has no physical output here. */
    if((pin < 1U) || (pin > INDUSTRIAL_IO_COUNT))
    {
        return;
    }

    outputs[0] = pin;
    states[0] = (state != 0U) ? 1U : 0U;

    if(
        GPIO_Service_SetOutputs(outputs, states, durations, 1U, &errorCode, &errorString)
        != GPIO_SERVICE_OK
    )
    {
        /* A timed pulse from the Identium protocol is running: drive the
         * pin directly so the tool's request still takes effect. */
        (void)Industrial_IO_SetOutput(pin, states[0]);
    }
}

static void Module_Bridge_ReplyIpGet(void)
{
    const ReaderSettings_t *settings = ReaderSettings_Get();
    uint8_t reply[MODULE_BRIDGE_IPSET_LENGTH];

    memcpy(reply, "IPGET", 5U);
    memcpy(&reply[5], settings->ip, 4U);
    memcpy(&reply[9], settings->netmask, 4U);
    memcpy(&reply[13], settings->gateway, 4U);
    memcpy(&reply[17], settings->mac, 6U);
    reply[23] = (uint8_t)(settings->configPort >> 8U);
    reply[24] = (uint8_t)(settings->configPort & 0xFFU);

    Module_Bridge_Reply(reply, sizeof(reply));
}

static void Module_Bridge_HandleIpSet(const uint8_t *request)
{
    ReaderSettings_t settings = *ReaderSettings_Get();
    const uint8_t *mac = &request[17];

    memcpy(settings.ip, &request[5], 4U);
    memcpy(settings.netmask, &request[9], 4U);
    memcpy(settings.gateway, &request[13], 4U);

    /* The tools invent "FC FF" + timestamp when no MAC was entered; keep
     * the reader's own MAC in that case (and never accept multicast). */
    if(
        !((mac[0] == 0xFCU) && (mac[1] == 0xFFU)) &&
        ((mac[0] & 0x01U) == 0U) &&
        ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0U)
    )
    {
        memcpy(settings.mac, mac, 6U);
    }

    /* The listening port stays 8080 (the tools always connect there). */

    if(ReaderSettings_Save(&settings) != 0U)
    {
        /* The PC does not wait for a reply; it sleeps 1-4 s and reconnects
         * at the new address. */
        ReaderSettings_RequestNetworkApply(MODULE_BRIDGE_IPSET_APPLY_MS);
    }
}

static void Module_Bridge_ProcessHostBytes(void)
{
    uint16_t frameLength;
    uint8_t match;

    while(g_hostLength > 0U)
    {
        if(g_hostRx[0] == 0xFFU)
        {
            if(g_hostLength < 2U)
            {
                return;
            }

            /* 32 x FF module-reset burst (ModuleAPI ResetModule). The
             * bridge only forwards whole frames, so the module parser
             * cannot be stuck; drop the burst. */
            if(g_hostRx[1] == 0xFFU)
            {
                Module_Bridge_Consume(1U);
                continue;
            }

            frameLength = (uint16_t)(g_hostRx[1] + MODULE_BRIDGE_FRAME_OVERHEAD);

            if(g_hostLength < frameLength)
            {
                return;
            }

            if(
                (g_consumeFirstStop != 0U) &&
                (frameLength == sizeof(g_stopAsyncFrame)) &&
                (memcmp(g_hostRx, g_stopAsyncFrame, sizeof(g_stopAsyncFrame)) == 0)
            )
            {
                g_consumeFirstStop = 0U;
                Module_Bridge_Consume(frameLength);
                continue;
            }

            g_consumeFirstStop = 0U;

            /* Module TX still busy: retry on the next pass. */
            if(SILION_Application_BridgeSend(g_hostRx, frameLength) == 0U)
            {
                return;
            }

            Module_Bridge_Consume(frameLength);
            continue;
        }

        /* "IOGET" and "IOSET" share their first two letters. */
        match = Module_Bridge_MatchAscii("IOGET");
        if(match == 2U)
        {
            uint8_t reply[6];

            Module_Bridge_Consume(5U);
            memcpy(reply, "IOGET", 5U);
            reply[5] = Module_Bridge_GpiBitmap();
            Module_Bridge_Reply(reply, sizeof(reply));
            continue;
        }

        if(Module_Bridge_MatchAscii("IOSET") != 0U)
        {
            if(g_hostLength < 8U)
            {
                return;
            }

            Module_Bridge_SetGpo(g_hostRx[6], g_hostRx[7]);
            Module_Bridge_Consume(8U);
            Module_Bridge_Reply((const uint8_t *)"IOSETOK", 7U);
            continue;
        }

        if(match == 1U)
        {
            return;   /* "I", "IO" ... still arriving */
        }

        match = Module_Bridge_MatchAscii("IPGET");
        if(match == 2U)
        {
            Module_Bridge_Consume(5U);
            Module_Bridge_ReplyIpGet();
            continue;
        }

        if(Module_Bridge_MatchAscii("IPSET") != 0U)
        {
            if(g_hostLength < MODULE_BRIDGE_IPSET_LENGTH)
            {
                return;
            }

            Module_Bridge_HandleIpSet(g_hostRx);
            Module_Bridge_Consume(MODULE_BRIDGE_IPSET_LENGTH);
            continue;
        }

        if(match == 1U)
        {
            return;
        }

        match = Module_Bridge_MatchAscii("GIO");
        if(match == 2U)
        {
            uint8_t payload[4];

            Module_Bridge_Consume(3U);
            payload[0] = 'G';
            payload[1] = 'I';
            payload[2] = 'O';
            payload[3] = Module_Bridge_GpiBitmap();
            Module_Bridge_ReplyAsyncFrame(payload, sizeof(payload));
            continue;
        }
        if(match == 1U)
        {
            return;
        }

        match = Module_Bridge_MatchAscii("SIO");
        if(match != 0U)
        {
            static const uint8_t payload[3] = { 'S', 'I', 'O' };

            if(g_hostLength < 6U)
            {
                return;
            }

            Module_Bridge_SetGpo(g_hostRx[4], g_hostRx[5]);
            Module_Bridge_Consume(6U);
            Module_Bridge_ReplyAsyncFrame(payload, sizeof(payload));
            continue;
        }

        match = Module_Bridge_MatchAscii("VER");
        if(match == 2U)
        {
            static const uint8_t reply[4] =
            {
                PRODUCT_MAINBOARD_VERSION_0,
                PRODUCT_MAINBOARD_VERSION_1,
                PRODUCT_MAINBOARD_VERSION_2,
                PRODUCT_MAINBOARD_VERSION_3
            };

            Module_Bridge_Consume(3U);
            Module_Bridge_Reply(reply, sizeof(reply));
            continue;
        }
        if(match == 1U)
        {
            return;
        }

        /* Unsupported mainboard command (e.g. INITUPFV firmware update,
         * CONFIGRX/WX, SM6E) or noise: resynchronise. */
        Module_Bridge_Consume(1U);
    }
}

static void Module_Bridge_RelayModuleBytes(void)
{
    uint16_t freeSize;
    uint16_t count;
    uint16_t i;
    uint16_t split;
    uint8_t pass;
    uint32_t now = Time_GetTickMs();

    /* Fast (async) inventory streams continuously at full UART speed;
     * drain everything queued in this loop pass, in large chunks. */
    for(pass = 0U; pass < 4U; pass++)
    {
        freeSize = Module_Bridge_HostTxFree();

        if(freeSize > sizeof(g_relay))
        {
            freeSize = sizeof(g_relay);
        }

        if(freeSize <= MODULE_BRIDGE_INJECT_SIZE)
        {
            break;
        }

        count = SILION_ReadRawBytes(g_relay, (uint16_t)(freeSize - MODULE_BRIDGE_INJECT_SIZE));

        if(count == 0U)
        {
            break;
        }

        g_lastModuleByteMs = now;
        split = count;

        for(i = 0U; i < count; i++)
        {
            Module_Bridge_TrackByte(g_relay[i]);

            if(
                (g_injectLength != 0U) &&
                (split == count) &&
                (g_trackState == MODULE_TRACK_IDLE)
            )
            {
                split = (uint16_t)(i + 1U);
            }
        }

        if(split < count)
        {
            /* Pending reply goes exactly at the frame boundary. */
            Module_Bridge_SendToHost(g_relay, split);
            Module_Bridge_FlushInject();
            Module_Bridge_SendToHost(&g_relay[split], (uint16_t)(count - split));
        }
        else
        {
            Module_Bridge_SendToHost(g_relay, count);

            if(g_trackState == MODULE_TRACK_IDLE)
            {
                Module_Bridge_FlushInject();
            }
        }
    }

    /* A tracker that lost sync (dropped bytes) must not hold a reply
     * forever: once the module has been quiet, send it anyway. */
    if(
        (g_injectLength != 0U) &&
        ((now - g_injectSinceMs) >= MODULE_BRIDGE_INJECT_TIMEOUT_MS) &&
        ((now - g_lastModuleByteMs) >= 20U)
    )
    {
        g_trackState = MODULE_TRACK_IDLE;
        Module_Bridge_FlushInject();
    }
}

static uint8_t Module_Bridge_BeginOn(uint8_t transport, const uint8_t *data, uint16_t length)
{
    if(length > sizeof(g_hostRx))
    {
        return 0U;
    }

    if(g_sessionActive != 0U)
    {
        if(
            (transport == g_transport) ||
            ((Time_GetTickMs() - g_lastHostByteMs) < MODULE_BRIDGE_TAKEOVER_IDLE_MS)
        )
        {
            return 0U;   /* the current session is still in use */
        }

        /* Abandoned session: drop it. A TCP client is disconnected; the
         * configuration port reopens the listening socket. */
        if(g_transport == MODULE_BRIDGE_TRANSPORT_TCP)
        {
            (void)W5500_Socket1_Close();
        }

        Module_Bridge_End();
    }

    if(g_moduleOwned == 0U)
    {
        if(SILION_Application_BridgeAcquire() == 0U)
        {
            return 0U;
        }

        g_moduleOwned = 1U;
        g_consumeFirstStop = 1U;
    }
    else
    {
        /* Reused within the grace period: the PC may have left the
         * module inventorying, so its STOP must reach the module. */
        g_consumeFirstStop = 0U;
    }

    if((data != NULL) && (length > 0U))
    {
        memcpy(g_hostRx, data, length);
    }

    g_hostLength = length;
    g_sessionActive = 1U;
    g_transport = transport;
    g_lastActivityMs = Time_GetTickMs();
    g_lastHostByteMs = g_lastActivityMs;
    g_trackState = MODULE_TRACK_IDLE;
    g_injectLength = 0U;

    Module_Bridge_ProcessHostBytes();

    return 1U;
}

uint8_t Module_Bridge_Begin(const uint8_t *data, uint16_t length)
{
    return Module_Bridge_BeginOn(MODULE_BRIDGE_TRANSPORT_TCP, data, length);
}

static uint8_t Module_Bridge_IsSerialLink(uint8_t link)
{
    return ((link == HOST_TRANSPORT_USB) || (link == HOST_TRANSPORT_SERIAL)) ? 1U : 0U;
}

uint8_t Module_Bridge_BeginLink(uint8_t link, const uint8_t *data, uint16_t length)
{
    if(Module_Bridge_IsSerialLink(link) == 0U)
    {
        return 0U;
    }

    return Module_Bridge_BeginOn(link, data, length);
}

uint8_t Module_Bridge_IsLinkActive(uint8_t link)
{
    return (
        (g_sessionActive != 0U) &&
        (Module_Bridge_IsSerialLink(link) != 0U) &&
        (g_transport == link)
    ) ? 1U : 0U;
}

void Module_Bridge_PushLinkBytes(uint8_t link, const uint8_t *data, uint16_t length)
{
    uint16_t space;

    if(Module_Bridge_IsLinkActive(link) == 0U)
    {
        return;
    }

    g_lastActivityMs = Time_GetTickMs();
    g_lastHostByteMs = g_lastActivityMs;

    space =(uint16_t)(sizeof(g_hostRx) - g_hostLength);
    if(length > space)
    {
        length = space;
    }

    memcpy(&g_hostRx[g_hostLength], data, length);
    g_hostLength = (uint16_t)(g_hostLength + length);
}

uint8_t Module_Bridge_HasPendingHostBytes(void)
{
    return (g_hostLength != 0U) ? 1U : 0U;
}

void Module_Bridge_EndLinkForLocalUse(uint8_t link)
{
    if(Module_Bridge_IsLinkActive(link) == 0U)
    {
        return;
    }

    Module_Bridge_End();

    /* Skip the reconnect grace period: the release runs on this poll. */
    g_sessionEndMs = Time_GetTickMs() - MODULE_BRIDGE_RELEASE_DELAY_MS;
    Module_Bridge_Poll();
}

void Module_Bridge_Task(void)
{
    uint16_t received;

    if(g_sessionActive == 0U)
    {
        return;
    }

    if(
        (g_transport == MODULE_BRIDGE_TRANSPORT_TCP) &&
        (g_hostLength < sizeof(g_hostRx))
    )
    {
        received = W5500_Socket1_Receive(
            &g_hostRx[g_hostLength],
            (uint16_t)(sizeof(g_hostRx) - g_hostLength)
        );

        g_hostLength = (uint16_t)(g_hostLength + received);

        if(received > 0U)
        {
            g_lastHostByteMs = Time_GetTickMs();
        }
    }

    Module_Bridge_ProcessHostBytes();

    /* A full buffer that cannot be parsed would stall the session. */
    if(g_hostLength >= sizeof(g_hostRx))
    {
        Module_Bridge_Consume(1U);
    }

    Module_Bridge_RelayModuleBytes();
}

void Module_Bridge_End(void)
{
    if(g_sessionActive == 0U)
    {
        return;
    }

    g_sessionActive = 0U;
    g_hostLength = 0U;
    g_consumeFirstStop = 0U;
    g_injectLength = 0U;
    g_trackState = MODULE_TRACK_IDLE;
    g_sessionEndMs = Time_GetTickMs();
}

uint8_t Module_Bridge_IsActive(void)
{
    /* TCP session (used by the configuration port). */
    return ((g_sessionActive != 0U) && (g_transport == MODULE_BRIDGE_TRANSPORT_TCP)) ? 1U : 0U;
}

void Module_Bridge_Poll(void)
{
    if(Module_Bridge_IsLinkActive(g_transport) != 0U)
    {
        Module_Bridge_Task();

        if((Time_GetTickMs() - g_lastActivityMs) >= MODULE_BRIDGE_LINK_IDLE_MS)
        {
            Module_Bridge_End();
        }
        return;
    }

    if(
        (g_moduleOwned == 0U) ||
        (g_sessionActive != 0U)
    )
    {
        return;
    }

    /* Between sessions: discard module output (e.g. tags from an
     * inventory the PC left running). */
    while(SILION_ReadRawBytes(g_relay, sizeof(g_relay)) > 0U)
    {
    }

    /* The tools reconnect several times while connecting, and open
     * separate one-shot connections for EE configuration commands, so
     * the module is handed back only after the PC has stayed away. */
    if((Time_GetTickMs() - g_sessionEndMs) >= MODULE_BRIDGE_RELEASE_DELAY_MS)
    {
        SILION_Application_BridgeRelease();
        g_moduleOwned = 0U;

        /* The tools may have switched the reader to active mode while it
         * was bridged: honour "start inventory at boot" in that case. */
        if(ReaderSettings_GetWorkState() == READER_SETTINGS_WORK_ACTIVE)
        {
            if(
                (SILION_Application_GetState() == SILION_APP_IDLE) &&
                (Configuration_Service_GetStartInventoryAtBoot() != 0U)
            )
            {
                (void)SILION_Application_StartInventory();
            }
        }
        else if(SILION_Application_IsInventoryRunning() != 0U)
        {
            (void)SILION_Application_StopInventory();
        }
    }
}
