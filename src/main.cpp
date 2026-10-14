// ESPressMiner32 - Bitcoin Stratum-Miner fuer den ESP32
//
//   Core 1 : Hardware-SHA-256-Miner (Register-Direktzugriff)
//   Core 0 : WLAN, Stratum-Client, Statistik, Software-SHA-256-Miner

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "settings.h"
#include "miner.h"
#include "mining_job.h"
#include "selftest.h"
#include "sha256_hw.h"
#include "stats.h"
#include "stratum_client.h"
#include "web_server.h"
#include "serial_config.h"

static StratumClient g_stratum;

#if HW_LOOP_TEST
// Reihenfolge: A = v2 schnell (Referenz), dann B..G (siehe hw_scan_v2.S)
// Runde 2 (nach Runde 1: H/M fallen unter Last durch, p1 muss 4 bleiben)
static const int  LT_IDX[]   = { 10, 11, 12, 15, 5, 0 };
static const char LT_TAG[]   = "IJKNDA";
static const int  LT_N       = sizeof(LT_IDX) / sizeof(LT_IDX[0]);
static bool  s_ltOk[LT_N];
static float s_ltCyc[LT_N];

// Vor dem Mining (Engine frei): Known-Answer-Test und Takte je Variante
static void loop_test_prepare()
{
    Serial.println("[Messbetrieb] Varianten der Hash-Schleife:");
    for (int k = 0; k < LT_N; k++) {
        s_ltOk[k]  = hw_sha_variant_selftest(LT_IDX[k]) || hw_sha_variant_selftest(LT_IDX[k]);
        s_ltCyc[k] = hw_sha_variant_cycles(LT_IDX[k]);
        Serial.printf("  %c %-30s %6.1f Takte = %6.1f kH/s  KAT %s\n", LT_TAG[k],
                      hw_sha_variant_name(LT_IDX[k]), s_ltCyc[k], 240000.0f / s_ltCyc[k],
                      s_ltOk[k] ? "OK" : "FEHLER (wird uebersprungen)");
    }
}

// Waehrend des Minings: reihum je HW_LOOP_TEST Minuten, Summen je Variante
static void loop_test_task(void*)
{
    uint64_t sumH[LT_N] = {}; uint32_t sumC[LT_N] = {}, sumE[LT_N] = {}; float sumT[LT_N] = {};
    vTaskDelay(pdMS_TO_TICKS(15000));          // Pool-Verbindung abwarten
    for (int round = 1;; round++) {
        for (int k = 0; k < LT_N; k++) {
            if (!s_ltOk[k]) continue;
            hw_sha_set_variant(LT_IDX[k]);
            vTaskDelay(pdMS_TO_TICKS(3000));    // Umschalten abwarten
            const uint32_t h0 = g_stats.hwHashes, c0 = g_stats.hwCandidates, e0 = g_stats.hwErrors;
            const uint32_t t0 = millis();
            // jede Minute: neue Fehler dieser Minute (Verlauf statt nur Summe)
            for (int m = 1; m <= HW_LOOP_TEST; m++) {
                const uint32_t cm = g_stats.hwCandidates, em = g_stats.hwErrors;
                vTaskDelay(pdMS_TO_TICKS(60000UL));
                Serial.printf("[Messbetrieb] %c Minute %d: %lu Kandidaten, %lu verworfen\n", LT_TAG[k], m,
                              (unsigned long)(g_stats.hwCandidates - cm), (unsigned long)(g_stats.hwErrors - em));
            }
            const float dt = (millis() - t0) / 1000.0f;
            sumH[k] += (uint32_t)(g_stats.hwHashes - h0);
            sumC[k] += g_stats.hwCandidates - c0;
            sumE[k] += g_stats.hwErrors - e0;
            sumT[k] += dt;

            // Bericht: HW-Hashrate, Fehlerquote = verworfene / alle HW-Kandidaten,
            // effektiv = Hashrate * (1 - Fehlerquote)
            int len = snprintf(g_loopTest, sizeof(g_loopTest),
                               "Runde %d | Var | kH/s HW | Takte(Bench) | Kand | verworfen | Fehler%% | effektiv\n", round);
            for (int j = 0; j < LT_N && len < (int)sizeof(g_loopTest) - 120; j++) {
                if (!sumT[j]) continue;
                const float kh  = sumH[j] / sumT[j] / 1000.0f;
                const float err = sumC[j] ? 100.0f * sumE[j] / sumC[j] : 0.0f;
                len += snprintf(g_loopTest + len, sizeof(g_loopTest) - len,
                                "%c %-28s | %6.1f | %5.1f | %6lu | %5lu | %6.3f | %6.1f\n", LT_TAG[j],
                                hw_sha_variant_name(LT_IDX[j]), kh, s_ltCyc[j], (unsigned long)sumC[j],
                                (unsigned long)sumE[j], err, kh * (1.0f - err / 100.0f));
            }
            Serial.print("[Messbetrieb]\n");
            Serial.print(g_loopTest);
        }
    }
}
#endif

const char* g_resetReason = "";
int         g_variantBeforeReset = -1;

static void stratum_task(void*)
{
    g_stratum.run();
}

