#include "sha256_hw.h"
#include "config.h"
#include "utils.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <soc/soc.h>
#include <soc/hwcrypto_reg.h>
#include <sha/sha_parallel_engine.h>
#include <xtensa/core-macros.h>
#include <stddef.h>

// Register-Offsets relativ zu SHA_TEXT_BASE (0x3FF03000):
//   0x00..0x3C Textpuffer T[0..15], 0x90 START, 0x94 CONTINUE, 0x98 LOAD, 0x9C BUSY
//
// Zugriffstechnik (Idee gepufferter Writes aus SparkMiner, MIT-Lizenz:
// github.com/BitzyLabs/sparkminer):
//  - Textregister ueber einen normalen Zeiger beschreiben: kein `memw` pro Zugriff.
//  - Basisadresse in einem Register halten ("opak" fuer den Compiler), damit er mit
//    `s32i base, offset` schreibt statt jede Adresse einzeln per `l32r` zu laden.
//  - Steuerbefehle ohne `memw`: Der Schreibpuffer haelt die Reihenfolge ein. Die
//    Wartezeiten laufen ab Absenden und enthalten damit die Laufzeit zur Engine.
//  - Kein BUSY-Polling: Das Flag faellt erst ~30 Takte nach dem tatsaechlichen Ende.
//    Stattdessen feste Wartezeiten je Phase, beim Start kalibriert (selftest.cpp)
//    Seltene Einzelfehler sind harmlos (Software-Pruefung faengt sie ab).
//  - Master 002: Gewartet wird mit einer taktgenauen Zero-Overhead-Schleife
//    (`loopnez` + `nop.n`) statt mit CCOUNT-Abfragen. Das spart pro Wartestelle das
//    Lesen der Taktuhr und den Ueberschuss der Abfrageschleife (~5 Takte Raster).
//    Die Wartezeiten zaehlen damit ab dem Ende der Befehle davor, nicht mehr ab
//    dem Steuerbefehl; die Kalibrierung misst genau diese Werte.
//    Ausnahme Block 1: Davor laeuft je nach Aufrufer unterschiedlicher Code
//    (Kandidatenpruefung, Schleifenende), daher zaehlt er weiter ab START per CCOUNT.

// Die Engine liest die Eingabewoerter nacheinander in den ersten Runden ein.
// Bevor hohe Wortindizes (T[8..15]) ueberschrieben werden, muss sie diese
// bereits gelesen haben (16 Takte ergaben im Test sporadisch Fehler).
#ifndef HW_TAIL_DELAY
#define HW_TAIL_DELAY 32
#endif

// Auch T[0..7] liest die Engine erst nach START nach und nach ein. Folgt der
// erste Block-2-Write direkt auf START, ueberholt er sie (gemessen: falsche Hashes).
// In der Mining-Schleife liegt die Kandidatenpruefung dazwischen; ueberall sonst
// (Pipeline-Start, Einzel-Hash) wird so lange gewartet.
#ifndef HW_START_GUARD
#define HW_START_GUARD 8
#endif

static HwTiming s_timing = HW_TIMING_SAFE;
static uint32_t s_guard = HW_ASM_LOOP_GUARD;

static inline __attribute__((always_inline)) uint32_t* sha_base()
{
    uint32_t* p = (uint32_t*)SHA_TEXT_BASE;
    __asm__ __volatile__("" : "+r"(p));
    return p;
}

static inline __attribute__((always_inline)) uint32_t opaque(uint32_t v)
{
    __asm__ __volatile__("" : "+r"(v));
    return v;
}

