#include "boot_flash.h"
#include "boot_watchdog.h"

#include "stm32f4xx.h"

#define BOOT_FLASH_KEY1         0x45670123UL
#define BOOT_FLASH_KEY2         0xCDEF89ABUL

#define BOOT_FLASH_ERRORS       \
    (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
     FLASH_SR_PGPERR | FLASH_SR_PGSERR)

/* x32 parallelism: valid for the 2.7-3.6 V supply on both boards. */
#define BOOT_FLASH_PSIZE_X32    FLASH_CR_PSIZE_1

static uint8_t BootFlash_Wait(void)
{
    while((FLASH->SR & FLASH_SR_BSY) != 0U)
    {
        BootWatchdog_Kick();
    }

    if((FLASH->SR & BOOT_FLASH_ERRORS) != 0U)
    {
        FLASH->SR = BOOT_FLASH_ERRORS;
        return 0U;
    }

    return 1U;
}

/* Sector number of an address: 4 x 16 KB, 64 KB, then 128 KB sectors,
 * repeated in the F429's second 1 MB bank (sectors 12-23). */
static uint32_t BootFlash_Sector(uint32_t address)
{
    uint32_t offset = address - FLASH_BASE;
    uint32_t bankFirst = 0U;

    if(offset >= 0x100000UL)
    {
        offset -= 0x100000UL;
        bankFirst = 12U;
    }

    if(offset < 0x10000UL)
        return bankFirst + (offset / 0x4000UL);

    if(offset < 0x20000UL)
        return bankFirst + 4U;

    return bankFirst + 4U + (offset / 0x20000UL);
}

void BootFlash_Unlock(void)
{
    if((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        FLASH->KEYR = BOOT_FLASH_KEY1;
        FLASH->KEYR = BOOT_FLASH_KEY2;
    }

    FLASH->SR = BOOT_FLASH_ERRORS;
}

void BootFlash_Lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

uint8_t BootFlash_EraseRange(uint32_t address, uint32_t length)
{
    uint32_t sector;
    uint32_t last;

    if(length == 0U)
        return 1U;

    last = BootFlash_Sector(address + length - 1U);

    for(sector = BootFlash_Sector(address); sector <= last; sector++)
    {
        /* Sectors 12-23 are SNB 16-27. */
        uint32_t snb = (sector < 12U) ? sector : (sector + 4U);

        if(BootFlash_Wait() == 0U)
            return 0U;

        FLASH->CR = BOOT_FLASH_PSIZE_X32 | FLASH_CR_SER |
                    (snb << FLASH_CR_SNB_Pos);
        FLASH->CR |= FLASH_CR_STRT;

        if(BootFlash_Wait() == 0U)
        {
            FLASH->CR &= ~FLASH_CR_SER;
            return 0U;
        }

        FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    }

    return 1U;
}

uint8_t BootFlash_ProgramWords(uint32_t address, const uint32_t *words, uint32_t count)
{
    uint32_t i;
    uint8_t ok = 1U;

    if(BootFlash_Wait() == 0U)
        return 0U;

    FLASH->CR = BOOT_FLASH_PSIZE_X32 | FLASH_CR_PG;

    for(i = 0U; i < count; i++)
    {
        *(volatile uint32_t *)(address + (i * 4U)) = words[i];

        if(BootFlash_Wait() == 0U)
        {
            ok = 0U;
            break;
        }
    }

    FLASH->CR &= ~FLASH_CR_PG;

    return ok;
}
