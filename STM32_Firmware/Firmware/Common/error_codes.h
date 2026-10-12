/*
 * error_codes.h
 *
 *  Created on: 26-Sept-2026
 *      Author: Identium
 */

#ifndef ERROR_CODES_H_
#define ERROR_CODES_H_


#include <stdint.h>

typedef enum
{
    ERROR_NONE = 0U,

    /* Generic */
    ERROR_INVALID_PARAMETER,
    ERROR_NOT_INITIALIZED,
    ERROR_BUSY,
    ERROR_TIMEOUT,
    ERROR_BUFFER_OVERFLOW,

    /* RFID */
    ERROR_RFID_UART,
    ERROR_RFID_TIMEOUT,
    ERROR_RFID_FRAME,
    ERROR_RFID_STATUS,

    /* Ethernet / W5500 */
    ERROR_W5500_INIT,
    ERROR_W5500_SPI,
    ERROR_ETHERNET_LINK,
    ERROR_ETHERNET_SOCKET,

    /* Host */
    ERROR_HOST_RX_OVERFLOW,
    ERROR_HOST_INVALID_COMMAND,

	/* System initialization */
	ERROR_SYSTEM_INIT,
	ERROR_RFID_INIT,
	ERROR_ETHERNET_INIT,
	ERROR_GPIO_INIT,
	ERROR_WATCHDOG_INIT
} ErrorCode_t;




#endif /* ERROR_CODES_H_ */
