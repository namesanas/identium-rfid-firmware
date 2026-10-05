#include "sillion_application_port.h"

#include <stddef.h>

#include "../../Services/Time/time_service.h"

#include "../../Services/watchdog/watchdog.h"

#include "../../Common/critical_section.h"
#include "../../Services/system/system_fault.h"


/*
 * ============================================================
 * CONFIGURATION
 * ============================================================
 */

#define SILION_APP_RX_QUEUE_SIZE    256U


/*
 * ============================================================
 * PRIVATE STATE
 * ============================================================
 */

static Silion_Handle_t *g_silion = NULL;
static USART_Handle_t *g_usart = NULL;


/*
 * ============================================================
 * SILION RX SOFTWARE QUEUE
 * ============================================================
 */

static volatile uint8_t rxQueue[SILION_APP_RX_QUEUE_SIZE];

static volatile uint16_t rxHead = 0U;
static volatile uint16_t rxTail = 0U;

static volatile uint32_t rxOverflow = 0U;

static uint32_t rxOverflowReported = 0U;
static uint32_t asyncQueueOverflowReported = 0U;


/*
 * ============================================================
 * SILION UART STATE
 * ============================================================
 */

volatile uint8_t txComplete = 0U;
volatile uint8_t rxComplete = 0U;

volatile uint8_t rxORE = 0U;
volatile uint8_t rxFE  = 0U;
volatile uint8_t rxNE  = 0U;
volatile uint8_t rxPE  = 0U;

volatile uint32_t silionAsyncPacketCount = 0U;
volatile uint32_t silionAsyncBadFrameCount;

volatile uint32_t silionTxCompleteEventCount = 0U;


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void SILION_Application_Port_Init(
    Silion_Handle_t *pSilionHandle,
    USART_Handle_t *pUSARTHandle
)
{
    g_silion = pSilionHandle;
    g_usart = pUSARTHandle;

    SILION_ClearRxQueue();
    SILION_ClearUartFlags();

    txComplete = 0U;
    rxComplete = 0U;

    silionAsyncPacketCount = 0U;
    silionAsyncBadFrameCount = 0U;

    rxOverflowReported = 0U;
    asyncQueueOverflowReported = 0U;
}


/*
 * ============================================================
 * UART EVENT ROUTING
 * ============================================================
 */

void SILION_Application_Port_UartEvent(USART_Handle_t *pUSARTHandle,uint8_t AppEvent,uint8_t receivedByte)
{
    uint16_t nextHead;

    if(pUSARTHandle != g_usart)
    {
        return;
    }


    /*
     * --------------------------------------------------------
     * RX BYTE
     * --------------------------------------------------------
     */

    if(AppEvent == USART_EVENT_RX_BYTE)
    {
        nextHead =
            (uint16_t)(rxHead + 1U);

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

        return;
    }


    /*
     * --------------------------------------------------------
     * TX COMPLETE
     * --------------------------------------------------------
     */

    if(AppEvent == USART_EVENT_TX_CMPLT)
    {
        txComplete = 1U;
        silionTxCompleteEventCount++;
        return;
    }


    /*
     * --------------------------------------------------------
     * RX COMPLETE
     * --------------------------------------------------------
     */

    if(AppEvent == USART_EVENT_RX_CMPLT)
    {
        rxComplete = 1U;
        return;
    }


    /*
     * --------------------------------------------------------
     * UART ERRORS
     * --------------------------------------------------------
     */

    if(AppEvent == USART_EVENT_ORE)
    {
        rxORE = 1U;
        return;
    }

    if(AppEvent == USART_EVENT_FE)
    {
        rxFE = 1U;
        return;
    }

    if(AppEvent == USART_EVENT_NE)
    {
        rxNE = 1U;
        return;
    }

    if(AppEvent == USART_EVENT_PE)
    {
        rxPE = 1U;
        return;
    }
}


/*
 * ============================================================
 * DEFAULT CALLBACK
 *
 * The Common UART dispatcher normally provides the real
 * application callback. This weak implementation keeps the
 * port independently usable.
 * ============================================================
 */

__attribute__((weak))
void USART_ApplicationEventCallback(USART_Handle_t *pUSARTHandle,uint8_t AppEvent,uint8_t receivedByte)
{
    SILION_Application_Port_UartEvent(
        pUSARTHandle,
        AppEvent,
        receivedByte
    );
}


/*
 * ============================================================
 * PROCESS SILION RX QUEUE
 * ============================================================
 */

