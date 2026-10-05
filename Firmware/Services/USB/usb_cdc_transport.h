#ifndef RFID_CONTROLLER_USB_CDC_TRANSPORT_H_
#define RFID_CONTROLLER_USB_CDC_TRANSPORT_H_

#include <stdint.h>

#define USB_CDC_TRANSPORT_TX_QUEUE_SIZE  4096U
#define USB_CDC_TRANSPORT_PACKET_SIZE      64U

void USB_CDC_Transport_Init(void);
uint8_t USB_CDC_Transport_IsReady(void);
uint16_t USB_CDC_Transport_Send(const uint8_t *data, uint16_t length);
void USB_CDC_Transport_Task(void);
void USB_CDC_Transport_OnTxComplete(void);
void USB_CDC_Transport_Reset(void);
uint32_t USB_CDC_Transport_GetTxOverflowCount(void);

#endif /* RFID_CONTROLLER_USB_CDC_TRANSPORT_H_ */
