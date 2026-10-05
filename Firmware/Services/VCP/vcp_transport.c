#include "vcp_transport.h"
#include "../host/host_transport.h"
#include "../../Services/USB/usb_cdc_transport.h"
#include "../../Services/ethernet/ethernet_service.h"
#include "../../Protocol/identium_protocol.h"
#include "../../Common/critical_section.h"

extern uint8_t HOST_IsTagTransportEthernet(void);

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static USART_Handle_t *g_vcpUart = NULL;

static uint8_t vcpTxBuffer[VCP_TX_BUFFER_SIZE];
static volatile uint16_t vcpTxHead = 0U;
static volatile uint16_t vcpTxTail = 0U;
static volatile uint16_t vcpTxActiveLen = 0U;
static volatile uint8_t vcpTxBusy = 0U;

static uint16_t VCP_GetContiguousLength(void)
{
    if(vcpTxHead > vcpTxTail)
    {
        return (uint16_t)(vcpTxHead - vcpTxTail);
    }

    return (uint16_t)(VCP_TX_BUFFER_SIZE - vcpTxTail);
}

void VCP_Transport_Init(USART_Handle_t *pUSARTHandle)
{
    g_vcpUart = pUSARTHandle;
    vcpTxHead = 0U;
    vcpTxTail = 0U;
    vcpTxActiveLen = 0U;
    vcpTxBusy = 0U;
}

void VCP_Transport_UartEvent(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
)
{
    if(pUSARTHandle != g_vcpUart)
    {
        return;
    }

    if(AppEvent == USART_EVENT_RX_BYTE)
    {
        uint8_t activeTransport;

        /*
         * The F429 ST-LINK VCP is only a legacy fallback.
         * Once native USB CDC is configured, do not let USART1
         * bytes claim or interfere with the native USB host path.
         */
        if(USB_CDC_Transport_IsReady())
        {
            return;
        }

        activeTransport =
            HOST_Transport_GetActive();

        if(
            (activeTransport == HOST_TRANSPORT_USB) ||
            (activeTransport == HOST_TRANSPORT_NONE)
        )
        {
            HOST_Transport_PushByte(
                HOST_TRANSPORT_USB,
                receivedByte
            );
        }
    }
    else if(AppEvent == USART_EVENT_TX_CMPLT)
    {
        vcpTxTail = (uint16_t)(
            (vcpTxTail + vcpTxActiveLen) % VCP_TX_BUFFER_SIZE
        );
        vcpTxActiveLen = 0U;

        if(vcpTxTail != vcpTxHead)
        {
            vcpTxActiveLen = VCP_GetContiguousLength();
            (void)USART_SendDataIT(
                g_vcpUart,
                &vcpTxBuffer[vcpTxTail],
                vcpTxActiveLen
            );
        }
        else
        {
            vcpTxBusy = 0U;
        }
    }
}

void VCP_SendString(const char *text)
{
    uint16_t i;
    uint32_t primask;

    if(
        (text == NULL) ||
        (g_vcpUart == NULL)
    )
    {
        return;
    }

    /*
     * Protect the complete TX producer operation.
     *
     * The USART1 ISR consumes the same ring buffer and
     * modifies vcpTxTail / vcpTxBusy / vcpTxActiveLen.
     *
     * Keep interrupts disabled only for this short section.
     */
    primask = CriticalSection_Enter();

    for(
        i = 0U;
        text[i] != '\0';
        i++
    )
    {
        uint16_t nextHead;

        nextHead =
            (uint16_t)(
                (vcpTxHead + 1U)
                % VCP_TX_BUFFER_SIZE
            );

        if(nextHead == vcpTxTail)
        {
            /*
             * TX queue full.
             * Preserve existing behavior:
             * stop adding this string.
             */
            break;
        }

        vcpTxBuffer[vcpTxHead] =
            (uint8_t)text[i];

        vcpTxHead =
            nextHead;
    }

    /*
     * Start transmission if USART1 is idle
     * and data is waiting.
     */
    if(
        !vcpTxBusy &&
        (vcpTxTail != vcpTxHead)
    )
    {
        vcpTxActiveLen =
            VCP_GetContiguousLength();

        vcpTxBusy = 1U;

        (void)USART_SendDataIT(
            g_vcpUart,
            &vcpTxBuffer[vcpTxTail],
            vcpTxActiveLen
        );
    }

    CriticalSection_Exit(
        primask
    );
}

void VCP_SendTag(const SILION_Tag_t *tag)
{
    char buffer[256];
    uint16_t pos = 0U;
    uint16_t i;
    int written;

    if(tag == NULL)
    {
        return;
    }

    /* New Identium protocol path is independent of legacy ASCII transport. */
    (void)Identium_Protocol_ReportTag(tag);

    written = snprintf(
        &buffer[pos],
        sizeof(buffer) - pos,
        "TAG,EPC="
    );

    if(written < 0)
    {
        return;
    }

    if((size_t)written >= (sizeof(buffer) - pos))
    {
        return;
    }

    pos += (uint16_t)written;

    for(i = 0U; i < tag->epcLengthBytes; i++)
    {
        written = snprintf(
            &buffer[pos],
            sizeof(buffer) - pos,
            "%02X",
            tag->epc[i]
        );

        if(written < 0)
        {
            return;
        }

        if((size_t)written >= (sizeof(buffer) - pos))
        {
            return;
        }

        pos += (uint16_t)written;
    }

    written = snprintf(
        &buffer[pos],
        sizeof(buffer) - pos,
        ",RSSI=%d,ANT=%u,FREQ=%lu,TIME=%lu\r\n",
        tag->rssi,
        tag->antenna,
        (unsigned long)tag->frequencyKHz,
        (unsigned long)tag->timestampMs
    );

    if(written < 0)
    {
        return;
    }

    if((size_t)written >= (sizeof(buffer) - pos))
    {
        return;
    }

    pos += (uint16_t)written;

    HOST_Transport_SendString(
        HOST_Transport_GetTagTransport(),
        buffer
    );
}
