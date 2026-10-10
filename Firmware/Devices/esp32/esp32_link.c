#include "esp32_link.h"

#include "../../Config/firmware_config.h"
#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "../../BSP/f429_discovery/board_config.h"
#else
#include "../../BSP/f411_custom/board_config.h"
#endif
#include "../../Drivers/gpio/gpio.h"
#include "../../Drivers/i2c/i2c.h"
#include "../../Drivers/rcc/rcc.h"
#include "../../Services/Time/time_service.h"

#include <stddef.h>
#include <string.h>

#define ESP32_LINK_TRANSFER_TIMEOUT_MS  80U
#define ESP32_LINK_RESPONSE_DELAY_MS     2U
#define ESP32_LINK_RETRY_LIMIT           2U
#define ESP32_LINK_FAILURE_RESET_LIMIT   3U
#define ESP32_LINK_RESET_LOW_MS         15U
#define ESP32_LINK_BOOT_DELAY_MS       250U
#define ESP32_LINK_BUS_RECOVERY_PULSES   9U

#define ESP32_LINK_SUPPORTED BOARD_ESP_LINK_ENABLED
#define ESP32_I2C_INSTANCE BOARD_ESP_I2C
#define ESP32_I2C_EVENT_IRQn BOARD_ESP_I2C_EVENT_IRQ
#define ESP32_I2C_ERROR_IRQn BOARD_ESP_I2C_ERROR_IRQ
#define ESP32_SCL_GPIO BOARD_ESP_SCL_PORT
#define ESP32_SCL_PIN BOARD_ESP_SCL_PIN
#define ESP32_SDA_GPIO BOARD_ESP_SDA_PORT
#define ESP32_SDA_PIN BOARD_ESP_SDA_PIN
#define ESP32_I2C_AF BOARD_ESP_GPIO_AF
#define ESP32_EN_GPIO BOARD_ESP_EN_PORT
#define ESP32_EN_PIN BOARD_ESP_EN_PIN

typedef enum
{
    ESP32_LINK_IO_IDLE = 0U,
    ESP32_LINK_IO_TX,
    ESP32_LINK_IO_WAIT_RESPONSE,
    ESP32_LINK_IO_RX,
    ESP32_LINK_IO_DONE,
    ESP32_LINK_IO_FAILED,
    ESP32_LINK_IO_RESET_LOW,
    ESP32_LINK_IO_RESET_WAIT,
    ESP32_LINK_IO_BUS_RECOVERY
} ESP32_LinkIoState_t;

static I2C_Handle_t g_i2c;
static uint8_t g_txFrame[ESP32_LINK_MAX_FRAME_SIZE];
static uint8_t g_rxFrame[ESP32_LINK_RESPONSE_SIZE];
static uint8_t g_resultPayload[ESP32_LINK_RESPONSE_MAX_PAYLOAD];
static uint16_t g_resultLength;
static uint8_t g_requestCommand;
static uint8_t g_requestSocket;
static uint8_t g_sequence;
static uint8_t g_retryCount;
static uint8_t g_failureCount;
static uint8_t g_resetRequested;
static uint8_t g_busRecoveryPulse;
static uint8_t g_busRecoveryPhase;
static uint8_t g_transferEvent;
static uint32_t g_stateAtMs;
static uint32_t g_transferStartedMs;
static uint32_t g_errorCount;
static ESP32_LinkIoState_t g_ioState = ESP32_LINK_IO_IDLE;
static uint8_t g_retryPendingTransmit;
static uint8_t g_initialized;

#define ESP32_LINK_EVENT_NONE       0U
#define ESP32_LINK_EVENT_TX_DONE    1U
#define ESP32_LINK_EVENT_RX_DONE    2U
#define ESP32_LINK_EVENT_ERROR      3U