// Zentrale LED-Steuerung: blinkt im Einrichtungs-Portal, leuchtet beim Minen,
// geht bei jedem gesendeten Share kurz aus, sonst aus.
static void led_task(void*)
{
#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    for (;;) {
        bool on;
        if (web_portal_active()) {
            on = (millis() / 400) % 2;                 // Einrichtung: langsames Blinken
        } else {
            on = jobs::valid() && WiFi.status() == WL_CONNECTED;   // Minen: dauerhaft an
        }
        digitalWrite(LED_PIN, on ? HIGH : LOW);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
#else
    vTaskDelete(nullptr);
#endif
}

// true = verbunden
static bool wifi_begin()
{
    WiFi.setHostname(g_settings.hostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPass.c_str());

    Serial.printf("[WiFi] Verbinde mit \"%s\" ", g_settings.wifiSsid.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Verbunden, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.println("\n[WiFi] nicht erreichbar - Portal wird zusaetzlich gestartet");
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    setCpuFrequencyMhz(240);

    Serial.println();
    Serial.println("==============================================");
    Serial.println("  " MINER_NAME " v" MINER_VERSION "  -  ESP32 Bitcoin Miner");
    {
        static const char* RR[] = { "unbekannt", "Einschalten", "extern", "Software", "PANIC (Absturz)",
                                    "Interrupt-Watchdog", "Task-Watchdog", "Watchdog", "Deep-Sleep",
                                    "Brownout (Spannung)", "SDIO" };
        const int r = (int)esp_reset_reason();
        g_resetReason = (r >= 0 && r < (int)(sizeof(RR) / sizeof(RR[0]))) ? RR[r] : "?";
        g_variantBeforeReset = hw_sha_variant_before_reset();
        Serial.printf("  Reset-Grund: %s", g_resetReason);
        if (g_variantBeforeReset >= 0)
            Serial.printf("  | lief vorher: Variante %s", hw_sha_variant_name(g_variantBeforeReset));
        Serial.println();
    }
    Serial.println("==============================================");
    Serial.printf("CPU %lu MHz, freier Heap %lu Bytes\n",
                  (unsigned long)getCpuFrequencyMhz(), (unsigned long)ESP.getFreeHeap());

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif

    const bool configured = settings_load();
    serial_config_start();      // Konfiguration per serieller Konsole ist immer moeglich

    hw_sha_begin();
    // Master 003: zuerst die v2-Schleife (NerdMiner Modded) pruefen. Besteht sie,
    // wird die klassische Schleife nicht kalibriert (spart Startzeit).
    Serial.println("[Hash-Schleife]");
    const bool v2 = hw_sha_select_loop();
    Serial.printf("[HW-SHA] aktiv: %s\n", hw_sha_loop_name());
    // Einzelne Fehlversuche kommen vor (beobachtet in Master 002); die
    // Notwerte kosten ~60 % Hashrate, daher mehrmals versuchen.
    bool calibrated = false;
    for (int attempt = 0; attempt < 3 && !calibrated && !v2; attempt++) calibrated = selftest_calibrate();
    if (!calibrated && !v2) {
        Serial.println("KALIBRIERUNG FEHLGESCHLAGEN - sichere Standardwerte werden genutzt.");
        hw_sha_set_timing(HW_TIMING_SAFE);
    }
    if (!selftest_run()) {
        Serial.println("SELBSTTEST FEHLGESCHLAGEN - Mining wird nicht gestartet.");
        for (;;) delay(1000);
    }
    selftest_benchmark();
#if HW_LOOP_TEST
    loop_test_prepare();
#endif

    jobs::begin();
    jobs::setDifficulty(POOL_SUGGEST_DIFFICULTY);

    // Der Webserver wird immer gebraucht: Dashboard oder Einrichtungs-Portal
    xTaskCreatePinnedToCore(web_task, "web", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(led_task, "led", 2048, nullptr, 1, nullptr, 0);

    if (!configured) {
        // Erster Start: noch keine Zugangsdaten im NVS -> nur Portal, kein Mining
        Serial.println("[Setup] Keine gespeicherten Einstellungen gefunden.");
        web_start_portal(false);
        return;
    }

    if (!wifi_begin()) web_start_portal(true);
    g_stratum.begin();

    xTaskCreatePinnedToCore(stratum_task, "stratum", 12288, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(stats_task, "stats", 4096, nullptr, 2, nullptr, 0);
#ifdef HW_MARGIN_SWEEP
    delay(3000);                // Pool-Verbindung aufbauen lassen (WLAN-Verkehr)
    selftest_margin_sweep();
#endif
#ifdef HW_LONG_TEST
    delay(3000);
    selftest_long_test(0, 10);
    selftest_long_test(1, 10);
#endif
    xTaskCreatePinnedToCore(miner::hw_task, "minerHW", 4096, nullptr, 2, nullptr, HW_MINER_CORE);
#if HW_LOOP_TEST
    xTaskCreatePinnedToCore(loop_test_task, "looptest", 4096, nullptr, 1, nullptr, 0);
#endif
#if ENABLE_SW_MINER
    xTaskCreatePinnedToCore(miner::sw_task, "minerSW", 4096, nullptr, 1, nullptr, SW_MINER_CORE);
#endif
}

void loop()
{
    vTaskDelete(nullptr);
}
