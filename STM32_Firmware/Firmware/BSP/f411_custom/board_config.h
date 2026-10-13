/*
 * board_config.h
 *
 *  Created on: 26-Sept-2026
 *      Author: Identium
 */

#ifndef F411_CUSTOM_BOARD_CONFIG_H_
#define F411_CUSTOM_BOARD_CONFIG_H_


#include <stddef.h>
#include "stm32f411xe.h"

/* ============================================================
 * STM32F411CEU6 (UFQFPN-48) custom industrial RFID controller
 *
 * Pin map traced from docs/hardware/SCH_Schematic1_2026-10-13.pdf.
 * HSE crystal X1 = 8 MHz, W5500 crystal X2 = 25 MHz. No LSE
 * (PC14/PC15 not connected). SWD on CN7 (PA13/PA14, NRST).
 * See docs/notes/f411-custom-board-port.md.
 * ============================================================ */

/* ---------------- RFID / SILION (CN1) ---------------- */
/* CN1 pin 9 = module RXD, pin 10 = module TXD. The module EN pin (CN1-5)
 * is tied to 3V3, so there is no enable GPIO. */
#define BOARD_RFID_USART              USART1
#define BOARD_RFID_TX_PORT            GPIOA
#define BOARD_RFID_TX_PIN             9U
#define BOARD_RFID_RX_PORT            GPIOA
#define BOARD_RFID_RX_PIN             10U
#define BOARD_RFID_ENABLE_PORT        NULL
#define BOARD_RFID_ENABLE_PIN         0xFFU
#define BOARD_RFID_GPIO_AF            7U

/* PB5 goes to CN1 pin 11 with a pull-up (R32). Leave it as an input so
 * the pull-up holds the line high; never drive it low. */
#define BOARD_RFID_PIN11_PORT         GPIOB
#define BOARD_RFID_PIN11_PIN          5U

/* ---------------- Shared RS232 / RS485 ---------------- */
/* USART2 reaches either transceiver through the U12 SN74CB3Q3257 mux. */
#define BOARD_HOST_USART              USART2
#define BOARD_HOST_TX_PORT            GPIOA
#define BOARD_HOST_TX_PIN             2U
#define BOARD_HOST_RX_PORT            GPIOA
#define BOARD_HOST_RX_PIN             3U
#define BOARD_HOST_GPIO_AF            7U

/* Mux select: low = RS232 (MAX3232), high = RS485 (SP3485).
 * R18 pulls it high, so the hardware default is RS485. */
#define BOARD_SERIAL_MODE_PORT        GPIOB
#define BOARD_SERIAL_MODE_PIN         14U

/* SP3485 DE (active high, R21 pull-down). */
#define BOARD_RS485_DE_RE_PORT        GPIOB
#define BOARD_RS485_DE_RE_PIN         12U

/* SP3485 RE# (active low). R20 pulls it HIGH, which disables the
 * receiver: firmware must drive it low to receive in RS485 mode. */
#define BOARD_RS485_RE_N_PORT         GPIOB
#define BOARD_RS485_RE_N_PIN          13U

/* TPS22919 load switch ON pin feeding MAX3232 VCC (R19 pull-up, on by
 * default). High = RS232 transceiver powered. */
#define BOARD_RS232_POWER_PORT        GPIOA
#define BOARD_RS232_POWER_PIN         8U

#define BOARD_HOST_USART_IRQ          38U   /* USART2_IRQn */
#define BOARD_RFID_USART_IRQ          37U   /* USART1_IRQn */

/* ---------------- ESP32-C3 WiFi coprocessor (U19) ---------------- */
/* PB6 = IO6 (SCL, R28 4.7k), PB7 = IO10 (SDA, R30 4.7k), PB9 = EN
 * (R17 pull-up, C66). No data-ready line: the link is polled. */
#define BOARD_ESP_LINK_ENABLED         1U
#define BOARD_ESP_I2C                  I2C1
#define BOARD_ESP_SCL_PORT             GPIOB
#define BOARD_ESP_SCL_PIN              6U
#define BOARD_ESP_SDA_PORT             GPIOB
#define BOARD_ESP_SDA_PIN              7U
#define BOARD_ESP_GPIO_AF              4U
#define BOARD_ESP_EN_PORT              GPIOB
#define BOARD_ESP_EN_PIN               9U
#define BOARD_ESP_I2C_EVENT_IRQ        I2C1_EV_IRQn
#define BOARD_ESP_I2C_ERROR_IRQ        I2C1_ER_IRQn
#define BOARD_ESP_I2C_EV_HANDLER       I2C1_EV_IRQHandler
#define BOARD_ESP_I2C_ER_HANDLER       I2C1_ER_IRQHandler

