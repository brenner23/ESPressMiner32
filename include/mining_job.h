#pragma once
#include <stdint.h>
#include "config.h"

struct MiningJob {
    uint32_t generation;      // steigt bei jedem neuen Job
    char     jobId[64];
    char     extranonce2[40]; // Hex
    char     ntime[12];       // Hex, wie vom Pool erhalten
    uint8_t  header[80];      // Blockheader (Nonce = 0)
    uint32_t blk1[16];        // Header-Woerter 0..15 (BE-Werte)
    uint32_t blk2[3];         // Header-Woerter 16..18 (BE-Werte)
    uint32_t midstate[8];     // SHA-256-Zustand nach blk1
    double   networkDiff;
};

struct ShareSubmit {
    char     jobId[64];
    char     extranonce2[40];
    char     ntime[12];
    uint32_t nonce;
    double   difficulty;
};

namespace jobs {
    void     begin();
    void     publish(const MiningJob& job);
    void     invalidate();
    bool     valid();
    uint32_t generation();
    // Kopiert den aktuellen Job, falls vorhanden. false = kein gueltiger Job.
    bool     copy(MiningJob& out);

    void     setDifficulty(double d);
    double   difficulty();

    // Ein Miner hat seinen Nonce-Bereich erschoepft -> neuer extranonce2 noetig
    void     requestNewWork();
    bool     takeNewWorkRequest();

    // Gefundene Shares (Miner -> Stratum-Task)
    bool     pushShare(const ShareSubmit& s);
    bool     popShare(ShareSubmit& s);
}
