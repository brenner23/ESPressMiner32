// ESPressMiner32 - Bitcoin Stratum-Miner fuer den ESP32
//
//   Core 1 : Hardware-SHA-256-Miner (Register-Direktzugriff)
//   Core 0 : WLAN, Stratum-Client, Statistik, Software-SHA-256-Miner

#include <Arduino.h>
#include <WiFi.h>

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
    // Einzelne Fehlversuche kommen vor (beobachtet in Master 002); die
    // Notwerte kosten ~60 % Hashrate, daher mehrmals versuchen.
    bool calibrated = false;
    for (int attempt = 0; attempt < 3 && !calibrated; attempt++) calibrated = selftest_calibrate();
    if (!calibrated) {
        Serial.println("KALIBRIERUNG FEHLGESCHLAGEN - sichere Standardwerte werden genutzt.");
        hw_sha_set_timing(HW_TIMING_SAFE);
    }
    if (!selftest_run()) {
        Serial.println("SELBSTTEST FEHLGESCHLAGEN - Mining wird nicht gestartet.");
        for (;;) delay(1000);
    }
    selftest_benchmark();

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
#if ENABLE_SW_MINER
    xTaskCreatePinnedToCore(miner::sw_task, "minerSW", 4096, nullptr, 1, nullptr, SW_MINER_CORE);
#endif
}

void loop()
{
    vTaskDelete(nullptr);
}