static void ESP32_Link_SetPinsToAlternate(void)
{
#if ESP32_LINK_SUPPORTED
    GPIO_Handle_t gpio;

    memset(&gpio, 0, sizeof(gpio));
    gpio.pGPIOx = ESP32_SCL_GPIO;
    gpio.GPIO_PinConfig.GPIO_PinNumber = ESP32_SCL_PIN;
    gpio.GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_ALTFN;
    gpio.GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_FAST;
    gpio.GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
    gpio.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_OD;
    gpio.GPIO_PinConfig.GPIO_PinAltFunMode = ESP32_I2C_AF;
    GPIO_Init(&gpio);

    gpio.pGPIOx = ESP32_SDA_GPIO;
    gpio.GPIO_PinConfig.GPIO_PinNumber = ESP32_SDA_PIN;
    GPIO_Init(&gpio);
#endif
}

static void ESP32_Link_StartBusRecovery(void)
{
#if ESP32_LINK_SUPPORTED
    I2C_PeripheralControl(ESP32_I2C_INSTANCE, DISABLE);
    GPIO_Handle_t gpio;

    memset(&gpio, 0, sizeof(gpio));
    gpio.GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_OUT;
    gpio.GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_FAST;
    gpio.GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
    gpio.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_OD;
    gpio.pGPIOx = ESP32_SCL_GPIO;
    gpio.GPIO_PinConfig.GPIO_PinNumber = ESP32_SCL_PIN;
    GPIO_Init(&gpio);
    gpio.pGPIOx = ESP32_SDA_GPIO;
    gpio.GPIO_PinConfig.GPIO_PinNumber = ESP32_SDA_PIN;
    GPIO_Init(&gpio);
    GPIO_WriteToOutputPin(ESP32_SCL_GPIO, ESP32_SCL_PIN, GPIO_PIN_SET);
    GPIO_WriteToOutputPin(ESP32_SDA_GPIO, ESP32_SDA_PIN, GPIO_PIN_SET);
    g_busRecoveryPulse = 0U;
    g_busRecoveryPhase = 0U;
    g_ioState = ESP32_LINK_IO_BUS_RECOVERY;
    g_stateAtMs = Time_GetTickMs();
#endif
}

static void ESP32_Link_FinishBusRecovery(void)
{
#if ESP32_LINK_SUPPORTED
    /* SCL high, SDA low-to-high generates a STOP before restoring AF mode. */
    GPIO_WriteToOutputPin(ESP32_SCL_GPIO, ESP32_SCL_PIN, GPIO_PIN_SET);
    GPIO_WriteToOutputPin(ESP32_SDA_GPIO, ESP32_SDA_PIN, GPIO_PIN_RESET);
    GPIO_WriteToOutputPin(ESP32_SDA_GPIO, ESP32_SDA_PIN, GPIO_PIN_SET);
    I2C_DeInit(ESP32_I2C_INSTANCE);
    ESP32_Link_SetPinsToAlternate();
    (void)I2C_InitEx(&g_i2c);
    g_i2c.TxRxState = I2C_READY;
    g_i2c.LastError = I2C_STATUS_OK;
    g_ioState = ESP32_LINK_IO_RESET_LOW;
    GPIO_WriteToOutputPin(ESP32_EN_GPIO, ESP32_EN_PIN, GPIO_PIN_RESET);
    g_stateAtMs = Time_GetTickMs();
#endif
}

static uint8_t ESP32_Link_BuildFrame(uint8_t command, uint8_t socket,
                                     const uint8_t *payload, uint16_t length)
{
    uint16_t crc;
    uint16_t frameLength;

    if ((length > ESP32_LINK_MAX_PAYLOAD) || ((payload == NULL) && (length != 0U)))
    {
        return 0U;
    }

    g_txFrame[0] = ESP32_LINK_MAGIC_0;
    g_txFrame[1] = ESP32_LINK_MAGIC_1;
    g_txFrame[2] = ESP32_LINK_PROTOCOL_VERSION;
    g_txFrame[3] = g_sequence;
    g_txFrame[4] = command;
    g_txFrame[5] = socket;
    g_txFrame[6] = (uint8_t)(length & 0xFFU);
    g_txFrame[7] = (uint8_t)(length >> 8U);
    g_txFrame[8] = 0U;
    if ((payload != NULL) && (length > 0U))
    {
        memcpy(&g_txFrame[ESP32_LINK_HEADER_SIZE], payload, length);
    }
    frameLength = (uint16_t)(ESP32_LINK_HEADER_SIZE + length);
    crc = ESP32_LinkProtocol_Crc16(g_txFrame, frameLength);
    g_txFrame[frameLength++] = (uint8_t)(crc & 0xFFU);
    g_txFrame[frameLength] = (uint8_t)(crc >> 8U);
    return 1U;
}

