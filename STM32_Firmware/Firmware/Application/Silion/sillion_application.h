#ifndef RFID_CONTROLLER_SILLION_APPLICATION_H_
#define RFID_CONTROLLER_SILLION_APPLICATION_H_

#include "../../Protocols/silion/silion_protocol.h"

typedef enum
{
    SILION_APP_IDLE = 0U,
    SILION_APP_INVENTORY,
    /* Module UART is owned by the Reader Manager module bridge. */
    SILION_APP_BRIDGE
} SILION_ApplicationState_t;

void SILION_Application_Init(Silion_Handle_t *pSilionHandle);
uint8_t SILION_Application_ConfigureReader(void);
void SILION_Application_Task(void);

uint8_t SILION_Application_GetVersion(void);
uint8_t SILION_Application_GetSerial(void);
uint8_t SILION_Application_GetTemperature(void);
uint8_t SILION_Application_GetRegion(void);
uint8_t SILION_Application_GetAntenna(void);
uint8_t SILION_Application_GetPower(void);
uint8_t SILION_Application_GetProtocol(void);
uint8_t SILION_Application_GetSession(void);
uint8_t SILION_Application_GetFrequency(void);
uint8_t SILION_Application_GetRegions(void);
uint8_t SILION_Application_GetGen2(void);

uint8_t SILION_Application_ReadTagData(
        uint16_t timeoutMs,
        uint8_t memBank,
        uint32_t address,
        uint8_t wordCount,
        const uint8_t *epc,
        uint8_t epcLengthBytes);

uint8_t SILION_Application_SetPower(
    uint16_t readPower,
    uint16_t writePower
);

uint8_t SILION_Application_SetAntenna(
    uint8_t txAntenna,
    uint8_t rxAntenna
);

uint8_t SILION_Application_SetRegion(uint8_t region);
uint8_t SILION_Application_SetSession(uint8_t session);

/* Gen2 inventory target and Q. */
#define SILION_GEN2_TARGET_A     0U
#define SILION_GEN2_TARGET_B     1U
#define SILION_GEN2_TARGET_AB    2U   /* A/B flipping */
#define SILION_GEN2_Q_DYNAMIC    0xFFU

uint8_t SILION_Application_SetGen2(uint8_t target, uint8_t q);

/* Gen2 RF mode (module gen2tagEncoding, e.g. 113 = RF mode 13).
 * 0 means "module default" and is not sent. */
uint8_t SILION_Application_IsGen2Profile(uint8_t profile);
uint8_t SILION_Application_SetGen2Profile(uint8_t profile);

uint8_t SILION_Application_WriteTagData(
    const uint8_t *epc,
    uint8_t epcLengthBytes,
    uint8_t memBank,
    uint32_t address,
    uint32_t accessPassword,
    const uint8_t *writeData,
    uint8_t writeDataLength
);

uint8_t SILION_Application_WriteTagEPC(
    const uint8_t *oldEpc,
    uint8_t oldEpcLengthBytes,
    const uint8_t *newEpc,
    uint8_t newEpcLengthBytes
);

uint8_t SILION_Application_LockTag(
    const uint8_t *epc,
    uint8_t epcLengthBytes,
    uint32_t accessPassword,
    uint16_t maskBits,
    uint16_t actionBits
);

uint8_t SILION_Application_KillTag(
    const uint8_t *epc,
    uint8_t epcLengthBytes,
    uint32_t killPassword
);

uint8_t SILION_Application_SingleInventory(uint16_t timeoutMs);
uint8_t SILION_Application_SynchronousInventory(uint16_t timeoutMs);
uint8_t SILION_Application_StartInventory(void);
uint8_t SILION_Application_StopInventory(void);
uint8_t SILION_Application_IsInventoryRunning(void);

SILION_ApplicationState_t SILION_Application_GetState(void);

/*
 * Reader Manager module bridge ownership.
 *
 * Acquire stops local inventory (remembering whether it ran) and
 * switches the module UART to raw mode. Release stops any module
 * async inventory left running by the PC, returns the UART to local
 * parsing and restarts local inventory if it was running before.
 */
uint8_t SILION_Application_BridgeAcquire(void);
void SILION_Application_BridgeRelease(void);
uint8_t SILION_Application_BridgeIsTxReady(void);
uint8_t SILION_Application_BridgeSend(
    const uint8_t *frame,
    uint16_t frameLength
);

#endif /* RFID_CONTROLLER_SILLION_APPLICATION_H_ */
