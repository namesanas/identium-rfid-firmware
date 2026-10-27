/*
 * sha256.h
 *
 * SHA-256 (FIPS 180-4). Shared by the bootloader and the application's
 * update service to check firmware images.
 */

#ifndef RFID_CONTROLLER_SHA256_H
#define RFID_CONTROLLER_SHA256_H

#include <stdint.h>

#define SHA256_DIGEST_SIZE  32U

typedef struct
{
    uint32_t state[8];
    uint64_t length;
    uint8_t  block[64];
    uint32_t blockUsed;
} Sha256_Context_t;

void Sha256_Init(Sha256_Context_t *ctx);
void Sha256_Update(Sha256_Context_t *ctx, const uint8_t *data, uint32_t length);
void Sha256_Final(Sha256_Context_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

#endif /* RFID_CONTROLLER_SHA256_H */
