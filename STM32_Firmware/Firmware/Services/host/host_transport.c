/*
 * host_transport.c
 *
 *  Created on: 28-Sept-2026
 *      Author: Identium
 */

#include "host_transport.h"

#include <stddef.h>
#include <string.h>

#include "../serial/serial_port.h"
#include "../USB/usb_cdc_transport.h"
#include "../ethernet/host_tcp_server.h"
#include "../wifi/wifi_service.h"
#include "../../Common/critical_section.h"
#include "../configuration/module_bridge.h"
#include "../configuration/configuration_port.h"
#include "../Time/time_service.h"


/* ============================================================
 * RX QUEUE
 * ============================================================
 */

static volatile uint8_t hostRxQueue[
    HOST_RX_QUEUE_SIZE
];

static volatile uint8_t hostRxSourceQueue[
    HOST_RX_QUEUE_SIZE
];

static volatile uint16_t hostRxHead = 0U;
static volatile uint16_t hostRxTail = 0U;

static volatile uint32_t hostRxOverflow = 0U;


/* ============================================================
 * TRANSPORT STATE
 * ============================================================
 */

static volatile uint8_t hostActiveTransport =
    HOST_TRANSPORT_NONE;

/* No released-transport blacklist. */

static volatile uint8_t hostTagTransport =
    HOST_TRANSPORT_NONE;


/* ============================================================
 * VENDOR-TOOL ROUTER ("COM route")
 *
 * Reader Manager / ReaderSense connect to a "COMx" address with the
 * same protocol they use on TCP 8080. That COM port is either the
 * native USB CDC port or the RS232/RS485 host port, and each link has
 * its own router. At the start of a line:
 *   FF ...                              -> module bridge session
 *   IOGET IOSET IPGET IPSET GIO SIO VER -> module bridge session
 *   EE 00 len cmd flags data [CRC]      -> configuration request
 * Replies go back on the link the request came from. Everything else
 * goes to the legacy ASCII parser unchanged. No legacy command starts
 * with one of these tokens.
 * ============================================================
 */

#define HOST_MUX_EE_MAX       (6U + 2048U + 2U)
#define HOST_MUX_TIMEOUT_MS   500U

typedef struct
{
    uint8_t link;
    uint8_t lineStart;
    uint8_t prefix[8];
    uint8_t prefixLength;
    uint8_t release[8];
    uint8_t releaseLength;
    uint8_t releasePos;
    uint8_t ee[HOST_MUX_EE_MAX];
    uint16_t eeLength;
    uint8_t eeActive;
    uint32_t lastByteMs;
} HOST_VendorMux_t;

static const char *const hostVendorTokens[] =
{
    "IOGET", "IOSET", "IPGET", "IPSET", "GIO", "SIO", "VER"
};

/* Zero-initialised (.bss); HOST_Transport_Init() sets them up. */
static HOST_VendorMux_t hostUsbMux;
static HOST_VendorMux_t hostSerialMux;

static void HOST_Mux_Init(HOST_VendorMux_t *mux, uint8_t link)
{
    memset(mux, 0, sizeof(*mux));
    mux->link = link;
    mux->lineStart = 1U;
}

static HOST_VendorMux_t *HOST_Mux_ForLink(uint8_t link)
{
    if(link == HOST_TRANSPORT_USB)
    {
        return &hostUsbMux;
    }

    if(link == HOST_TRANSPORT_SERIAL)
    {
        return &hostSerialMux;
    }

    return NULL;
}

static void HOST_Mux_Release(HOST_VendorMux_t *mux, const uint8_t *data, uint8_t length)
{
    memcpy(mux->release, data, length);
    mux->releaseLength = length;
    mux->releasePos = 0U;
}

/* A started token/frame that never completes is abandoned. */
static void HOST_Mux_CheckTimeout(HOST_VendorMux_t *mux)
{
    if(
        ((mux->prefixLength != 0U) || (mux->eeActive != 0U)) &&
        ((Time_GetTickMs() - mux->lastByteMs) >= HOST_MUX_TIMEOUT_MS)
    )
    {
        if(mux->prefixLength != 0U)
        {
            HOST_Mux_Release(mux, mux->prefix, mux->prefixLength);
            mux->prefixLength = 0U;
        }

        mux->eeActive = 0U;
        mux->eeLength = 0U;
        mux->lineStart = 1U;
    }
}