/* ---------------- W5500 (U1) ---------------- */
#define BOARD_W5500_SPI               SPI1
#define BOARD_W5500_SCK_PORT          GPIOA
#define BOARD_W5500_SCK_PIN           5U
#define BOARD_W5500_MISO_PORT         GPIOA
#define BOARD_W5500_MISO_PIN          6U
#define BOARD_W5500_MOSI_PORT         GPIOA
#define BOARD_W5500_MOSI_PIN          7U
#define BOARD_W5500_GPIO_AF           5U
#define BOARD_W5500_CS_PORT           GPIOA
#define BOARD_W5500_CS_PIN            4U
#define BOARD_W5500_INT_PORT          GPIOB
#define BOARD_W5500_INT_PIN           0U
#define BOARD_W5500_RESET_PORT        GPIOB
#define BOARD_W5500_RESET_PIN         1U

/* ---------------- GD25Q32E 4 MB SPI flash ---------------- */
/* Shares SPI1 (PA5/PA6/PA7) with the W5500; own chip select on PA1.
 * Holds the tag read backup log (Services/storage/tag_log.c). */
#define BOARD_TAG_LOG_ENABLED         1U
#define BOARD_FLASH_CS_PORT           GPIOA
#define BOARD_FLASH_CS_PIN            1U

/* ---------------- USB (USB-C1, OTG_FS) ---------------- */
/* No VBUS sense GPIO on this board: VBUS sensing must stay disabled. */
#define BOARD_USB_DM_PORT             GPIOA
#define BOARD_USB_DM_PIN              11U
#define BOARD_USB_DP_PORT             GPIOA
#define BOARD_USB_DP_PIN              12U
#define BOARD_USB_VBUS_PORT           NULL
#define BOARD_USB_VBUS_PIN            0xFFU

/* ---------------- Industrial Inputs ---------------- */
/* Each input goes through an NPN stage (Q5-Q8) with a pull-up on the
 * MCU side, so the pin reads LOW when the field input is active. */
#define BOARD_INPUTS_ACTIVE_LOW       1U
#define BOARD_INPUT1_PORT             GPIOB   /* CN10 pin 1 */
#define BOARD_INPUT1_PIN              3U
#define BOARD_INPUT2_PORT             GPIOA   /* CN10 pin 2 */
#define BOARD_INPUT2_PIN              15U
#define BOARD_INPUT3_PORT             GPIOB   /* CN10 pin 3 */
#define BOARD_INPUT3_PIN              4U
#define BOARD_INPUT4_PORT             GPIOB   /* CN5 pin 1 */
#define BOARD_INPUT4_PIN              15U

/* ---------------- Industrial Outputs (relays) ---------------- */
/* AO3400 gate drives with 10k pull-downs; high = relay on. The board has
 * three relays only. */
#define BOARD_OUTPUT1_PORT            GPIOB   /* U4,  CN12 GPIO_OUT 1 */
#define BOARD_OUTPUT1_PIN             2U
#define BOARD_OUTPUT2_PORT            GPIOB   /* U9,  CN13 GPIO_OUT 2 */
#define BOARD_OUTPUT2_PIN             10U
#define BOARD_OUTPUT3_PORT            GPIOA   /* U16, CN14 GPIO_OUT 3 */
#define BOARD_OUTPUT3_PIN             0U
#define BOARD_OUTPUT4_PORT            NULL
#define BOARD_OUTPUT4_PIN             0xFFU

/* ---------------- LED / buzzer ---------------- */
/* One status LED (LED3 via R10 330R), active high. */
#define BOARD_LED_STATUS_PORT         GPIOB
#define BOARD_LED_STATUS_PIN          8U

/* BUZZER1 (3 kHz) low-side switched by Q1, active high. */
#define BOARD_BUZZER_PORT             GPIOC
#define BOARD_BUZZER_PIN              13U


#endif /* F411_CUSTOM_BOARD_CONFIG_H_ */
