// ESPressMiner32 Master 003 - Hash-Schleife "v2" fuer den SHA-Beschleuniger
//
// Uebernommen (MIT-Lizenz) aus NerdMiner Modded von HenrysCat,
// src/pipelined_hw_sha_classic_v2.S  (Copyright (c) 2026 HenrysCat,
// Fork von BitMaker-hub/NerdMiner_v2, Copyright (c) 2023 Bitmaker).
// Schleife und gemessene Wartezeiten unveraendert, nur Namen/Kommentare angepasst.
//
// Messungen von dort (240-MHz-Takte):
//   - START/CONTINUE halten die Engine ~68 Takte beschaeftigt (eine Runde pro Takt)
//   - die Engine liest T[i] etwa i+1 Takte nach dem Steuerbefehl und danach nie
//     wieder -> T ist ab ~Takt 17 frei
//   - LOAD schreibt nur T[0..7], ein Wort pro Takt; CPU-Writes waehrenddessen
//     beschaedigen die Uebertragung
// Bis auf das Zurueckschreiben von T[0..7] nach dem letzten LOAD liegt alles
// in Wartezeiten der Engine:
//   W1 (Block 1 rechnet): Kandidat/Budget pruefen, Block 2 komplett schreiben
//   W2 (Block 2 rechnet): Padding fuer Hash 2 in T[8], T[15]
//   W3 (Hash 2 rechnet):  T[8..15] fuer Block 1 der naechsten Nonce
//
// Zwei Regeln (dort an echten Fehl-Hashes gelernt):
// 1. Writes auf SHA-Register mindestens 3 Takte auseinander. Dichter gehen
//    Writes verloren, sobald der andere Kern gleichzeitig auf den DPORT-Bus
//    zugreift (30-60 % falsche Hashes bei 2 Takten Abstand, keine bei 3).
//    -> Das ist auch der Grund, warum bei uns der Software-Miner auf Core 0
//       den HW-Miner gebremst bzw. gestoert hat.
// 2. Kein BUSY-Polling, sondern gezaehlte Mindest-Wartezeiten. Interrupts und
//    Busverkehr bremsen nur die CPU, nie die Engine.
//
// Nonce: ctx->nonce wird UNVERAENDERT nach T[3] geschrieben (Big-Endian-Wort
// der Header-Bytes 76..79) und pro Hash um 1 erhoeht. Echte Nonce = bswap32.
//
// uint32_t fn(volatile uint32_t* sha_text_base, HwV2Ctx* ctx)
//   1 = Kandidat (untere 16 Bit von H7 == 0) bei ctx->nonce - 1,
//   0 = Budget aufgebraucht oder ctx->run == 0.
//   Engine muss beim Aufruf frei sein und ist es danach wieder.
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32)

#define CTX_HDR16   64
#define CTX_NONCE   80
#define CTX_BUDGET  84
#define CTX_RUN     88
#define CTX_PAD     92

#define R_START 0x90
#define R_CONT  0x94
#define R_LOAD  0x98

    .macro NOPS n
    .rept \n
    nop.n
    .endr
    .endm

    // mindestens 84 Takte: laenger als jede Operation der Engine
    .macro ENGINE_SETTLE
    movi.n  a7, 28
1:  addi.n  a7, a7, -1
    bnez    a7, 1b
    .endm

    // T[\dst/4] = ctx-Wort bei \src, mit Abstand zum naechsten Write
    .macro COPY src, dst, wgap
    l32i    a7, a3, \src
    s32i.n  a7, a2, \dst
    NOPS    \wgap
    .endm

    // T[\dst/4] = 0, mit Abstand zum naechsten Write
    .macro ZERO dst, wgap
    s32i.n  a8, a2, \dst
    nop.n
    NOPS    \wgap
    .endm

    // T[\k] = Header-Wort \k; H7 wird nach \lpos dieser Writes gelesen
    // (muss vor dem Ueberschreiben von T[7] passieren)
    .macro HDRW k, lpos, wgap
    COPY    \k * 4, \k * 4, \wgap
    .if \lpos == \k + 1
    l16ui   a6, a2, 28          // untere 16 Bit von H7: 0 = Kandidat
    .endif
    .endm

// Stellschrauben (Takte nop):
//   p1  nach Block-2-Fuellung, vor CONTINUE
//   d2  nach CONTINUE, bevor T[8]/T[15] fuer Hash 2 ueberschrieben werden
//   p2  Rest von Block 2, vor LOAD
//   pl1 LOAD -> START Hash 2
//   d3  nach START Hash 2, bevor T[8..15] fuer Block 1 ueberschrieben werden
//   p3  Rest von Hash 2, vor LOAD
//   pl2 LOAD -> erster Header-Write
//   lpos, wgap siehe oben
// Register: a2 T-Basis, a3 ctx, a4 Nonce, a5 Budget, a6 Steuerwert / H7,
//           a7 Zwischenwert, a8 Null
    .macro PL_MINE name, sect, p1, d2, p2, pl1, d3, p3, pl2, lpos, wgap
    .section \sect, "ax"
    .global \name
    .type   \name, @function
    .balign 4
