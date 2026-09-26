#include "sillion_application_port.h"
#include <stddef.h>

static Silion_Handle_t *g_silion = NULL;
static USART_Handle_t *g_usart = NULL;

static volatile uint8_t rxQueue[SILION_APP_RX_QUEUE_SIZE];
static volatile uint16_t rxHead = 0U;
static volatile uint16_t rxTail = 0U;
static volatile uint32_t rxOverflow = 0U;

volatile uint8_t txComplete = 0U;
volatile uint8_t rxComplete = 0U;
volatile uint8_t rxORE = 0U;
volatile uint8_t rxFE = 0U;
volatile uint8_t rxNE = 0U;
volatile uint8_t rxPE = 0U;
volatile uint32_t silionAsyncPacketCount = 0U;
volatile uint32_t silionAsyncBadFrameCount = 0U;

static uint32_t port_time_ms(void)
{
    if(SystemCoreClock == 0U)
    {
        return 0U;
    }

    return (uint32_t)(DWT->CYCCNT / (SystemCoreClock / 1000U));
}

static void port_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void SILION_Application_Port_Init(
    Silion_Handle_t *pSilionHandle,
    USART_Handle_t *pUSARTHandle
)
{
    g_silion = pSilionHandle;
    g_usart = pUSARTHandle;

    txComplete = 0U;
    rxComplete = 0U;
    SILION_ClearUartFlags();
    SILION_ClearRxQueue();

    port_dwt_init();
}

void SILION_Application_Port_UartEvent(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
)
{
    uint16_t nextHead;

    if(pUSARTHandle != g_usart)
    {
        return;
    }

    if(AppEvent == USART_EVENT_RX_BYTE)
    {
        nextHead = (uint16_t)(rxHead + 1U);
        if(nextHead >= SILION_APP_RX_QUEUE_SIZE)
        {
            nextHead = 0U;
        }

        if(nextHead != rxTail)
        {
            rxQueue[rxHead] = receivedByte;
            rxHead = nextHead;
        }
        else
        {
            rxOverflow++;
        }
    }
    else if(AppEvent == USART_EVENT_TX_CMPLT)
    {
        txComplete = 1U;
    }
    else if(AppEvent == USART_EVENT_RX_CMPLT)
    {
        rxComplete = 1U;
    }
    else if(AppEvent == USART_EVENT_ORE)
    {
        rxORE = 1U;
    }
    else if(AppEvent == USART_EVENT_FE)
    {
        rxFE = 1U;
    }
    else if(AppEvent == USART_EVENT_NE)
    {
        rxNE = 1U;
    }
    else if(AppEvent == USART_EVENT_PE)
    {
        rxPE = 1U;
    }
}

/* Weak default: later transport/board glue can own the central UART callback. */
__attribute__((weak)) void USART_ApplicationEventCallback(
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
}

void SILION_ProcessRxQueue(void)
{
    if(g_silion == NULL)
    {
        return;
    }

    while(rxHead != rxTail)
    {
        uint8_t byte = rxQueue[rxTail];

        rxTail = (uint16_t)(rxTail + 1U);
        if(rxTail >= SILION_APP_RX_QUEUE_SIZE)
        {
            rxTail = 0U;
        }

        SILION_ProcessByte(g_silion, byte);
    }
}

void SILION_ClearUartFlags(void)
{
    rxORE = 0U;
    rxFE = 0U;
    rxNE = 0U;
    rxPE = 0U;
}

void SILION_ClearRxQueue(void)
{
    rxHead = 0U;
    rxTail = 0U;
    rxOverflow = 0U;
}

void SILION_DelayMs(uint32_t delayMs)
{
    uint32_t start = port_time_ms();

    while((port_time_ms() - start) < delayMs)
    {
        SILION_ProcessRxQueue();
    }
}

int SILION_WaitForResponse(uint32_t timeoutMs)
{
    uint32_t start = port_time_ms();

    while((port_time_ms() - start) < timeoutMs)
    {
        SILION_ProcessRxQueue();

        if(rxORE || rxFE || rxNE || rxPE)
        {
            return -1;
        }

        if(g_silion != NULL && SILION_IsFrameReady(g_silion))
        {
            return 1;
        }
    }

    return 0;
}

int SILION_WaitForTxComplete(uint32_t timeoutMs)
{
    uint32_t start = port_time_ms();

    while((port_time_ms() - start) < timeoutMs)
    {
        SILION_ProcessRxQueue();

        if(rxORE || rxFE || rxNE || rxPE)
        {
            return -1;
        }

        if(txComplete)
        {
            return 1;
        }
    }

    return 0;
}

/* Weak fallback only. The host layer will provide real output later. */
__attribute__((weak)) void HOST_SendString(const char *text)
{
    (void)text;
}

__attribute__((weak)) void VCP_SendTag(const SILION_Tag_t *tag)
{
    (void)tag;
}
