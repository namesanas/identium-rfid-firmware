/*
 * host_transport.c
 *
 *  Created on: 28-Sept-2026
 *      Author: Identium
 */

#include "host_transport.h"

#include <stddef.h>
#include <string.h>

#include "../VCP/vcp_transport.h"
#include "../USB/usb_cdc_transport.h"
#include "../ethernet/ethernet_service.h"
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
 * USB VENDOR-TOOL ROUTER ("COM route")
 *
 * Reader Manager / ReaderSense connect to a "COMx" address with the
 * same protocol they use on TCP 8080. On the USB CDC port, at the start
 * of a line:
 *   FF ...                              -> module bridge session (USB)
 *   IOGET IOSET IPGET IPSET GIO SIO VER -> module bridge session (USB)
 *   EE 00 len cmd flags data [CRC]      -> configuration request (reply on USB)
 * Everything else goes to the legacy ASCII parser unchanged. No legacy
 * command starts with one of these tokens.
 * ============================================================
 */

#define HOST_USB_MUX_EE_MAX       (6U + 2048U + 2U)
#define HOST_USB_MUX_TIMEOUT_MS   500U

static const char *const hostUsbVendorTokens[] =
{
    "IOGET", "IOSET", "IPGET", "IPSET", "GIO", "SIO", "VER"
};

static uint8_t hostUsbLineStart = 1U;
static uint8_t hostUsbPrefix[8];
static uint8_t hostUsbPrefixLength = 0U;
static uint8_t hostUsbRelease[8];
static uint8_t hostUsbReleaseLength = 0U;
static uint8_t hostUsbReleasePos = 0U;
static uint8_t hostUsbEe[HOST_USB_MUX_EE_MAX];
static uint16_t hostUsbEeLength = 0U;
static uint8_t hostUsbEeActive = 0U;
static uint32_t hostUsbLastByteMs = 0U;

static void HOST_UsbMux_Release(const uint8_t *data, uint8_t length)
{
    memcpy(hostUsbRelease, data, length);
    hostUsbReleaseLength = length;
    hostUsbReleasePos = 0U;
}

/* A started token/frame that never completes is abandoned. */
static void HOST_UsbMux_CheckTimeout(void)
{
    if(
        ((hostUsbPrefixLength != 0U) || (hostUsbEeActive != 0U)) &&
        ((Time_GetTickMs() - hostUsbLastByteMs) >= HOST_USB_MUX_TIMEOUT_MS)
    )
    {
        if(hostUsbPrefixLength != 0U)
        {
            HOST_UsbMux_Release(hostUsbPrefix, hostUsbPrefixLength);
            hostUsbPrefixLength = 0U;
        }

        hostUsbEeActive = 0U;
        hostUsbEeLength = 0U;
        hostUsbLineStart = 1U;
    }
}

/* Returns 1 when the byte was taken by the vendor-tool path. */
static uint8_t HOST_UsbMux_Byte(uint8_t byte)
{
    uint8_t i;
    uint8_t anyPrefix = 0U;

    hostUsbLastByteMs = Time_GetTickMs();

    if(Module_Bridge_IsUsbActive() != 0U)
    {
        Module_Bridge_PushUsbBytes(&byte, 1U);
        return 1U;
    }

    if(hostUsbEeActive != 0U)
    {
        uint16_t needed;

        hostUsbEe[hostUsbEeLength++] = byte;

        if(hostUsbEeLength < 6U)
        {
            return 1U;
        }

        needed = (uint16_t)(6U + (((uint16_t)hostUsbEe[2] << 8U) | hostUsbEe[3]));
        if((hostUsbEe[5] & 0x02U) != 0U)
        {
            needed = (uint16_t)(needed + 2U);   /* serial CRC16-Modbus trailer */
        }

        if((needed > HOST_USB_MUX_EE_MAX) || (hostUsbEe[1] != 0x00U))
        {
            hostUsbEeActive = 0U;
            hostUsbEeLength = 0U;
            hostUsbLineStart = 1U;
            return 1U;
        }

        if(hostUsbEeLength >= needed)
        {
            Configuration_Port_HandleUsbRequest(hostUsbEe, hostUsbEeLength);
            hostUsbEeActive = 0U;
            hostUsbEeLength = 0U;
            hostUsbLineStart = 1U;
        }
        return 1U;
    }

    if((hostUsbLineStart != 0U) && (hostUsbPrefixLength == 0U))
    {
        if(byte == 0xFFU)
        {
            if(Module_Bridge_BeginUsb(&byte, 1U) != 0U)
            {
                return 1U;
            }
        }
        else if(byte == 0xEEU)
        {
            hostUsbEe[0] = byte;
            hostUsbEeLength = 1U;
            hostUsbEeActive = 1U;
            return 1U;
        }
    }

    if((hostUsbLineStart != 0U) || (hostUsbPrefixLength != 0U))
    {
        hostUsbPrefix[hostUsbPrefixLength++] = byte;

        for(i = 0U; i < (sizeof(hostUsbVendorTokens) / sizeof(hostUsbVendorTokens[0])); i++)
        {
            size_t tokenLength = strlen(hostUsbVendorTokens[i]);

            if(
                (hostUsbPrefixLength <= tokenLength) &&
                (memcmp(hostUsbVendorTokens[i], hostUsbPrefix, hostUsbPrefixLength) == 0)
            )
            {
                anyPrefix = 1U;

                if(hostUsbPrefixLength == tokenLength)
                {
                    uint8_t length = hostUsbPrefixLength;

                    hostUsbPrefixLength = 0U;

                    if(Module_Bridge_BeginUsb(hostUsbPrefix, length) != 0U)
                    {
                        return 1U;
                    }

                    /* Bridge busy (TCP session): hand the text back. */
                    HOST_UsbMux_Release(hostUsbPrefix, length);
                    hostUsbLineStart = 0U;
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
        HOST_UsbMux_Release(hostUsbPrefix, hostUsbPrefixLength);
        hostUsbPrefixLength = 0U;
        hostUsbLineStart = ((byte == '\r') || (byte == '\n')) ? 1U : 0U;
        return 1U;
    }

    hostUsbLineStart = ((byte == '\r') || (byte == '\n')) ? 1U : 0U;
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
        (source != HOST_TRANSPORT_ETHERNET)
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

    HOST_UsbMux_CheckTimeout();

    for(;;)
    {
        uint8_t queuedSource;
        uint8_t queuedByte;

        if(hostUsbReleasePos < hostUsbReleaseLength)
        {
            /* Bytes held back by the USB router (not vendor traffic). */
            queuedByte = hostUsbRelease[hostUsbReleasePos++];
            queuedSource = HOST_TRANSPORT_USB;
        }
        else
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

            if(
                (queuedSource == HOST_TRANSPORT_USB) &&
                (HOST_UsbMux_Byte(queuedByte) != 0U)
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
        (transport == HOST_TRANSPORT_ETHERNET)
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


    if(
        transport
        == HOST_TRANSPORT_ETHERNET
    )
    {
        (void)Ethernet_Service_Send(
            (const uint8_t *)text,
            length
        );
    }
    else if(
        transport
        == HOST_TRANSPORT_USB
    )
    {
        /* Native USB CDC is preferred once the PC has configured it. */
        if(USB_CDC_Transport_IsReady())
        {
            (void)USB_CDC_Transport_Send(
                (const uint8_t *)text,
                length
            );
        }
        else
        {
            /* Preserve F429 ST-LINK VCP fallback before USB enumeration. */
            VCP_SendString(text);
        }
    }
}