#define SHA_CTRL(base, off) \
    __asm__ __volatile__("s32i %0, %1, " #off : : "r"(1), "r"(base) : "memory")
#define SHA_FENCE() __asm__ __volatile__("memw" : : : "memory")

static inline __attribute__((always_inline)) uint32_t sha_rd(uint32_t* base, int wordIdx)
{
    return *(volatile uint32_t*)(base + wordIdx);
}

// Wartet n Takte (n = 0 erlaubt). Der Schleifenkoerper laeuft ohne Sprungkosten.
// Ein Interrupt verlaengert die Wartezeit nur - fuer die Engine immer unkritisch.
static inline __attribute__((always_inline)) uint32_t now() { return XTHAL_GET_CCOUNT(); }

static inline __attribute__((always_inline)) void wait_until(uint32_t start, uint32_t n)
{
    while (XTHAL_GET_CCOUNT() - start < n) {
    }
}

static inline __attribute__((always_inline)) void spin(uint32_t n)
{
    __asm__ __volatile__("loopnez %0, 1f\n\t"
                         "nop.n\n"
                         "1:"
                         : : "r"(n) : "memory");
}

void hw_sha_begin()
{
    // SHA-1 und SHA-256 sperren, damit mbedTLS (WPA2) den Textpuffer nie
    // anfasst und stattdessen Software-SHA nutzt.
    if (!esp_sha_try_lock_engine(SHA1))     Serial.println("[HW-SHA] SHA1-Engine belegt");
    if (!esp_sha_try_lock_engine(SHA2_256)) Serial.println("[HW-SHA] SHA256-Engine belegt");
    Serial.println("[HW-SHA] Beschleuniger reserviert");
}

// Vorbedingung/Nachbedingung jeder Nonce: T[8..15] enthaelt b1[8..15].
static inline __attribute__((always_inline)) void load_block1_tail(uint32_t* T, const uint32_t* b1)
{
    T[8]  = b1[8];  T[9]  = b1[9];  T[10] = b1[10]; T[11] = b1[11];
    T[12] = b1[12]; T[13] = b1[13]; T[14] = b1[14]; T[15] = b1[15];
}

// Double-SHA256 fuer eine Nonce; H0..H7 stehen danach in T[0..7].
// Die Engine uebernimmt den Textpuffer beim START/CONTINUE, daher werden die Daten
// fuer den naechsten Schritt geschrieben, waehrend der aktuelle Block noch rechnet.
static inline __attribute__((always_inline)) void hw_double_sha(uint32_t* T, const uint32_t* b1, const uint32_t* b2,
                                                                uint32_t nonceBE, uint32_t pad, uint32_t zero,
                                                                const HwTiming& tm)
{
    // Block 1 (Header-Bytes 0..63), T[8..15] ist schon gesetzt
    T[0] = b1[0]; T[1] = b1[1]; T[2] = b1[2]; T[3] = b1[3];
    T[4] = b1[4]; T[5] = b1[5]; T[6] = b1[6]; T[7] = b1[7];
    SHA_CTRL(T, 0x90);
    uint32_t t = now();
    spin(HW_START_GUARD);

    // ... waehrenddessen Block 2 (Header-Bytes 64..79 + Padding, 640 Bit)
    T[0]  = b2[0]; T[1] = b2[1]; T[2] = b2[2]; T[3] = nonceBE;
    T[4]  = pad;  T[5] = zero; T[6] = zero; T[7] = zero;
    T[8]  = zero; T[9] = zero; T[10] = zero; T[11] = zero;
    T[12] = zero; T[13] = zero; T[14] = zero; T[15] = 640;
    wait_until(t, tm.block[0]);
    SHA_CTRL(T, 0x94);

    // ... waehrenddessen Padding fuer den zweiten Hash (256 Bit); T[9..14] sind bereits 0
    spin(HW_TAIL_DELAY);
    T[8] = pad; T[15] = 256;
    spin(tm.block[1]);
    SHA_CTRL(T, 0x98);      // Digest -> T[0..7]
    spin(tm.load[0]);
    SHA_CTRL(T, 0x90);

    // ... waehrenddessen Block 1 der naechsten Nonce vorbereiten
    spin(HW_TAIL_DELAY);
    load_block1_tail(T, b1);
    spin(tm.block[2]);
    SHA_CTRL(T, 0x98);
    spin(tm.load[1]);
    SHA_FENCE();            // LOAD muss angekommen sein, bevor T[7] gelesen wird
}

void hw_sha256d_header(const uint8_t header[80], uint8_t out[32])
{
    uint32_t b1[16], b2[3];
    for (int i = 0; i < 16; i++) b1[i] = read_be32(header + 4 * i);
    for (int i = 0; i < 3; i++)  b2[i] = read_be32(header + 64 + 4 * i);
    uint32_t nonceBE = read_be32(header + 76);

    uint32_t* T = sha_base();
    // Nur fuer den Selbsttest (Rechenlogik): Die kalibrierten Werte gelten fuer die
    // Mining-Schleife, nicht fuer diesen anders aufgebauten Pfad -> sichere Werte.
    const HwTiming tm = HW_TIMING_SAFE;
    load_block1_tail(T, b1);
    hw_double_sha(T, b1, b2, nonceBE, opaque(0x80000000), opaque(0), tm);

    for (int i = 0; i < 8; i++) {
        uint32_t v = sha_rd(T, i);
        out[4 * i + 0] = v >> 24;
        out[4 * i + 1] = v >> 16;
        out[4 * i + 2] = v >> 8;
        out[4 * i + 3] = v;
    }
}

// Ein Pipeline-Schritt: Block 1 laeuft bereits. Rechnet Block 2, Hash 1 und
// Hash 2, liest H7 und startet SOFORT Block 1 der naechsten Nonce, bevor der
// Aufrufer die Kandidatenpruefung macht (die dann mit-rechnet).
static inline __attribute__((always_inline))
uint32_t hw_pipe_step(uint32_t* T, const uint32_t* b1, const uint32_t* b2, uint32_t nonceBE,
                      uint32_t pad, uint32_t zero, uint32_t d0, uint32_t d1, uint32_t d2,
                      uint32_t l1, uint32_t l2,
                      uint32_t& tStart)
{
    // Block 2 schreiben, waehrend Block 1 rechnet
    T[0]  = b2[0]; T[1] = b2[1]; T[2] = b2[2]; T[3] = nonceBE;
    T[4]  = pad;  T[5] = zero; T[6] = zero; T[7] = zero;
    T[8]  = zero; T[9] = zero; T[10] = zero; T[11] = zero;
    T[12] = zero; T[13] = zero; T[14] = zero; T[15] = 640;
    wait_until(tStart, d0);
    SHA_CTRL(T, 0x94);

    spin(HW_TAIL_DELAY);
    T[8] = pad; T[15] = 256;
    spin(d1);
    SHA_CTRL(T, 0x98);          // Hash-1-Digest -> T[0..7]
    spin(l1);
    SHA_CTRL(T, 0x90);          // Hash 2 starten

    spin(HW_TAIL_DELAY);
    load_block1_tail(T, b1);    // T[8..15] fuer die naechste Nonce wiederherstellen
    spin(d2);
    SHA_CTRL(T, 0x98);          // Hash-2-Ergebnis -> T[0..7]
    spin(l2);
    SHA_FENCE();
    uint32_t h7 = sha_rd(T, 7);

    // SOFORT Block 1 der naechsten Nonce starten (T[8..15] steht bereits)
    T[0] = b1[0]; T[1] = b1[1]; T[2] = b1[2]; T[3] = b1[3];
    T[4] = b1[4]; T[5] = b1[5]; T[6] = b1[6]; T[7] = b1[7];
    SHA_CTRL(T, 0x90);
    tStart = now();
    return h7;
}

// Startet Block 1 der ersten Nonce (Prolog der Pipeline).
static inline __attribute__((always_inline))
uint32_t hw_pipe_prime(uint32_t* T, const uint32_t* b1)
{
    load_block1_tail(T, b1);
    T[0] = b1[0]; T[1] = b1[1]; T[2] = b1[2]; T[3] = b1[3];
    T[4] = b1[4]; T[5] = b1[5]; T[6] = b1[6]; T[7] = b1[7];
    SHA_CTRL(T, 0x90);
    uint32_t t = now();
    spin(HW_START_GUARD);
    return t;
}

// Argumente fuer die Assembler-Schleife (src/hw_scan_asm.S, Offsets dort)
struct HwScanArgs {
    const uint32_t* b1;
    const uint32_t* b2;
    uint32_t nonce;
    uint32_t count;
    uint32_t mask;
    uint32_t* cands;
    int32_t  maxCands;
    int32_t  found;         // Ausgabe
    uint32_t d0, d1, d2, l1, l2;
    uint32_t guard;         // Takte zwischen START und erstem Block-2-Write (in der Schleife)
};
static_assert(offsetof(HwScanArgs, b1) == 0 && offsetof(HwScanArgs, b2) == 4 &&
              offsetof(HwScanArgs, nonce) == 8 && offsetof(HwScanArgs, count) == 12 &&
              offsetof(HwScanArgs, mask) == 16 && offsetof(HwScanArgs, cands) == 20 &&
              offsetof(HwScanArgs, maxCands) == 24 && offsetof(HwScanArgs, found) == 28 &&
              offsetof(HwScanArgs, d0) == 32 && offsetof(HwScanArgs, l2) == 48 &&
              offsetof(HwScanArgs, guard) == 52, "Offsets passen nicht zu hw_scan_asm.S");

extern "C" uint32_t hw_scan_asm(HwScanArgs* a);

#if HW_USE_ASM
uint32_t IRAM_ATTR hw_scan(const uint32_t blk1[16], const uint32_t blk2[3],
                           uint32_t nonce, uint32_t count, uint32_t mask,
                           uint32_t* cands, int maxCands, int* nCands)
{
    HwScanArgs a = { blk1, blk2, nonce, count, mask, cands, maxCands, 0,
                     s_timing.block[0], s_timing.block[1], s_timing.block[2],
                     s_timing.load[0], s_timing.load[1], s_guard };
    uint32_t done = hw_scan_asm(&a);
    *nCands = a.found;
    return done;
}
#else
__attribute__((optimize("O3")))
uint32_t IRAM_ATTR hw_scan(const uint32_t blk1[16], const uint32_t blk2[3],
                           uint32_t nonce, uint32_t count, uint32_t mask,
                           uint32_t* cands, int maxCands, int* nCands)
{
    uint32_t* T = sha_base();
    const uint32_t pad = opaque(0x80000000), zero = opaque(0);
    const uint32_t d0 = s_timing.block[0], d1 = s_timing.block[1];
    const uint32_t d2 = s_timing.block[2];
    const uint32_t l1 = s_timing.load[0], l2 = s_timing.load[1];
    int found = 0;

    uint32_t tStart = hw_pipe_prime(T, blk1);
    for (uint32_t i = 0; i < count; i++, nonce++) {
        uint32_t h7 = hw_pipe_step(T, blk1, blk2, __builtin_bswap32(nonce), pad, zero, d0, d1, d2, l1, l2, tStart);
        if ((h7 & mask) == 0 && found < maxCands) cands[found++] = nonce;
    }
    *nCands = found;
    return count;
}
#endif

void hw_sha_set_timing(const HwTiming& t) { s_timing = t; }

HwTiming hw_sha_timing() { return s_timing; }

void hw_sha_set_guard(uint32_t g) { s_guard = g; }
