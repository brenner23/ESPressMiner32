// Messprogramm fuer die Pipeline von Master 002 (taktgenaue Warteschleifen)
// Bauen/Flashen:  pio run -e pipe_probe -t upload
//
// Sucht fuer Varianten der Pipeline jeweils die kleinsten fehlerfreien Wartezeiten
// und misst Takte/Hash. Referenz: Software-SHA256d ueber dieselben Nonces.
#include <Arduino.h>
#include <esp_random.h>
#include <soc/soc.h>
#include <soc/hwcrypto_reg.h>
#include <sha/sha_parallel_engine.h>
#include <xtensa/core-macros.h>
#include "sha256_sw.h"

static uint32_t hb1[16], hb2[3];

static inline __attribute__((always_inline)) uint32_t* opq(uint32_t* p) { __asm__ __volatile__("" : "+r"(p)); return p; }
static inline __attribute__((always_inline)) uint32_t opv(uint32_t v) { __asm__ __volatile__("" : "+r"(v)); return v; }
#define CTRL(base, off) __asm__ __volatile__("s32i %0, %1, " #off : : "r"(1), "r"(base) : "memory")
#define FENCE() __asm__ __volatile__("memw" : : : "memory")
static inline __attribute__((always_inline)) uint32_t CC() { return XTHAL_GET_CCOUNT(); }
static inline __attribute__((always_inline)) void until(uint32_t s, uint32_t n) { while (CC() - s < n) {} }
static inline __attribute__((always_inline)) void spin(uint32_t n)
{
    __asm__ __volatile__("loopnez %0, 1f\n\t" "nop.n\n" "1:" : : "r"(n) : "memory");
}
static inline __attribute__((always_inline)) uint32_t rd7(uint32_t* base)
{
    uint32_t v;
    __asm__ __volatile__("l32i %0, %1, 28" : "=r"(v) : "r"(base) : "memory");
    return v;
}

// Parameter: d0 d1 d2 l1 l2 tailB2 tailH2
enum { D0, D1, D2, L1, L2, TB, TH, NP };

// Z = 1: b1[7] == b1[8] == 0 ausnutzen (Bitcoin-Vorgaengerhash endet auf Nullbytes)
//        -> T[7] fuer Block 1 und Block 2 nicht schreiben, T[8] nur fuer Hash 2 und zurueck
template <int Z>
static uint32_t IRAM_ATTR __attribute__((optimize("O3")))
pipe(uint32_t nonce, uint32_t count, uint32_t* h7, const uint32_t* p)
{
    uint32_t* P = opq((uint32_t*)SHA_TEXT_BASE);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    const uint32_t d0 = p[D0], d1 = p[D1], d2 = p[D2], l1 = p[L1], l2 = p[L2], tb = p[TB], th = p[TH];
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    for (int i = 0; i < 8; i++) P[i] = hb1[i];
    CTRL(P, 0x90);
    uint32_t tStart = CC();
    const uint32_t c0 = tStart;
    for (uint32_t k = 0; k < count; k++, nonce++) {
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = pad; P[5] = zero; P[6] = zero;
        if (!Z) { P[7] = zero; P[8] = zero; }
        P[9] = zero; P[10] = zero; P[11] = zero;
        P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        until(tStart, d0);
        CTRL(P, 0x94);
        spin(tb);
        P[8] = pad; P[15] = 256;
        spin(d1);
        CTRL(P, 0x98);
        spin(l1);
        CTRL(P, 0x90);
        spin(th);
        P[8] = Z ? zero : hb1[8];
        P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        spin(d2);
        CTRL(P, 0x98);
        spin(l2);
        uint32_t v;
        if (Z == 2) {
            // T[0..6] schreiben, START, erst dann H7 lesen und T[7] nachschieben
            FENCE();
            P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
            P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6];
            CTRL(P, 0x90);
            tStart = CC();
            v = rd7(P);
            P[7] = hb1[7];
        } else if (Z == 3) {
            // H7 lesen ohne memw, T[0..7], START
            v = rd7(P);
            P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
            P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
            CTRL(P, 0x90);
            tStart = CC();
        } else {
            FENCE();
            v = rd7(P);
            P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
            P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6];
            P[7] = Z ? zero : hb1[7];
            CTRL(P, 0x90);
            tStart = CC();
        }
        h7[k] = v;
    }
    return CC() - c0;
}

static const uint32_t N = 20000;
static uint32_t* s_h7;
static uint32_t* s_ref;
static const uint32_t NONCE0 = 777000;

static void make_ref(bool zero78)
{
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    if (zero78) { hb1[7] = 0; hb1[8] = 0; }
    uint32_t mid[8];
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, hb1);
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8];
        w1[3] = __builtin_bswap32(NONCE0 + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        s_ref[i] = s[7];
    }
}

typedef uint32_t (*PipeFn)(uint32_t, uint32_t, uint32_t*, const uint32_t*);

static int run(PipeFn f, const uint32_t* p, uint32_t* cyc)
{
    *cyc = f(NONCE0, N, s_h7, p) / N;
    int err = 0;
    for (uint32_t i = 0; i < N; i++) err += (s_h7[i] != s_ref[i]);
    return err;
}

// Jede Wartezeit einzeln abwaerts, solange <= 2 Fehler; zwei Durchgaenge
static uint32_t optimize(PipeFn f, uint32_t* p)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NP; i++) {
            if ((i == TB || i == TH) && p[i] <= 8) continue;
            uint32_t best = p[i];
            for (int32_t x = (int32_t)p[i] - 1; x >= 0; x--) {
                p[i] = x;
                uint32_t cyc;
                if (run(f, p, &cyc) > 2) break;
                best = x;
            }
            p[i] = best;
        }
    }
    uint32_t cyc;
    run(f, p, &cyc);
    return cyc;
}

static void report(const char* name, PipeFn f, uint32_t tailMin)
{
    uint32_t p[NP] = { 70, 30, 20, 10, 10, 32, 32 };
    (void)tailMin;
    uint32_t cyc = optimize(f, p);
    uint32_t c2;
    int e = run(f, p, &c2);
    Serial.printf("%-34s d %lu/%lu/%lu L %lu/%lu tail %lu/%lu -> %lu Takte = %.1f kH/s (%d Fehler)\n", name,
                  (unsigned long)p[D0], (unsigned long)p[D1], (unsigned long)p[D2], (unsigned long)p[L1],
                  (unsigned long)p[L2], (unsigned long)p[TB], (unsigned long)p[TH], (unsigned long)cyc,
                  240000.0 / cyc, e);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    setCpuFrequencyMhz(240);
    esp_sha_try_lock_engine(SHA1);
    esp_sha_try_lock_engine(SHA2_256);
    s_h7 = (uint32_t*)malloc(N * 4);
    s_ref = (uint32_t*)malloc(N * 4);

    Serial.println("\n=== Pipe-Probe Master 002 (Teil 2) ===");
    make_ref(false);
    report("Standard (alle Writes)", pipe<0>, 0);
    report("Spaet: START vor H7-Lesen", pipe<2>, 0);
    report("Ohne memw vor H7-Lesen", pipe<3>, 0);
    Serial.println("=== Ende ===");
}

void loop() { delay(1000); }
