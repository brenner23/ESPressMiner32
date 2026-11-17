#include "persist.h"
#include "stats.h"

#include <Arduino.h>
#include <Preferences.h>

namespace persist {

static const char*    NS          = "esppers";
static const uint32_t OP_SAVE_S   = 600;     // Betriebszeit alle 10 min sichern
static const uint32_t BEST_MIN_MS = 60000;   // neuer Rekord hoechstens 1x pro Minute schreiben

static bool     s_keepBest  = true;
static double   s_savedBest = 0;
static uint32_t s_opBase    = 0;             // gespeicherte Betriebszeit beim Start
static uint32_t s_opSaved   = 0;             // zuletzt gespeicherter Stand
static uint32_t s_bootMs    = 0;
static uint32_t s_lastBestMs = 0;
static volatile bool s_reset = false;

static uint32_t run_s() { return (millis() - s_bootMs) / 1000; }

void begin()
{
    s_bootMs = millis();
    Preferences p;
    if (p.begin(NS, true)) {
        s_keepBest  = p.getBool("keepBest", true);
        s_savedBest = p.getDouble("best", 0);
        s_opBase    = p.getUInt("opS", 0);
        p.end();
    }
    s_opSaved = s_opBase;
    if (s_keepBest && s_savedBest > g_stats.bestDiff) {
        g_stats.bestDiff = s_savedBest;
        Serial.printf("[Persist] best difficulty %.4f restored\n", s_savedBest);
    }
    Serial.printf("[Persist] operating time %lu h total\n", (unsigned long)(s_opBase / 3600));
}

uint32_t op_seconds() { return s_opBase + run_s(); }
bool keep_best() { return s_keepBest; }

void tick(uint32_t)
{
    Preferences p;
    if (s_reset) {
        s_reset = false;
        s_savedBest = 0;
        if (p.begin(NS, false)) { p.putDouble("best", 0); p.end(); }
    }
    const uint32_t op = op_seconds();
    if (op - s_opSaved >= OP_SAVE_S) {
        if (p.begin(NS, false)) { p.putUInt("opS", op); p.end(); }
        s_opSaved = op;
    }
    const double best = g_stats.bestDiff;
    if (s_keepBest && best > s_savedBest && millis() - s_lastBestMs >= BEST_MIN_MS) {
        if (p.begin(NS, false)) { p.putDouble("best", best); p.end(); }
        s_savedBest = best;
        s_lastBestMs = millis();
    }
}

void to_json(JsonDocument& d)
{
    d["keepBest"]  = s_keepBest;
    d["savedBest"] = s_savedBest;
    d["opSeconds"] = op_seconds();
}

const char* set(JsonDocument& j)
{
    Preferences p;
    if (j["keepBest"].is<bool>()) {
        s_keepBest = j["keepBest"];
        if (p.begin(NS, false)) { p.putBool("keepBest", s_keepBest); p.end(); }
        s_lastBestMs = 0;                       // gleich sichern, falls eingeschaltet
    }
    if (j["resetBest"].is<bool>() && j["resetBest"]) {
        g_stats.bestDiff = 0;
        s_reset = true;                         // NVS im stats_task (nicht im Webserver) loeschen
    }
    if (j["best"].is<double>()) {               // Rekord uebernehmen (z. B. vom alten Stand), nur hoeher
        const double b = j["best"];
        if (b > g_stats.bestDiff) g_stats.bestDiff = b;
        s_lastBestMs = 0;
    }
    return nullptr;
}

}  // namespace persist
