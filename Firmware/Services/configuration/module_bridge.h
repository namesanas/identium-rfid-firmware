#ifndef RFID_CONTROLLER_MODULE_BRIDGE_H_
#define RFID_CONTROLLER_MODULE_BRIDGE_H_

#include <stdint.h>

/*
 * Reader Manager module bridge on the configuration port (TCP 8080).
 *
 * Reader Manager (ModuleAPI.dll) connects to port 8080 and speaks the
 * Silion module protocol (FF frames) plus a few ASCII mainboard
 * commands. Module frames are forwarded unchanged to the module UART
 * and the module's replies are relayed back. Mainboard commands are
 * answered locally.
 */

/* Start a session on the currently ESTABLISHED socket 1 connection.
 * The bytes already received on the connection are passed in. */
uint8_t Module_Bridge_Begin(const uint8_t *data, uint16_t length);

/* Socket 1 is ESTABLISHED and the session is active. */
void Module_Bridge_Task(void);

/* The PC closed the connection (or the socket closed). */
void Module_Bridge_End(void);

uint8_t Module_Bridge_IsActive(void);

/* Same session over a serial link -- the tools' "COMx" connection, on
 * the native USB CDC port (HOST_TRANSPORT_USB) or the RS232/RS485 host
 * port (HOST_TRANSPORT_SERIAL). Bytes are pushed in by the host
 * transport. */
uint8_t Module_Bridge_BeginLink(uint8_t link, const uint8_t *data, uint16_t length);
void Module_Bridge_PushLinkBytes(uint8_t link, const uint8_t *data, uint16_t length);
uint8_t Module_Bridge_IsLinkActive(uint8_t link);

/* Pushed bytes not yet consumed: a module frame or board command is
 * still incomplete. */
uint8_t Module_Bridge_HasPendingHostBytes(void);

/* The link switched to the legacy ASCII protocol: end its session and
 * hand the module back to local use immediately. */
void Module_Bridge_EndLinkForLocalUse(uint8_t link);

/* Called every loop: drains the module UART and releases the module
 * back to local use after the PC has been gone for a grace period. */
void Module_Bridge_Poll(void);

#endif /* RFID_CONTROLLER_MODULE_BRIDGE_H_ */
