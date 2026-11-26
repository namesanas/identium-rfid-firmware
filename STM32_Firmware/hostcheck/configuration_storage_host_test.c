#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../Firmware/Services/configuration/configuration_storage.c"
static int failWord = -1;
static uint32_t erasedSector;
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase, uint32_t *error)
{
    assert(erase->Sector == 6U || erase->Sector == 7U);
    erasedSector = erase->Sector;
    memset((void *)(uintptr_t)(erase->Sector == 6U ? CONFIG_STORAGE_ADDRESS_A : CONFIG_STORAGE_ADDRESS_B), 0xFF, 128U*1024U);
    *error = 0xFFFFFFFFU;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t word)
{
    (void)type;
    if(failWord == 0) return HAL_ERROR;
    if(failWord > 0) failWord--;
    *(uint32_t *)(uintptr_t)address = (uint32_t)word;
    return HAL_OK;
}
int main(void)
{
    void *base = (void *)(uintptr_t)CONFIG_STORAGE_ADDRESS_A;
#ifdef _WIN32
    assert(VirtualAlloc(base, 256U*1024U, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE) == base);
#else
    assert(mmap(base, 256U*1024U, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) == base);
#endif
    memset(base, 0xFF, 256U*1024U);
    Configuration_StorageData_t data, loaded;
    Configuration_Storage_GetDefaults(&data);
    for(unsigned i = 1; i <= 8; ++i)
    {
        data.heartbeatSeconds = i + 10U;
        assert(Configuration_Storage_Save(&data));
        assert(erasedSector == ((i & 1U) ? 6U : 7U));
        assert(Configuration_Storage_Load(&loaded));
        assert(loaded.heartbeatSeconds == i + 10U);
    }
    /* Interrupt every programmed word: latest committed settings survive. */
    for(unsigned i = 0; i < sizeof(Configuration_StorageRecordV2_t)/4U; ++i)
    {
        failWord = (int)i;
        data.heartbeatSeconds = 99U;
        assert(!Configuration_Storage_Save(&data));
        assert(Configuration_Storage_Load(&loaded));
        assert(loaded.heartbeatSeconds == 18U);
    }
    failWord = -1;
    Configuration_StorageRecordV2_t *a = (void *)(uintptr_t)CONFIG_STORAGE_ADDRESS_A;
    Configuration_StorageRecordV2_t *b = (void *)(uintptr_t)CONFIG_STORAGE_ADDRESS_B;
    *a = *b; a->sequence = 0xFFFFFFFEU; b->sequence = 0xFFFFFFFFU;
    assert(Configuration_Storage_Save(&data));
    assert(a->sequence == 0U);
    assert(Configuration_Storage_Save(&data));
    assert(b->sequence == 1U);
    puts("Configuration alternating slots, interrupted writes and sequence wrap: PASS");
    return 0;
}