static uint8_t ESP32_Link_StartTransmit(void)
{
#if ESP32_LINK_SUPPORTED
    uint16_t length = (uint16_t)(ESP32_LINK_HEADER_SIZE +
        ((uint16_t)g_txFrame[6] | ((uint16_t)g_txFrame[7] << 8U)) + ESP32_LINK_CRC_SIZE);
    uint8_t result;

    result = I2C_MasterSendDataIT(&g_i2c, g_txFrame, length,
                                  ESP32_LINK_I2C_ADDRESS, I2C_DISABLE_SR);
    if (result != I2C_STATUS_OK)
    {
        return 0U;
    }
    g_transferEvent = ESP32_LINK_EVENT_NONE;
    g_ioState = ESP32_LINK_IO_TX;
    g_transferStartedMs = Time_GetTickMs();
    return 1U;
#else
    return 0U;
#endif
}

uint8_t ESP32_Link_Init(void)
{
#if ESP32_LINK_SUPPORTED
    GPIO_Handle_t gpio;
    memset(&g_i2c, 0, sizeof(g_i2c));
    memset(&gpio, 0, sizeof(gpio));

    gpio.pGPIOx = ESP32_EN_GPIO;
    gpio.GPIO_PinConfig.GPIO_PinNumber = ESP32_EN_PIN;
    gpio.GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_OUT;
    gpio.GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_FAST;
    gpio.GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
    gpio.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;
    GPIO_Init(&gpio);
    GPIO_WriteToOutputPin(ESP32_EN_GPIO, ESP32_EN_PIN, GPIO_PIN_SET);

    ESP32_Link_SetPinsToAlternate();

    g_i2c.pI2Cx = ESP32_I2C_INSTANCE;
    g_i2c.I2C_Config.I2C_SCLSpeed = I2C_SCL_SPEED_FM4K;
    g_i2c.I2C_Config.I2C_DeviceAddress = 0U;
    g_i2c.I2C_Config.I2C_ACKControl = I2C_ACK_ENABLE;
    g_i2c.I2C_Config.I2C_FMDutyCycle = I2C_FM_DUTY_2;
    if (I2C_InitEx(&g_i2c) != I2C_STATUS_OK)
    {
        g_ioState = ESP32_LINK_IO_FAILED;
        return 0U;
    }

    I2C_IRQPriorityConfig((uint8_t)ESP32_I2C_EVENT_IRQn, 6U);
    I2C_IRQPriorityConfig((uint8_t)ESP32_I2C_ERROR_IRQn, 6U);
    I2C_IRQInterruptConfig((uint8_t)ESP32_I2C_EVENT_IRQn, ENABLE);
    I2C_IRQInterruptConfig((uint8_t)ESP32_I2C_ERROR_IRQn, ENABLE);
    g_initialized = 1U;
    g_ioState = ESP32_LINK_IO_RESET_WAIT;
    g_stateAtMs = Time_GetTickMs();
    g_resetRequested = 0U;
    g_failureCount = 0U;
    g_errorCount = 0U;
    return 1U;
#else
    g_initialized = 0U;
    g_ioState = ESP32_LINK_IO_FAILED;
    return 0U;
#endif
}

uint8_t ESP32_Link_IsSupported(void)
{
    return ESP32_LINK_SUPPORTED;
}

uint8_t ESP32_Link_IsBooted(void)
{
    return (g_initialized != 0U) && (g_ioState == ESP32_LINK_IO_IDLE);
}

