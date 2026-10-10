/*
 * diagnostics.h
 *
 * Health report for the host: reset cause, the fault that ended the
 * previous run, main-loop timing, stack use and brown-out level.
 *
 * GET_DIAG reply (one line):
 *   DIAG,RESET=<cause>,UPTIME_MS=<n>,FAULTS=<n>,LAST_FAULT=<type>
 *       [,PC=0x..,LR=0x..,CFSR=0x..,HFSR=0x..,MMFAR=0x..,BFAR=0x..,AT_MS=<n>]
 *       ,LOOP_MAX_US=<n>,PASSES=<n>,ETH_US=<n>,WIFI_US=<n>,CFG_US=<n>,PROTO_US=<n>,
 *       GPIO_US=<n>,RFID_US=<n>,HOST_US=<n>,STACK_USED=<n>,STACK_SIZE=<n>,
 *       BOR=<0..3>
 *
 * FAULTS counts faults since power-on; LAST_FAULT is the fault (if any)
 * that reset the reader before this run.
 */

#ifndef RFID_CONTROLLER_DIAGNOSTICS_H_
#define RFID_CONTROLLER_DIAGNOSTICS_H_

#include <stdint.h>

#define DIAGNOSTICS_LINE_MAX 512U

/* Writes the GET_DIAG line, CR LF terminated. Returns its length. */
uint16_t Diagnostics_Format(char *buffer, uint16_t size);

/* CLEAR_DIAG: restarts the loop-timing maxima. */
void Diagnostics_Clear(void);

#ifdef DEBUG
/*
 * Debug builds only: DEBUG_FAULT,<kind> deliberately crashes the reader
 * to test the fault record. kind: "USAGE" (undefined instruction),
 * "BUS" (read of unmapped memory), "STACK" (overflow into the guard).
 * Returns 0 for an unknown kind; otherwise does not return.
 */
uint8_t Diagnostics_TriggerFault(const char *kind);
#endif

#endif /* RFID_CONTROLLER_DIAGNOSTICS_H_ */
