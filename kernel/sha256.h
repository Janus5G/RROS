#ifndef EXPERIMENTALOS_KERNEL_SHA256_H
#define EXPERIMENTALOS_KERNEL_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    uint32_t block_len;
} os_sha256;

void os_sha256_init(os_sha256 *ctx);
void os_sha256_update(os_sha256 *ctx, const void *data, size_t size);
void os_sha256_final(os_sha256 *ctx, uint8_t digest[32]);
void os_sha256_hash(const void *data, size_t size, uint8_t digest[32]);

#endif
