#include "stats.h"
#include "sha256_hw.h"
#include "miner.h"
#include "config.h"
#include "mining_job.h"
#include "utils.h"

#include <Arduino.h>
#include <WiFi.h>

char g_loopTest[1600] = "";

MinerStats g_stats = {};

static portMUX_TYPE s_bestMux = portMUX_INITIALIZER_UNLOCKED;

void stats_update_best(double diff)
{
    portENTER_CRITICAL(&s_bestMux);
    if (diff > g_stats.bestDiff) g_stats.bestDiff = diff;
    portEXIT_CRITICAL(&s_bestMux);
}

void stats_task(void*)
{
    uint32_t lastHw = g_stats.hwHashes;
    uint32_t lastSw = g_stats.swHashes;
    uint32_t lastMs = millis();
    uint64_t totalHashes = 0;
    const uint32_t startMs = lastMs;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATS_INTERVAL_MS));
        thermal::poll();

        uint32_t hw = g_stats.hwHashes;
        uint32_t sw = g_stats.swHashes;
        uint32_t now = millis();
        uint32_t dHw = hw - lastHw;
        uint32_t dSw = sw - lastSw;
        float    dt  = (now - lastMs) / 1000.0f;
        lastHw = hw; lastSw = sw; lastMs = now;
        totalHashes += (uint64_t)dHw + dSw;

        float khHw = dHw / dt / 1000.0f;
        float khSw = dSw / dt / 1000.0f;
        float avg  = totalHashes / ((now - startMs) / 1000.0f) / 1000.0f;

        // Fuer die Webseite
        g_stats.khCurrent = khHw + khSw;
        g_stats.khAverage = avg;
        g_stats.uptimeS   = (now - startMs) / 1000;
        g_stats.totalHashes = totalHashes;
        g_stats.history[g_stats.historyHead] = khHw + khSw;
        g_stats.historyHead = (g_stats.historyHead + 1) % STATS_HISTORY_LEN;
        if (g_stats.historyCount < STATS_HISTORY_LEN) g_stats.historyCount++;

        char best[16], pool[16], net[16];
        format_difficulty(g_stats.bestDiff, best, sizeof(best));
        format_difficulty(jobs::difficulty(), pool, sizeof(pool));
        format_difficulty(g_stats.networkDiff, net, sizeof(net));

        uint32_t up = (now - startMs) / 1000;
        uint32_t acc = g_stats.accepted, rej = g_stats.rejected;
        float    quote = (acc + rej) ? acc * 100.0f / (acc + rej) : 100.0f;
        Serial.printf(
            "[%02lu:%02lu:%02lu] %7.1f kH/s (HW %6.1f | SW %5.1f) avg %7.1f | "
            "Shares(R/A) %lu/%lu/%.1f%% | Best %s | Pool-Diff %s | Net %s | %s%s\n",
            (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60),
            khHw + khSw, khHw, khSw, avg,
            (unsigned long)rej, (unsigned long)acc, quote, best, pool, net,
            WiFi.isConnected() ? "WiFi " : "WiFi-OFF ",
            g_stats.poolAuthorized ? "Pool OK" : (g_stats.poolConnected ? "Pool..." : "Pool-OFF"));

        // Projekt-Kennung jede Minute
        static uint32_t lastId = 0;
        if (now - lastId >= 60000UL) {
            lastId = now;
            Serial.printf("           " MINER_NAME " v" MINER_VERSION "  | HW loop: %s | chip %.1f C (rough)\n",
                          hw_sha_loop_name(), g_stats.chipTemp);
        }

        if (g_stats.hwErrors) {
            Serial.printf("           HW errors (discarded): %lu of %lu candidates\n",
                          (unsigned long)g_stats.hwErrors, (unsigned long)g_stats.candidates);
        }
    }
}
