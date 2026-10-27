#include "boot_image.h"

#include "../Firmware/Common/sha256.h"
#include "micro-ecc/uECC.h"

BootImage_Result_t BootImage_Check(
    FwImage_Read_t read,
    const uint8_t publicKey[64],
    FwImage_Header_t *header
)
{
    Sha256_Context_t sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    if(read(0U, (uint8_t *)header, sizeof(*header)) == 0U)
        return BOOT_IMAGE_NONE;

    /* Erased staging, or an image the bootloader already handled. */
    if(
        (header->magic != FW_IMAGE_MAGIC) ||
        (header->state != FW_IMAGE_STATE_PENDING)
    )
    {
        return BOOT_IMAGE_NONE;
    }

    if(FwImage_HeaderIsValid(header) == 0U)
        return BOOT_IMAGE_BAD_HEADER;

    if(FwImage_PayloadMatches(read, header) == 0U)
        return BOOT_IMAGE_BAD_HASH;

    Sha256_Init(&sha);
    Sha256_Update(&sha, (const uint8_t *)header, FW_IMAGE_SIGNED_SIZE);
    Sha256_Final(&sha, digest);

    if(
        uECC_verify(
            publicKey,
            digest,
            sizeof(digest),
            header->signature,
            uECC_secp256r1()
        ) == 0
    )
    {
        return BOOT_IMAGE_BAD_SIGNATURE;
    }

    return BOOT_IMAGE_VALID;
}

uint32_t BootImage_RejectState(BootImage_Result_t result)
{
    switch(result)
    {
        case BOOT_IMAGE_BAD_HEADER:    return FW_IMAGE_STATE_BAD_HEADER;
        case BOOT_IMAGE_BAD_HASH:      return FW_IMAGE_STATE_BAD_HASH;
        default:                       return FW_IMAGE_STATE_BAD_SIGNATURE;
    }
}