/* 0 = prefix of no token, 1 = prefix of a token, 2 = whole token. */
static uint8_t HOST_Mux_MatchToken(const uint8_t *prefix, uint8_t length)
{
    uint8_t i;
    uint8_t result = 0U;

    for(i = 0U; i < (sizeof(hostVendorTokens) / sizeof(hostVendorTokens[0])); i++)
    {
        size_t tokenLength = strlen(hostVendorTokens[i]);

        if(
            (length <= tokenLength) &&
            (memcmp(hostVendorTokens[i], prefix, length) == 0)
        )
        {
            if(length == tokenLength)
            {
                return 2U;
            }
            result = 1U;
        }
    }

    return result;
}

/* A module-bridge session is open on this link. Module frames and board
 * commands go to the bridge. A printable byte at a frame boundary that
 * cannot start a board command begins a legacy ASCII line (the desktop
 * GUI after a VSWR scan, for example): the session ends, the module goes
 * back to local use and the line goes to the legacy parser, instead of
 * being swallowed until the session times out. */
static uint8_t HOST_Mux_SessionByte(HOST_VendorMux_t *mux, uint8_t byte)
{
    uint8_t match;
    uint8_t length;

    if(
        (mux->prefixLength == 0U) &&
        (
            (Module_Bridge_HasPendingHostBytes() != 0U) ||
            (byte < 0x20U) ||
            (byte > 0x7EU)
        )
    )
    {
        Module_Bridge_PushLinkBytes(mux->link, &byte, 1U);
        return 1U;
    }

    mux->prefix[mux->prefixLength++] = byte;
    match = HOST_Mux_MatchToken(mux->prefix, mux->prefixLength);

    if(match == 1U)
    {
        return 1U;
    }

    length = mux->prefixLength;
    mux->prefixLength = 0U;

    if(match == 2U)
    {
        Module_Bridge_PushLinkBytes(mux->link, mux->prefix, length);
        return 1U;
    }

    Module_Bridge_EndLinkForLocalUse(mux->link);
    HOST_Mux_Release(mux, mux->prefix, length);
    mux->lineStart = 0U;
    return 1U;
}

/* Returns 1 when the byte was taken by the vendor-tool path. */
static uint8_t HOST_Mux_Byte(HOST_VendorMux_t *mux, uint8_t byte)
{
    uint8_t i;
    uint8_t anyPrefix = 0U;

    mux->lastByteMs = Time_GetTickMs();

    if(Module_Bridge_IsLinkActive(mux->link) != 0U)
    {
        return HOST_Mux_SessionByte(mux, byte);
    }

    if(mux->eeActive != 0U)
    {
        uint16_t needed;

        mux->ee[mux->eeLength++] = byte;

        if(mux->eeLength < 6U)
        {
            return 1U;
        }

        needed = (uint16_t)(6U + (((uint16_t)mux->ee[2] << 8U) | mux->ee[3]));
        if((mux->ee[5] & 0x02U) != 0U)
        {
            needed = (uint16_t)(needed + 2U);   /* serial CRC16-Modbus trailer */
        }

        if((needed > HOST_MUX_EE_MAX) || (mux->ee[1] != 0x00U))
        {
            mux->eeActive = 0U;
            mux->eeLength = 0U;
            mux->lineStart = 1U;
            return 1U;
        }

        if(mux->eeLength >= needed)
        {
            Configuration_Port_HandleLinkRequest(mux->link, mux->ee, mux->eeLength);
            mux->eeActive = 0U;
            mux->eeLength = 0U;
            mux->lineStart = 1U;
        }
        return 1U;
    }

    if((mux->lineStart != 0U) && (mux->prefixLength == 0U))
    {
        if(byte == 0xFFU)
        {
            if(Module_Bridge_BeginLink(mux->link, &byte, 1U) != 0U)
            {
                return 1U;
            }
        }
        else if(byte == 0xEEU)
        {
            mux->ee[0] = byte;
            mux->eeLength = 1U;
            mux->eeActive = 1U;
            return 1U;
        }
    }

    if((mux->lineStart != 0U) || (mux->prefixLength != 0U))
    {
        mux->prefix[mux->prefixLength++] = byte;

        for(i = 0U; i < (sizeof(hostVendorTokens) / sizeof(hostVendorTokens[0])); i++)
        {
            size_t tokenLength = strlen(hostVendorTokens[i]);

            if(
                (mux->prefixLength <= tokenLength) &&
                (memcmp(hostVendorTokens[i], mux->prefix, mux->prefixLength) == 0)
            )
            {
                anyPrefix = 1U;

                if(mux->prefixLength == tokenLength)
                {
                    uint8_t length = mux->prefixLength;

                    mux->prefixLength = 0U;

                    if(Module_Bridge_BeginLink(mux->link, mux->prefix, length) != 0U)
                    {
                        return 1U;
                    }

                    /* Bridge busy (another session): hand the text back. */
                    HOST_Mux_Release(mux, mux->prefix, length);
                    mux->lineStart = 0U;
                    return 1U;
                }
                break;
            }
        }

        if(anyPrefix != 0U)
        {
            return 1U;
        }

        /* Not a vendor token: give the held bytes to the legacy parser. */
        HOST_Mux_Release(mux, mux->prefix, mux->prefixLength);
        mux->prefixLength = 0U;
        mux->lineStart = ((byte == '\r') || (byte == '\n')) ? 1U : 0U;
        return 1U;
    }

    mux->lineStart = ((byte == '\r') || (byte == '\n')) ? 1U : 0U;
    return 0U;
}

