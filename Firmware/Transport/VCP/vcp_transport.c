#include "vcp_transport.h"
#include "../../Application/Host/host_transport_port.h"

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
        if(
            (hostActiveTransport == HOST_TRANSPORT_USB) ||
            (hostActiveTransport == HOST_TRANSPORT_NONE)
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

    if((text == NULL) || (g_vcpUart == NULL))
    {
        return;
    }

    for(i = 0U; text[i] != '\0'; i++)
    {
        uint16_t nextHead =
            (uint16_t)((vcpTxHead + 1U) % VCP_TX_BUFFER_SIZE);

        if(nextHead == vcpTxTail)
        {
            break;
        }

        vcpTxBuffer[vcpTxHead] = (uint8_t)text[i];
        vcpTxHead = nextHead;
    }

    if(!vcpTxBusy && (vcpTxTail != vcpTxHead))
    {
        vcpTxActiveLen = VCP_GetContiguousLength();
        vcpTxBusy = 1U;

        (void)USART_SendDataIT(
            g_vcpUart,
            &vcpTxBuffer[vcpTxTail],
            vcpTxActiveLen
        );
    }
}

void VCP_SendTag(const SILION_Tag_t *tag)
{
    char buffer[160];
    uint16_t pos = 0U;
    uint16_t i;

    if(tag == NULL)
    {
        return;
    }

    pos += (uint16_t)sprintf(
        &buffer[pos],
        "TAG,EPC="
    );

    for(i = 0U; i < tag->epcLengthBytes; i++)
    {
        pos += (uint16_t)sprintf(
            &buffer[pos],
            "%02X",
            tag->epc[i]
        );
    }

    pos += (uint16_t)sprintf(
        &buffer[pos],
        ",RSSI=%d,ANT=%u,FREQ=%lu,TIME=%lu\r\n",
        tag->rssi,
        tag->antenna,
        (unsigned long)tag->frequencyKHz,
        (unsigned long)tag->timestampMs
    );

    VCP_SendString(buffer);
}
