#include "w5500.h"
#include "../../Services/Time/time_service.h"
#include <stddef.h>

/* ============================================================
 * INTERNAL DEVICE CONTEXT
 * ============================================================ */

typedef struct
{
    SPI_Handle_t *pSPI;

    GPIO_RegDef_t *pCSPort;
    uint8_t csPin;

    GPIO_RegDef_t *pResetPort;
    uint8_t resetPin;

    uint8_t initialized;

} W5500_Context_t;

static W5500_Context_t g_w5500;

/* ============================================================
 * CHIP SELECT
 * ============================================================ */

static void W5500_CS_Low(void)
{
    GPIO_WriteToOutputPin(
        g_w5500.pCSPort,
        g_w5500.csPin,
        GPIO_PIN_RESET
    );
}

static void W5500_CS_High(void)
{
    GPIO_WriteToOutputPin(
        g_w5500.pCSPort,
        g_w5500.csPin,
        GPIO_PIN_SET
    );
}

/* ============================================================
 * SPI BYTE TRANSFER
 *
 * W5500 uses full-duplex SPI transactions.
 * Therefore every transmitted byte also clocks one
 * received byte from the device.
 * ============================================================ */

static uint8_t W5500_SPI_Transfer(uint8_t data)
{
    SPI_RegDef_t *pSPIx;

    if(
        (g_w5500.pSPI == NULL) ||
        (g_w5500.pSPI->pSPIx == NULL)
    )
    {
        return 0U;
    }

    pSPIx = g_w5500.pSPI->pSPIx;

    while(SPI_GetFlagStatus(pSPIx,SPI_TXE_FLAG) == FLAG_RESET)
    {
    }

    *((volatile uint8_t *)&pSPIx->DR) = data;

    while(
        SPI_GetFlagStatus(
            pSPIx,
            SPI_RXNE_FLAG
        ) == FLAG_RESET
    )
    {
    }

    return *((volatile uint8_t *)&pSPIx->DR);
}

/* ============================================================
 * WAIT FOR SPI NOT BUSY
 * ============================================================ */

static void W5500_SPI_WaitUntilNotBusy(void)
{
    SPI_RegDef_t *pSPIx;

    if(
        (g_w5500.pSPI == NULL) ||
        (g_w5500.pSPI->pSPIx == NULL)
    )
    {
        return;
    }

    pSPIx = g_w5500.pSPI->pSPIx;

    while(SPI_GetFlagStatus(pSPIx,(1UL << SPI_SR_BSY_BIT)) == FLAG_SET)
    {
    }
}

/* ============================================================
 * W5500 CONTROL BYTE
 *
 * Control byte format:
 *
 * [7:3] BSB
 * [2]   RWB
 * [1:0] OM
 *
 * OM = 00 -> variable length data mode
 * ============================================================ */

static uint8_t W5500_MakeControlByte(
    uint8_t blockSelect,
    uint8_t write
)
{
    uint8_t control = 0U;

    control |=(uint8_t)((blockSelect & 0x1FU) << 3U);

    if(write != 0U)
    {
        control |= (1U << 2U);
    }

    return control;
}

/* ============================================================
 * WRITE 16-BIT W5500 ADDRESS
 * ============================================================ */

static void W5500_WriteAddress(
    uint16_t address
)
{
    W5500_SPI_Transfer((uint8_t)(address >> 8U));

    W5500_SPI_Transfer((uint8_t)(address & 0xFFU));
}

/* ============================================================
 * REGISTER READ
 * ============================================================ */

uint8_t W5500_ReadRegisters(uint16_t address,uint8_t blockSelect,uint8_t *buffer,uint16_t length)
{
    uint16_t i;

    if(
        (g_w5500.initialized == 0U) ||
        (buffer == NULL) ||
        (length == 0U)
    )
    {
        return 0U;
    }

    W5500_CS_Low();

    W5500_WriteAddress(address);

    W5500_SPI_Transfer(
        W5500_MakeControlByte(
            blockSelect,
            0U
        )
    );

    for(i = 0U; i < length; i++)
    {
        buffer[i] =
            W5500_SPI_Transfer(0x00U);
    }

    W5500_SPI_WaitUntilNotBusy();

    W5500_CS_High();

    return 1U;
}

