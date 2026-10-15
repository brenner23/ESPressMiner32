#include "sha256_sw.h"
#include <string.h>
#include <esp_attr.h>
#include <esp_random.h>

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

// ---------------------------------------------------------------------------
//  Schneller Software-Scan (ab 1.3.0.010)
// ---------------------------------------------------------------------------
// Gleiches Ergebnis wie sw_scan_ref, aber ohne doppelte Arbeit:
//  - Hash 1: Runden 0..2 haengen nicht von der Nonce ab -> einmal pro Aufruf
//  - feste Nachrichtenwoerter (Padding, Nullen, Laenge) sind in W16..W30 eingerechnet
//  - Hash 2: nur H7 wird gebraucht -> nach Runde 60 Schluss (Runden 61..63 entfallen)
// Beim Start prueft sw_selfcheck() gegen den Referenzpfad; bei Abweichung bleibt der alte Pfad.
#ifndef SW_FAST_SCAN
#define SW_FAST_SCAN 1     // Master 005 Pre 3: schneller Pfad an, HW-Schleifen per HW_LOOP_PIN festgenagelt
#endif
static bool s_swFast = SW_FAST_SCAN;

#define WX(W, i) (W[i] = SIG1(W[(i) - 2]) + W[(i) - 7] + SIG0(W[(i) - 15]) + W[(i) - 16])

struct SwPre { uint32_t v[8]; uint32_t w16, w17, c18, c19; };

static void sw_prep(const uint32_t mid[8], const uint32_t blk2[3], SwPre& p)
{
    uint32_t W[3] = { blk2[0], blk2[1], blk2[2] };
    uint32_t a = mid[0], b = mid[1], c = mid[2], d = mid[3];
    uint32_t e = mid[4], f = mid[5], g = mid[6], h = mid[7];
    ROUND(a, b, c, d, e, f, g, h, 0);
    ROUND(h, a, b, c, d, e, f, g, 1);
    ROUND(g, h, a, b, c, d, e, f, 2);
    p.v[0] = a; p.v[1] = b; p.v[2] = c; p.v[3] = d;
    p.v[4] = e; p.v[5] = f; p.v[6] = g; p.v[7] = h;
    p.w16 = SIG0(blk2[1]) + blk2[0];
    p.w17 = SIG1(640u) + SIG0(blk2[2]) + blk2[1];
    p.c18 = SIG1(p.w16) + blk2[2];
    p.c19 = SIG1(p.w17) + SIG0(0x80000000u);
}

