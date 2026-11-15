#include "chacha20.h"

#define ROTL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define QR(a, b, c, d) \
    a += b; d ^= a; d = ROTL(d, 16); \
    c += d; b ^= c; b = ROTL(b, 12); \
    a += b; d ^= a; d = ROTL(d, 8); \
    c += d; b ^= c; b = ROTL(b, 7)

static uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void block(const uint32_t in[16], uint8_t out[64])
{
    uint32_t x[16];
    for (unsigned i = 0; i < 16; i++) {
        x[i] = in[i];
    }
    for (unsigned round = 0; round < 10; round++) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (unsigned i = 0; i < 16; i++) {
        uint32_t v = x[i] + in[i];
        out[i * 4] = (uint8_t)v;
        out[i * 4 + 1] = (uint8_t)(v >> 8);
        out[i * 4 + 2] = (uint8_t)(v >> 16);
        out[i * 4 + 3] = (uint8_t)(v >> 24);
    }
}

void os_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                     const uint8_t *in, uint8_t *out, size_t length)
{
    uint32_t state[16];
    uint8_t stream[64];
    state[0] = 0x61707865u; state[1] = 0x3320646eu; state[2] = 0x79622d32u; state[3] = 0x6b206574u;
    for (unsigned i = 0; i < 8; i++) {
        state[4 + i] = load32(key + i * 4);
    }
    state[12] = counter;
    for (unsigned i = 0; i < 3; i++) {
        state[13 + i] = load32(nonce + i * 4);
    }
    size_t done = 0;
    while (done < length) {
        block(state, stream);
        state[12]++;
        size_t n = length - done < 64 ? length - done : 64;
        for (size_t i = 0; i < n; i++) {
            out[done + i] = (uint8_t)(in[done + i] ^ stream[i]);
        }
        done += n;
    }
    for (unsigned i = 0; i < 64; i++) {
        ((volatile uint8_t *)stream)[i] = 0;
    }
    for (unsigned i = 0; i < 16; i++) {
        ((volatile uint32_t *)state)[i] = 0;
    }
}
