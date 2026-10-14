#include "miner.h"
#include "config.h"
#include "mining_job.h"
#include "sha256_hw.h"
#include "sha256_sw.h"
#include "stats.h"
#include "utils.h"

#include <Arduino.h>
#include <string.h>

// ---------------------------------------------------------------------------
//  Ueberhitzungsschutz (ab 1.3.0.004)
// ---------------------------------------------------------------------------
// Interner Sensor des klassischen ESP32: grob (Versatz bis +-10 Grad), auf manchen
// Chips unbrauchbar -> unplausible Werte werden ignoriert, statt grundlos abzuschalten.
// Stufen mit Rueckschalt-Abstand (Hysterese), damit nichts hin- und herspringt.
namespace thermal {

static volatile int      s_level = 0;
static volatile uint32_t s_lastMs = 0;

void update(float t)
{
    s_lastMs = millis();
    g_stats.chipTemp = t;
    if (t < TEMP_PLAUSIBLE_MIN_C || t > TEMP_PLAUSIBLE_MAX_C) return;   // Sensor-Unsinn
    int lv = s_level;
    if (t >= TEMP_STOP_C)                  lv = 2;
    else if (t >= TEMP_SW_PAUSE_C)         lv = (lv == 2) ? 2 : 1;      // aus Stufe 2 erst unter RESUME
    if (t < TEMP_RESUME_C)                 lv = 0;
    if (lv != s_level) {
        static const char* txt[] = { "normal", "Software-Miner pausiert", "Mining gestoppt" };
        Serial.printf("[Temperatur] %.1f C -> %s\n", t, txt[lv]);
        s_level = lv;
    }
}

void poll()
{
    // Rueckfall, falls lange kein Pool-Job kam (z.B. Pool weg): selbst nachlesen
    if (millis() - s_lastMs >= 60000UL) update(temperatureRead());
}

int level() { return s_level; }

const char* text()
{
    switch (s_level) {
        case 1:  return "Software-Miner pausiert (zu warm)";
        case 2:  return "Mining gestoppt (zu warm)";
        default: return "normal";
    }
}

}  // namespace thermal

namespace miner {

// Prueft einen Kandidaten in Software und reicht ihn ggf. als Share ein
static void verify_candidate(const MiningJob& job, uint32_t nonce, bool fromHw)
{
    uint8_t header[80];
    memcpy(header, job.header, 80);
    write_le32(header + 76, nonce);   // nonce = echte Nonce (bereits zurueckgetauscht)

    uint8_t hash[32];
    sha256d(header, 80, hash);

    g_stats.candidates++;
    if (fromHw) g_stats.hwCandidates++;
    if (hash[31] != 0 || hash[30] != 0) {
        // Seltener, harmloser HW-Rechenfehler: hier abgefangen, bevor er zum Pool geht.
        // Wird nur gezaehlt (Diagnose), nicht bestraft - kein Share, kein Schaden.
        if (fromHw) {
            g_stats.hwErrors++;
            hw_sha_report_candidate(false);
        }
        return;
    }
    if (fromHw) hw_sha_report_candidate(true);

    double diff = hash_difficulty(hash);
    stats_update_best(diff);

    if (diff >= jobs::difficulty()) {
        ShareSubmit s;
        strncpy(s.jobId, job.jobId, sizeof(s.jobId));
        strncpy(s.extranonce2, job.extranonce2, sizeof(s.extranonce2));
        strncpy(s.ntime, job.ntime, sizeof(s.ntime));
        s.nonce = nonce;
        s.difficulty = diff;
        g_stats.sharesFound++;
        if (diff >= job.networkDiff) {
            g_stats.blocksFound++;
            Serial.println("\n*** BLOCK GEFUNDEN! ***\n");
        }
        jobs::pushShare(s);
    }
}

typedef uint32_t (*ScanFn)(const MiningJob& job, uint32_t nonce, uint32_t count,
                           uint32_t* cands, int maxCands, int* nCands);

static uint32_t scan_hw(const MiningJob& job, uint32_t nonce, uint32_t count,
                        uint32_t* cands, int maxCands, int* nCands)
{
    return hw_scan(job.blk1, job.blk2, nonce, count, HW_CAND_MASK, cands, maxCands, nCands);
}

static uint32_t scan_sw(const MiningJob& job, uint32_t nonce, uint32_t count,
                        uint32_t* cands, int maxCands, int* nCands)
{
    return sw_scan(job.midstate, job.blk2, nonce, count, cands, maxCands, nCands);
}

static void mine_loop(ScanFn scan, bool isHw, uint32_t nonceStart, uint32_t nonceEnd,
                      uint32_t batch, volatile uint32_t* hashCounter)
{
    static MiningJob jobHw, jobSw;   // statisch -> nicht auf dem Task-Stack
    MiningJob& job = isHw ? jobHw : jobSw;

    uint32_t gen = 0;
    uint32_t nonce = nonceStart;
    bool     exhausted = true;
    uint32_t lastYield = millis();

    for (;;) {
        if (jobs::generation() != gen) {
            gen = jobs::generation();
            if (jobs::copy(job)) {
                nonce = nonceStart;
                exhausted = false;
            } else {
                exhausted = true;
            }
        }

        if (exhausted) {
            vTaskDelay(pdMS_TO_TICKS(20));
            lastYield = millis();
            continue;
        }

        // Ueberhitzungsschutz: SW-Miner ab Stufe 1, HW-Miner ab Stufe 2 pausieren
        if (thermal::level() >= (isHw ? 2 : 1)) {
            vTaskDelay(pdMS_TO_TICKS(500));
            lastYield = millis();
            continue;
        }

        uint32_t count = batch;
        if (nonceEnd - nonce < count) count = nonceEnd - nonce;

        uint32_t cands[8];
        int nc = 0;
        uint32_t done = scan(job, nonce, count, cands, 8, &nc);
        *hashCounter += done;

        // Scanner zaehlen im T[3]-Wort (Big-Endian) -> echte Nonce = bswap32
        for (int i = 0; i < nc; i++) verify_candidate(job, __builtin_bswap32(cands[i]), isHw);

        nonce += done;
        if (nonce >= nonceEnd || nonce < nonceStart) {
            exhausted = true;
            jobs::requestNewWork();
        }

        // Kurz abgeben, damit IDLE-Task/Watchdog laufen koennen
        uint32_t now = millis();
        if (now - lastYield > 500) {
            vTaskDelay(1);
            lastYield = now;
        }
    }
}

void hw_task(void*)
{
    mine_loop(scan_hw, true, HW_NONCE_START, HW_NONCE_END, HW_BATCH_SIZE, &g_stats.hwHashes);
}

void sw_task(void*)
{
    mine_loop(scan_sw, false, SW_NONCE_START, SW_NONCE_END, SW_BATCH_SIZE, &g_stats.swHashes);
}

}  // namespace miner
