#include "board.h"
#include "board_config.h"

static void GPIO_ConfigOutput(
    GPIO_Handle_t *pGPIOHandle,
    GPIO_RegDef_t *pGPIOx,
    uint8_t pin
)
{
    pGPIOHandle->pGPIOx = pGPIOx;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinNumber = pin;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_OUT;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_FAST;
    pGPIOHandle->GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;

    GPIO_Init(pGPIOHandle);
}

static void GPIO_ConfigAlternate(
    GPIO_Handle_t *pGPIOHandle,
    GPIO_RegDef_t *pGPIOx,
    uint8_t pin,
    uint8_t alternateFunction,
    uint8_t pull
)
{
    pGPIOHandle->pGPIOx = pGPIOx;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinNumber = pin;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_ALTFN;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_FAST;
    pGPIOHandle->GPIO_PinConfig.GPIO_PuPdControl = pull;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;
    pGPIOHandle->GPIO_PinConfig.GPIO_PinAltFunMode = alternateFunction;

    GPIO_Init(pGPIOHandle);
}

void Board_F429_RFID_UART_GPIO_Init(void)
{
    GPIO_Handle_t gpio;

    GPIO_ConfigAlternate(
        &gpio,
        BOARD_RFID_TX_PORT,
        BOARD_RFID_TX_PIN,
        BOARD_RFID_GPIO_AF,
        GPIO_PIN_PU
    );

    GPIO_ConfigAlternate(
        &gpio,
        BOARD_RFID_RX_PORT,
        BOARD_RFID_RX_PIN,
        BOARD_RFID_GPIO_AF,
        GPIO_PIN_PU
    );
}

void Board_F429_Host_UART_GPIO_Init(void)
{
    GPIO_Handle_t gpio;

    GPIO_ConfigAlternate(
        &gpio,
        BOARD_HOST_TX_PORT,
        BOARD_HOST_TX_PIN,
        BOARD_HOST_GPIO_AF,
        GPIO_PIN_PU
    );

    GPIO_ConfigAlternate(
        &gpio,
        BOARD_HOST_RX_PORT,
        BOARD_HOST_RX_PIN,
        BOARD_HOST_GPIO_AF,
        GPIO_PIN_PU
    );
}

void Board_F429_W5500_GPIO_Init(void)
{
    GPIO_Handle_t gpio;

    /* W5500 CS */
    GPIO_ConfigOutput(
        &gpio,
        BOARD_W5500_CS_PORT,
        BOARD_W5500_CS_PIN
    );

    GPIO_WriteToOutputPin(
        BOARD_W5500_CS_PORT,
        BOARD_W5500_CS_PIN,
        GPIO_PIN_SET
    );

    /* SPI1 SCK */
    GPIO_ConfigAlternate(
        &gpio,
        BOARD_W5500_SCK_PORT,
        BOARD_W5500_SCK_PIN,
        BOARD_W5500_GPIO_AF,
        GPIO_NO_PUPD
    );

    /* SPI1 MISO */
    GPIO_ConfigAlternate(
        &gpio,
        BOARD_W5500_MISO_PORT,
        BOARD_W5500_MISO_PIN,
        BOARD_W5500_GPIO_AF,
        GPIO_PIN_PU
    );

    /* SPI1 MOSI */
    GPIO_ConfigAlternate(
        &gpio,
        BOARD_W5500_MOSI_PORT,
        BOARD_W5500_MOSI_PIN,
        BOARD_W5500_GPIO_AF,
        GPIO_NO_PUPD
    );

    /* W5500 reset */
    GPIO_ConfigOutput(
        &gpio,
        BOARD_W5500_RESET_PORT,
        BOARD_W5500_RESET_PIN
    );

    GPIO_WriteToOutputPin(
        BOARD_W5500_RESET_PORT,
        BOARD_W5500_RESET_PIN,
        GPIO_PIN_SET
    );
}

void Board_F429_GPIO_Init(void)
{
    GPIO_Handle_t gpio;

    /* RFID enable - active HIGH on the current F429 prototype. */
    GPIO_ConfigOutput(
        &gpio,
        BOARD_RFID_ENABLE_PORT,
        BOARD_RFID_ENABLE_PIN
    );

    GPIO_WriteToOutputPin(
        BOARD_RFID_ENABLE_PORT,
        BOARD_RFID_ENABLE_PIN,
        GPIO_PIN_SET
    );

    Board_F429_RFID_UART_GPIO_Init();
    Board_F429_Host_UART_GPIO_Init();
    Board_F429_W5500_GPIO_Init();
}

void Board_F429_RFID_UART_Init(USART_Handle_t *pUSART)
{
    pUSART->pUSARTx = BOARD_RFID_USART;

    pUSART->USART_Config.USART_Mode = USART_MODE_TXRX;
    pUSART->USART_Config.USART_Baud = USART_STD_BAUD_115200;
    pUSART->USART_Config.USART_NoOfStopBits = USART_STOPBITS_1;
    pUSART->USART_Config.USART_WordLength = USART_WORDLEN_8BITS;
    pUSART->USART_Config.USART_ParityControl = USART_PARITY_DISABLE;
    pUSART->USART_Config.USART_HWFlowControl = USART_HW_FLOW_CTRL_NONE;
    pUSART->USART_Config.USART_OverSampling = USART_OVERSAMPLING_16;

    USART_Init(pUSART);
}

void Board_F429_Host_UART_Init(USART_Handle_t *pUSART)
{
    pUSART->pUSARTx = BOARD_HOST_USART;

    pUSART->USART_Config.USART_Mode = USART_MODE_TXRX;
    pUSART->USART_Config.USART_Baud = USART_STD_BAUD_115200;
    pUSART->USART_Config.USART_NoOfStopBits = USART_STOPBITS_1;
    pUSART->USART_Config.USART_WordLength = USART_WORDLEN_8BITS;
    pUSART->USART_Config.USART_ParityControl = USART_PARITY_DISABLE;
    pUSART->USART_Config.USART_HWFlowControl = USART_HW_FLOW_CTRL_NONE;
    pUSART->USART_Config.USART_OverSampling = USART_OVERSAMPLING_16;

    USART_Init(pUSART);
}

void Board_F429_W5500_SPI_Init(SPI_Handle_t *pSPI)
{
    pSPI->pSPIx = BOARD_W5500_SPI;

    pSPI->SPIConfig.SPI_DeviceMode = SPI_DEVICE_MODE_MASTER;
    pSPI->SPIConfig.SPI_BusConfig = SPI_BUS_CONFIG_FD;
    pSPI->SPIConfig.SPI_SclkSpeed = SPI_SCLK_SPEED_DIV8;
    pSPI->SPIConfig.SPI_DFF = SPI_DFF_8BITS;
    pSPI->SPIConfig.SPI_CPOL = SPI_CPOL_LOW;
    pSPI->SPIConfig.SPI_CPHA = SPI_CPHA_LOW;
    pSPI->SPIConfig.SPI_SSM = SPI_SSM_EN;

    SPI_Init(pSPI);
    SPI_SSIConfig(BOARD_W5500_SPI, ENABLE);
    SPI_PeripheralControl(BOARD_W5500_SPI, ENABLE);
}

void Board_F429_Init(
    USART_Handle_t *pRFIDUart,
    USART_Handle_t *pHostUart,
    SPI_Handle_t *pW5500Spi
)
{
    Board_F429_GPIO_Init();
    Board_F429_RFID_UART_Init(pRFIDUart);
    Board_F429_Host_UART_Init(pHostUart);
    Board_F429_W5500_SPI_Init(pW5500Spi);
}