\name:
    entry   a1, 32
    l32i    a4, a3, CTX_NONCE
    l32i    a5, a3, CTX_BUDGET
    movi.n  a8, 0
    ENGINE_SETTLE
    // Kaltstart: T[0..15] = Header-Woerter 0..15
    COPY    0, 0, \wgap
    COPY    4, 4, \wgap
    COPY    8, 8, \wgap
    COPY    12, 12, \wgap
    COPY    16, 16, \wgap
    COPY    20, 20, \wgap
    COPY    24, 24, \wgap
    COPY    28, 28, \wgap
    COPY    32, 32, \wgap
    COPY    36, 36, \wgap
    COPY    40, 40, \wgap
    COPY    44, 44, \wgap
    COPY    48, 48, \wgap
    COPY    52, 52, \wgap
    COPY    56, 56, \wgap
    COPY    60, 60, \wgap
    movi.n  a6, 1               // "vorige Nonce war kein Kandidat"
    s32i    a6, a2, R_START     // START Block 1 der ersten Nonce
    j       .Lhead_\name

    .balign 4
.Lw2b_\name:
    // ===== W2, zweite Haelfte: Block 2 abwarten =====
    NOPS    \p2
    s32i    a6, a2, R_LOAD      // ===== LOAD Digest 1 -> T[0..7] =====
    NOPS    \pl1
    s32i    a6, a2, R_START     // ===== START Hash 2 =====

    // ===== W3: T[8..15] fuer Block 1, sobald die Engine Hash 2 gelesen hat =====
    NOPS    \d3
    COPY    32, 32, \wgap
    COPY    36, 36, \wgap
    COPY    40, 40, \wgap
    COPY    44, 44, \wgap
    COPY    48, 48, \wgap
    COPY    52, 52, \wgap
    COPY    56, 56, \wgap
    COPY    60, 60, \wgap
    NOPS    \p3
    s32i    a6, a2, R_LOAD      // ===== LOAD Digest 2 -> T[0..7] =====
    NOPS    \pl2

    // Einzige Arbeit bei freier Engine: T[0..7] = Header-Woerter 0..7
    HDRW    0, \lpos, \wgap
    HDRW    1, \lpos, \wgap
    HDRW    2, \lpos, \wgap
    HDRW    3, \lpos, \wgap
    HDRW    4, \lpos, \wgap
    HDRW    5, \lpos, \wgap
    HDRW    6, \lpos, \wgap
    HDRW    7, \lpos, \wgap
    movi.n  a7, 1
    s32i    a7, a2, R_START     // ===== START Block 1 (naechste Nonce) =====

.Lhead_\name:
    // ===== W1: Entscheidung ueber die gerade fertige Nonce ... =====
    NOPS    5                   // gleiches Timing wie der gemessene Bench-Build
    beqz    a6, .Lhit_\name
    beqz    a5, .Lstop_\name
    l32i    a7, a3, CTX_RUN
    beqz    a7, .Lstop_\name
    addi.n  a5, a5, -1
    movi.n  a6, 1

    // ===== ... und Block 2 hinter dem Lesezeiger der Engine schreiben =====
    COPY    CTX_HDR16, 0, \wgap
    COPY    CTX_HDR16 + 4, 4, \wgap
    COPY    CTX_HDR16 + 8, 8, \wgap
    l32i    a7, a3, CTX_PAD
    s32i.n  a4, a2, 12          // Nonce
    addi.n  a4, a4, 1
    NOPS    \wgap
    s32i.n  a7, a2, 16          // 0x80000000
    nop.n
    NOPS    \wgap
    ZERO    20, \wgap
    ZERO    24, \wgap
    ZERO    28, \wgap
    ZERO    32, \wgap
    ZERO    36, \wgap
    ZERO    40, \wgap
    ZERO    44, \wgap
    ZERO    48, \wgap
    ZERO    52, \wgap
    ZERO    56, \wgap
    movi    a7, 640
    s32i.n  a7, a2, 60          // Nachrichtenlaenge erster SHA
    NOPS    \p1
    s32i    a6, a2, R_CONT      // ===== CONTINUE Block 2 =====

    // ===== W2, erste Haelfte: Padding fuer Hash 2, sobald Block 2 gelesen ist =====
    NOPS    \d2
    COPY    CTX_PAD, 32, \wgap  // T[8] = 0x80000000 (T[9..14] bleiben 0)
    movi    a7, 256
    s32i.n  a7, a2, 60          // T[15] = 256
    j       .Lw2b_\name

.Lhit_\name:
    movi.n  a6, 1
    j       .Lout_\name
.Lstop_\name:
    movi.n  a6, 0
.Lout_\name:
    // Der gerade gestartete Block 1 wird verworfen; abwarten, damit die
    // Engine fuer den naechsten Nutzer frei ist.
    ENGINE_SETTLE
    s32i    a4, a3, CTX_NONCE
    s32i    a5, a3, CTX_BUDGET
    mov.n   a2, a6
    retw.n
    .size   \name, . - \name
    .endm

    // Gemessene Grenzen (ruhig / mit Busverkehr auf Core 0):
    //   p1 3/4, p2 39/40, pl1 9/10, p3 26/27, pl2 9/10, d2 12, d3 8.
    // 247 Takte pro Nonce.
    PL_MINE hw_v2_fast,   .iram1.hwv2f,  4, 16, 40, 10, 14, 27, 10, 2, 1
    // +2 bzw. +14 Takte auf jede Wartezeit (257 / 317 Takte): Rueckfall, wenn der
    // Selbsttest scheitert oder die Software Kandidaten nicht bestaetigt.
    PL_MINE hw_v2_margin, .iram1.hwv2m,  6, 16, 42, 12, 14, 29, 12, 2, 1
    PL_MINE hw_v2_wide,   .iram1.hwv2w, 18, 16, 54, 24, 14, 41, 24, 2, 1

#endif
