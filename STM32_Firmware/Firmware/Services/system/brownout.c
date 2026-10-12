#include "brownout.h"

#include "stm32f4xx_hal.h"

#include "../../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F411_CUSTOM)
#define BROWNOUT_LEVEL     3U
#define BROWNOUT_OB_LEVEL  OB_BOR_LEVEL3
#else
#define BROWNOUT_LEVEL     2U
#define BROWNOUT_OB_LEVEL  OB_BOR_LEVEL2
#endif


uint8_t Brownout_GetLevel(void)
{
    FLASH_OBProgramInitTypeDef ob;

    HAL_FLASHEx_OBGetConfig(&ob);

    switch(ob.BORLevel)
    {
        case OB_BOR_LEVEL3: return 3U;
        case OB_BOR_LEVEL2: return 2U;
        case OB_BOR_LEVEL1: return 1U;
        default:            return 0U;
    }
}


uint8_t Brownout_Init(void)
{
    FLASH_OBProgramInitTypeDef ob;
    HAL_StatusTypeDef status;

    if(Brownout_GetLevel() == BROWNOUT_LEVEL)
    {
        return 1U;
    }

    /* An option byte write cut short by a reset leaves the option bytes
     * unloadable, and the MCU falls back to read protection level 1
     * with PCROP on every sector: flash can then only be recovered by a
     * mass erase. A debugger resets the core whenever it likes, so the
     * write waits for a boot without one. */
    if((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
    {
        return 0U;
    }

    /* Option bytes that are not in the plain unprotected state are left
     * alone. */
    HAL_FLASHEx_OBGetConfig(&ob);

    if(ob.RDPLevel != OB_RDP_LEVEL_0)
    {
        return 0U;
    }

    /* Only the BOR bits of the option bytes are written; read
     * protection and write protection are left as they are. */
    ob.OptionType = OPTIONBYTE_BOR;
    ob.BORLevel = BROWNOUT_OB_LEVEL;

    if(HAL_FLASH_OB_Unlock() != HAL_OK)
    {
        return 0U;
    }

    status = HAL_FLASHEx_OBProgram(&ob);

    if(status == HAL_OK)
    {
        status = HAL_FLASH_OB_Launch();
    }

    (void)HAL_FLASH_OB_Lock();

    return (uint8_t)((status == HAL_OK) && (Brownout_GetLevel() == BROWNOUT_LEVEL));
}