/* ============================================================
 * REGISTER WRITE
 * ============================================================ */

uint8_t W5500_WriteRegisters(uint16_t address,uint8_t blockSelect,const uint8_t *buffer,uint16_t length)
{
    uint16_t i;

    if(
        (g_w5500.initialized == 0U) ||
        (buffer == NULL) ||
        (length == 0U)
    )
    {
        return 0U;
    }

    W5500_CS_Low();

    W5500_WriteAddress(address);

    W5500_SPI_Transfer(
        W5500_MakeControlByte(
            blockSelect,
            1U
        )
    );

    for(i = 0U; i < length; i++)
    {
        W5500_SPI_Transfer(
            buffer[i]
        );
    }

    W5500_SPI_WaitUntilNotBusy();

    W5500_CS_High();

    return 1U;
}

/* ============================================================
 * DEVICE INITIALIZATION
 * ============================================================ */

uint8_t W5500_Init(
    SPI_Handle_t *pSPI,
    GPIO_RegDef_t *pCSPort,
    uint8_t csPin,
    GPIO_RegDef_t *pResetPort,
    uint8_t resetPin
)
{
    g_w5500.pSPI = pSPI;

    g_w5500.pCSPort = pCSPort;
    g_w5500.csPin = csPin;

    g_w5500.pResetPort = pResetPort;
    g_w5500.resetPin = resetPin;

    g_w5500.initialized = 0U;

    if(
        (g_w5500.pSPI == NULL) ||
        (g_w5500.pSPI->pSPIx == NULL) ||
        (g_w5500.pCSPort == NULL) ||
        (g_w5500.pResetPort == NULL)
    )
    {
        return 0U;
    }

    W5500_CS_High();

    GPIO_WriteToOutputPin(
        g_w5500.pResetPort,
        g_w5500.resetPin,
        GPIO_PIN_RESET
    );

    Time_DelayMs(2U);

    GPIO_WriteToOutputPin(
        g_w5500.pResetPort,
        g_w5500.resetPin,
        GPIO_PIN_SET
    );

    Time_DelayMs(50U);

    g_w5500.initialized = 1U;

    return 1U;
}

/* ============================================================
 * NETWORK CONFIGURATION
 *
 * These values are intentionally preserved from the
 * known-working implementation.
 *
 * Product-level configuration will be separated later.
 * ============================================================ */

uint8_t W5500_SetNetworkConfig(void)
{
	{
		static const uint8_t gateway[4] =
		{
		    192U,
		    168U,
		    150U,
		    1U
		};

	    static const uint8_t subnet[4] =
	    {
	        255U,
	        255U,
	        255U,
	        0U
	    };

	    static const uint8_t mac[6] =
	    {
	        0x11U,
	        0x22U,
	        0x33U,
	        0x44U,
	        0x55U,
	        0x66U
	    };

	    static const uint8_t ip[4] =
	    {
	        192U,
	        168U,
	        150U,
	        50U
	    };


	    if(
	        W5500_WriteRegisters(
	            W5500_GAR,
	            0U,
	            gateway,
	            4U
	        ) == 0U
	    )
	    {
	        return 0U;
	    }


	    if(
	        W5500_WriteRegisters(
	            W5500_SUBR,
	            0U,
	            subnet,
	            4U
	        ) == 0U
	    )
	    {
	        return 0U;
	    }


	    if(
	        W5500_WriteRegisters(
	            W5500_SHAR,
	            0U,
	            mac,
	            6U
	        ) == 0U
	    )
	    {
	        return 0U;
	    }


	    if(
	        W5500_WriteRegisters(
	            W5500_SIPR,
	            0U,
	            ip,
	            4U
	        ) == 0U
	    )
	    {
	        return 0U;
	    }


	    return 1U;
	}
}

/* ============================================================
 * READ W5500 VERSION
 * ============================================================ */

uint8_t W5500_ReadVersion(void)
{
    uint8_t version = 0U;

    if(
        W5500_ReadRegisters(
            W5500_VERSIONR,
            0U,
            &version,
            1U
        ) == 0U
    )
    {
        return 0U;
    }

    return version;
}

/* ============================================================
 * PHY LINK STATUS
 * ============================================================ */