/* Next byte held back by a router (not vendor traffic), if any. */
static uint8_t HOST_Mux_TakeReleased(uint8_t *source, uint8_t *byte)
{
    HOST_VendorMux_t *const muxes[2] = { &hostUsbMux, &hostSerialMux };
    uint8_t i;

    for(i = 0U; i < 2U; i++)
    {
        if(muxes[i]->releasePos < muxes[i]->releaseLength)
        {
            *byte = muxes[i]->release[muxes[i]->releasePos++];
            *source = muxes[i]->link;
            return 1U;
        }
    }

    return 0U;
}


/* ============================================================
 * INITIALIZATION
 * ============================================================
 */

void HOST_Transport_Init(void)
{
    hostRxHead = 0U;
    hostRxTail = 0U;

    hostRxOverflow = 0U;

    hostActiveTransport =
        HOST_TRANSPORT_NONE;


    hostTagTransport =
        HOST_TRANSPORT_NONE;

    HOST_Mux_Init(&hostUsbMux, HOST_TRANSPORT_USB);
    HOST_Mux_Init(&hostSerialMux, HOST_TRANSPORT_SERIAL);
}


/* ============================================================
 * PUSH BYTE
 * ============================================================
 */

void HOST_Transport_PushByte(
    uint8_t source,
    uint8_t byte
)
{
    uint16_t nextHead;
    uint32_t primask;

    if(
        (source != HOST_TRANSPORT_USB) &&
        (source != HOST_TRANSPORT_ETHERNET) &&
        (source != HOST_TRANSPORT_SERIAL) &&
        (source != HOST_TRANSPORT_WIFI)
    )
    {
        return;
    }

    primask = CriticalSection_Enter();

    nextHead =
        (uint16_t)(hostRxHead + 1U);

    if(nextHead >= HOST_RX_QUEUE_SIZE)
    {
        nextHead = 0U;
    }

    if(nextHead == hostRxTail)
    {
        hostRxOverflow++;

        CriticalSection_Exit(
            primask
        );

        return;
    }

    hostRxQueue[hostRxHead] = byte;

    hostRxSourceQueue[hostRxHead] =
        source;

    hostRxHead = nextHead;

    CriticalSection_Exit(
        primask
    );
}

/* ============================================================
 * GET RX OVERFLOW COUNT
 * ============================================================
 */

uint32_t HOST_Transport_GetRxOverflowCount(void)
{
    return hostRxOverflow;
}


/* ============================================================
 * READ BYTE
 * ============================================================
 *
 * This function owns:
 *
 * - queue consumption
 * - transport claiming
 * - RELEASE protection
 * - transport filtering
 *
 * The host parser does not touch queue internals.
 * ============================================================
 */

