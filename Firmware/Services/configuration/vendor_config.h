#ifndef RFID_CONTROLLER_VENDOR_CONFIG_H_
#define RFID_CONTROLLER_VENDOR_CONFIG_H_

#include <stdint.h>

/*
 * Configuration-port (TCP 8080, EE frames) write commands used by
 * Reader Manager and ReaderSense:
 *
 *   0x25  set ActiveModeJson      (content = UTF-8 JSON)
 *   0x1F  set StaticModelJson     (content = UTF-8 JSON)
 *   0x20  clear parameters, return to passive (content 00 50)
 *   0x23  switch work state passive <-> active (content 01)
 *
 * The tools send complete documents (they read the reader's JSON, edit
 * it and send it back), so fields the F429 cannot act on are ignored
 * instead of rejecting the whole document.
 */

#define VENDOR_CONFIG_CMD_CLEAR        0x20U
#define VENDOR_CONFIG_CMD_SWITCH       0x23U
#define VENDOR_CONFIG_CMD_SET_ACTIVE   0x25U
#define VENDOR_CONFIG_CMD_SET_STATIC   0x1FU

/* Validates and queues a request. Returns the 32-bit reply status
 * (0 = accepted) and a 1-byte reply content (resulting work state). */
uint32_t VendorConfig_Handle(
    uint8_t commandId,
    const char *json,
    uint16_t length,
    uint8_t *replyByte
);

/* Applies a request accepted by VendorConfig_Handle(). Call from the
 * main loop after the reply has been sent. */
void VendorConfig_Task(void);

/* Set the reader work state (1 passive, 3 active), persist it and
 * start/stop local inventory accordingly. */
uint8_t VendorConfig_SetWorkState(uint8_t workState);

#endif /* RFID_CONTROLLER_VENDOR_CONFIG_H_ */