uint8_t ESP32_Link_IsBusy(void)
{
    return (g_ioState == ESP32_LINK_IO_TX) ||
           (g_ioState == ESP32_LINK_IO_WAIT_RESPONSE) ||
           (g_ioState == ESP32_LINK_IO_RX) ||
           (g_ioState == ESP32_LINK_IO_BUS_RECOVERY) ||
           (g_ioState == ESP32_LINK_IO_RESET_LOW) ||
           (g_ioState == ESP32_LINK_IO_RESET_WAIT);
}

uint8_t ESP32_Link_Submit(uint8_t command, uint8_t socket,
                          const uint8_t *payload, uint16_t length)
{
#if ESP32_LINK_SUPPORTED
    if ((g_initialized == 0U) || (g_ioState != ESP32_LINK_IO_IDLE) ||
        (length > ESP32_LINK_MAX_PAYLOAD) || (socket >= ESP32_LINK_SOCKET_COUNT))
    {
        return 0U;
    }

    g_sequence++;
    g_requestCommand = command;
    g_requestSocket = socket;
    g_resultLength = 0U;
    g_retryCount = 0U;
    g_retryPendingTransmit = 0U;
    if (ESP32_Link_BuildFrame(command, socket, payload, length) == 0U)
    {
        return 0U;
    }
    if (ESP32_Link_StartTransmit() == 0U)
    {
        /* No transaction was accepted, so keep the public state idle. The
         * service has not marked a command in flight and cannot consume a
         * FAILED result for this submission. */
        g_errorCount++;
        g_failureCount++;
        if (g_failureCount >= ESP32_LINK_FAILURE_RESET_LIMIT)
            g_resetRequested = 1U;
        return 0U;
    }
    return 1U;
#else
    (void)command;
    (void)socket;
    (void)payload;
    (void)length;
    return 0U;
#endif
}

static uint8_t ESP32_Link_ValidateResponse(void)
{
    uint16_t payloadLength;
    uint16_t crcIndex;
    uint16_t expectedCrc;
    uint16_t actualCrc;

    if ((g_rxFrame[0] != ESP32_LINK_MAGIC_0) ||
        (g_rxFrame[1] != ESP32_LINK_MAGIC_1) ||
        (g_rxFrame[2] != ESP32_LINK_PROTOCOL_VERSION) ||
        (g_rxFrame[3] != g_sequence) ||
        (g_rxFrame[4] != g_requestCommand) ||
        (g_rxFrame[5] != g_requestSocket) ||
        ((g_rxFrame[8] & ESP32_LINK_FLAG_RESPONSE) == 0U))
    {
        return 0U;
    }

    payloadLength = (uint16_t)g_rxFrame[6] | ((uint16_t)g_rxFrame[7] << 8U);
    if (payloadLength > ESP32_LINK_RESPONSE_MAX_PAYLOAD)
    {
        return 0U;
    }
    crcIndex = (uint16_t)(ESP32_LINK_HEADER_SIZE + payloadLength);
    expectedCrc = (uint16_t)g_rxFrame[crcIndex] |
                  ((uint16_t)g_rxFrame[crcIndex + 1U] << 8U);
    actualCrc = ESP32_LinkProtocol_Crc16(g_rxFrame, crcIndex);
    if (actualCrc != expectedCrc)
    {
        return 0U;
    }
    g_resultLength = payloadLength;
    if (payloadLength > 0U)
    {
        memcpy(g_resultPayload, &g_rxFrame[ESP32_LINK_HEADER_SIZE], payloadLength);
    }
    if ((g_rxFrame[8] & ESP32_LINK_FLAG_NOT_READY) != 0U)
    {
        return 2U;
    }
    return 1U;
}

