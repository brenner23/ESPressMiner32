// ESPressMiner32 - Bitcoin Stratum-Miner fuer den ESP32
//
//   Core 1 : Hardware-SHA-256-Miner (Register-Direktzugriff)
//   Core 0 : WLAN, Stratum-Client, Statistik, Software-SHA-256-Miner

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "handover.h"
#include "settings.h"
#include "miner.h"
#include "mining_job.h"
#include "selftest.h"
#include "sha256_hw.h"
#include "stats.h"
#include "stratum_client.h"
#include "web_server.h"
#include "serial_config.h"
#include "diag.h"
#include "display.h"

static StratumClient g_stratum;

#if HW_LOOP_TEST
// Reihenfolge: A = v2 schnell (Referenz), dann B..G (siehe hw_scan_v2.S)
// Runde 2 (nach Runde 1: H/M fallen unter Last durch, p1 muss 4 bleiben)
// 1.3.0.009: J (Referenz) gegen die Feinvarianten O/P/Q/R (siehe hw_scan_v2.S)
static const int  LT_IDX[]   = { 11, 16, 17, 18, 19 };
static const char LT_TAG[]   = "JOPQR";
static const int  LT_N       = sizeof(LT_IDX) / sizeof(LT_IDX[0]);
static bool  s_ltOk[LT_N];
static float s_ltCyc[LT_N];

// Vor dem Mining (Engine frei): Known-Answer-Test und Takte je Variante
static void loop_test_prepare()
{
    Serial.println("[Loop test] hash loop variants:");
    for (int k = 0; k < LT_N; k++) {
        s_ltOk[k]  = hw_sha_variant_selftest(LT_IDX[k]) || hw_sha_variant_selftest(LT_IDX[k]);
        s_ltCyc[k] = hw_sha_variant_cycles(LT_IDX[k]);
        Serial.printf("  %c %-30s %6.1f cycles = %6.1f kH/s  KAT %s\n", LT_TAG[k],
                      hw_sha_variant_name(LT_IDX[k]), s_ltCyc[k], 240000.0f / s_ltCyc[k],
                      s_ltOk[k] ? "OK" : "FAILED (skipped)");
    }
}

