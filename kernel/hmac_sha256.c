#include "hmac_sha256.h"
#include "sha256.h"

void os_hmac_sha256(const uint8_t *key, size_t key_len, const void *msg, size_t msg_len, uint8_t out[32])
{
    uint8_t block[64], inner[32];
    os_sha256 ctx;
    for (size_t i = 0; i < sizeof block; i++) block[i] = 0;
    if (key_len > sizeof block) os_sha256_hash(key, key_len, block);
    else for (size_t i = 0; i < key_len; i++) block[i] = key[i];

    for (size_t i = 0; i < sizeof block; i++) block[i] ^= 0x36u;
    os_sha256_init(&ctx);
    os_sha256_update(&ctx, block, sizeof block);
    os_sha256_update(&ctx, msg, msg_len);
    os_sha256_final(&ctx, inner);

    for (size_t i = 0; i < sizeof block; i++) block[i] ^= (uint8_t)(0x36u ^ 0x5cu);
    os_sha256_init(&ctx);
    os_sha256_update(&ctx, block, sizeof block);
    os_sha256_update(&ctx, inner, sizeof inner);
    os_sha256_final(&ctx, out);

    volatile uint8_t *w = block;
    for (size_t i = 0; i < sizeof block; i++) w[i] = 0;
}

int os_hmac_tag_equal(const uint8_t a[32], const uint8_t b[32])
{
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}
