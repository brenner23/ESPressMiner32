#pragma once
#include <stdint.h>
#include <stddef.h>

// Software-SHA-256 (portabel, fuer Coinbase/Merkle, Verifikation und den Core-0-Miner)

extern const uint32_t SHA256_H0[8];

// Eine 512-Bit-Kompression. `w` enthaelt 16 Message-Woerter (Big-Endian-Werte).
void sha256_transform(uint32_t state[8], const uint32_t w[16]);

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void sha256d(const uint8_t* data, size_t len, uint8_t out[32]);

// Scannt `count` Nonces ab `nonce` mit vorberechnetem Midstate.
// Kandidaten (obere 16 Bit des Hashes = 0) landen in `cands`.
uint32_t sw_scan(const uint32_t midstate[8], const uint32_t blk2[3],
                 uint32_t nonce, uint32_t count,
                 uint32_t* cands, int maxCands, int* nCands);

// Referenzpfad (zwei volle Transformationen) und Pruefung des schnellen Pfads (ab 1.3.0.010)
uint32_t sw_scan_ref(const uint32_t midstate[8], const uint32_t blk2[3],
                     uint32_t nonce, uint32_t count,
                     uint32_t* cands, int maxCands, int* nCands);
int  sw_selfcheck(int n);      // Abweichungen schnell vs. Referenz; > 0 schaltet den schnellen Pfad ab
bool sw_fast_active();
