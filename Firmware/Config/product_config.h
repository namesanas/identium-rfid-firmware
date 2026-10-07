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

/* Current F429 ActiveMode defaults.  Step 8 now owns the runtime copies
 * of these values in the persistent Identium configuration service. */
#define PRODUCT_PROTOCOL_UPLOAD_S_BUF_SIZE       1580U
#define PRODUCT_PROTOCOL_UPLOAD_CLIENT_ACK       0U
#define PRODUCT_PROTOCOL_UPLOAD_RECV_TIMEOUT_S   10U
#define PRODUCT_PROTOCOL_UPLOAD_CLR_R_BUF_TIME_S  8U
#define PRODUCT_PROTOCOL_UPLOAD_DATA_AGGR_MODE    0U
#define PRODUCT_PROTOCOL_UPLOAD_DATA_AGGR_TIME_MS 1000U
#define PRODUCT_PROTOCOL_THDWT_NET_TIMEOUT_MIN    0U
#define PRODUCT_PROTOCOL_THDWT_INV_TIMEOUT_MIN    0U
#define PRODUCT_PROTOCOL_REMOTE_CMD_REPLY         1U

#define PRODUCT_RFID_BAUDRATE           115200U
#define PRODUCT_HOST_BAUDRATE           115200U

#define PRODUCT_RFID_DEFAULT_REGION     8U
#define PRODUCT_RFID_TX_ANTENNA         1U
#define PRODUCT_RFID_RX_ANTENNA         1U
#define PRODUCT_RFID_READ_POWER         2500U
#define PRODUCT_RFID_WRITE_POWER        2500U
#define PRODUCT_RFID_SESSION            0U

/* Mainboard firmware version a.b.c.d reported to Reader Manager ("VER"). */
#define PRODUCT_MAINBOARD_VERSION_0     1U
#define PRODUCT_MAINBOARD_VERSION_1     0U
#define PRODUCT_MAINBOARD_VERSION_2     0U
#define PRODUCT_MAINBOARD_VERSION_3     8U



#endif /* PRODUCT_CONFIG_H_ */
