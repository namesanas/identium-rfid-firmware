#include "firmware_image.h"
#include "sha256.h"

#include <string.h>

uint8_t FwImage_HeaderIsValid(const FwImage_Header_t *header)
{
    if(header == NULL)
        return 0U;

    if(
        (header->magic != FW_IMAGE_MAGIC) ||
        (header->headerVersion != FW_IMAGE_HEADER_VERSION) ||
        (header->headerSize != FW_IMAGE_HEADER_SIZE) ||
        (header->boardId != FW_BOARD_ID) ||
        (header->loadAddress != FW_APP_BASE) ||
        (header->imageSize == 0U) ||
        (header->imageSize > FW_APP_MAX_SIZE) ||
        ((FW_IMAGE_HEADER_SIZE + header->imageSize) > FW_STAGING_SIZE) ||
        (memchr(header->version, '\0', sizeof(header->version)) == NULL)
    )
    {
        return 0U;
    }

    return 1U;
}

uint8_t FwImage_PayloadMatches(
    FwImage_Read_t read,
    const FwImage_Header_t *header
)
{
    Sha256_Context_t sha;
    uint8_t chunk[256];
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint32_t offset;

    Sha256_Init(&sha);

    for(offset = 0U; offset < header->imageSize; offset += sizeof(chunk))
    {
        uint32_t length = header->imageSize - offset;

        if(length > sizeof(chunk))
            length = sizeof(chunk);

        if(read(FW_IMAGE_HEADER_SIZE + offset, chunk, length) == 0U)
            return 0U;

        Sha256_Update(&sha, chunk, length);
    }

    Sha256_Final(&sha, digest);

    return (memcmp(digest, header->imageSha256, sizeof(digest)) == 0) ? 1U : 0U;
}
