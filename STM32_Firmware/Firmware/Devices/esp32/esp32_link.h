#ifndef RFID_CONTROLLER_ESP32_LINK_H_
#define RFID_CONTROLLER_ESP32_LINK_H_

#include <stdint.h>
#include "esp32_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ESP32_LINK_STATE_IDLE = 0U,
    ESP32_LINK_STATE_BUSY,
    ESP32_LINK_STATE_DONE,
    ESP32_LINK_STATE_FAILED
} ESP32_LinkState_t;

/* The hardware is optional on the F411 build because its documented GPIOs
 * currently collide with the production UART/input assignments. */
uint8_t ESP32_Link_Init(void);
void ESP32_Link_Task(void);
uint8_t ESP32_Link_IsSupported(void);
uint8_t ESP32_Link_IsBooted(void);
uint8_t ESP32_Link_IsBusy(void);
uint8_t ESP32_Link_Submit(uint8_t command, uint8_t socket,
                          const uint8_t *payload, uint16_t length);
ESP32_LinkState_t ESP32_Link_GetResult(uint8_t *command, uint8_t *socket,
                                       uint8_t *payload, uint16_t *length,
                                       uint16_t capacity);
void ESP32_Link_ConsumeResult(void);
void ESP32_Link_RequestReset(void);
uint32_t ESP32_Link_GetErrorCount(void);

#ifdef __cplusplus
}
#endif
#endif /* RFID_CONTROLLER_ESP32_LINK_H_ */