uint8_t W5500_IsLinkUp(void)
{
    uint8_t phyConfig = 0U;

    if(
        W5500_ReadRegisters(
            W5500_PHYCFGR,
            0U,
            &phyConfig,
            1U
        ) == 0U
    )
    {
        return 0U;
    }

    return ((phyConfig & W5500_PHYCFGR_LINK_UP) != 0U) ? 1U : 0U;
}

/* ============================================================
 * SOCKET 0 STATUS
 * ============================================================ */

uint8_t W5500_Socket0_GetStatus(void)
{
    uint8_t status =
        W5500_Sn_SR_CLOSED;

    (void)W5500_ReadRegisters(
        W5500_S0_SR,
        W5500_S0_BSB,
        &status,
        1U
    );

    return status;
}

/* ============================================================
 * SOCKET 0 CLOSE
 * ============================================================ */

uint8_t W5500_Socket0_Close(void)
{
    uint8_t command;
    uint8_t interruptFlags = 0xFFU;
    uint8_t status;
    uint8_t attempt;

    (void)W5500_WriteRegisters(
        W5500_S0_IR,
        W5500_S0_BSB,
        &interruptFlags,
        1U
    );

    command =
        W5500_Sn_CR_CLOSE;

    (void)W5500_WriteRegisters(
        W5500_S0_CR,
        W5500_S0_BSB,
        &command,
        1U
    );

    for(
        attempt = 0U;
        attempt < 20U;
        attempt++
    )
    {
        Time_DelayMs(1U);

        status =
            W5500_Socket0_GetStatus();

        if(
            status ==
            W5500_Sn_SR_CLOSED
        )
        {
            return 1U;
        }
    }

    return 0U;
}

/* ============================================================
 * START TCP SERVER
 * ============================================================ */

uint8_t W5500_StartTCPServer(
    uint16_t port
)
{
    uint8_t mode = 0x01U;
    uint8_t portData[2];
    uint8_t command;
    uint8_t status = 0U;
    uint8_t attempt;

    portData[0] =
        (uint8_t)(port >> 8U);

    portData[1] =
        (uint8_t)(port & 0xFFU);

    /* TCP mode */
    (void)W5500_WriteRegisters(
        W5500_S0_MR,
        W5500_S0_BSB,
        &mode,
        1U
    );

    /* Local listening port */
    (void)W5500_WriteRegisters(
        W5500_S0_PORT,
        W5500_S0_BSB,
        portData,
        2U
    );

    /* OPEN */
    command =
        W5500_Sn_CR_OPEN;

    (void)W5500_WriteRegisters(
        W5500_S0_CR,
        W5500_S0_BSB,
        &command,
        1U
    );

    /* Wait for SOCK_INIT */
    for(
        attempt = 0U;
        attempt < 20U;
        attempt++
    )
    {
        Time_DelayMs(1U);

        status =
            W5500_Socket0_GetStatus();

        if(
            status ==
            W5500_Sn_SR_INIT
        )
        {
            break;
        }
    }

    if(
        status !=
        W5500_Sn_SR_INIT
    )
    {
        return 0U;
    }

    /* LISTEN */
    command =
        W5500_Sn_CR_LISTEN;

    (void)W5500_WriteRegisters(
        W5500_S0_CR,
        W5500_S0_BSB,
        &command,
        1U
    );

    /* Wait for SOCK_LISTEN */
    for(
        attempt = 0U;
        attempt < 20U;
        attempt++
    )
    {
        Time_DelayMs(1U);

        status =
            W5500_Socket0_GetStatus();

        if(
            status ==
            W5500_Sn_SR_LISTEN
        )
        {
            return 1U;
        }
    }

    return 0U;
}

/* ============================================================
 * START TCP CLIENT
 * ============================================================ */

