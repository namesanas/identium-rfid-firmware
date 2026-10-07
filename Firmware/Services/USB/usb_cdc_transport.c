#include "usb_cdc_transport.h"

#include <stddef.h>
#include <string.h>

#include "../../Common/critical_section.h"
#include "../../USB_DEVICE/App/usb_device.h"
#include "../../USB_DEVICE/App/usbd_cdc_if.h"
#include "usbd_def.h"

static uint8_t usbTxQueue[USB_CDC_TRANSPORT_TX_QUEUE_SIZE];
static uint8_t usbTxPacket[USB_CDC_TRANSPORT_PACKET_SIZE];

static volatile uint16_t usbTxHead = 0U;
static volatile uint16_t usbTxTail = 0U;
static volatile uint16_t usbTxActiveLen = 0U;
static volatile uint8_t usbTxInFlight = 0U;
static volatile uint32_t usbTxOverflow = 0U;

static uint16_t USB_CDC_Transport_QueuedLength(void)
{
    if(usbTxHead >= usbTxTail)
    {
        return (uint16_t)(usbTxHead - usbTxTail);
    }

    return (uint16_t)(USB_CDC_TRANSPORT_TX_QUEUE_SIZE - usbTxTail + usbTxHead);
}

static uint16_t USB_CDC_Transport_AdvanceIndex(uint16_t index, uint16_t amount)
{
    index = (uint16_t)(index + amount);

    while(index >= USB_CDC_TRANSPORT_TX_QUEUE_SIZE)
    {
        index = (uint16_t)(index - USB_CDC_TRANSPORT_TX_QUEUE_SIZE);
    }

    return index;
}

void USB_CDC_Transport_Init(void)
{
    uint32_t primask = CriticalSection_Enter();

    usbTxHead = 0U;
    usbTxTail = 0U;
    usbTxActiveLen = 0U;
    usbTxInFlight = 0U;
    usbTxOverflow = 0U;

    CriticalSection_Exit(primask);
}

uint8_t USB_CDC_Transport_IsReady(void)
{
    return USB_Device_IsConfigured();
}

uint16_t USB_CDC_Transport_Send(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t accepted = 0U;
    uint16_t nextHead;
    uint32_t primask;

    if((data == NULL) || (length == 0U))
    {
        return 0U;
    }

    primask = CriticalSection_Enter();

    for(i = 0U; i < length; i++)
    {
        nextHead = (uint16_t)(usbTxHead + 1U);

        if(nextHead >= USB_CDC_TRANSPORT_TX_QUEUE_SIZE)
        {
            nextHead = 0U;
        }

        if(nextHead == usbTxTail)
        {
            usbTxOverflow++;
            break;
        }

        usbTxQueue[usbTxHead] = data[i];
        usbTxHead = nextHead;
        accepted++;
    }

    CriticalSection_Exit(primask);

    return accepted;
}

uint16_t USB_CDC_Transport_GetFreeSpace(void)
{
    return (uint16_t)(USB_CDC_TRANSPORT_TX_QUEUE_SIZE - 1U - USB_CDC_Transport_QueuedLength());
}

void USB_CDC_Transport_OnTxComplete(void)
{
    usbTxActiveLen = 0U;
    usbTxInFlight = 0U;
}

void USB_CDC_Transport_Reset(void)
{
    uint32_t primask = CriticalSection_Enter();

    usbTxHead = 0U;
    usbTxTail = 0U;
    usbTxActiveLen = 0U;
    usbTxInFlight = 0U;

    CriticalSection_Exit(primask);
}

uint32_t USB_CDC_Transport_GetTxOverflowCount(void)
{
    return usbTxOverflow;
}

void USB_CDC_Transport_Task(void)
{
    uint16_t queued;
    uint16_t contiguous;
    uint16_t packetLength;
    uint8_t result;
    uint32_t primask;

    if(!USB_CDC_Transport_IsReady())
    {
        return;
    }

    primask = CriticalSection_Enter();

    if(usbTxInFlight != 0U)
    {
        CriticalSection_Exit(primask);
        return;
    }

    queued = USB_CDC_Transport_QueuedLength();

    if(queued == 0U)
    {
        CriticalSection_Exit(primask);
        return;
    }

    if(usbTxHead > usbTxTail)
    {
        contiguous = (uint16_t)(usbTxHead - usbTxTail);
    }
    else
    {
        contiguous = (uint16_t)(USB_CDC_TRANSPORT_TX_QUEUE_SIZE - usbTxTail);
    }

    packetLength = contiguous;

    if(packetLength > USB_CDC_TRANSPORT_PACKET_SIZE)
    {
        packetLength = USB_CDC_TRANSPORT_PACKET_SIZE;
    }

    memcpy(usbTxPacket, &usbTxQueue[usbTxTail], packetLength);

    usbTxActiveLen = packetLength;
    usbTxInFlight = 1U;

    CriticalSection_Exit(primask);

    result = CDC_Transmit_HS(usbTxPacket, packetLength);

    if(result != USBD_OK)
    {
        primask = CriticalSection_Enter();
        usbTxInFlight = 0U;
        usbTxActiveLen = 0U;
        CriticalSection_Exit(primask);
        return;
    }

    primask = CriticalSection_Enter();
    usbTxTail = USB_CDC_Transport_AdvanceIndex(usbTxTail, packetLength);
    CriticalSection_Exit(primask);
}
