#include "uart_dispatch.h"
#include <stddef.h>
#include "../Application/Silion/sillion_application_port.h"
#include "../Services/VCP/vcp_transport.h"

static USART_Handle_t *g_rfidUart = NULL;
static USART_Handle_t *g_hostUart = NULL;

void USART_Dispatch_Init(
    USART_Handle_t *pRFIDUart,
    USART_Handle_t *pHostUart
)
{
    g_rfidUart = pRFIDUart;
    g_hostUart = pHostUart;
}

void USART_ApplicationEventCallback(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
)
{
    SILION_Application_Port_UartEvent(
        pUSARTHandle,
        AppEvent,
        receivedByte
    );

    VCP_Transport_UartEvent(
        pUSARTHandle,
        AppEvent,
        receivedByte
    );
}

void USART1_IRQHandler(void)
{
    if(g_hostUart != NULL)
    {
        USART_IRQHandling(g_hostUart);
    }
}

void USART3_IRQHandler(void)
{
    if(g_rfidUart != NULL)
    {
        USART_IRQHandling(g_rfidUart);
    }
}
