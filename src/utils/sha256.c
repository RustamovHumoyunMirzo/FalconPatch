#include "utils/sha256.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    unsigned char block[64];
    uint32_t state[8];
    uint64_t bit_count;
    size_t block_size;
} FpatchSha256Context;

static uint32_t rotate_right(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

static uint32_t read_be32(const unsigned char *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void transform(FpatchSha256Context *context, const unsigned char block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t words[64];
    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    size_t i;

    for (i = 0; i < 16; i++) {
        words[i] = read_be32(block + i * 4);
    }
    for (; i < 64; i++) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
                      rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
                      rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    for (i = 0; i < 64; i++) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choice + constants[i] + words[i];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void initialize(FpatchSha256Context *context) {
    memset(context, 0, sizeof(*context));
    context->state[0] = 0x6a09e667u;
    context->state[1] = 0xbb67ae85u;
    context->state[2] = 0x3c6ef372u;
    context->state[3] = 0xa54ff53au;
    context->state[4] = 0x510e527fu;
    context->state[5] = 0x9b05688cu;
    context->state[6] = 0x1f83d9abu;
    context->state[7] = 0x5be0cd19u;
}

static void update(FpatchSha256Context *context, const unsigned char *data, size_t size) {
    size_t i;
    for (i = 0; i < size; i++) {
        context->block[context->block_size++] = data[i];
        if (context->block_size == sizeof(context->block)) {
            transform(context, context->block);
            context->bit_count += 512u;
            context->block_size = 0;
        }
    }
}

static void finish(FpatchSha256Context *context, unsigned char digest[32]) {
    uint64_t total_bits = context->bit_count + (uint64_t)context->block_size * 8u;
    size_t i = context->block_size;

    context->block[i++] = 0x80u;
    if (i > 56) {
        memset(context->block + i, 0, sizeof(context->block) - i);
        transform(context, context->block);
        i = 0;
    }
    memset(context->block + i, 0, 56 - i);
    for (i = 0; i < 8; i++) {
        context->block[63 - i] = (unsigned char)(total_bits >> (i * 8));
    }
    transform(context, context->block);
    for (i = 0; i < 8; i++) {
        digest[i * 4] = (unsigned char)(context->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(context->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(context->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)context->state[i];
    }
}

void fpatch_sha256(const void *data, size_t size, unsigned char digest[32]) {
    FpatchSha256Context context;
    initialize(&context);
    update(&context, (const unsigned char *)data, size);
    finish(&context, digest);
}

void fpatch_sha256_hex(const void *data, size_t size, char output[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned char digest[32];
    size_t i;

    fpatch_sha256(data, size, digest);
    for (i = 0; i < sizeof(digest); i++) {
        output[i * 2] = digits[digest[i] >> 4];
        output[i * 2 + 1] = digits[digest[i] & 0x0fu];
    }
    output[64] = '\0';
}
