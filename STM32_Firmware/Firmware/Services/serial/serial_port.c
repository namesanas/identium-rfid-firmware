/*
 * serial_port.c
 *
 * Host serial port (RS232 / RS485). See serial_port.h.
 */

#include "serial_port.h"

#include <stddef.h>

#include "../host/host_transport.h"
#include "../../Common/critical_section.h"
#include "../../Config/firmware_config.h"
#include "../../Drivers/gpio/gpio.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "../../BSP/f429_discovery/board_config.h"
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "../../BSP/f411_custom/board_config.h"
#endif


/* ============================================================
 * STATE
 * ============================================================
 */

static USART_Handle_t *g_serialUart = NULL;
static volatile uint8_t g_serialMode = SERIAL_PORT_MODE_RS232;

static uint8_t g_txBuffer[SERIAL_PORT_TX_BUFFER_SIZE];
static volatile uint16_t g_txHead = 0U;
static volatile uint16_t g_txTail = 0U;
static volatile uint16_t g_txActiveLength = 0U;
static volatile uint8_t g_txBusy = 0U;

static volatile uint32_t g_txOverflow = 0U;
static volatile uint32_t g_rxOverrun = 0U;


/* ============================================================
 * TRANSCEIVER CONTROL
 * ============================================================
 */

static void Serial_Port_ConfigOutputPin(GPIO_RegDef_t *port, uint8_t pin)
{
    GPIO_Handle_t handle;

    if((port == NULL) || (pin > 15U))
    {
        return;
    }

    handle.pGPIOx = port;
    handle.GPIO_PinConfig.GPIO_PinNumber = pin;
    handle.GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_OUT;
    handle.GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_HIGH;
    handle.GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
    handle.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;
    handle.GPIO_PinConfig.GPIO_PinAltFunMode = 0U;

    GPIO_WriteToOutputPin(port, pin, GPIO_PIN_RESET);
    GPIO_Init(&handle);
}

