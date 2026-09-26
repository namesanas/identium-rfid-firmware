#ifndef RFID_CONTROLLER_FIRMWARE_CONFIG_H
#define RFID_CONTROLLER_FIRMWARE_CONFIG_H

/*
 * Target board selection.
 *
 * For the first clean project we develop against the working F429 board.
 * The F411 custom board map is prepared separately in its BSP directory.
 */
#define BOARD_F429_DISCOVERY    1U
#define BOARD_F411_CUSTOM       2U

#define TARGET_BOARD BOARD_F429_DISCOVERY

/* Feature switches */
#define ENABLE_RFID             1U
#define ENABLE_ETHERNET         1U
#define ENABLE_USB              0U
#define ENABLE_RS232_RS485      0U
#define ENABLE_INDUSTRIAL_IO    0U
#define ENABLE_DEBUG_LOG        1U

#endif /* RFID_CONTROLLER_FIRMWARE_CONFIG_H */
