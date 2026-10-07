#ifndef RFID_CONTROLLER_CONFIGURATION_PORT_H_
#define RFID_CONTROLLER_CONFIGURATION_PORT_H_

#include <stdint.h>

void Configuration_Port_Init(void);
void Configuration_Port_Task(void);

/* EE configuration request received on the USB CDC (COM) port; the
 * reply is sent back on USB. frame = complete request, CRC excluded. */
void Configuration_Port_HandleUsbRequest(const uint8_t *frame, uint16_t length);

#endif /* RFID_CONTROLLER_CONFIGURATION_PORT_H_ */
