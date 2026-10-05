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

    while(hostRxHead != hostRxTail)
    {
        uint8_t queuedSource;
        uint8_t queuedByte;

        queuedByte =
            hostRxQueue[hostRxTail];

        queuedSource =
            hostRxSourceQueue[hostRxTail];


        hostRxTail++;

        if(hostRxTail >= HOST_RX_QUEUE_SIZE)
        {
            hostRxTail = 0U;
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
