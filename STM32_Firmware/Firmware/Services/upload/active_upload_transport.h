#ifndef RFID_CONTROLLER_ACTIVE_UPLOAD_TRANSPORT_H_
#define RFID_CONTROLLER_ACTIVE_UPLOAD_TRANSPORT_H_

#include <stdint.h>

#define ACTIVE_UPLOAD_INTERFACE_ETH  0U
#define ACTIVE_UPLOAD_INTERFACE_WIFI 1U

/* Interface choice is live: auto selects Ethernet while physical link is up. */
uint8_t ActiveUpload_Transport_GetSelected(void);
uint8_t ActiveUpload_Transport_IsConnected(void);
uint16_t ActiveUpload_Transport_GetTxFreeSize(void);
uint16_t ActiveUpload_Transport_Send(const uint8_t *data, uint16_t length);
uint16_t ActiveUpload_Transport_Receive(uint8_t *data, uint16_t capacity);
uint8_t ActiveUpload_Transport_Close(void);
uint16_t ActiveUpload_Transport_SendComplete(const uint8_t *data, uint16_t length);

/* HTTP upload (either interface): one POST per message, one at a time. */
uint8_t ActiveUpload_Transport_IsHttp(void);
uint8_t ActiveUpload_Transport_IsHttpBusy(void);

#endif /* RFID_CONTROLLER_ACTIVE_UPLOAD_TRANSPORT_H_ */
