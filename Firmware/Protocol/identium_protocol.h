#ifndef RFID_CONTROLLER_IDENTIUM_PROTOCOL_H_
#define RFID_CONTROLLER_IDENTIUM_PROTOCOL_H_

#include <stdint.h>
#include "../Protocols/silion/silion_protocol.h"

/*
 * Identium Reader active-mode protocol.
 *
 * Phase 1 uses the JSON-over-TCP variant described by the
 * Reader-Protocol-Specification.  The application-level JSON
 * envelopes are shared with the HTTP and MQTT transports.
 */

#define IDENTIUM_PROTOCOL_RX_BUFFER_SIZE       2048U
#define IDENTIUM_PROTOCOL_READER_NAME_SIZE     64U

void Identium_Protocol_Init(void);
void Identium_Protocol_Task(void);

/* Called by the active TCP transport for every received byte. */
void Identium_Protocol_PushTcpByte(uint8_t receivedByte);

/* Events emitted by the reader. */
uint8_t Identium_Protocol_SendHeartbeat(void);
uint8_t Identium_Protocol_ReportTag(const SILION_Tag_t *tag);

/* Report the complete four-input state snapshot. */
void Identium_Protocol_ReportGpiChange(
    const uint8_t states[4]
);

/* Returns 1 when the protocol's TCP session is currently established. */
uint8_t Identium_Protocol_IsConnected(void);

#endif /* RFID_CONTROLLER_IDENTIUM_PROTOCOL_H_ */
