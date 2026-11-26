/*
 * host_interface.h
 *
 *  Created on: 04-Sept-2026
 *      Author: Identium
 */

#ifndef INC_HOST_INTERFACE_H_
#define INC_HOST_INTERFACE_H_

#include <stdint.h>

void HOST_Interface_Init(void);
/* Discard a partial command when its transport releases ownership. */
void HOST_Interface_ResetInput(void);
void HOST_Interface_Task(void);
void HOST_SendString(const char *text);
uint8_t HOST_IsTagTransportEthernet(void);

#endif /* INC_HOST_INTERFACE_H_ */