// Waehrend des Minings: reihum je HW_LOOP_TEST Minuten, Summen je Variante
static void loop_test_task(void*)
{
    // sumE = Fehler AUSSERHALB von Schutzfenster und erkannten Schueben (= Fehler der Schleife selbst),
    // sumW = Fehler der WLAN-Welle (im Fenster oder als Schub erkannt) - zaehlen nicht gegen die Variante
    uint64_t sumH[LT_N] = {}; uint32_t sumC[LT_N] = {}, sumE[LT_N] = {}, sumW[LT_N] = {}; float sumT[LT_N] = {};
    vTaskDelay(pdMS_TO_TICKS(15000));          // Pool-Verbindung abwarten
    for (int round = 1;; round++) {
        for (int k = 0; k < LT_N; k++) {
            if (!s_ltOk[k]) continue;
            hw_sha_set_variant(LT_IDX[k]);
            vTaskDelay(pdMS_TO_TICKS(3000));    // Umschalten abwarten
            const uint32_t h0 = g_stats.hwHashes, c0 = g_stats.hwCandidates, e0 = g_stats.hwErrors;
            const uint32_t w0 = g_stats.guardErrors + g_stats.burstErrors;
            const uint32_t t0 = millis();
            // jede Minute: neue Fehler dieser Minute (Verlauf statt nur Summe)
            for (int m = 1; m <= HW_LOOP_TEST; m++) {
                const uint32_t cm = g_stats.hwCandidates, em = g_stats.hwErrors;
                const uint32_t wm = g_stats.guardErrors + g_stats.burstErrors;
                vTaskDelay(pdMS_TO_TICKS(60000UL));
                const uint32_t wave = g_stats.guardErrors + g_stats.burstErrors - wm;
                Serial.printf("[Loop test] %c minute %d: %lu candidates, %lu discarded (%lu of them WiFi wave)\n",
                              LT_TAG[k], m, (unsigned long)(g_stats.hwCandidates - cm),
                              (unsigned long)(g_stats.hwErrors - em), (unsigned long)wave);
            }
            const float dt = (millis() - t0) / 1000.0f;
            sumH[k] += (uint32_t)(g_stats.hwHashes - h0);
            sumC[k] += g_stats.hwCandidates - c0;
            const uint32_t w = g_stats.guardErrors + g_stats.burstErrors - w0;
            const uint32_t eAll = g_stats.hwErrors - e0;
            sumW[k] += w;
            sumE[k] += eAll > w ? eAll - w : 0;
            sumT[k] += dt;

            // Bericht: HW-Hashrate, Fehlerquote = verworfene / alle HW-Kandidaten,
            // effektiv = Hashrate * (1 - Fehlerquote)
            int len = snprintf(g_loopTest, sizeof(g_loopTest),
                               "Round %d | var | kH/s HW | cycles(bench) | cand | own errors | wave | own error%% | effective\n", round);
            for (int j = 0; j < LT_N && len < (int)sizeof(g_loopTest) - 120; j++) {
                if (!sumT[j]) continue;
                const float kh  = sumH[j] / sumT[j] / 1000.0f;
                const float err = sumC[j] ? 100.0f * sumE[j] / sumC[j] : 0.0f;
                len += snprintf(g_loopTest + len, sizeof(g_loopTest) - len,
                                "%c %-28s | %6.1f | %5.1f | %6lu | %5lu | %4lu | %6.3f | %6.1f\n", LT_TAG[j],
                                hw_sha_variant_name(LT_IDX[j]), kh, s_ltCyc[j], (unsigned long)sumC[j],
                                (unsigned long)sumE[j], (unsigned long)sumW[j], err, kh * (1.0f - err / 100.0f));
            }
            Serial.print("[Loop test]\n");
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

// Ab 1.3.0.008: immer alle Kanaele absuchen und im Mesh den staerksten Knoten nehmen,
// nichts im NVS zwischenspeichern (sonst wird ein alter Kanal / Knoten weiterverwendet)
// Ab .003: zweites WLAN. Jeder Verbindungsversuch nimmt das naechste eingetragene Netz
// (WLAN 1, WLAN 2, WLAN 1, ...); verbunden bleibt er mit dem, das zuerst antwortet.
static int s_wifiIdx = 0;   // 0 = WLAN 1, 1 = WLAN 2 (das gerade versuchte bzw. verbundene)
static bool wifi2_set() { return g_settings.wifiSsid2.length() > 0; }

static void wifi_connect()
{
    WiFi.persistent(false);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    const String& ssid = s_wifiIdx ? g_settings.wifiSsid2 : g_settings.wifiSsid;
    const String& pass = s_wifiIdx ? g_settings.wifiPass2 : g_settings.wifiPass;
    g_stats.wifiNet = s_wifiIdx + 1;
    WiFi.begin(ssid.c_str(), pass.c_str());
}

// Vom Webserver nach "Wi-Fi 1 <-> Wi-Fi 2 tauschen": das verbundene Netz hat jetzt die andere Nummer
void wifi_note_swapped()
{
    if (wifi2_set()) s_wifiIdx ^= 1;
    g_stats.wifiNet = s_wifiIdx + 1;
}

// Naechstes Netz fuer den naechsten Versuch (nur wenn WLAN 2 eingetragen ist)
static void wifi_next()
{
    if (wifi2_set()) s_wifiIdx ^= 1;
    else s_wifiIdx = 0;
}

static void wifi_event(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        g_stats.wifiLastReason = info.wifi_sta_disconnected.reason;
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.printf("[WiFi] connected, IP %s, channel %d, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                      WiFi.channel(), WiFi.RSSI());
    }
}

// WLAN-Waechter (ab 1.3.0.008): 1.3.0.007 blieb nach einem Kanalwechsel im Mesh dauerhaft
// offline. Ist das WLAN weg, wird nach WIFI_RECONNECT_AFTER_MS selbst neu verbunden (alle
// WIFI_RECONNECT_EVERY_MS erneut); nach WIFI_REBOOT_AFTER_MS hilft nur noch ein Neustart.
static void wifi_watch_task(void*)
{
    uint32_t lostAt = 0, lastTry = 0, onWifi2Since = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (web_portal_active() && !g_settings.wifiSsid.length()) continue;
        const uint32_t now = millis();
        if (WiFi.status() == WL_CONNECTED) {
            if (lostAt) Serial.printf("[WiFi] back after %lu s\n", (unsigned long)((now - lostAt) / 1000));
            lostAt = 0;
            // ab .003: auf WLAN 2 alle WIFI2_RETRY_PRIMARY_MS kurz nachsehen, ob WLAN 1 wieder da ist
            // (wie beim Pool: Secondary -> nach 10 min zurueck zum Primary)
            if (s_wifiIdx == 1 && wifi2_set()) {
                if (!onWifi2Since) onWifi2Since = now;
                if (now - onWifi2Since >= WIFI2_RETRY_PRIMARY_MS) {
                    onWifi2Since = now;
                    const int n = WiFi.scanNetworks(false, false);
                    bool seen = false;
                    for (int i = 0; i < n && !seen; i++) seen = (WiFi.SSID(i) == g_settings.wifiSsid);
                    WiFi.scanDelete();
                    if (seen) {
                        Serial.printf("[WiFi] Wi-Fi 1 \"%s\" is back - switching from Wi-Fi 2\n", g_settings.wifiSsid.c_str());
                        s_wifiIdx = 0;
                        WiFi.disconnect(false, false);
                        vTaskDelay(pdMS_TO_TICKS(200));
                        wifi_connect();   // klappt es nicht, wechselt der Waechter nach 10 s wieder auf WLAN 2
                    }
                }
            } else {
                onWifi2Since = 0;
            }
            continue;
        }
        if (!lostAt) {
            lostAt = now;
            lastTry = now;
            g_stats.wifiDrops++;
            Serial.printf("[WiFi] connection lost (reason %d)\n", g_stats.wifiLastReason);
            continue;
        }
        if (now - lostAt >= WIFI_REBOOT_AFTER_MS && !web_portal_active()) {   // Portal nicht abschiessen
            Serial.println("[WiFi] still offline - restarting");
            delay(200);
            ESP.restart();
        }
        if (now - lostAt >= WIFI_RECONNECT_AFTER_MS && now - lastTry >= WIFI_RECONNECT_EVERY_MS - 1000) {
            lastTry = now;
            g_stats.wifiReconnects++;
            Serial.printf("[WiFi] reconnecting (scan all channels, reason %d)\n", g_stats.wifiLastReason);
            WiFi.disconnect(false, false);
            vTaskDelay(pdMS_TO_TICKS(200));
            wifi_next();        // ab .003: im Wechsel auch WLAN 2 versuchen (falls eingetragen)
            wifi_connect();
        }
    }
}

// true = verbunden
static bool wifi_begin()
{
    WiFi.setHostname(g_settings.hostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(wifi_event);
    // ab .003: mit WLAN 2 bekommt jedes Netz die halbe Wartezeit (zusammen weiter 20 s)
    const int tries = wifi2_set() ? 2 : 1;
    for (int t = 0; t < tries && WiFi.status() != WL_CONNECTED; t++) {
        if (t) { WiFi.disconnect(false, false); delay(200); wifi_next(); }
        wifi_connect();
        Serial.printf("[WiFi] Connecting to \"%s\" (Wi-Fi %d) ",
                      (s_wifiIdx ? g_settings.wifiSsid2 : g_settings.wifiSsid).c_str(), s_wifiIdx + 1);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS / tries) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.println("[WiFi] not reachable - starting setup portal as well");
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
        static const char* RR[] = { "unknown", "power-on", "external", "software", "panic (crash)",
                                    "interrupt watchdog", "task watchdog", "watchdog", "deep sleep",
                                    "brownout (voltage)", "SDIO" };
        const int r = (int)esp_reset_reason();
        g_resetReason = (r >= 0 && r < (int)(sizeof(RR) / sizeof(RR[0]))) ? RR[r] : "?";
        g_variantBeforeReset = hw_sha_variant_before_reset();
        Serial.printf("  Reset reason: %s", g_resetReason);
        if (g_variantBeforeReset >= 0)
            Serial.printf("  | was running: variant %s", hw_sha_variant_name(g_variantBeforeReset));
        Serial.println();
    }
    Serial.println("==============================================");
    handover::restore();        // Neustart ohne Stromtrennung: Betriebszeit, Best Diff, Blocks weiter
    Serial.printf("CPU %lu MHz, free heap %lu bytes\n",
                  (unsigned long)getCpuFrequencyMhz(), (unsigned long)ESP.getFreeHeap());

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif

    display_begin();            // ab 1.3.1-d1: Startbild (nur auf Display-Boards)
    const bool configured = settings_load();
    display_progress(15, "self-test");
    serial_config_start();      // Konfiguration per serieller Konsole ist immer moeglich

    hw_sha_begin();
    // Master 003: zuerst die v2-Schleife (NerdMiner Modded) pruefen. Besteht sie,
    // wird die klassische Schleife nicht kalibriert (spart Startzeit).
    Serial.println("[Hash loop]");
    const bool v2 = hw_sha_select_loop();
    Serial.printf("[HW-SHA] active: %s\n", hw_sha_loop_name());
    // Einzelne Fehlversuche kommen vor (beobachtet in Master 002); die
    // Notwerte kosten ~60 % Hashrate, daher mehrmals versuchen.
    bool calibrated = false;
    for (int attempt = 0; attempt < 3 && !calibrated && !v2; attempt++) calibrated = selftest_calibrate();
    if (!calibrated && !v2) {
        Serial.println("CALIBRATION FAILED - using safe default values.");
        hw_sha_set_timing(HW_TIMING_SAFE);
    }
    if (!selftest_run()) {
        Serial.println("SELF-TEST FAILED - mining will not start.");
        for (;;) delay(1000);
    }
    display_progress(35, "benchmark");
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
        Serial.println("[Setup] No saved settings found.");
        display_progress(100, "setup portal (Wi-Fi AP)");
        web_start_portal(false);
        return;
    }

    display_progress(50, "connecting Wi-Fi");
    if (!wifi_begin()) web_start_portal(true);
#if HAS_DISPLAY
    {
        char line[48];
        if (WiFi.status() == WL_CONNECTED) snprintf(line, sizeof(line), "IP %s", WiFi.localIP().toString().c_str());
        else strlcpy(line, "no Wi-Fi - setup portal", sizeof(line));
        display_progress(75, line);
    }
#endif
    xTaskCreatePinnedToCore(wifi_watch_task, "wifiWatch", 3072, nullptr, 1, nullptr, 0);
    diag_begin();               // nur im Test-Build (DIAG_NET) aktiv
    g_stratum.begin();

    xTaskCreatePinnedToCore(stratum_task, "stratum", 12288, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(stats_task, "stats", 4096, nullptr, 2, nullptr, 0);
    display_start_task();       // D2: wartet auf den Pool, dann Dashboard (nur geaenderte Felder)
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
    xTaskCreatePinnedToCore(guard::task, "guard", 3072, nullptr, 2, nullptr, 0);
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
