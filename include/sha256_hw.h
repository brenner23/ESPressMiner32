#pragma once
#include <stdint.h>

// Direkter Zugriff auf den SHA-256-Hardwarebeschleuniger des ESP32.
// Der Beschleuniger wird fuer die gesamte Laufzeit exklusiv reserviert;
// mbedTLS weicht dann automatisch auf Software-SHA aus.

void hw_sha_begin();

// Double-SHA256 eines 80-Byte-Headers ueber die Hardware (fuer Selbsttest).
void hw_sha256d_header(const uint8_t header[80], uint8_t out[32]);

// Scannt `count` Nonces ab `nonce`.
// blk1 = die ersten 16 Header-Woerter, blk2 = Header-Woerter 16..18 (Big-Endian-Werte).
// Kandidat ist jede Nonce mit (H7 & mask) == 0. Mining: HW_CAND_MASK. Selbsttest und
// Kalibrierung nutzen eine kleine Maske und pruefen so genau den Mining-Code.
#define HW_CAND_MASK 0xFFFFu
uint32_t hw_scan(const uint32_t blk1[16], const uint32_t blk2[3],
                 uint32_t nonce, uint32_t count, uint32_t mask,
                 uint32_t* cands, int maxCands, int* nCands);

// Feste Wartezeiten der Pipeline in CPU-Takten. block[0] zaehlt ab START (CCOUNT),
// die uebrigen sind taktgenaue Warteschleifen zusaetzlich zu den Befehlen der Phase:
//   block[0] = Block 1 (START), block[1] = Block 2 (CONTINUE), block[2] = Hash 2 (START)
//   load[0] = nach LOAD von Hash 1 (vor START Hash 2)
//   load[1] = nach LOAD von Hash 2 (vor dem Lesen von H7) - braucht laenger
struct HwTiming {
    uint32_t block[3];
    uint32_t load[2];
};

// Sicher fuer jeden Chip (Rueckfall, falls die Kalibrierung scheitert)
#define HW_TIMING_SAFE HwTiming{ { 120, 120, 120 }, { 40, 40 } }

void     hw_sha_set_timing(const HwTiming& t);
HwTiming hw_sha_timing();
void     hw_sha_set_guard(uint32_t g);   // nur Assembler-Schleife
