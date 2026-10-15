#include "selftest.h"
#include "sha256_hw.h"
#include "sha256_sw.h"
#include "utils.h"
#include "config.h"

#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

// Bitcoin-Genesis-Block-Header
static const char* GENESIS_HEADER =
    "0100000000000000000000000000000000000000000000000000000000000000"
    "000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa"
    "4b1e5e4a29ab5f49ffff001d1dac2b7c";

// Hash in interner Byte-Reihenfolge
static const char* GENESIS_HASH =
    "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000";

static bool check(const char* name, bool ok)
{
    Serial.printf("  %-38s %s\n", name, ok ? "OK" : "FAILED");
    return ok;
}

// Vergleicht den Mining-Code (hw_scan) ueber `n` aufeinanderfolgende Nonces mit der
// Software. Mit Maske 1 ist jede zweite Nonce ein Kandidat; ein falscher Hash faellt
// so mit 50 % Wahrscheinlichkeit auf. Liefert die Anzahl Abweichungen (-1 bei Speichermangel).
static int pipeline_mismatches(uint32_t n)
{
    uint32_t* cands = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!cands) return -1;

    uint32_t b1[16], b2[3], mid[8];
    esp_fill_random(b1, sizeof(b1));
    esp_fill_random(b2, sizeof(b2));
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, b1);
    uint32_t start = esp_random();
    int nc = 0;
    hw_scan(b1, b2, start, n, 1, cands, (int)n, &nc);

    uint32_t w1[16] = { b2[0], b2[1], b2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    int mismatches = 0, k = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t s[8];
        w1[3] = start + i;              // T[3]-Wort, siehe hw_scan
        memcpy(s, mid, 32);
        sha256_transform(s, w1);
        memcpy(w2, s, 32);
        memcpy(s, SHA256_H0, 32);
        sha256_transform(s, w2);
        const bool expect = (s[7] & 1) == 0;
        const bool got = k < nc && cands[k] == start + i;
        if (got) k++;
        if (expect != got) mismatches++;
    }
    free(cands);
    return mismatches;
}

// Unabhaengig von den Wartezeiten tritt ~1 falscher Hash pro 25 000-50 000 Nonces auf
// (gemessen, vermutlich Busverkehr des anderen Kerns). Das kostet nur diesen einen
// Hash - Shares werden immer in Software nachgeprueft. Zu kurze Wartezeiten
// erzeugen dagegen Tausende Fehler; nur die sollen erkannt werden.
static const int CAL_MAX_ERRORS_6K  = 1;
static const int CAL_MAX_ERRORS_20K = 4;

static bool timing_ok_once(const HwTiming& tm)
{
    hw_sha_set_timing(tm);
    int errors = 0;
    for (int r = 0; r < 3; r++) {
        int e = pipeline_mismatches(2000);
        if (e < 0) return false;
        errors += e;
    }
    return errors <= CAL_MAX_ERRORS_6K;
}

// Grundrauschen (~1 falscher Hash je 17 000, Langzeittest Master 002) laesst einen
// guten Wert gelegentlich durchfallen; die Binaersuche springt dann viel zu hoch
// (gesehen: Hash 2 = 60 statt 3). Daher zaehlt erst ein zweites Durchfallen.
static bool timing_ok(const HwTiming& tm)
{
    return timing_ok_once(tm) || timing_ok_once(tm);
}

// Sucht per Binaersuche den kleinsten fehlerfreien Wert fuer `*field` in [lo, hi]
// (alle anderen Werte bleiben). `hi` muss fehlerfrei sein.
static bool find_min(HwTiming& tm, uint32_t* field, uint32_t lo, uint32_t hi)
{
    *field = hi;
    if (!timing_ok(tm)) return false;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        *field = mid;
        if (timing_ok(tm)) hi = mid;
        else               lo = mid + 1;
    }
    *field = hi;
    return true;
}

// Zweiter Durchgang: Nachdem alle Phasen knapp eingestellt sind, geht oft noch
// etwas weniger (die Phasen beeinflussen sich gegenseitig ueber den Bus).
static void tighten(HwTiming& tm, uint32_t* field)
{
    while (*field > 0) {
        (*field)--;
        if (!timing_ok(tm)) { (*field)++; return; }
    }
}

static HwTiming s_lim;   // gefundene Grenzwerte (ohne Reserve)