__attribute__((optimize("O3")))
static inline uint32_t IRAM_ATTR sw_h7(const SwPre& p, const uint32_t mid[8], const uint32_t blk2[3],
                                       uint32_t nonce)
{
    uint32_t W[64];
    W[0] = blk2[0]; W[1] = blk2[1]; W[2] = blk2[2]; W[3] = nonce;
    W[4] = 0x80000000u;
    W[5] = W[6] = W[7] = W[8] = W[9] = W[10] = W[11] = W[12] = W[13] = W[14] = 0;
    W[15] = 640;
    W[16] = p.w16; W[17] = p.w17;
    W[18] = p.c18 + SIG0(nonce);
    W[19] = p.c19 + nonce;
    W[20] = SIG1(W[18]) + 0x80000000u;
    W[21] = SIG1(W[19]);
    W[22] = SIG1(W[20]) + 640;
    for (int i = 23; i < 30; i++) W[i] = SIG1(W[i - 2]) + W[i - 7];
    W[30] = SIG1(W[28]) + W[23] + SIG0(640u);
    #pragma GCC unroll 33
    for (int i = 31; i < 64; i++) WX(W, i);

    uint32_t a = p.v[0], b = p.v[1], c = p.v[2], d = p.v[3];
    uint32_t e = p.v[4], f = p.v[5], g = p.v[6], h = p.v[7];
    ROUND(f, g, h, a, b, c, d, e, 3);
    ROUND(e, f, g, h, a, b, c, d, 4);
    ROUND(d, e, f, g, h, a, b, c, 5);
    ROUND(c, d, e, f, g, h, a, b, 6);
    ROUND(b, c, d, e, f, g, h, a, 7);
    ROUND8(8);  ROUND8(16); ROUND8(24);
    ROUND8(32); ROUND8(40); ROUND8(48); ROUND8(56);

    // Hash 2 ueber den 32-Byte-Digest
    uint32_t X[61];
    X[0] = mid[0] + a; X[1] = mid[1] + b; X[2] = mid[2] + c; X[3] = mid[3] + d;
    X[4] = mid[4] + e; X[5] = mid[5] + f; X[6] = mid[6] + g; X[7] = mid[7] + h;
    X[8] = 0x80000000u;
    X[9] = X[10] = X[11] = X[12] = X[13] = X[14] = 0;
    X[15] = 256;
    X[16] = SIG0(X[1]) + X[0];
    X[17] = SIG1(256u) + SIG0(X[2]) + X[1];
    X[18] = SIG1(X[16]) + SIG0(X[3]) + X[2];
    X[19] = SIG1(X[17]) + SIG0(X[4]) + X[3];
    X[20] = SIG1(X[18]) + SIG0(X[5]) + X[4];
    X[21] = SIG1(X[19]) + SIG0(X[6]) + X[5];
    X[22] = SIG1(X[20]) + 256 + SIG0(X[7]) + X[6];
    X[23] = SIG1(X[21]) + X[16] + SIG0(0x80000000u) + X[7];
    X[24] = SIG1(X[22]) + X[17] + 0x80000000u;
    for (int i = 25; i < 30; i++) X[i] = SIG1(X[i - 2]) + X[i - 7];
    X[30] = SIG1(X[28]) + X[23] + SIG0(256u);
    X[31] = SIG1(X[29]) + X[24] + SIG0(X[16]) + 256;
    #pragma GCC unroll 29
    for (int i = 32; i < 61; i++) WX(X, i);

    #define W X
    a = SHA256_H0[0]; b = SHA256_H0[1]; c = SHA256_H0[2]; d = SHA256_H0[3];
    e = SHA256_H0[4]; f = SHA256_H0[5]; g = SHA256_H0[6]; h = SHA256_H0[7];
    ROUND8(0);  ROUND8(8);  ROUND8(16); ROUND8(24);
    ROUND8(32); ROUND8(40); ROUND8(48);
    ROUND(a, b, c, d, e, f, g, h, 56);
    ROUND(h, a, b, c, d, e, f, g, 57);
    ROUND(g, h, a, b, c, d, e, f, 58);
    ROUND(f, g, h, a, b, c, d, e, 59);
    // Runde 60 nur der Teil, der spaeter zu H7 wird (Ergebnis landet in h)
    #undef W
    return SHA256_H0[7] + h + (d + EP1(a) + CH(a, b, c) + K[60] + X[60]);
}

__attribute__((optimize("O3")))
uint32_t IRAM_ATTR sw_scan(const uint32_t midstate[8], const uint32_t blk2[3],
                           uint32_t nonce, uint32_t count,
                           uint32_t* cands, int maxCands, int* nCands)
{
    if (!s_swFast) return sw_scan_ref(midstate, blk2, nonce, count, cands, maxCands, nCands);
    SwPre p;
    sw_prep(midstate, blk2, p);
    int found = 0;
    for (uint32_t i = 0; i < count; i++, nonce++) {
        if ((sw_h7(p, midstate, blk2, nonce) & 0xFFFF) == 0 && found < maxCands) cands[found++] = nonce;
    }
    *nCands = found;
    return count;
}

// Vergleicht den schnellen Pfad mit dem Referenzpfad (volle H7) fuer n zufaellige Eingaben.
// Gibt die Zahl der Abweichungen zurueck; bei > 0 wird der schnelle Pfad abgeschaltet.
int sw_selfcheck(int n)
{
    int bad = 0;
    for (int t = 0; t < n; t++) {
        uint32_t mid[8], b2[3];
        esp_fill_random(mid, sizeof(mid));
        esp_fill_random(b2, sizeof(b2));
        uint32_t nonce = esp_random();
        SwPre p;
        sw_prep(mid, b2, p);
        uint32_t w1[16] = { b2[0], b2[1], b2[2], nonce, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
        uint32_t s[8], w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
        memcpy(s, mid, 32);
        sha256_transform(s, w1);
        memcpy(w2, s, 32);
        memcpy(s, SHA256_H0, 32);
        sha256_transform(s, w2);
        if (sw_h7(p, mid, b2, nonce) != s[7]) bad++;
    }
    s_swFast = SW_FAST_SCAN && (bad == 0);
    return bad;
}

bool sw_fast_active() { return s_swFast; }

// Referenzpfad (bis 1.3.0.009 der einzige): zwei volle Transformationen je Nonce
__attribute__((optimize("O3")))
uint32_t IRAM_ATTR sw_scan_ref(const uint32_t midstate[8], const uint32_t blk2[3],
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