uint8_t W5500_StartTCPClient(
    const uint8_t *serverIp,
    uint16_t serverPort,
    uint16_t sourcePort
)
{
    uint8_t mode = 0x01U; /* TCP mode; W5500 transmits when SEND is issued */
    uint8_t portData[2];
    uint8_t ipData[4];
    uint8_t keepAlive = 6U; /* 30 s: W5500 unit is 5 s */
    uint8_t command;
    uint8_t status = W5500_Sn_SR_CLOSED;
    uint8_t attempt;

    if(serverIp == NULL)
    {
        return 0U;
    }

    if(W5500_IsLinkUp() == 0U)
    {
        return 0U;
    }

    portData[0] = (uint8_t)(sourcePort >> 8U);
    portData[1] = (uint8_t)(sourcePort & 0xFFU);

    ipData[0] = serverIp[0];
    ipData[1] = serverIp[1];
    ipData[2] = serverIp[2];
    ipData[3] = serverIp[3];

    if(W5500_WriteRegisters(W5500_S0_MR, W5500_S0_BSB, &mode, 1U) == 0U)
    {
        return 0U;
    }

    if(W5500_WriteRegisters(W5500_S0_PORT, W5500_S0_BSB, portData, 2U) == 0U)
    {
        return 0U;
    }

    if(W5500_WriteRegisters(W5500_S0_DIPR, W5500_S0_BSB, ipData, 4U) == 0U)
    {
        return 0U;
    }

    portData[0] = (uint8_t)(serverPort >> 8U);
    portData[1] = (uint8_t)(serverPort & 0xFFU);

    if(W5500_WriteRegisters(W5500_S0_DPORT, W5500_S0_BSB, portData, 2U) == 0U)
    {
        return 0U;
    }

    if(W5500_WriteRegisters(W5500_S0_KPALVTR, W5500_S0_BSB, &keepAlive, 1U) == 0U)
    {
        return 0U;
    }

    command = W5500_Sn_CR_OPEN;

    if(W5500_WriteRegisters(W5500_S0_CR, W5500_S0_BSB, &command, 1U) == 0U)
    {
        return 0U;
    }

    for(attempt = 0U; attempt < 20U; attempt++)
    {
        Time_DelayMs(1U);

        status = W5500_Socket0_GetStatus();

        if(status == W5500_Sn_SR_INIT)
        {
            break;
        }

        if(status == W5500_Sn_SR_CLOSED)
        {
            continue;
        }
    }

    if(status != W5500_Sn_SR_INIT)
    {
        return 0U;
    }

    /* TCP client active-open: send SYN with CONNECT. */
    command = W5500_Sn_CR_CONNECT;

    if(W5500_WriteRegisters(W5500_S0_CR, W5500_S0_BSB, &command, 1U) == 0U)
    {
        return 0U;
    }

    return 1U;
}

/* ============================================================
 * SOCKET 0 RX SIZE
 * ============================================================ */

uint16_t W5500_Socket0_GetRxSize(void)
{
    uint8_t data[2];

    if(
        W5500_ReadRegisters(
            W5500_S0_RX_RSR,
            W5500_BSB_SOCKET0,
            data,
            2U
        ) == 0U
    )
    {
        return 0U;
    }

    return (uint16_t)(
        ((uint16_t)data[0] << 8U) |
        data[1]
    );
}

/* ============================================================
 * SOCKET 0 RECEIVE
 * ============================================================ */

uint16_t W5500_Socket0_Receive(
    uint8_t *buffer,
    uint16_t bufferSize
)
{
    uint8_t rxRdData[2];

    uint16_t rxSize;
    uint16_t rxReadPtr;
    uint16_t firstPart;
    uint16_t secondPart;

    if(
        (buffer == NULL) ||
        (bufferSize == 0U)
    )
    {
        return 0U;
    }

    rxSize =
        W5500_Socket0_GetRxSize();

    if(rxSize == 0U)
    {
        return 0U;
    }

    if(rxSize > bufferSize)
    {
        rxSize = bufferSize;
    }

    /* Read current RX read pointer */
    if(
        W5500_ReadRegisters(
            W5500_S0_RX_RD,
            W5500_BSB_SOCKET0,
            rxRdData,
            2U
        ) == 0U
    )
    {
        return 0U;
    }

    rxReadPtr =
        (uint16_t)(
            ((uint16_t)rxRdData[0] << 8U) |
            rxRdData[1]
        );

    /*
     * Socket 0 RX memory is initially 2 KB.
     * RX pointer therefore wraps at 0x0800.
     */
    firstPart =
        (uint16_t)(
            0x0800U -
            (rxReadPtr & 0x07FFU)
        );

    if(firstPart > rxSize)
    {
        firstPart = rxSize;
    }

    secondPart =
        (uint16_t)(
            rxSize - firstPart
        );

    /* First contiguous section */
    (void)W5500_ReadRegisters(
        (uint16_t)(
            rxReadPtr & 0x07FFU
        ),
        3U,
        buffer,
        firstPart
    );

    /* Wrapped section */
    if(secondPart > 0U)
    {
        (void)W5500_ReadRegisters(
            0U,
            3U,
            &buffer[firstPart],
            secondPart
        );
    }

    /* Advance RX read pointer */
    rxReadPtr =
        (uint16_t)(
            rxReadPtr + rxSize
        );

    rxRdData[0] =
        (uint8_t)(rxReadPtr >> 8U);

    rxRdData[1] =
        (uint8_t)(
            rxReadPtr & 0xFFU
        );

    (void)W5500_WriteRegisters(
        W5500_S0_RX_RD,
        W5500_BSB_SOCKET0,
        rxRdData,
        2U
    );

    /* Notify W5500 that RX data was consumed */
    {
        uint8_t command =
            W5500_S0_RECV;

        (void)W5500_WriteRegisters(
            W5500_S0_CR,
            W5500_BSB_SOCKET0,
            &command,
            1U
        );
    }

    return rxSize;
}

