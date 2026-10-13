#include "sha256_sw.h"
#include <string.h>
#include <esp_attr.h>

const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static DRAM_ATTR const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  ((z) ^ ((x) & ((y) ^ (z))))
#define MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define EP0(x)  (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)  (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

#define ROUND(a, b, c, d, e, f, g, h, i)                        \
    do {                                                        \
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];   \
        uint32_t t2 = EP0(a) + MAJ(a, b, c);                    \
        d += t1;                                                \
        h = t1 + t2;                                            \
    } while (0)

#define ROUND8(i)                                   \
    ROUND(a, b, c, d, e, f, g, h, (i) + 0);         \
    ROUND(h, a, b, c, d, e, f, g, (i) + 1);         \
    ROUND(g, h, a, b, c, d, e, f, (i) + 2);         \
    ROUND(f, g, h, a, b, c, d, e, (i) + 3);         \
    ROUND(e, f, g, h, a, b, c, d, (i) + 4);         \
    ROUND(d, e, f, g, h, a, b, c, (i) + 5);         \
    ROUND(c, d, e, f, g, h, a, b, (i) + 6);         \
    ROUND(b, c, d, e, f, g, h, a, (i) + 7)

__attribute__((optimize("O3")))
void IRAM_ATTR sha256_transform(uint32_t s[8], const uint32_t w[16])
{
    uint32_t W[64];
    for (int i = 0; i < 16; i++) W[i] = w[i];
    for (int i = 16; i < 64; i++) W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];

    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    uint32_t e = s[4], f = s[5], g = s[6], h = s[7];

    ROUND8(0);  ROUND8(8);  ROUND8(16); ROUND8(24);
    ROUND8(32); ROUND8(40); ROUND8(48); ROUND8(56);

    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
    s[4] += e; s[5] += f; s[6] += g; s[7] += h;
}

void sha256(const uint8_t* data, size_t len, uint8_t out[32])
{
    uint32_t st[8];
    memcpy(st, SHA256_H0, sizeof(st));

    uint32_t w[16];
    uint8_t  block[64];
    size_t   off = 0;

    while (len - off >= 64) {
        for (int i = 0; i < 16; i++) {
            const uint8_t* p = data + off + 4 * i;
            w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        }
        sha256_transform(st, w);
        off += 64;
    }

    // Padding
    size_t rest = len - off;
    memset(block, 0, sizeof(block));
    memcpy(block, data + off, rest);
    block[rest] = 0x80;
    int blocks = (rest + 9 > 64) ? 2 : 1;
    uint64_t bits = (uint64_t)len * 8;

    for (int b = 0; b < blocks; b++) {
        if (b == blocks - 1) {
            for (int i = 0; i < 8; i++) block[63 - i] = (uint8_t)(bits >> (8 * i));
        }
        for (int i = 0; i < 16; i++) {
            const uint8_t* p = block + 4 * i;
            w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        }
        sha256_transform(st, w);
        memset(block, 0, sizeof(block));
    }

    for (int i = 0; i < 8; i++) {
        out[4 * i + 0] = st[i] >> 24;
        out[4 * i + 1] = st[i] >> 16;
        out[4 * i + 2] = st[i] >> 8;
        out[4 * i + 3] = st[i];
    }
}

void sha256d(const uint8_t* data, size_t len, uint8_t out[32])
{
    uint8_t tmp[32];
    sha256(data, len, tmp);
    sha256(tmp, 32, out);
}

__attribute__((optimize("O3")))
uint32_t IRAM_ATTR sw_scan(const uint32_t midstate[8], const uint32_t blk2[3],
                           uint32_t nonce, uint32_t count,
                           uint32_t* cands, int maxCands, int* nCands)
{
    uint32_t w1[16] = { blk2[0], blk2[1], blk2[2], 0, 0x80000000, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0,
                        0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    int found = 0;

    for (uint32_t i = 0; i < count; i++, nonce++) {
        w1[3] = nonce;                  // T[3]-Wort, echte Nonce = bswap32

        uint32_t s[8];
        memcpy(s, midstate, 32);
        sha256_transform(s, w1);

        memcpy(w2, s, 32);
        memcpy(s, SHA256_H0, 32);
        sha256_transform(s, w2);

        // Obere 16 Bit des (little-endian) Hashes = untere 16 Bit von H7
        if ((s[7] & 0xFFFF) == 0 && found < maxCands) cands[found++] = nonce;
    }
    *nCands = found;
    return count;
}
