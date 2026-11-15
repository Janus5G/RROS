#ifndef EXPERIMENTALOS_KERNEL_CHACHA20_H
#define EXPERIMENTALOS_KERNEL_CHACHA20_H

#include <stddef.h>
#include <stdint.h>

/* RFC 8439 ChaCha20 stream cipher (no authentication; callers must add a MAC). */
void os_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                     const uint8_t *in, uint8_t *out, size_t length);

#endif
