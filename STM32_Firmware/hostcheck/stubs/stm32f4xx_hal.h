#ifndef HOSTCHECK_STM32F4XX_HAL_H
#define HOSTCHECK_STM32F4XX_HAL_H
#include <stdint.h>
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct { uint32_t TypeErase, VoltageRange, Sector, NbSectors; } FLASH_EraseInitTypeDef;
#define FLASH_SECTOR_6 6U
#define FLASH_SECTOR_7 7U
#define FLASH_TYPEERASE_SECTORS 0U
#define FLASH_VOLTAGE_RANGE_3 3U
#define FLASH_TYPEPROGRAM_WORD 0U
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *, uint32_t *);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t, uint32_t, uint64_t);
#endif
