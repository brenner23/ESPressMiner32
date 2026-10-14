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
        static const char* txt[] = { "normal", "software miner paused", "mining stopped" };
        Serial.printf("[Temperature] %.1f C -> %s\n", t, txt[lv]);
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
        case 1:  return "software miner paused (too hot)";
        case 2:  return "mining stopped (too hot)";
        default: return "normal";
    }
}

}  // namespace thermal

// ---------------------------------------------------------------------------
//  Schutzfenster (ab 1.3.0.008)
// ---------------------------------------------------------------------------
// Diagnose 14.10.2026: Ein Geraet im Netz stoert den ESP alle 600,47 s fuer einen
// Augenblick - 1-8 HW-Fehler innerhalb weniger Millisekunden, unabhaengig von Pool und
// Neustarts. Der Waechter ("Wecker") lernt den Takt aus den Fehlerschueben und schaltet um
// den erwarteten Zeitpunkt (HW_GUARD_BEFORE_MS / _AFTER_MS) die vorsichtigere Schleife ein.
//  - 1. Schub: Wecker vorsorglich auf +600 s (haeufigster Fall), sonst auf den letzten Abstand
//  - Takt bestaetigt, wenn zwei Abstaende passen (+-1 %, auch 2x/3x bei verpasster Welle)
//  - erlaubt HW_GUARD_PERIOD_MIN_MS .. _MAX_MS; unregelmaessige Schuebe -> kein Wecker
//  - Stichprobe: jedes HW_GUARD_PROBE_EVERY-te Fenster bleibt ungeschuetzt. Kommt dabei
//    zweimal hintereinander keine Welle, ist sie weg -> Wecker aus (kostet dann nichts)
namespace guard {

volatile uint32_t g_simErrors = 0;   // nur Diag-Build: gespielte Fehler (/api/guardsim)
static uint32_t s_lastBurst = 0;      // millis() des letzten Schubs (0 = keiner)
static uint32_t s_next = 0;           // erwarteter naechster Schub (0 = kein Wecker)
static uint32_t s_period = 0;         // bestaetigter Takt (0 = keiner)
static uint32_t s_cand = 0;           // Takt-Verdacht (noch nicht bestaetigt)
static uint32_t s_misfit = 0;         // Schuebe, die nicht zum bestaetigten Takt passen
static uint32_t s_round = 0;          // Fenster seit dem letzten Schub
static uint32_t s_probeMiss = 0;      // Stichproben ohne Welle in Folge
static bool     s_hit = false;        // Schub im aktuellen Fenster

static inline uint32_t tol(uint32_t p) { return p / 100 > 1500 ? p / 100 : 1500; }

// passt d zu p (auch als Vielfaches bei verpassten Wellen)? -> Anzahl Takte, sonst 0
static uint32_t fits(uint32_t d, uint32_t p)
{
    if (!p) return 0;
    const uint32_t n = (d + p / 2) / p;
    if (n < 1 || n > 12) return 0;
    const int32_t err = (int32_t)(d - n * p);
    return (uint32_t)abs(err) <= tol(p) * n ? n : 0;
}

static inline bool in_range(uint32_t d) { return d >= HW_GUARD_PERIOD_MIN_MS && d <= HW_GUARD_PERIOD_MAX_MS; }

static void on_burst(uint32_t t)
{
    g_stats.guardBursts++;
    s_hit = true;
    s_probeMiss = 0;
    if (s_lastBurst) {
        const uint32_t d = t - s_lastBurst;
        uint32_t n;
        if (s_period) {
            if ((n = fits(d, s_period))) { s_period = (s_period * 3 + d / n) / 4; s_misfit = 0; }
            else if (++s_misfit >= 2) { s_period = 0; s_cand = in_range(d) ? d : 0; s_misfit = 0; }
        } else if ((n = fits(d, s_cand ? s_cand : 600000UL))) {
            s_period = d / n;                                    // bestaetigt
            s_cand = 0;
        } else {
            s_cand = in_range(d) ? d : 0;                        // neuer Verdacht
        }
    }
    s_lastBurst = t;
    s_round = 0;
    const uint32_t p = s_period ? s_period : s_cand ? s_cand : 600000UL;
    s_next = t + p;
    g_stats.guardPeriodMs = s_period;
    Serial.printf("[Guard] error burst, period %s %.2f s\n", s_period ? "confirmed" : "assumed", p / 1000.0f);
}

void task(void*)
{
#if HW_GUARD_ENABLE
    uint32_t lastErr = g_stats.hwErrors + g_simErrors, firstErrMs = 0, errsInBurst = 0, burstLock = 0;
    bool inWin = false, probe = false;
    g_stats.guardNextInS = -1;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const uint32_t now = millis();
        const uint32_t e = g_stats.hwErrors + g_simErrors;

        // Fehlerschub erkennen: HW_GUARD_BURST_ERRORS Fehler innerhalb HW_GUARD_BURST_MS
        if (e != lastErr) {
            if (inWin && !probe) g_stats.guardErrors += e - lastErr;
            if (!firstErrMs || now - firstErrMs > HW_GUARD_BURST_MS) { firstErrMs = now; errsInBurst = 0; }
            errsInBurst += e - lastErr;
            const uint32_t lastErrPrev = lastErr;
            lastErr = e;
            if (errsInBurst >= HW_GUARD_BURST_ERRORS && (!burstLock || now - burstLock > 30000UL)) {
                burstLock = now;
                g_stats.burstErrors += errsInBurst;
                on_burst(firstErrMs);
            } else if (burstLock && now - burstLock <= HW_GUARD_BURST_MS) {
                g_stats.burstErrors += e - lastErrPrev;              // Nachzuegler desselben Schubs
            }
        }

        if (!s_next) { g_stats.guardNextInS = -1; continue; }
        const int32_t dt = (int32_t)(now - s_next);
        if (!inWin && dt >= -(int32_t)HW_GUARD_BEFORE_MS && dt <= (int32_t)HW_GUARD_AFTER_MS) {
            // Fenster auf: geschuetzt, oder als Stichprobe nur beobachten
            inWin = true;
            s_hit = false;
            probe = (s_round + 1) % HW_GUARD_PROBE_EVERY == 0;
            if (!probe) { g_stats.guardWindows++; hw_sha_guard(true); }
        } else if (inWin && (dt > (int32_t)HW_GUARD_AFTER_MS || dt < -(int32_t)HW_GUARD_BEFORE_MS)) {
            // Fenster zu (Zeit um, oder ein Schub hat den Wecker neu gestellt)
            inWin = false;
            hw_sha_guard(false);
            if (dt > 0) {                                       // Zeit um, kein neuer Wecker
                if (probe && !s_hit && ++s_probeMiss >= 2) {
                    Serial.println("[Guard] no wave in two probes - alarm off");
                    s_next = s_period = s_cand = 0;
                    s_probeMiss = 0;
                    g_stats.guardPeriodMs = 0;
                    continue;
                }
                s_round++;
                s_next += s_period ? s_period : s_cand ? s_cand : 600000UL;
            }
        } else if (!inWin && dt > (int32_t)HW_GUARD_AFTER_MS) {
            s_round++;
            s_next += s_period ? s_period : s_cand ? s_cand : 600000UL;   // verpasst
        }
        g_stats.guardNextInS = inWin ? 0 : (int32_t)(s_next - now - HW_GUARD_BEFORE_MS) / 1000;
    }
#else
    vTaskDelete(nullptr);
#endif
}

}  // namespace guard

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
            Serial.println("\n*** BLOCK FOUND! ***\n");
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

        // Kurz abgeben, damit IDLE-Task/Watchdog laufen koennen (HW-Miner: nur wenn HW_YIELD_MS > 0)
        uint32_t now = millis();
        uint32_t yieldMs = isHw ? HW_YIELD_MS : 500;
        if (yieldMs && now - lastYield > yieldMs) {
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
