#include "gd25q.h"
#include "../../Services/Time/time_service.h"
#include <stddef.h>

/* ============================================================
 * COMMANDS
 * ============================================================ */

#define GD25Q_CMD_WRITE_ENABLE      0x06U
#define GD25Q_CMD_READ_STATUS1      0x05U
#define GD25Q_CMD_WRITE_STATUS1     0x01U
#define GD25Q_CMD_READ_DATA         0x03U
#define GD25Q_CMD_PAGE_PROGRAM      0x02U
#define GD25Q_CMD_SECTOR_ERASE      0x20U
#define GD25Q_CMD_BLOCK_ERASE       0xD8U
#define GD25Q_CMD_CHIP_ERASE        0xC7U
#define GD25Q_CMD_READ_JEDEC_ID     0x9FU
#define GD25Q_CMD_RELEASE_POWERDOWN 0xABU

#define GD25Q_STATUS_WIP            0x01U
#define GD25Q_STATUS_WEL            0x02U
/* BP0..BP4: block protection, all clear = whole array writable. */
#define GD25Q_STATUS_BP_MASK        0x7CU

/* ============================================================
 * INTERNAL DEVICE CONTEXT
 * ============================================================ */

typedef struct
{
    SPI_Handle_t *pSPI;

    GPIO_RegDef_t *pCSPort;
    uint8_t csPin;

    uint8_t present;

} GD25Q_Context_t;

static GD25Q_Context_t g_gd25q;

/* ============================================================
 * CHIP SELECT / SPI
 * ============================================================ */

static void GD25Q_CS_Low(void)
{
    GPIO_WriteToOutputPin(
        g_gd25q.pCSPort,
        g_gd25q.csPin,
        GPIO_PIN_RESET
    );
}

static void GD25Q_SPI_WaitUntilNotBusy(void)
{
    SPI_RegDef_t *pSPIx = g_gd25q.pSPI->pSPIx;

    while(SPI_GetFlagStatus(pSPIx,(1UL << SPI_SR_BSY_BIT)) == FLAG_SET)
    {
    }
}

/* Waits for the last byte to leave the shifter before releasing CS. */
static void GD25Q_CS_High(void)
{
    GD25Q_SPI_WaitUntilNotBusy();

    GPIO_WriteToOutputPin(
        g_gd25q.pCSPort,
        g_gd25q.csPin,
        GPIO_PIN_SET
    );
}

static uint8_t GD25Q_SPI_Transfer(uint8_t data)
{
    SPI_RegDef_t *pSPIx = g_gd25q.pSPI->pSPIx;

    while(SPI_GetFlagStatus(pSPIx,SPI_TXE_FLAG) == FLAG_RESET)
    {
    }

    *((volatile uint8_t *)&pSPIx->DR) = data;

    while(SPI_GetFlagStatus(pSPIx,SPI_RXNE_FLAG) == FLAG_RESET)
    {
    }

    return *((volatile uint8_t *)&pSPIx->DR);
}

static void GD25Q_SendAddress(uint32_t address)
{
    (void)GD25Q_SPI_Transfer((uint8_t)(address >> 16U));
    (void)GD25Q_SPI_Transfer((uint8_t)(address >> 8U));
    (void)GD25Q_SPI_Transfer((uint8_t)address);
}

static void GD25Q_SimpleCommand(uint8_t command)
{
    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(command);
    GD25Q_CS_High();
}

static uint8_t GD25Q_ReadStatus1(void)
{
    uint8_t status;

    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(GD25Q_CMD_READ_STATUS1);
    status = GD25Q_SPI_Transfer(0xFFU);
    GD25Q_CS_High();

    return status;
}

/* Write enable, then confirm WEL so a dropped command is caught
 * before an erase or program silently does nothing. */