void ESP32_Link_Task(void)
{
#if ESP32_LINK_SUPPORTED
    uint32_t now = Time_GetTickMs();

    if (g_initialized == 0U)
    {
        return;
    }

    if (g_ioState == ESP32_LINK_IO_BUS_RECOVERY)
    {
        /* One open-drain edge per tick keeps bus recovery out of a busy wait. */
        if (g_busRecoveryPulse < ESP32_LINK_BUS_RECOVERY_PULSES)
        {
            if (g_busRecoveryPhase == 0U)
            {
                GPIO_WriteToOutputPin(ESP32_SCL_GPIO, ESP32_SCL_PIN, GPIO_PIN_RESET);
                g_busRecoveryPhase = 1U;
            }
            else
            {
                GPIO_WriteToOutputPin(ESP32_SCL_GPIO, ESP32_SCL_PIN, GPIO_PIN_SET);
                g_busRecoveryPhase = 0U;
                g_busRecoveryPulse++;
            }
        }
        else
        {
            ESP32_Link_FinishBusRecovery();
        }
        return;
    }

    if (g_ioState == ESP32_LINK_IO_RESET_LOW)
    {
        if ((uint32_t)(now - g_stateAtMs) >= ESP32_LINK_RESET_LOW_MS)
        {
            GPIO_WriteToOutputPin(ESP32_EN_GPIO, ESP32_EN_PIN, GPIO_PIN_SET);
            g_ioState = ESP32_LINK_IO_RESET_WAIT;
            g_stateAtMs = now;
        }
        return;
    }

    if (g_ioState == ESP32_LINK_IO_RESET_WAIT)
    {
        if ((int32_t)(now - g_stateAtMs) >= (int32_t)ESP32_LINK_BOOT_DELAY_MS)
        {
            g_ioState = ESP32_LINK_IO_IDLE;
        }
        return;
    }

    if (g_resetRequested != 0U)
    {
        g_resetRequested = 0U;
        ESP32_Link_StartBusRecovery();
        return;
    }

    if ((g_ioState == ESP32_LINK_IO_TX) || (g_ioState == ESP32_LINK_IO_RX))
    {
        if ((uint32_t)(now - g_transferStartedMs) > ESP32_LINK_TRANSFER_TIMEOUT_MS)
        {
            g_errorCount++;
            g_failureCount++;
            g_ioState = ESP32_LINK_IO_FAILED;
            if (g_failureCount >= ESP32_LINK_FAILURE_RESET_LIMIT)
            {
                g_resetRequested = 1U;
            }
        }
        else if (g_transferEvent == ESP32_LINK_EVENT_ERROR)
        {
            g_errorCount++;
            g_failureCount++;
            g_ioState = ESP32_LINK_IO_FAILED;
            if (g_failureCount >= ESP32_LINK_FAILURE_RESET_LIMIT)
            {
                g_resetRequested = 1U;
            }
        }
        else if ((g_ioState == ESP32_LINK_IO_TX) &&
                 (g_transferEvent == ESP32_LINK_EVENT_TX_DONE))
        {
            g_ioState = ESP32_LINK_IO_WAIT_RESPONSE;
            g_stateAtMs = now + ESP32_LINK_RESPONSE_DELAY_MS;
            g_transferEvent = ESP32_LINK_EVENT_NONE;
        }
        else if ((g_ioState == ESP32_LINK_IO_RX) &&
                 (g_transferEvent == ESP32_LINK_EVENT_RX_DONE))
        {
            uint8_t validated;
            g_transferEvent = ESP32_LINK_EVENT_NONE;
            validated = ESP32_Link_ValidateResponse();
            if (validated == 1U)
            {
                g_failureCount = 0U;
                g_ioState = ESP32_LINK_IO_DONE;
            }
            else if ((validated == 2U) && (g_retryCount < ESP32_LINK_RETRY_LIMIT))
            {
                g_retryCount++;
                g_retryPendingTransmit = 1U;
                g_ioState = ESP32_LINK_IO_WAIT_RESPONSE;
                g_stateAtMs = now + 1U;
            }
            else
            {
                g_errorCount++;
                g_failureCount++;
                g_ioState = ESP32_LINK_IO_FAILED;
                if (g_failureCount >= ESP32_LINK_FAILURE_RESET_LIMIT)
                {
                    g_resetRequested = 1U;
                }
            }
        }
        return;
    }

    if (g_ioState == ESP32_LINK_IO_WAIT_RESPONSE)
    {
        if ((int32_t)(now - g_stateAtMs) >= 0)
        {
            if (g_retryPendingTransmit != 0U)
            {
                /* A NOT_READY response is retried by reissuing the same request. */
                g_retryPendingTransmit = 0U;
                if (ESP32_Link_StartTransmit() == 0U)
                {
                    g_errorCount++;
                    g_failureCount++;
                    g_ioState = ESP32_LINK_IO_FAILED;
                    if (g_failureCount >= ESP32_LINK_FAILURE_RESET_LIMIT)
                        g_resetRequested = 1U;
                }
                return;
            }
            memset(g_rxFrame, 0, sizeof(g_rxFrame));
            g_transferEvent = ESP32_LINK_EVENT_NONE;
            if (I2C_MasterReceiverDataIT(&g_i2c, g_rxFrame,
                                        ESP32_LINK_RESPONSE_SIZE,
                                        ESP32_LINK_I2C_ADDRESS, I2C_DISABLE_SR) == I2C_STATUS_OK)
            {
                g_ioState = ESP32_LINK_IO_RX;
                g_transferStartedMs = now;
            }
            else
            {
                g_errorCount++;
                g_failureCount++;
                g_ioState = ESP32_LINK_IO_FAILED;
            }
        }
        return;
    }
#endif
}

