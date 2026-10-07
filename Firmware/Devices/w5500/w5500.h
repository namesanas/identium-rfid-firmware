#ifndef RFID_CONTROLLER_DEVICE_W5500_H
#define RFID_CONTROLLER_DEVICE_W5500_H

#include <stdint.h>
#include "../../Drivers/spi/spi.h"
#include "../../Drivers/gpio/gpio.h"

/* ============================================================
 * W5500 COMMON REGISTERS
 * ============================================================ */

#define W5500_VERSIONR              0x0039U
#define W5500_VERSION               0x04U

#define W5500_GAR                   0x0001U
#define W5500_SUBR                  0x0005U
#define W5500_SHAR                  0x0009U
#define W5500_SIPR                  0x000FU

/* ============================================================
 * W5500 SOCKET 0 BUFFER CONFIGURATION
 * ============================================================ */

#define W5500_S0_RXBUF_SIZE         0x001EU
#define W5500_S0_TXBUF_SIZE         0x001FU

#define W5500_BSB_SOCKET0           1U

/* ============================================================
 * SOCKET 0 REGISTERS
 * ============================================================ */

#define W5500_S0_MR                 0x0000U
#define W5500_S0_CR                 0x0001U
#define W5500_S0_SR                 0x0003U
#define W5500_S0_PORT               0x0004U
#define W5500_S0_DHAR               0x0006U
#define W5500_S0_DIPR               0x000CU
#define W5500_S0_DPORT              0x0010U
#define W5500_S0_KPALVTR            0x002FU

#define W5500_S0_BSB                1U

#define W5500_S0_IR                 0x0002U

#define W5500_S0_RX_RSR             0x0026U
#define W5500_S0_RX_RD              0x0028U
#define W5500_S0_RX_WR              0x002AU

#define W5500_S0_TX_FSR             0x0020U
#define W5500_S0_TX_WR              0x0024U

/* ============================================================
 * SOCKET COMMANDS
 * ============================================================ */

#define W5500_Sn_CR_OPEN            0x01U
#define W5500_Sn_CR_LISTEN          0x02U
#define W5500_Sn_CR_CLOSE           0x10U
#define W5500_Sn_CR_CONNECT         0x04U

#define W5500_S1_MR                 0x0000U
#define W5500_S1_CR                 0x0001U
#define W5500_S1_SR                 0x0003U
#define W5500_S1_PORT               0x0004U
#define W5500_S1_IR                 0x0002U
#define W5500_S1_RX_RSR             0x0026U
#define W5500_S1_RX_RD              0x0028U
#define W5500_S1_TX_FSR             0x0020U
#define W5500_S1_TX_WR              0x0024U
#define W5500_S1_RXBUF_SIZE         0x001EU
#define W5500_S1_TXBUF_SIZE         0x001FU
#define W5500_BSB_SOCKET1_REG       5U
#define W5500_BSB_SOCKET1_TXBUF     6U
#define W5500_BSB_SOCKET1_RXBUF     7U

#define W5500_S0_RECV               0x40U
#define W5500_S0_SEND               0x20U

/* ============================================================
 * SOCKET STATUS VALUES
 * ============================================================ */

#define W5500_Sn_SR_CLOSED          0x00U
#define W5500_Sn_SR_INIT            0x13U
#define W5500_Sn_SR_LISTEN          0x14U
#define W5500_Sn_SR_ESTABLISHED     0x17U
#define W5500_Sn_SR_CLOSE_WAIT      0x1CU

#define W5500_PHYCFGR               0x002EU
#define W5500_PHYCFGR_LINK_UP       0x01U

/* ============================================================
 * DEVICE INITIALIZATION
 * ============================================================ */

uint8_t W5500_Init(
    SPI_Handle_t *pSPI,
    GPIO_RegDef_t *pCSPort,
    uint8_t csPin,
    GPIO_RegDef_t *pResetPort,
    uint8_t resetPin
);

/* ============================================================
 * COMMON REGISTER ACCESS
 * ============================================================ */

uint8_t W5500_ReadRegisters(
    uint16_t address,
    uint8_t blockSelect,
    uint8_t *buffer,
    uint16_t length
);

uint8_t W5500_WriteRegisters(
    uint16_t address,
    uint8_t blockSelect,
    const uint8_t *buffer,
    uint16_t length
);

/* ============================================================
 * NETWORK CONFIGURATION
 * ============================================================ */

uint8_t W5500_SetNetworkConfig(void);
uint8_t W5500_SetNetworkConfigValues(
    const uint8_t ip[4],
    const uint8_t subnet[4],
    const uint8_t gateway[4],
    const uint8_t mac[6]
);
uint8_t W5500_ReadVersion(void);
uint8_t W5500_IsLinkUp(void);

/* ============================================================
 * TCP SERVER
 * ============================================================ */

uint8_t W5500_StartTCPServer(uint16_t port);

uint8_t W5500_StartTCPClient(
    const uint8_t *serverIp,
    uint16_t serverPort,
    uint16_t sourcePort
);

uint8_t W5500_Socket0_GetStatus(void);
uint8_t W5500_Socket0_Close(void);

/* ============================================================
 * SOCKET 1 - LOCAL CONFIGURATION PORT
 * ============================================================ */

uint8_t W5500_StartTCPServer1(uint16_t port);
uint8_t W5500_Socket1_GetStatus(void);
uint8_t W5500_Socket1_Close(void);
uint16_t W5500_Socket1_GetRxSize(void);
uint16_t W5500_Socket1_Receive(uint8_t *buffer, uint16_t bufferSize);
uint16_t W5500_Socket1_GetTxFreeSize(void);
uint16_t W5500_Socket1_Send(const uint8_t *buffer, uint16_t length);

/* Socket 2: UDP (vendor-tool device discovery). */
uint8_t W5500_Socket2_GetStatus(void);
uint8_t W5500_Socket2_OpenUdp(uint16_t port);
uint16_t W5500_Socket2_ReceiveFrom(uint8_t *buffer, uint16_t bufferSize, uint8_t sourceIp[4], uint16_t *sourcePort);
uint8_t W5500_Socket2_SendTo(const uint8_t *buffer, uint16_t length, const uint8_t destinationIp[4], uint16_t destinationPort);

/* ============================================================
 * SOCKET 0 RX
 * ============================================================ */

uint16_t W5500_Socket0_GetRxSize(void);

uint16_t W5500_Socket0_Receive(
    uint8_t *buffer,
    uint16_t bufferSize
);

/* ============================================================
 * SOCKET 0 TX
 * ============================================================ */

uint16_t W5500_Socket0_GetTxFreeSize(void);

uint16_t W5500_Socket0_Send(
    const uint8_t *buffer,
    uint16_t length
);

#endif /* RFID_CONTROLLER_DEVICE_W5500_H */