bool selftest_calibrate()
{
    Serial.println("[Calibration]");
    HwTiming tm = HW_TIMING_SAFE;
    const uint32_t t0 = millis();

    uint32_t* fields[5] = { &tm.block[0], &tm.block[1], &tm.block[2], &tm.load[0], &tm.load[1] };
    const HwTiming safe = HW_TIMING_SAFE;
    const uint32_t safeVal[5] = { safe.block[0], safe.block[1], safe.block[2], safe.load[0], safe.load[1] };
    static const char* names[5] = { "Block 1", "Block 2", "Hash 2", "Load 1", "Load 2" };

    // Jede Phase einzeln minimal suchen, dann gemeinsam nachschaerfen
    for (int i = 0; i < 5; i++) {
        if (!find_min(tm, fields[i], 0, safeVal[i])) {
            Serial.printf("  No error-free delay found for %s\n", names[i]);
            return false;
        }
    }
    for (int i = 0; i < 5; i++) tighten(tm, fields[i]);
    const HwTiming lim = tm;
    s_lim = lim;

    // Reserve drauf
    for (int i = 0; i < 3; i++) tm.block[i] += HW_CAL_MARGIN_BLOCK;
    for (int i = 0; i < 2; i++) tm.load[i] += HW_CAL_MARGIN_LOAD;

    // Endpruefung ueber 20 000 Nonces; bei zu vielen Fehlern schrittweise Reserve drauf
    int errors = -1;
    bool ok = false;
    for (int extra = 0; extra < 4 && !ok; extra++) {
        if (extra) {
            for (int i = 0; i < 5; i++) (*fields[i])++;
        }
        hw_sha_set_timing(tm);
        errors = pipeline_mismatches(20000);
        ok = errors >= 0 && errors <= CAL_MAX_ERRORS_20K;
    }
    Serial.printf("  Limit: %lu/%lu/%lu load %lu/%lu  ->  set: %lu/%lu/%lu load %lu/%lu  (%d/20000 errors, %lu ms) %s\n",
                  (unsigned long)lim.block[0], (unsigned long)lim.block[1], (unsigned long)lim.block[2],
                  (unsigned long)lim.load[0], (unsigned long)lim.load[1],
                  (unsigned long)tm.block[0], (unsigned long)tm.block[1], (unsigned long)tm.block[2],
                  (unsigned long)tm.load[0], (unsigned long)tm.load[1], errors,
                  (unsigned long)(millis() - t0), ok ? "OK" : "FAILED");
    return ok;
}

bool selftest_run()
{
    Serial.println("[Self-test]");
    uint8_t header[80], expect[32], out[32];
    hex_to_bytes(GENESIS_HEADER, header, 80);
    hex_to_bytes(GENESIS_HASH, expect, 32);

    bool ok = true;

    sha256d(header, 80, out);
    ok &= check("Software SHA256d (Genesis)", memcmp(out, expect, 32) == 0);

    hw_sha256d_header(header, out);
    ok &= check("Hardware SHA256d (Genesis)", memcmp(out, expect, 32) == 0);

    // Midstate-Pfad des Software-Miners
    uint32_t b1[16], b2[3], mid[8];
    for (int i = 0; i < 16; i++) b1[i] = read_be32(header + 4 * i);
    for (int i = 0; i < 3; i++)  b2[i] = read_be32(header + 64 + 4 * i);
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, b1);
    uint32_t genesisNonce = __builtin_bswap32(0x7c2bac1du);   // als T[3]-Wort
    uint32_t cands[8];
    int nc = 0;
    sw_scan(mid, b2, genesisNonce - 2, 4, cands, 8, &nc);
    ok &= check("Software scan finds genesis nonce", nc == 1 && cands[0] == genesisNonce);

    // Schneller Software-Pfad (ab 1.3.0.010) gegen den Referenzpfad
    {
        int bad = sw_selfcheck(2000);
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "Fast software scan == reference (2000, %d errors)", bad);
        check(lbl, bad == 0);   // bei Fehlern laeuft der alte Pfad weiter, kein Abbruch
    }

    nc = 0;
    hw_scan(b1, b2, genesisNonce - 2, 4, HW_CAND_MASK, cands, 8, &nc);
    ok &= check("Hardware scan finds genesis nonce", nc == 1 && cands[0] == genesisNonce);

    // Zufaellige Header: HW == SW
    int mismatches = 0;
    for (int i = 0; i < 2000; i++) {
        uint8_t hdr[80], a[32], b[32];
        esp_fill_random(hdr, sizeof(hdr));
        sha256d(hdr, 80, a);
        hw_sha256d_header(hdr, b);
        if (memcmp(a, b, 32) != 0) mismatches++;
    }
    char label[64];
    snprintf(label, sizeof(label), "HW == SW (2000 random headers, %d errors)", mismatches);
    ok &= check(label, mismatches <= 1);   // seltene Einzelfehler, siehe CAL_MAX_ERRORS_*

    // Pipeline ueber aufeinanderfolgende Nonces (wie im Mining-Betrieb)
    mismatches = pipeline_mismatches(5000);
    snprintf(label, sizeof(label), "HW pipeline 5000 nonces (%d errors)", mismatches);
    ok &= check(label, mismatches >= 0 && mismatches <= 2);

    return ok;
}