static uint8_t GD25Q_WriteEnable(void)
{
    GD25Q_SimpleCommand(GD25Q_CMD_WRITE_ENABLE);

    return ((GD25Q_ReadStatus1() & GD25Q_STATUS_WEL) != 0U) ? 1U : 0U;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

uint32_t GD25Q_ReadJedecId(void)
{
    uint32_t id;

    if((g_gd25q.pSPI == NULL) || (g_gd25q.pCSPort == NULL))
    {
        return 0U;
    }

    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(GD25Q_CMD_READ_JEDEC_ID);
    id = (uint32_t)GD25Q_SPI_Transfer(0xFFU) << 16U;
    id |= (uint32_t)GD25Q_SPI_Transfer(0xFFU) << 8U;
    id |= (uint32_t)GD25Q_SPI_Transfer(0xFFU);
    GD25Q_CS_High();

    return id;
}

uint8_t GD25Q_Init(
    SPI_Handle_t *pSPI,
    GPIO_RegDef_t *pCSPort,
    uint8_t csPin
)
{
    uint32_t id;
    uint8_t status;

    g_gd25q.pSPI = pSPI;
    g_gd25q.pCSPort = pCSPort;
    g_gd25q.csPin = csPin;
    g_gd25q.present = 0U;

    if((pSPI == NULL) || (pSPI->pSPIx == NULL) || (pCSPort == NULL))
    {
        return 0U;
    }

    GPIO_WriteToOutputPin(pCSPort, csPin, GPIO_PIN_SET);

    /* Wakes the chip if a previous run left it in deep power-down;
     * tRES1 is 20 us at most. */
    GD25Q_SimpleCommand(GD25Q_CMD_RELEASE_POWERDOWN);
    Time_DelayMs(1U);

    id = GD25Q_ReadJedecId();

    if(
        (((id >> 16U) & 0xFFU) != GD25Q_JEDEC_MANUFACTURER) ||
        ((id & 0xFFU) != GD25Q_JEDEC_CAPACITY_32MBIT)
    )
    {
        return 0U;
    }

    /* A program or erase cut short by a reset may still be running. */
    if(GD25Q_WaitReady(500U) == 0U)
    {
        return 0U;
    }

    status = GD25Q_ReadStatus1();

    if((status & GD25Q_STATUS_BP_MASK) != 0U)
    {
        if(GD25Q_WriteEnable() == 0U)
        {
            return 0U;
        }

        GD25Q_CS_Low();
        (void)GD25Q_SPI_Transfer(GD25Q_CMD_WRITE_STATUS1);
        (void)GD25Q_SPI_Transfer(
            (uint8_t)(status & (uint8_t)~GD25Q_STATUS_BP_MASK)
        );
        GD25Q_CS_High();

        if(GD25Q_WaitReady(100U) == 0U)
        {
            return 0U;
        }
    }

    g_gd25q.present = 1U;
    return 1U;
}

uint8_t GD25Q_IsPresent(void)
{
    return g_gd25q.present;
}

uint8_t GD25Q_IsBusy(void)
{
    if(g_gd25q.present == 0U)
    {
        return 0U;
    }

    return ((GD25Q_ReadStatus1() & GD25Q_STATUS_WIP) != 0U) ? 1U : 0U;
}

uint8_t GD25Q_WaitReady(uint32_t timeoutMs)
{
    uint32_t startMs = Time_GetTickMs();

    while((GD25Q_ReadStatus1() & GD25Q_STATUS_WIP) != 0U)
    {
        if((Time_GetTickMs() - startMs) >= timeoutMs)
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t GD25Q_Read(uint32_t address, uint8_t *pData, uint32_t length)
{
    uint32_t i;

    if(
        (g_gd25q.present == 0U) ||
        (pData == NULL) ||
        (address >= GD25Q_SIZE_BYTES) ||
        (length > (GD25Q_SIZE_BYTES - address))
    )
    {
        return 0U;
    }

    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(GD25Q_CMD_READ_DATA);
    GD25Q_SendAddress(address);

    for(i = 0U; i < length; i++)
    {
        pData[i] = GD25Q_SPI_Transfer(0xFFU);
    }

    GD25Q_CS_High();

    return 1U;
}

uint8_t GD25Q_StartPageProgram(
    uint32_t address,
    const uint8_t *pData,
    uint32_t length
)
{
    uint32_t i;

    if(
        (g_gd25q.present == 0U) ||
        (pData == NULL) ||
        (length == 0U) ||
        (address >= GD25Q_SIZE_BYTES) ||
        (((address % GD25Q_PAGE_SIZE) + length) > GD25Q_PAGE_SIZE)
    )
    {
        return 0U;
    }

    if(GD25Q_IsBusy() != 0U)
    {
        return 0U;
    }

    if(GD25Q_WriteEnable() == 0U)
    {
        return 0U;
    }

    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(GD25Q_CMD_PAGE_PROGRAM);
    GD25Q_SendAddress(address);

    for(i = 0U; i < length; i++)
    {
        (void)GD25Q_SPI_Transfer(pData[i]);
    }

    GD25Q_CS_High();

    return 1U;
}

uint8_t GD25Q_StartSectorErase(uint32_t address)
{
    if((g_gd25q.present == 0U) || (address >= GD25Q_SIZE_BYTES))
    {
        return 0U;
    }

    if(GD25Q_IsBusy() != 0U)
    {
        return 0U;
    }

    if(GD25Q_WriteEnable() == 0U)
    {
        return 0U;
    }

    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(GD25Q_CMD_SECTOR_ERASE);
    GD25Q_SendAddress(address - (address % GD25Q_SECTOR_SIZE));
    GD25Q_CS_High();

    return 1U;
}

uint8_t GD25Q_StartBlockErase(uint32_t address)
{
    if((g_gd25q.present == 0U) || (address >= GD25Q_SIZE_BYTES))
    {
        return 0U;
    }

    if(GD25Q_IsBusy() != 0U)
    {
        return 0U;
    }

    if(GD25Q_WriteEnable() == 0U)
    {
        return 0U;
    }

    GD25Q_CS_Low();
    (void)GD25Q_SPI_Transfer(GD25Q_CMD_BLOCK_ERASE);
    GD25Q_SendAddress(address - (address % GD25Q_BLOCK_SIZE));
    GD25Q_CS_High();

    return 1U;
}

uint8_t GD25Q_StartChipErase(void)
{
    if(g_gd25q.present == 0U)
    {
        return 0U;
    }

    if(GD25Q_IsBusy() != 0U)
    {
        return 0U;
    }

    if(GD25Q_WriteEnable() == 0U)
    {
        return 0U;
    }

    GD25Q_SimpleCommand(GD25Q_CMD_CHIP_ERASE);

    return 1U;
}
