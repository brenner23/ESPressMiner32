#pragma once
#include <stdint.h>

// Anzahl Hashrate-Messpunkte fuer den Verlauf (x STATS_INTERVAL_MS = 10 min)
#define STATS_HISTORY_LEN 120

struct MinerStats {
    // Zaehler laufen ueber (uint32), Hashrate wird aus Differenzen berechnet
    volatile uint32_t hwHashes;
    volatile uint32_t swHashes;
    volatile uint32_t candidates;
    volatile uint32_t hwErrors;       // Kandidat hielt der SW-Pruefung nicht stand
    volatile uint32_t hwCandidates;   // davon aus der Hardware (Messbetrieb)
    volatile uint32_t sharesFound;
    volatile uint32_t submitted;
    volatile uint32_t accepted;
    volatile uint32_t rejected;
    volatile uint32_t blocksFound;
    volatile uint32_t jobsReceived;
    volatile bool     poolConnected;
    volatile bool     poolAuthorized;
    volatile int      activePool;       // 0 = Primary, 1 = Secondary
    volatile double   bestDiff;
    volatile double   networkDiff;
    volatile float    chipTemp;         // Grad C, grob (interner Sensor), 0 = noch nicht gelesen

    // Vom Statistik-Task berechnet (fuer die Webseite)
    volatile float    khCurrent;
    volatile float    khAverage;
    volatile uint32_t uptimeS;
    volatile uint64_t totalHashes;
    float             history[STATS_HISTORY_LEN];   // kH/s, Ringpuffer
    volatile uint16_t historyHead;
    volatile uint16_t historyCount;
};

extern MinerStats g_stats;

// Master 004: Ergebnis des Messbetriebs (HW_LOOP_TEST), leer im Normalbetrieb
extern char g_loopTest[1600];
extern const char* g_resetReason;     // Grund des letzten Neustarts
extern int         g_variantBeforeReset;   // Messbetrieb: Variante vor dem Neustart

void stats_update_best(double diff);
void stats_task(void* arg);
