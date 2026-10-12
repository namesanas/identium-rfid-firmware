#ifndef RFID_CONTROLLER_FIRMWARE_CONFIG_H
#define RFID_CONTROLLER_FIRMWARE_CONFIG_H

/* Supported board identifiers. */
#define BOARD_F429_DISCOVERY   1U
#define BOARD_F411_CUSTOM      2U

/* Current build target. */
#ifndef TARGET_BOARD
#define TARGET_BOARD            BOARD_F429_DISCOVERY
#endif

#if (TARGET_BOARD != BOARD_F429_DISCOVERY) && \
    (TARGET_BOARD != BOARD_F411_CUSTOM)
#error "Unsupported TARGET_BOARD"
#endif

#endif /* RFID_CONTROLLER_FIRMWARE_CONFIG_H */