uint8_t HOST_Transport_ReadByte(
    uint8_t *source,
    uint8_t *byte
)
{
    if(
        (source == NULL) ||
        (byte == NULL)
    )
    {
        return 0U;
    }

    HOST_Mux_CheckTimeout(&hostUsbMux);
    HOST_Mux_CheckTimeout(&hostSerialMux);

    for(;;)
    {
        uint8_t queuedSource;
        uint8_t queuedByte;
        HOST_VendorMux_t *mux;

        if(HOST_Mux_TakeReleased(&queuedSource, &queuedByte) == 0U)
        {
            if(hostRxHead == hostRxTail)
            {
                break;
            }

            queuedByte =
                hostRxQueue[hostRxTail];

            queuedSource =
                hostRxSourceQueue[hostRxTail];


            hostRxTail++;

            if(hostRxTail >= HOST_RX_QUEUE_SIZE)
            {
                hostRxTail = 0U;
            }

            mux = HOST_Mux_ForLink(queuedSource);

            if(
                (mux != NULL) &&
                (HOST_Mux_Byte(mux, queuedByte) != 0U)
            )
            {
                continue;
            }
        }


        /*
         * ----------------------------------------------------
         * No transport owns the interface.
         *
         * The transport that just issued RELEASE is not
         * allowed to immediately reclaim it.
         * ----------------------------------------------------
         */

        if(
            hostActiveTransport
            == HOST_TRANSPORT_NONE
        )
        {
            /*
             * Only a printable character can claim the parser.
             * On a UART the LF ending a RELEASE line arrives after
             * the release has flushed the queue, and an RS485
             * bus can carry noise; neither may lock the other
             * transports out.
             */
            if(
                (queuedByte < 0x20U) ||
                (queuedByte > 0x7EU)
            )
            {
                continue;
            }

            hostActiveTransport =
                queuedSource;
        }


        /*
         * ----------------------------------------------------
         * Reject bytes from a non-active transport.
         * ----------------------------------------------------
         */

        if(
            queuedSource
            != hostActiveTransport
        )
        {
            continue;
        }


        *source = queuedSource;
        *byte = queuedByte;

        return 1U;
    }

    return 0U;
}


/* ============================================================
 * GET ACTIVE TRANSPORT
 * ============================================================
 */

uint8_t HOST_Transport_GetActive(void)
{
    return hostActiveTransport;
}


/* ============================================================
 * TAG TRANSPORT
 * ============================================================
 */

void HOST_Transport_SetTagTransport(
    uint8_t transport
)
{
    if(
        (transport == HOST_TRANSPORT_USB) ||
        (transport == HOST_TRANSPORT_ETHERNET) ||
        (transport == HOST_TRANSPORT_SERIAL) ||
        (transport == HOST_TRANSPORT_WIFI)
    )
    {
        hostTagTransport = transport;
    }
    else
    {
        hostTagTransport =
            HOST_TRANSPORT_NONE;
    }
}


uint8_t HOST_Transport_GetTagTransport(void)
{
    return hostTagTransport;
}


/* ============================================================
 * RELEASE
 * ============================================================
 */

void HOST_Transport_Release(void)
{
    uint32_t primask;

    primask = CriticalSection_Enter();

    /*
     * Release current transport.
     *
     * IMPORTANT:
     * There is intentionally NO released-transport blacklist.
     * The same transport must be allowed to reconnect.
     */
    hostActiveTransport =
        HOST_TRANSPORT_NONE;

    hostTagTransport =
        HOST_TRANSPORT_NONE;

    /*
     * Flush stale host input.
     */
    hostRxHead = 0U;
    hostRxTail = 0U;

    CriticalSection_Exit(
        primask
    );
}


/* ============================================================
 * OUTPUT
 * ============================================================
 */

void HOST_Transport_SendString(uint8_t transport,const char *text
)
{
    uint16_t length;

    if(text == NULL)
    {
        return;
    }

    length =
        (uint16_t)strlen(text);


    if(transport == HOST_TRANSPORT_ETHERNET)
    {
        (void)Host_Tcp_Server_Send((const uint8_t *)text, length);
    }
    else if(transport == HOST_TRANSPORT_WIFI)
    {
        (void)WiFi_Service_HostSend((const uint8_t *)text, length);
    }
    else
    {
        (void)HOST_Transport_LinkSend(
            transport,
            (const uint8_t *)text,
            length
        );
    }
}


uint16_t HOST_Transport_LinkSend(
    uint8_t link,
    const uint8_t *data,
    uint16_t length
)
{
    if(link == HOST_TRANSPORT_SERIAL)
    {
        return Serial_Port_Send(data, length);
    }

    if(
        (link == HOST_TRANSPORT_USB) &&
        (USB_CDC_Transport_IsReady() != 0U)
    )
    {
        return USB_CDC_Transport_Send(data, length);
    }

    return 0U;
}


uint16_t HOST_Transport_LinkGetFreeSpace(uint8_t link)
{
    if(link == HOST_TRANSPORT_SERIAL)
    {
        return Serial_Port_GetFreeSpace();
    }

    if(link == HOST_TRANSPORT_USB)
    {
        return USB_CDC_Transport_GetFreeSpace();
    }

    return 0U;
}
