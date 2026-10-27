/*
 * boot_main.c
 *
 * Bootloader in flash sector 0 (docs/design/0020-bootloader-and-signed-update.md).
 *
 * On every reset:
 *   1. If staging holds a PENDING image, check its SHA-256 and its
 *      ECDSA P-256 signature against the public key built in here.
 *   2. Valid: erase the application area, copy the binary with its
 *      first block (the vector table) last, check its SHA-256, then
 *      mark the image INSTALLED. A power cut before that mark simply
 *      repeats the copy on the next reset. If the install fails, the
 *      application is not started.
 *      Invalid: mark it with the reason; the application stays as is.
 *   3. Start the application at FW_APP_BASE.
 *
 * Runs on the 16 MHz HSI as left by reset and enables no interrupts.
 * Leaves RCC->CSR alone so the application still sees the reset cause,
 * and keeps its RAM in the top 8 KB, clear of the application's
 * .noinit fault record.
 */

#include "boot_flash.h"
#include "boot_image.h"
#include "boot_staging.h"
#include "boot_watchdog.h"

#include "../Firmware/Common/firmware_image.h"
#include "../Firmware/Common/sha256.h"

#include "stm32f4xx.h"

#include <string.h>

extern const uint8_t g_bootPublicKey[64];

__attribute__((section(".boot_info"), used))
const FwBoot_Info_t g_bootInfo =
{
    FW_BOOT_INFO_MAGIC,
    FW_BOOT_INFO_VERSION
};

#if defined(STM32F411xE)
#define BOOT_RAM_END    0x20020000UL
#else
#define BOOT_RAM_END    0x20030000UL
#endif

/* An install that fails this many times in a row is left for the
 * next reset (watchdog or power cycle) to try again. */
#define BOOT_INSTALL_ATTEMPTS   3U

/* Copies one block of up to 256 bytes from staging to the application
 * area. The last word is padded with erased bytes. */
static uint8_t Boot_CopyBlock(uint32_t offset, uint32_t length, uint32_t *words)
{
    memset(words, 0xFF, FW_IMAGE_HEADER_SIZE);

    if(BootStaging_Read(FW_IMAGE_HEADER_SIZE + offset, (uint8_t *)words, length) == 0U)
        return 0U;

    return BootFlash_ProgramWords(FW_APP_BASE + offset, words, (length + 3U) / 4U);
}

/* The first block, which holds the vector table, is programmed last
 * and only once the rest checks out. Until then the vectors read as
 * erased and Boot_StartApplication() refuses them, so an install that
 * fails or is cut short never leaves a startable half-copied image,
 * even if staging cannot be read on the next reset. */
static uint8_t Boot_Install(const FwImage_Header_t *header)
{
    uint32_t words[FW_IMAGE_HEADER_SIZE / 4U];
    uint32_t firstLength;
    uint32_t offset;
    Sha256_Context_t sha;
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint8_t ok;

    firstLength = (header->imageSize < sizeof(words)) ? header->imageSize : sizeof(words);

    BootFlash_Unlock();

    ok = BootFlash_EraseRange(FW_APP_BASE, header->imageSize);
    BootWatchdog_Kick();

    for(offset = firstLength; (ok != 0U) && (offset < header->imageSize); offset += sizeof(words))
    {
        uint32_t length = header->imageSize - offset;

        if(length > sizeof(words))
            length = sizeof(words);

        ok = Boot_CopyBlock(offset, length, words);
        BootWatchdog_Kick();
    }

    if(ok != 0U)
    {
        /* SHA-256 of the first block from staging plus the rest as
         * programmed. */
        memset(words, 0xFF, sizeof(words));
        ok = BootStaging_Read(FW_IMAGE_HEADER_SIZE, (uint8_t *)words, firstLength);
    }

    if(ok != 0U)
    {
        Sha256_Init(&sha);
        Sha256_Update(&sha, (const uint8_t *)words, firstLength);
        Sha256_Update(
            &sha,
            (const uint8_t *)(FW_APP_BASE + firstLength),
            header->imageSize - firstLength
        );
        Sha256_Final(&sha, digest);
        BootWatchdog_Kick();

        ok = (memcmp(digest, header->imageSha256, sizeof(digest)) == 0) ? 1U : 0U;
    }

    if(ok != 0U)
    {
        ok = BootFlash_ProgramWords(FW_APP_BASE, words, (firstLength + 3U) / 4U);
    }

    BootFlash_Lock();

    if(ok != 0U)
    {
        ok = (memcmp((const void *)FW_APP_BASE, words, firstLength) == 0) ? 1U : 0U;
    }

    return ok;
}

/* Returns 0 when an install failed, so the application must not be
 * started. */
static uint8_t Boot_CheckStaging(void)
{
    FwImage_Header_t header;
    BootImage_Result_t result;
    uint32_t attempt;
    uint8_t startApplication = 1U;

    if(BootStaging_Init() != 0U)
    {
        result = BootImage_Check(BootStaging_Read, g_bootPublicKey, &header);
        BootWatchdog_Kick();

        if(result == BOOT_IMAGE_VALID)
        {
            startApplication = 0U;

            /* On failure the image stays PENDING, intact in staging,
             * and the next reset tries again. */
            for(attempt = 0U; (startApplication == 0U) && (attempt < BOOT_INSTALL_ATTEMPTS); attempt++)
            {
                startApplication = Boot_Install(&header);
            }

            if(startApplication != 0U)
                (void)BootStaging_SetState(FW_IMAGE_STATE_INSTALLED);
        }
        else if(result != BOOT_IMAGE_NONE)
        {
            (void)BootStaging_SetState(BootImage_RejectState(result));
        }
    }

    BootStaging_Deinit();

    return startApplication;
}

static void Boot_StartApplication(void)
{
    const uint32_t *vectors = (const uint32_t *)FW_APP_BASE;
    uint32_t stack = vectors[0];
    uint32_t entry = vectors[1];

    if(
        (stack <= SRAM1_BASE) || (stack > BOOT_RAM_END) ||
        ((entry & 1U) == 0U) ||
        (entry < FW_APP_BASE) || (entry >= (FW_APP_BASE + FW_APP_MAX_SIZE))
    )
    {
        return;
    }

    SCB->VTOR = FW_APP_BASE;
    __DSB();
    __ISB();
    __set_MSP(stack);

    ((void (*)(void))entry)();
}

int main(void)
{
    BootWatchdog_Kick();

    if(Boot_CheckStaging() != 0U)
        Boot_StartApplication();

    /* No valid application, or an install failed. If the watchdog runs
     * it resets the board and the staging check runs again; otherwise
     * wait for a power cycle or SWD. */
    for(;;)
    {
    }
}