void SILION_ProcessRxQueue(void)
{
    if(g_silion == NULL)
    {
        return;
    }

    while(rxHead != rxTail)
    {
        uint8_t byte;

        byte = rxQueue[rxTail];

        rxTail++;

        if(rxTail >= SILION_APP_RX_QUEUE_SIZE)
        {
            rxTail = 0U;
        }

        SILION_ProcessByte(
            g_silion,
            byte
        );
    }

    /*
     * --------------------------------------------------------
     * Report SILION RX software queue overflow.
     * --------------------------------------------------------
     */
    if(rxOverflow != rxOverflowReported)
    {
        System_Fault_Set(
            ERROR_BUFFER_OVERFLOW
        );

        rxOverflowReported =
            rxOverflow;
    }


    /*
     * --------------------------------------------------------
     * Report asynchronous tag queue overflow.
     * --------------------------------------------------------
     */
    if(
        (g_silion != NULL) &&
        (
            g_silion->asyncTagQueue.overflow
            !=
            asyncQueueOverflowReported
        )
    )
    {
        System_Fault_Set(
            ERROR_BUFFER_OVERFLOW
        );

        asyncQueueOverflowReported =
            g_silion->asyncTagQueue.overflow;
    }
}


/*
 * ============================================================
 * CLEAR UART ERROR FLAGS
 * ============================================================
 */

void SILION_ClearUartFlags(void)
{
    rxORE = 0U;
    rxFE  = 0U;
    rxNE  = 0U;
    rxPE  = 0U;
}


/*
 * ============================================================
 * CLEAR SILION RX QUEUE
 * ============================================================
 */

void SILION_ClearRxQueue(void)
{
    uint32_t primask;

    /*
     * Atomically reset the RX queue.
     *
     * USART3 ISR can modify rxHead while the main context
     * clears the queue. Interrupts must be masked only for
     * this very short critical section.
     */
    primask = CriticalSection_Enter();

    rxHead = 0U;
    rxTail = 0U;
    rxOverflow = 0U;

    CriticalSection_Exit(
        primask
    );
}


/*
 * ============================================================
 * DELAY
 *
 * Time is provided exclusively by the Time Service.
 *
 * SILION RX processing continues while waiting.
 * ============================================================
 */

void SILION_DelayMs(uint32_t delayMs)
{
	uint32_t start;

    start = Time_GetTickMs();

    while(
        (Time_GetTickMs() - start) < delayMs
    )
    {
        SILION_ProcessRxQueue();

        Watchdog_Refresh();
    }
}


/*
 * ============================================================
 * WAIT FOR SILION RESPONSE
 * ============================================================
 */

int SILION_WaitForResponse(uint32_t timeoutMs)
{
    uint32_t start;

    start = Time_GetTickMs();

    while(
        (Time_GetTickMs() - start) < timeoutMs
    )
    {
        /*
         * Process any bytes already received by the ISR.
         */
        SILION_ProcessRxQueue();
        Watchdog_Refresh();


        /*
         * Report UART errors.
         */
        if(
            rxORE ||
            rxFE  ||
            rxNE  ||
            rxPE
        )
        {
            System_Fault_Set(
                ERROR_RFID_UART
            );

            return -1;
        }

        /*
         * Report a valid complete SILION frame.
         */
        if(
            (g_silion != NULL) &&
            SILION_IsFrameReady(g_silion)
        )
        {
            return 1;
        }
    }

    return 0;
}


/*
 * ============================================================
 * WAIT FOR TX COMPLETE
 * ============================================================
 */

int SILION_WaitForTxComplete(uint32_t timeoutMs)
{
    uint32_t start;

    start = Time_GetTickMs();

    while(
        (Time_GetTickMs() - start) < timeoutMs
    )
    {
        /*
         * Continue processing incoming data while waiting.
         */
        SILION_ProcessRxQueue();
        Watchdog_Refresh();


        /*
         * Report UART errors.
         */
        if(
            rxORE ||
            rxFE  ||
            rxNE  ||
            rxPE
        )
        {
            System_Fault_Set(
                ERROR_RFID_UART
            );

            return -1;
        }


        /*
         * TX interrupt has reported completion.
         */
        if(txComplete)
        {
            return 1;
        }
    }

    return 0;
}


/*
 * ============================================================
 * WEAK FALLBACK HOST OUTPUT
 *
 * Real implementations are supplied by the host/VCP layers.
 * ============================================================
 */

__attribute__((weak))
void HOST_SendString(const char *text)
{
    (void)text;
}


__attribute__((weak))
void VCP_SendTag(const SILION_Tag_t *tag)
{
    (void)tag;
}
