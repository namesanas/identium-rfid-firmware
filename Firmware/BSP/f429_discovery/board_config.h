#ifndef RFID_CONTROLLER_BSP_F429_DISCOVERY_CONFIG_H
#define RFID_CONTROLLER_BSP_F429_DISCOVERY_CONFIG_H

#include <stddef.h>
#include "stm32f429xx.h"

/* ============================================================
 * STM32F429ZI Discovery / current working prototype
 *
 * These mappings are taken from the current working F429 source.
 * Keep physical board wiring here, not in application code.
 * ============================================================ */

/* ---------------- RFID / SILION ---------------- */
#define BOARD_RFID_USART              USART3
#define BOARD_RFID_TX_PORT            GPIOB
#define BOARD_RFID_TX_PIN             10U
#define BOARD_RFID_RX_PORT            GPIOB
#define BOARD_RFID_RX_PIN             11U
#define BOARD_RFID_ENABLE_PORT        GPIOB
#define BOARD_RFID_ENABLE_PIN         0U
#define BOARD_RFID_GPIO_AF            7U

/* ---------------- Host / ST-LINK VCP ---------------- */
#define BOARD_HOST_USART              USART1
#define BOARD_HOST_TX_PORT            GPIOA
#define BOARD_HOST_TX_PIN             9U
#define BOARD_HOST_RX_PORT            GPIOA
#define BOARD_HOST_RX_PIN             10U
#define BOARD_HOST_GPIO_AF            7U

/* ---------------- W5500 ---------------- */
#define BOARD_W5500_SPI               SPI1
#define BOARD_W5500_SCK_PORT          GPIOA
#define BOARD_W5500_SCK_PIN           5U
#define BOARD_W5500_MISO_PORT         GPIOA
#define BOARD_W5500_MISO_PIN          6U
#define BOARD_W5500_MOSI_PORT         GPIOA
#define BOARD_W5500_MOSI_PIN          7U
#define BOARD_W5500_GPIO_AF           5U
#define BOARD_W5500_CS_PORT           GPIOB
#define BOARD_W5500_CS_PIN            12U
#define BOARD_W5500_RESET_PORT        GPIOC
#define BOARD_W5500_RESET_PIN         0U
#define BOARD_W5500_INT_PORT          NULL
#define BOARD_W5500_INT_PIN           0xFFU

#define BOARD_HOST_USART_IRQ          37U
#define BOARD_RFID_USART_IRQ          39U
#endif /* RFID_CONTROLLER_BSP_F429_DISCOVERY_CONFIG_H */
