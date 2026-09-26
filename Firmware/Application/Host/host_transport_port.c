#include "host_transport_port.h"
#include <stddef.h>

volatile uint8_t hostRxQueue[HOST_RX_QUEUE_SIZE];
volatile uint16_t hostRxHead = 0U;
volatile uint16_t hostRxTail = 0U;
volatile uint16_t hostRxCount = 0U;
volatile uint32_t hostRxOverflow = 0U;
volatile uint8_t hostRxSourceQueue[HOST_RX_QUEUE_SIZE];
volatile uint8_t hostActiveTransport = HOST_TRANSPORT_NONE;

void HOST_Transport_Reset(void)
{
    hostRxHead = 0U;
    hostRxTail = 0U;
    hostRxCount = 0U;
    hostRxOverflow = 0U;
    hostActiveTransport = HOST_TRANSPORT_NONE;
}

void HOST_Transport_PushByte(uint8_t source, uint8_t byte)
{
    uint16_t nextHead = (uint16_t)(hostRxHead + 1U);

    if(nextHead >= HOST_RX_QUEUE_SIZE)
    {
        nextHead = 0U;
    }

    if(nextHead == hostRxTail)
    {
        hostRxOverflow++;
        return;
    }

    hostRxQueue[hostRxHead] = byte;
    hostRxSourceQueue[hostRxHead] = source;
    hostRxHead = nextHead;
}

/* Weak USB output hook; the board transport will provide the real implementation. */
__attribute__((weak)) void VCP_SendString(const char *text)
{
    (void)text;
}

/* Weak Ethernet output hook; the W5500 driver will provide the real implementation. */
__attribute__((weak)) uint16_t W5500_Socket0_Send(
    const uint8_t *buffer,
    uint16_t length
)
{
    (void)buffer;
    return length;
}
