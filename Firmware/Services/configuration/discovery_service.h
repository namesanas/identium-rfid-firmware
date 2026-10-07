#ifndef RFID_CONTROLLER_DISCOVERY_SERVICE_H_
#define RFID_CONTROLLER_DISCOVERY_SERVICE_H_

/*
 * Vendor-tool device search (Reader Manager "IP search", ReaderSense
 * "discover"): UDP port 15000, request EE 06 00 00 50 01 + 6-byte MAC
 * (all zero = any reader); the reply is unicast to the sender.
 */
void Discovery_Service_Task(void);

#endif /* RFID_CONTROLLER_DISCOVERY_SERVICE_H_ */