ESP32_LinkState_t ESP32_Link_GetResult(uint8_t *command, uint8_t *socket,
                                       uint8_t *payload, uint16_t *length,
                                       uint16_t capacity)
{
    if (g_ioState == ESP32_LINK_IO_DONE)
    {
        if (command != NULL) *command = g_requestCommand;
        if (socket != NULL) *socket = g_requestSocket;
        if ((length != NULL) && (payload != NULL))
        {
            uint16_t copyLength = g_resultLength;
            if (copyLength > capacity) copyLength = capacity;
            memcpy(payload, g_resultPayload, copyLength);
            *length = copyLength;
        }
        return ESP32_LINK_STATE_DONE;
    }
    if (g_ioState == ESP32_LINK_IO_FAILED)
    {
        if (command != NULL) *command = g_requestCommand;
        if (socket != NULL) *socket = g_requestSocket;
        if (length != NULL) *length = 0U;
        return ESP32_LINK_STATE_FAILED;
    }
    return ESP32_Link_IsBusy() ? ESP32_LINK_STATE_BUSY : ESP32_LINK_STATE_IDLE;
}

void ESP32_Link_ConsumeResult(void)
{
    if ((g_ioState == ESP32_LINK_IO_DONE) || (g_ioState == ESP32_LINK_IO_FAILED))
    {
        g_ioState = ESP32_LINK_IO_IDLE;
        g_resultLength = 0U;
    }
}

void ESP32_Link_RequestReset(void)
{
    g_resetRequested = 1U;
}

uint32_t ESP32_Link_GetErrorCount(void)
{
    return g_errorCount;
}

void I2C_ApplicationEventCallback(I2C_Handle_t *pI2CHandle, uint8_t event)
{
#if ESP32_LINK_SUPPORTED
    if (pI2CHandle != &g_i2c)
    {
        return;
    }
    if (event == I2C_EV_TX_CMPLT)
    {
        g_transferEvent = ESP32_LINK_EVENT_TX_DONE;
    }
    else if (event == I2C_EV_RX_CMPLT)
    {
        g_transferEvent = ESP32_LINK_EVENT_RX_DONE;
    }
    else if ((event >= I2C_ERROR_BERR) && (event <= I2C_ERROR_TIMEOUT))
    {
        g_transferEvent = ESP32_LINK_EVENT_ERROR;
    }
#else
    (void)pI2CHandle;
    (void)event;
#endif
}

#if ESP32_LINK_SUPPORTED
void I2C3_EV_IRQHandler(void)
{
    I2C_EV_IRQHandling(&g_i2c);
}

void I2C3_ER_IRQHandler(void)
{
    I2C_ER_IRQHandling(&g_i2c);
}
#endif