void selftest_benchmark()
{
    uint32_t b1[16], b2[3], mid[8], cands[8];
    int nc;
    esp_fill_random(b1, sizeof(b1));
    esp_fill_random(b2, sizeof(b2));
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, b1);

    const uint32_t N_HW = 200000;
    uint32_t t0 = micros();
    hw_scan(b1, b2, 0, N_HW, HW_CAND_MASK, cands, 8, &nc);
    uint32_t tHw = micros() - t0;

    const uint32_t N_SW = 10000;
    t0 = micros();
    sw_scan(mid, b2, 0, N_SW, cands, 8, &nc);
    uint32_t tSw = micros() - t0;

    Serial.printf("[Benchmark] Hardware: %.1f kH/s (%lu cycles/hash) | Software: %.1f kH/s\n",
                  N_HW * 1000.0 / tHw, (unsigned long)((uint64_t)tHw * 240 / N_HW),
                  N_SW * 1000.0 / tSw);
}

// Misst fuer Reserve 0..4 Takte die Fehlerrate (bei laufendem WLAN/Pool) und die Takte/Hash.
void selftest_margin_sweep()
{
    const HwTiming keep = hw_sha_timing();
    uint32_t b1[16], b2[3], cands[8];
    int nc;
    esp_fill_random(b1, sizeof(b1));
    esp_fill_random(b2, sizeof(b2));
    Serial.println("[Margin test] 100000 nonces each, software check (Wi-Fi active)");
    for (uint32_t m = 0; m <= 4; m++) {
        HwTiming tm = s_lim;
        for (int i = 0; i < 3; i++) tm.block[i] += m;
        for (int i = 0; i < 2; i++) tm.load[i] += m;
        hw_sha_set_timing(tm);
        int errors = 0;
        for (int r = 0; r < 5; r++) errors += pipeline_mismatches(20000);
        uint32_t t0 = micros();
        hw_scan(b1, b2, 0, 100000, HW_CAND_MASK, cands, 8, &nc);
        uint32_t dt = micros() - t0;
        Serial.printf("  Margin %lu: %lu/%lu/%lu load %lu/%lu -> %lu cycles = %.1f kH/s | %d errors (detected) /100000\n",
                      (unsigned long)m, (unsigned long)tm.block[0], (unsigned long)tm.block[1],
                      (unsigned long)tm.block[2], (unsigned long)tm.load[0], (unsigned long)tm.load[1],
                      (unsigned long)((uint64_t)dt * 240 / 100000), 100000 * 1000.0 / dt, errors);
    }
    hw_sha_set_timing(keep);
}

// Langzeit-Fehlertest: Reserve `margin` fuer `minutes` Minuten, Zwischenstand jede Minute.
// Mit Maske 1 wird jeder falsche Hash mit 50 % Wahrscheinlichkeit erkannt.
void selftest_long_test(uint32_t margin, uint32_t minutes)
{
    const HwTiming keep = hw_sha_timing();
    HwTiming tm = s_lim;
    for (int i = 0; i < 3; i++) tm.block[i] += margin;
    for (int i = 0; i < 2; i++) tm.load[i] += margin;
    hw_sha_set_timing(tm);
    Serial.printf("[Long test] margin %lu (%lu/%lu/%lu load %lu/%lu), %lu minutes, Wi-Fi active\n",
                  (unsigned long)margin, (unsigned long)tm.block[0], (unsigned long)tm.block[1],
                  (unsigned long)tm.block[2], (unsigned long)tm.load[0], (unsigned long)tm.load[1],
                  (unsigned long)minutes);
    const uint32_t t0 = millis();
    uint64_t checked = 0;
    uint32_t errors = 0;
    for (uint32_t m = 1; m <= minutes; m++) {
        while (millis() - t0 < m * 60000UL) {
            int e = pipeline_mismatches(20000);
            if (e > 0) errors += e;
            checked += 20000;
            vTaskDelay(1);
        }
        Serial.printf("  Margin %lu, minute %2lu: %llu hashes checked, %lu errors detected -> about 1 error per %s hashes\n",
                      (unsigned long)margin, (unsigned long)m, (unsigned long long)checked, (unsigned long)errors,
                      errors ? String((unsigned long)(checked / (2ULL * errors))).c_str() : "(keiner)");
    }
    hw_sha_set_timing(keep);
}
