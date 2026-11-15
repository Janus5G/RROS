#ifndef EXPERIMENTALOS_KERNEL_HMAC_SHA256_H
#define EXPERIMENTALOS_KERNEL_HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

void os_hmac_sha256(const uint8_t *key, size_t key_len, const void *msg, size_t msg_len, uint8_t out[32]);
/* Constant-time comparison of two 32-byte tags; returns 1 when equal. */
int os_hmac_tag_equal(const uint8_t a[32], const uint8_t b[32]);

#endif
