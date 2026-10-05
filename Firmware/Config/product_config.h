/*
 * product_config.h
 *
 *  Created on: 26-Sept-2026
 *      Author: Identium
 */

#ifndef PRODUCT_CONFIG_H_
#define PRODUCT_CONFIG_H_


/* These are the currently working product defaults from the F429 firmware. */
#define PRODUCT_ETHERNET_TCP_PORT       5000U

/* Identium active-mode JSON-over-TCP development target. */
#define PRODUCT_PROTOCOL_TCP_SERVER_PORT 9000U
#define PRODUCT_PROTOCOL_TCP_LOCAL_PORT  40001U
#define PRODUCT_PROTOCOL_TCP_SERVER_IP_0 192U
#define PRODUCT_PROTOCOL_TCP_SERVER_IP_1 168U
#define PRODUCT_PROTOCOL_TCP_SERVER_IP_2 150U
#define PRODUCT_PROTOCOL_TCP_SERVER_IP_3 100U
#define PRODUCT_PROTOCOL_READER_NAME     "RFID-F429-DEV"

#define PRODUCT_RFID_BAUDRATE           115200U
#define PRODUCT_HOST_BAUDRATE           115200U

#define PRODUCT_RFID_DEFAULT_REGION     8U
#define PRODUCT_RFID_TX_ANTENNA         1U
#define PRODUCT_RFID_RX_ANTENNA         1U
#define PRODUCT_RFID_READ_POWER         2500U
#define PRODUCT_RFID_WRITE_POWER        2500U
#define PRODUCT_RFID_SESSION            0U



#endif /* PRODUCT_CONFIG_H_ */
