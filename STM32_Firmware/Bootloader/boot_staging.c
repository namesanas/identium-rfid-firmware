#include "boot_staging.h"
#include "boot_flash.h"

#include "../Firmware/Common/firmware_image.h"

#include "stm32f4xx.h"

#include <string.h>

#if FW_STAGING_SPI_FLASH

#include "../Firmware/BSP/f411_custom/board_config.h"

/* ------------------------------------------------------------------
 * F411: GD25Q32 on SPI1, shared with the W5500. Polled, mode 0,
 * 16 MHz HSI / 8 = 2 MHz. The W5500 chip select is held high so the
 * W5500 stays off the bus.
 * ------------------------------------------------------------------ */

#define BOOT_SPI                    BOARD_W5500_SPI

#define GD25Q_CMD_READ              0x03U
#define GD25Q_CMD_PAGE_PROGRAM      0x02U
#define GD25Q_CMD_WRITE_ENABLE      0x06U
#define GD25Q_CMD_READ_STATUS       0x05U
#define GD25Q_CMD_RELEASE_PD        0xABU
#define GD25Q_CMD_JEDEC_ID          0x9FU
#define GD25Q_STATUS_WIP            0x01U
#define GD25Q_MANUFACTURER          0xC8U

static void BootStaging_PinAf(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFUL << ((pin & 7U) * 4U))) |
                          (af << ((pin & 7U) * 4U));
    port->OSPEEDR |= (2UL << (pin * 2U));
    port->MODER = (port->MODER & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U));
}

static void BootStaging_PinOutputHigh(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR = (1UL << pin);
    port->MODER = (port->MODER & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U));
}

static void BootStaging_Select(void)
{
    BOARD_FLASH_CS_PORT->BSRR = (1UL << (BOARD_FLASH_CS_PIN + 16U));
}

static void BootStaging_Deselect(void)
{
    while((BOOT_SPI->SR & SPI_SR_BSY) != 0U)
    {
    }

    BOARD_FLASH_CS_PORT->BSRR = (1UL << BOARD_FLASH_CS_PIN);
}

static uint8_t BootStaging_Transfer(uint8_t value)
{
    while((BOOT_SPI->SR & SPI_SR_TXE) == 0U)
    {
    }

    *(volatile uint8_t *)&BOOT_SPI->DR = value;

    while((BOOT_SPI->SR & SPI_SR_RXNE) == 0U)
    {
    }

    return *(volatile uint8_t *)&BOOT_SPI->DR;
}

static void BootStaging_Address(uint8_t command, uint32_t address)
{
    (void)BootStaging_Transfer(command);
    (void)BootStaging_Transfer((uint8_t)(address >> 16));
    (void)BootStaging_Transfer((uint8_t)(address >> 8));
    (void)BootStaging_Transfer((uint8_t)address);
}

static uint8_t BootStaging_WaitReady(void)
{
    uint32_t polls;
    uint8_t status = GD25Q_STATUS_WIP;

    /* A 4-byte page program takes well under 1 ms. */
    for(polls = 0U; (polls < 100000U) && ((status & GD25Q_STATUS_WIP) != 0U); polls++)
    {
        BootStaging_Select();
        (void)BootStaging_Transfer(GD25Q_CMD_READ_STATUS);
        status = BootStaging_Transfer(0xFFU);
        BootStaging_Deselect();
    }

    return ((status & GD25Q_STATUS_WIP) == 0U) ? 1U : 0U;
}

uint8_t BootStaging_Init(void)
{
    volatile uint32_t delay;
    uint8_t manufacturer;

    /* SCK, MISO, MOSI and both chip selects are all on GPIOA. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    BootStaging_PinOutputHigh(BOARD_W5500_CS_PORT, BOARD_W5500_CS_PIN);
    BootStaging_PinOutputHigh(BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN);
    BootStaging_PinAf(BOARD_W5500_SCK_PORT, BOARD_W5500_SCK_PIN, BOARD_W5500_GPIO_AF);
    BootStaging_PinAf(BOARD_W5500_MISO_PORT, BOARD_W5500_MISO_PIN, BOARD_W5500_GPIO_AF);
    BootStaging_PinAf(BOARD_W5500_MOSI_PORT, BOARD_W5500_MOSI_PIN, BOARD_W5500_GPIO_AF);

    BOOT_SPI->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                    (2UL << SPI_CR1_BR_Pos);
    BOOT_SPI->CR1 |= SPI_CR1_SPE;

    BootStaging_Select();
    (void)BootStaging_Transfer(GD25Q_CMD_RELEASE_PD);
    BootStaging_Deselect();

    /* tRES1 is 20 us. */
    for(delay = 0U; delay < 1000U; delay++)
    {
    }

    BootStaging_Select();
    (void)BootStaging_Transfer(GD25Q_CMD_JEDEC_ID);
    manufacturer = BootStaging_Transfer(0xFFU);
    (void)BootStaging_Transfer(0xFFU);
    (void)BootStaging_Transfer(0xFFU);
    BootStaging_Deselect();

    return (manufacturer == GD25Q_MANUFACTURER) ? 1U : 0U;
}

uint8_t BootStaging_Read(uint32_t offset, uint8_t *data, uint32_t length)
{
    uint32_t i;

    if((offset + length) > FW_STAGING_SIZE)
        return 0U;

    BootStaging_Select();
    BootStaging_Address(GD25Q_CMD_READ, FW_STAGING_ADDRESS + offset);

    for(i = 0U; i < length; i++)
        data[i] = BootStaging_Transfer(0xFFU);

    BootStaging_Deselect();

    return 1U;
}

uint8_t BootStaging_SetState(uint32_t state)
{
    uint32_t i;

    BootStaging_Select();
    (void)BootStaging_Transfer(GD25Q_CMD_WRITE_ENABLE);
    BootStaging_Deselect();

    BootStaging_Select();
    BootStaging_Address(
        GD25Q_CMD_PAGE_PROGRAM,
        FW_STAGING_ADDRESS + FW_IMAGE_STATE_OFFSET
    );

    /* Little-endian, as the header is read. */
    for(i = 0U; i < 4U; i++)
        (void)BootStaging_Transfer((uint8_t)(state >> (i * 8U)));

    BootStaging_Deselect();

    return BootStaging_WaitReady();
}

void BootStaging_Deinit(void)
{
    BOOT_SPI->CR1 = 0U;

    RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
    RCC->AHB1RSTR |= RCC_AHB1RSTR_GPIOARST;
    RCC->AHB1RSTR &= ~RCC_AHB1RSTR_GPIOARST;

    RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;
    RCC->AHB1ENR &= ~RCC_AHB1ENR_GPIOAEN;
}

#else

/* ------------------------------------------------------------------
 * F429: staging in internal flash bank 2, memory mapped.
 * ------------------------------------------------------------------ */

uint8_t BootStaging_Init(void)
{
    return 1U;
}

uint8_t BootStaging_Read(uint32_t offset, uint8_t *data, uint32_t length)
{
    if((offset + length) > FW_STAGING_SIZE)
        return 0U;

    memcpy(data, (const void *)(FW_STAGING_ADDRESS + offset), length);

    return 1U;
}

uint8_t BootStaging_SetState(uint32_t state)
{
    uint8_t ok;

    BootFlash_Unlock();
    ok = BootFlash_ProgramWords(FW_STAGING_ADDRESS + FW_IMAGE_STATE_OFFSET, &state, 1U);
    BootFlash_Lock();

    return ok;
}

void BootStaging_Deinit(void)
{
}

#endif
