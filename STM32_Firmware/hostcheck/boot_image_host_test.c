/*
 * Host test of the bootloader's image check (Bootloader/boot_image.c)
 * against images signed by Tools/fw_sign.py (hostcheck/make_test_images.py).
 * Build once per board: -DSTM32F429xx or -DSTM32F411xE.
 *
 * Usage: boot_image_host_test <signed image>
 */

#include "../Bootloader/boot_image.h"
#include "../Firmware/Common/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t g_bootPublicKey[64];

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if(!(cond)) {                                                   \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);            \
            g_failures++;                                               \
        }                                                               \
    } while(0)

static uint8_t *g_image;
static uint32_t g_imageSize;

static uint8_t ReadImage(uint32_t offset, uint8_t *data, uint32_t length)
{
    if((offset > g_imageSize) || (length > (g_imageSize - offset)))
        return 0U;

    memcpy(data, &g_image[offset], length);
    return 1U;
}

static BootImage_Result_t Check(void)
{
    FwImage_Header_t header;

    return BootImage_Check(ReadImage, g_bootPublicKey, &header);
}

static void TestSha256(void)
{
    static const uint8_t abc[32] =
    {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
        0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    /* 1,000,000 x 'a', fed in odd-sized pieces. */
    static const uint8_t million[32] =
    {
        0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7, 0xe2,
        0x84, 0xd7, 0x3e, 0x67, 0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97, 0x20, 0x0e,
        0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0
    };
    Sha256_Context_t sha;
    uint8_t digest[32];
    uint8_t block[997];
    uint32_t left = 1000000U;

    Sha256_Init(&sha);
    Sha256_Update(&sha, (const uint8_t *)"abc", 3U);
    Sha256_Final(&sha, digest);
    CHECK(memcmp(digest, abc, 32) == 0);

    memset(block, 'a', sizeof(block));
    Sha256_Init(&sha);

    while(left > 0U)
    {
        uint32_t n = (left < sizeof(block)) ? left : (uint32_t)sizeof(block);

        Sha256_Update(&sha, block, n);
        left -= n;
    }

    Sha256_Final(&sha, digest);
    CHECK(memcmp(digest, million, 32) == 0);
}

int main(int argc, char **argv)
{
    FILE *f;
    uint32_t i;

    printf("boot image host test (board 0x%lX)\n", (unsigned long)FW_BOARD_ID);

    TestSha256();

    if(argc != 2 || (f = fopen(argv[1], "rb")) == NULL)
    {
        printf("  cannot open the test image\n");
        return 1;
    }

    g_image = malloc(FW_STAGING_SIZE);
    g_imageSize = (uint32_t)fread(g_image, 1, FW_STAGING_SIZE, f);
    fclose(f);

    /* The signed image as fw_sign.py wrote it. */
    CHECK(Check() == BOOT_IMAGE_VALID);

    /* A changed byte anywhere in the binary. */
    for(i = FW_IMAGE_HEADER_SIZE; i < g_imageSize; i += 4099U)
    {
        g_image[i] ^= 0x01U;
        CHECK(Check() == BOOT_IMAGE_BAD_HASH);
        g_image[i] ^= 0x01U;
    }

    /* Signed header fields: version text, the hash itself, a reserved byte. */
    g_image[0x21] ^= 0x01U;
    CHECK(Check() == BOOT_IMAGE_BAD_SIGNATURE);
    g_image[0x21] ^= 0x01U;

    g_image[0x7F] ^= 0x80U;
    CHECK(Check() == BOOT_IMAGE_BAD_SIGNATURE);
    g_image[0x7F] ^= 0x80U;

    /* The signature itself. */
    for(i = 0x80U; i < 0xC0U; i += 7U)
    {
        g_image[i] ^= 0x10U;
        CHECK(Check() == BOOT_IMAGE_BAD_SIGNATURE);
        g_image[i] ^= 0x10U;
    }

    /* Wrong board, wrong load address, oversized: rejected before the
     * signature is even checked. */
    g_image[0x08] ^= 0x01U;
    CHECK(Check() == BOOT_IMAGE_BAD_HEADER);
    g_image[0x08] ^= 0x01U;

    g_image[0x0D] ^= 0x01U;
    CHECK(Check() == BOOT_IMAGE_BAD_HEADER);
    g_image[0x0D] ^= 0x01U;

    g_image[0x13] = 0x01U;          /* imageSize >= 16 MB */
    CHECK(Check() == BOOT_IMAGE_BAD_HEADER);
    g_image[0x13] = 0x00U;

    /* Handled already (INSTALLED or rejected), or erased staging: nothing. */
    memset(&g_image[FW_IMAGE_STATE_OFFSET], 0x00, 4U);
    CHECK(Check() == BOOT_IMAGE_NONE);
    memset(&g_image[FW_IMAGE_STATE_OFFSET], 0xFF, 4U);

    g_image[0] = 0xFFU;
    CHECK(Check() == BOOT_IMAGE_NONE);
    g_image[0] = 0x54U;

    /* A truncated upload: the binary runs past the staged bytes. */
    g_imageSize -= 1U;
    CHECK(Check() != BOOT_IMAGE_VALID);
    g_imageSize += 1U;

    CHECK(Check() == BOOT_IMAGE_VALID);

    free(g_image);

    if(g_failures != 0)
    {
        printf("%d boot image checks FAILED\n", g_failures);
        return 1;
    }

    printf("all boot image checks passed\n");
    return 0;
}
