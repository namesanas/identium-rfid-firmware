#ifndef RFID_CONTROLLER_BSP_F411_CUSTOM_CONFIG_H
#define RFID_CONTROLLER_BSP_F411_CUSTOM_CONFIG_H

#include <stddef.h>
#include "stm32f411xe.h"

/* ============================================================
 * STM32F411CEU6 custom industrial RFID controller
 *
 * Values come from the preparation specification. Any hardware
 * item explicitly marked TBD remains TBD until the final PCB
 * schematic/ICD is verified.
 * ============================================================ */

/* ---------------- RFID / SILION ---------------- */
#define BOARD_RFID_USART              USART2
#define BOARD_RFID_TX_PORT            GPIOA
#define BOARD_RFID_TX_PIN             2U
#define BOARD_RFID_RX_PORT            GPIOA
#define BOARD_RFID_RX_PIN             3U
#define BOARD_RFID_ENABLE_PORT        NULL
#define BOARD_RFID_ENABLE_PIN         0xFFU /* TBD */
#define BOARD_RFID_GPIO_AF            7U

/* ---------------- Shared RS232 / RS485 ---------------- */
#define BOARD_HOST_USART              USART1
#define BOARD_HOST_TX_PORT            GPIOB
#define BOARD_HOST_TX_PIN             6U
#define BOARD_HOST_RX_PORT            GPIOB
#define BOARD_HOST_RX_PIN             7U
#define BOARD_HOST_GPIO_AF            7U
#define BOARD_SERIAL_MODE_PORT        NULL /* TBD */
#define BOARD_SERIAL_MODE_PIN         0xFFU /* TBD */
#define BOARD_RS485_DE_RE_PORT        NULL /* TBD */
#define BOARD_RS485_DE_RE_PIN         0xFFU /* TBD */

/* ---------------- W5500 ---------------- */
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

/* ---------------- USB ---------------- */
#define BOARD_USB_DM_PORT             GPIOA
#define BOARD_USB_DM_PIN              11U
#define BOARD_USB_DP_PORT             GPIOA
#define BOARD_USB_DP_PIN              12U
#define BOARD_USB_VBUS_PORT           GPIOA
#define BOARD_USB_VBUS_PIN            9U

/* ---------------- Industrial Inputs ---------------- */
#define BOARD_INPUT1_PORT             GPIOB
#define BOARD_INPUT1_PIN              8U
#define BOARD_INPUT2_PORT             GPIOB
#define BOARD_INPUT2_PIN              9U
#define BOARD_INPUT3_PORT             GPIOB
#define BOARD_INPUT3_PIN              10U
#define BOARD_INPUT4_PORT             GPIOA
#define BOARD_INPUT4_PIN              10U

/* ---------------- Industrial Outputs ---------------- */
#define BOARD_OUTPUT1_PORT            GPIOB
#define BOARD_OUTPUT1_PIN             12U
#define BOARD_OUTPUT2_PORT            GPIOB
#define BOARD_OUTPUT2_PIN             13U
#define BOARD_OUTPUT3_PORT            GPIOB
#define BOARD_OUTPUT3_PIN             14U
#define BOARD_OUTPUT4_PORT            GPIOB
#define BOARD_OUTPUT4_PIN             15U

/* Final LED GPIO assignment: TBD until verified PCB schematic. */
#define BOARD_LED_POWER_PORT          NULL
#define BOARD_LED_POWER_PIN           0xFFU
#define BOARD_LED_LINK_PORT           NULL
#define BOARD_LED_LINK_PIN            0xFFU
#define BOARD_LED_RF_ACTIVE_PORT      NULL
#define BOARD_LED_RF_ACTIVE_PIN       0xFFU
#define BOARD_LED_ERROR_PORT           NULL
#define BOARD_LED_ERROR_PIN            0xFFU

/* Buzzer GPIO assignment: TBD until verified PCB schematic. */
#define BOARD_BUZZER_PORT             NULL
#define BOARD_BUZZER_PIN              0xFFU

#endif /* RFID_CONTROLLER_BSP_F411_CUSTOM_CONFIG_H */