/* ============================================================
 * SOCKET 0 TX FREE SIZE
 * ============================================================ */

uint16_t W5500_Socket0_GetTxFreeSize(void)
{
    uint8_t data[2];

    if(
        W5500_ReadRegisters(
            W5500_S0_TX_FSR,
            W5500_BSB_SOCKET0,
            data,
            2U
        ) == 0U
    )
    {
        return 0U;
    }

    return (uint16_t)(
        ((uint16_t)data[0] << 8U) |
        data[1]
    );
}

/* ============================================================
 * SOCKET 0 SEND
 * ============================================================ */

uint16_t W5500_Socket0_Send(
    const uint8_t *buffer,
    uint16_t length
)
{
    uint8_t txWrData[2];

    uint16_t txFreeSize;
    uint16_t txWritePtr;
    uint16_t firstPart;
    uint16_t secondPart;

    if(
        (buffer == NULL) ||
        (length == 0U)
    )
    {
        return 0U;
    }

    txFreeSize =
        W5500_Socket0_GetTxFreeSize();

    if(txFreeSize == 0U)
    {
        return 0U;
    }

    if(length > txFreeSize)
    {
        length = txFreeSize;
    }

    /* Read current TX write pointer */
    if(
        W5500_ReadRegisters(
            W5500_S0_TX_WR,
            W5500_BSB_SOCKET0,
            txWrData,
            2U
        ) == 0U
    )
    {
        return 0U;
    }

    txWritePtr =
        (uint16_t)(
            ((uint16_t)txWrData[0] << 8U) |
            txWrData[1]
        );

    /*
     * Socket 0 TX memory is initially 2 KB.
     */
    firstPart =
        (uint16_t)(
            0x0800U -
            (txWritePtr & 0x07FFU)
        );

    if(firstPart > length)
    {
        firstPart = length;
    }

    secondPart =
        (uint16_t)(
            length - firstPart
        );

    /* First contiguous section */
    (void)W5500_WriteRegisters(
        (uint16_t)(
            txWritePtr & 0x07FFU
        ),
        2U,
        buffer,
        firstPart
    );

    /* Wrapped section */
    if(secondPart > 0U)
    {
        (void)W5500_WriteRegisters(
            0U,
            2U,
            &buffer[firstPart],
            secondPart
        );
    }

    /* Advance TX write pointer */
    txWritePtr =
        (uint16_t)(
            txWritePtr + length
        );

    txWrData[0] =
        (uint8_t)(
            txWritePtr >> 8U
        );

    txWrData[1] =
        (uint8_t)(
            txWritePtr & 0xFFU
        );

    (void)W5500_WriteRegisters(
        W5500_S0_TX_WR,
        W5500_BSB_SOCKET0,
        txWrData,
        2U
    );

    /* Tell W5500 to transmit */
    {
        uint8_t command =
            W5500_S0_SEND;

        (void)W5500_WriteRegisters(
            W5500_S0_CR,
            W5500_S0_BSB,
            &command,
            1U
        );
    }

    return length;
}