static void Serial_Port_SetDriverEnable(uint8_t enable)
{
    if(((GPIO_RegDef_t *)BOARD_RS485_DE_RE_PORT == NULL) || (BOARD_RS485_DE_RE_PIN > 15U))
    {
        return;
    }

    GPIO_WriteToOutputPin(
        (GPIO_RegDef_t *)BOARD_RS485_DE_RE_PORT,
        BOARD_RS485_DE_RE_PIN,
        (enable != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET
    );

#if defined(BOARD_RS485_RE_N_PORT)
    /* Separate receiver enable (active low): it follows DE, so the
     * receiver is off while driving the bus and on otherwise. */
    GPIO_WriteToOutputPin(
        (GPIO_RegDef_t *)BOARD_RS485_RE_N_PORT,
        BOARD_RS485_RE_N_PIN,
        (enable != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
#endif
}

/* Mode selector: high = RS485, low = RS232 (custom board mux U12). */
static void Serial_Port_SetModePin(uint8_t mode)
{
#if defined(BOARD_RS232_POWER_PORT)
    /* The RS232 transceiver is powered only in RS232 mode. */
    GPIO_WriteToOutputPin(
        (GPIO_RegDef_t *)BOARD_RS232_POWER_PORT,
        BOARD_RS232_POWER_PIN,
        (mode == SERIAL_PORT_MODE_RS232) ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
#endif

    if(((GPIO_RegDef_t *)BOARD_SERIAL_MODE_PORT == NULL) || (BOARD_SERIAL_MODE_PIN > 15U))
    {
        return;
    }

    GPIO_WriteToOutputPin(
        (GPIO_RegDef_t *)BOARD_SERIAL_MODE_PORT,
        BOARD_SERIAL_MODE_PIN,
        (mode == SERIAL_PORT_MODE_RS485) ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
}


/* ============================================================
 * TX QUEUE
 * ============================================================
 */

static uint16_t Serial_Port_ContiguousLength(void)
{
    if(g_txHead >= g_txTail)
    {
        return (uint16_t)(g_txHead - g_txTail);
    }

    return (uint16_t)(SERIAL_PORT_TX_BUFFER_SIZE - g_txTail);
}

/* Caller holds the critical section. */
static void Serial_Port_StartNextChunk(void)
{
    g_txActiveLength = Serial_Port_ContiguousLength();

    if(g_txActiveLength == 0U)
    {
        g_txBusy = 0U;

        if(g_serialMode == SERIAL_PORT_MODE_RS485)
        {
            Serial_Port_SetDriverEnable(0U);
        }
        return;
    }

    if((g_txBusy == 0U) && (g_serialMode == SERIAL_PORT_MODE_RS485))
    {
        /* Take the bus before the first start bit. */
        Serial_Port_SetDriverEnable(1U);
    }

    g_txBusy = 1U;

    (void)USART_SendDataIT(
        g_serialUart,
        &g_txBuffer[g_txTail],
        g_txActiveLength
    );
}


/* ============================================================
 * PUBLIC API
 * ============================================================
 */

void Serial_Port_Init(USART_Handle_t *pUSARTHandle, uint8_t mode)
{
    g_serialUart = pUSARTHandle;

    g_txHead = 0U;
    g_txTail = 0U;
    g_txActiveLength = 0U;
    g_txBusy = 0U;
    g_txOverflow = 0U;
    g_rxOverrun = 0U;

    Serial_Port_ConfigOutputPin(
        (GPIO_RegDef_t *)BOARD_RS485_DE_RE_PORT,
        BOARD_RS485_DE_RE_PIN
    );
    Serial_Port_ConfigOutputPin(
        (GPIO_RegDef_t *)BOARD_SERIAL_MODE_PORT,
        BOARD_SERIAL_MODE_PIN
    );
#if defined(BOARD_RS485_RE_N_PORT)
    Serial_Port_ConfigOutputPin(
        (GPIO_RegDef_t *)BOARD_RS485_RE_N_PORT,
        BOARD_RS485_RE_N_PIN
    );
#endif
#if defined(BOARD_RS232_POWER_PORT)
    Serial_Port_ConfigOutputPin(
        (GPIO_RegDef_t *)BOARD_RS232_POWER_PORT,
        BOARD_RS232_POWER_PIN
    );
#endif

    Serial_Port_SetMode(mode);
}

void Serial_Port_SetMode(uint8_t mode)
{
    uint32_t primask;

    if((mode != SERIAL_PORT_MODE_RS232) && (mode != SERIAL_PORT_MODE_RS485))
    {
        return;
    }

    primask = CriticalSection_Enter();

    g_serialMode = mode;
    Serial_Port_SetModePin(mode);

    /* RS232 never drives DE; RS485 holds it only during a transfer. */
    Serial_Port_SetDriverEnable(
        ((mode == SERIAL_PORT_MODE_RS485) && (g_txBusy != 0U)) ? 1U : 0U
    );

    CriticalSection_Exit(primask);
}

uint8_t Serial_Port_GetMode(void)
{
    return g_serialMode;
}

uint16_t Serial_Port_Send(const uint8_t *data, uint16_t length)
{
    uint16_t accepted = 0U;
    uint32_t primask;

    if((data == NULL) || (length == 0U) || (g_serialUart == NULL))
    {
        return 0U;
    }

    primask = CriticalSection_Enter();

    while(accepted < length)
    {
        uint16_t nextHead = (uint16_t)((g_txHead + 1U) % SERIAL_PORT_TX_BUFFER_SIZE);

        if(nextHead == g_txTail)
        {
            g_txOverflow += (uint32_t)(length - accepted);
            break;
        }

        g_txBuffer[g_txHead] = data[accepted];
        g_txHead = nextHead;
        accepted++;
    }

    if((g_txBusy == 0U) && (g_txHead != g_txTail))
    {
        Serial_Port_StartNextChunk();
    }

    CriticalSection_Exit(primask);

    return accepted;
}

uint16_t Serial_Port_GetFreeSpace(void)
{
    uint16_t used;
    uint32_t primask;

    primask = CriticalSection_Enter();
    used = (uint16_t)((g_txHead + SERIAL_PORT_TX_BUFFER_SIZE - g_txTail) % SERIAL_PORT_TX_BUFFER_SIZE);
    CriticalSection_Exit(primask);

    return (uint16_t)(SERIAL_PORT_TX_BUFFER_SIZE - 1U - used);
}

uint32_t Serial_Port_GetTxOverflowCount(void)
{
    return g_txOverflow;
}

uint32_t Serial_Port_GetRxOverrunCount(void)
{
    return g_rxOverrun;
}


/* ============================================================
 * USART EVENTS (interrupt context)
 * ============================================================
 */

void Serial_Port_UartEvent(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
)
{
    if((pUSARTHandle == NULL) || (pUSARTHandle != g_serialUart))
    {
        return;
    }

    if(AppEvent == USART_EVENT_RX_BYTE)
    {
        /* With the driver enabled, anything received is our own echo
         * (transceivers whose receiver is not disabled during TX). */
        if((g_serialMode == SERIAL_PORT_MODE_RS485) && (g_txBusy != 0U))
        {
            return;
        }

        HOST_Transport_PushByte(HOST_TRANSPORT_SERIAL, receivedByte);
    }
    else if(AppEvent == USART_EVENT_TX_CMPLT)
    {
        /* Raised on TC: the last stop bit of the chunk has left the pin. */
        g_txTail = (uint16_t)((g_txTail + g_txActiveLength) % SERIAL_PORT_TX_BUFFER_SIZE);
        g_txActiveLength = 0U;

        Serial_Port_StartNextChunk();
    }
    else if(AppEvent == USART_EVENT_ORE)
    {
        g_rxOverrun++;
    }
}
