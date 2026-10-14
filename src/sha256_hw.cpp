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
#include <string.h>
#include <hal/efuse_hal.h>

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
    if (!esp_sha_try_lock_engine(SHA1))     Serial.println("[HW-SHA] SHA1 engine busy");
    if (!esp_sha_try_lock_engine(SHA2_256)) Serial.println("[HW-SHA] SHA256 engine busy");
    Serial.println("[HW-SHA] accelerator reserved");
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
static uint32_t IRAM_ATTR hw_scan_classic(const uint32_t blk1[16], const uint32_t blk2[3],
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
static uint32_t IRAM_ATTR hw_scan_classic(const uint32_t blk1[16], const uint32_t blk2[3],
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
        uint32_t h7 = hw_pipe_step(T, blk1, blk2, nonce, pad, zero, d0, d1, d2, l1, l2, tStart);
        if ((h7 & mask) == 0 && found < maxCands) cands[found++] = nonce;
    }
    *nCands = found;
    return count;
}
#endif

void hw_sha_set_timing(const HwTiming& t) { s_timing = t; }

HwTiming hw_sha_timing() { return s_timing; }

void hw_sha_set_guard(uint32_t g) { s_guard = g; }

// ---------------------------------------------------------------------------
//  Master 003: Hash-Schleife v2 (src/hw_scan_v2.S, aus NerdMiner Modded, MIT)
// ---------------------------------------------------------------------------

// Layout passend zu den CTX_*-Offsets in hw_scan_v2.S
struct HwV2Ctx {
    uint32_t hdr[19];       // Header-Woerter 0..18 (Big-Endian-Werte)
    uint32_t pad1;
    uint32_t nonce;         // naechstes T[3]-Wort
    uint32_t budget;        // verbleibende Nonces
    uint32_t run;           // 0 = sofort aufhoeren
    uint32_t pad;           // 0x80000000
};
static_assert(offsetof(HwV2Ctx, nonce) == 80 && offsetof(HwV2Ctx, budget) == 84 &&
              offsetof(HwV2Ctx, run) == 88 && offsetof(HwV2Ctx, pad) == 92,
              "Offsets passen nicht zu hw_scan_v2.S");

typedef uint32_t (*HwV2Fn)(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_v2_fast(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_v2_margin(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_v2_wide(volatile uint32_t* sha_base, HwV2Ctx* ctx);
// Master 004: Testvarianten (Messbetrieb, siehe HW_LOOP_TEST)
extern "C" uint32_t hw_t_quiet(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_r3(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_r2(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_r1(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_r2q(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_r1q(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_d_p1(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_d_p2(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_d_pl1(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_d_p3(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_d_pl2(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_d_q4(volatile uint32_t* sha_base, HwV2Ctx* ctx);
extern "C" uint32_t hw_t_n(volatile uint32_t* sha_base, HwV2Ctx* ctx);

static const struct { HwV2Fn fn; const char* name; } s_v2[] = {
    { hw_v2_fast,   "v2 fast (247 cycles)" },
    { hw_v2_margin, "v2 +2 margin (257 cycles)" },
    { hw_v2_wide,   "v2 wide margin (317 cycles)" },
    // ab hier nur im Messbetrieb (nicht Teil der Rueckfallkette)
    { hw_t_quiet,   "B quiet delays" },
    { hw_t_r3,      "C registers, gap 3" },
    { hw_t_r2,      "D registers, gap 2" },
    { hw_t_r1,      "E registers, gap 1" },
    { hw_t_r2q,     "F registers, gap 2, quiet" },
    { hw_t_r1q,     "G registers, gap 1, quiet" },
    { hw_t_d_p1,    "H D + p1 quiet" },
    { hw_t_d_p2,    "I D + p2 quiet" },
    { hw_t_d_pl1,   "J registers + pl1 quiet (241 cycles)" },
    { hw_t_d_p3,    "K D + p3 quiet" },
    { hw_t_d_pl2,   "L D + pl2 quiet" },
    { hw_t_d_q4,    "M D + quiet except pl2" },
    { hw_t_n,       "N D + p2 pl1 p3 quiet" },
};
static const int V2_ALL   = sizeof(s_v2) / sizeof(s_v2[0]);
// Rueckfallkette (Indizes in s_v2). Ab 1.3.0.002: J zuerst (Messbetrieb: 0 Fehler unter Last)
static const int V2_CHAIN[] = { 11, 0, 1, 2 };
static const int V2_COUNT   = sizeof(V2_CHAIN) / sizeof(V2_CHAIN[0]);
static int s_chainPos = 0;
static volatile bool s_testMode = false;   // Messbetrieb: keine automatischen Wechsel
static volatile int s_v2Loop = -1;                  // -1 = klassische Schleife (Master 002)
static volatile int s_v2Switches = 0;
static const int V2_GUARD = 1;                      // Schutzfenster-Stufe (ab 1.3.0.008)
static volatile bool s_window = false;

static uint32_t IRAM_ATTR hw_scan_v2(HwV2Fn fn, const uint32_t blk1[16], const uint32_t blk2[3],
                                     uint32_t nonce, uint32_t count,
                                     uint32_t* cands, int maxCands, int* nCands)
{
    static HwV2Ctx c;      // nur von einem Task gleichzeitig benutzt (HW-Miner bzw. Setup)
    memcpy(c.hdr, blk1, 64);
    c.hdr[16] = blk2[0]; c.hdr[17] = blk2[1]; c.hdr[18] = blk2[2];
    c.nonce = nonce;
    c.budget = count;
    c.run = 1;
    c.pad = 0x80000000u;
    int found = 0;
    while (c.budget > 0 && fn((volatile uint32_t*)SHA_TEXT_BASE, &c)) {
        if (found < maxCands) cands[found++] = c.nonce - 1;
    }
    *nCands = found;
    return count;
}

uint32_t IRAM_ATTR hw_scan(const uint32_t blk1[16], const uint32_t blk2[3],
                           uint32_t nonce, uint32_t count, uint32_t mask,
                           uint32_t* cands, int maxCands, int* nCands)
{
    // v2 prueft fest die unteren 16 Bit; andere Masken (Kalibrierung/Selbsttest
    // der klassischen Schleife) laufen ueber die klassische Schleife.
    const int sel = s_v2Loop;
    if (sel >= 0 && mask == HW_CAND_MASK)
        return hw_scan_v2(s_v2[sel].fn, blk1, blk2, nonce, count, cands, maxCands, nCands);
    return hw_scan_classic(blk1, blk2, nonce, count, mask, cands, maxCands, nCands);
}

// Known-Answer-Test: Header-Wort i = 0x9E3779B9 * (i + 1), Nonces (T[3]-Wort)
// 0x1000 .. 0x1000+262143. Genau diese 6 Kandidaten (in Python nachgerechnet).
// Ein einziger falscher Hash, der zufaellig Kandidat wird oder einen versteckt,
// laesst ihn scheitern.
static bool v2_selftest(HwV2Fn fn)
{
    static const uint32_t expected[] = { 0xa94c, 0xe496, 0x1f476, 0x302f0, 0x35af9, 0x3cf5c };
    const int nExp = sizeof(expected) / sizeof(expected[0]);
    const uint32_t first = 0x1000;
    uint32_t b1[16], b2[3], cands[8];
    for (int i = 0; i < 16; i++) b1[i] = 0x9E3779B9u * (i + 1);
    for (int i = 0; i < 3; i++)  b2[i] = 0x9E3779B9u * (i + 17);
    int nc = 0;
    hw_scan_v2(fn, b1, b2, first, 262144, cands, 8, &nc);
    if (nc != nExp) return false;
    for (int i = 0; i < nExp; i++)
        if (cands[i] - first != expected[i]) return false;
    return true;
}

bool hw_sha_select_loop()
{
    const unsigned rev = efuse_hal_get_major_chip_version();
    if (rev < 3) {
        // Rev. 0-2: DPORT-Fehler (lesen ohne Schutz stoert den anderen Kern)
        Serial.printf("[HW-SHA] chip revision %u: v2 loop needs rev. 3 -> classic loop\n", rev);
        s_v2Loop = -1;
        return false;
    }
    for (int n = 0; n < V2_COUNT; n++) {
        const int v = V2_CHAIN[n];
        // Bester aus drei: ein einzelner Ausreisser soll die schnelle Stufe nicht kosten
        for (int tries = 0; tries < 3; tries++) {
            uint32_t t0 = millis();
            bool ok = v2_selftest(s_v2[v].fn);
            Serial.printf("  Loop %-30s known-answer test %s (%lu ms)\n", s_v2[v].name,
                          ok ? "OK" : "FAILED", (unsigned long)(millis() - t0));
            if (ok) {
                s_chainPos = n;
                s_v2Loop = v;
                return true;
            }
        }
    }
    s_v2Loop = -1;
    return false;
}

// Jeder Kandidat wird in Software nachgerechnet. Werden davon zu viele nicht
// bestaetigt, ist die Stufe auf diesem Chip zu knapp -> naechste, vorsichtigere.
void hw_sha_report_candidate(bool confirmed)
{
    // Ab 1.3.0.005 zwei Fenster: kurze Fehlerschuebe (Nachtlauf .004: alle ~10 min 4-8 Stueck,
    // im Schnitt 0,06 %) sind harmlos und duerfen nicht auf eine langsamere Stufe werfen.
    //  - kurz: 256 Kandidaten (~35 s), wechseln erst ab 64 falschen (25 %) -> Variante wirklich kaputt
    //  - lang: 4096 Kandidaten (~9 min), wechseln ab 41 falschen (> 1 %)
    static uint32_t hitsS = 0, badS = 0, hitsL = 0, badL = 0;
    if (s_v2Loop < 0 || s_testMode) return;
    hitsS++; hitsL++;
    if (!confirmed) { badS++; badL++; }

    uint32_t bad = 0, hits = 0;
    if (hitsS >= 256) {
        if (badS >= 64) { bad = badS; hits = hitsS; }
        hitsS = badS = 0;
    }
    if (!bad && hitsL >= 4096) {
        if (badL >= 41) { bad = badL; hits = hitsL; }
        hitsL = badL = 0;
    }
    if (bad && s_chainPos + 1 < V2_COUNT) {
        s_chainPos++;
        if (!s_window) s_v2Loop = V2_CHAIN[s_chainPos];
        s_v2Switches++;
        hitsS = badS = hitsL = badL = 0;
        Serial.printf("[HW-SHA] %lu of %lu candidates wrong -> switching to %s\n",
                      (unsigned long)bad, (unsigned long)hits, s_v2[V2_CHAIN[s_chainPos]].name);
    }
}

// Ab 1.3.0.008: Schutzfenster. Waehrend der erwarteten Stoerung (siehe miner.cpp, guard)
// rechnet der HW-Miner kurz mit der vorsichtigeren Stufe "v2 +2 margin" (257 Takte).

void hw_sha_guard(bool on)
{
    if (on == s_window || s_testMode) return;
    s_window = on;
    if (on) {
        if (s_v2Loop >= 0 && s_v2Loop != V2_GUARD) s_v2Loop = V2_GUARD;
    } else if (s_v2Loop >= 0) {
        s_v2Loop = V2_CHAIN[s_chainPos];
    }
}

bool hw_sha_guard_active() { return s_window; }

const char* hw_sha_loop_name()
{
    return s_v2Loop >= 0 ? s_v2[s_v2Loop].name : "classic (calibrated)";
}

// ---------------------------------------------------------------------------
//  Master 004: Messbetrieb
// ---------------------------------------------------------------------------
int hw_sha_variant_count() { return V2_ALL; }

const char* hw_sha_variant_name(int i) { return (i >= 0 && i < V2_ALL) ? s_v2[i].name : "?"; }

bool hw_sha_variant_selftest(int i)
{
    return i >= 0 && i < V2_ALL && v2_selftest(s_v2[i].fn);
}

// Ueberlebt einen Neustart (nicht beim Einschalten): zuletzt aktive Variante
static RTC_NOINIT_ATTR uint32_t s_rtcMagic;
static RTC_NOINIT_ATTR int32_t  s_rtcVariant;

void hw_sha_set_variant(int i)
{
    s_testMode = true;
    if (i >= 0 && i < V2_ALL) s_v2Loop = i;
    s_rtcMagic = 0x4C4F4F50;     // "LOOP"
    s_rtcVariant = i;
}

// Variante, die vor dem letzten Neustart lief (-1 = keine / Kaltstart)
int hw_sha_variant_before_reset()
{
    int v = (s_rtcMagic == 0x4C4F4F50) ? s_rtcVariant : -1;
    s_rtcMagic = 0;
    return v;
}

// Takte pro Nonce im reinen Durchlauf (ohne Betriebssystem-Last der Mining-Schleife)
float hw_sha_variant_cycles(int i)
{
    uint32_t b1[16], b2[3], cands[8];
    for (int k = 0; k < 16; k++) b1[k] = 0x01234567u * (k + 3);
    for (int k = 0; k < 3; k++)  b2[k] = 0x89abcdefu * (k + 5);
    int nc;
    const uint32_t n = 200000;
    uint32_t t0 = XTHAL_GET_CCOUNT();
    hw_scan_v2(s_v2[i].fn, b1, b2, 0, n, cands, 8, &nc);
    return (float)(XTHAL_GET_CCOUNT() - t0) / n;
}
