#include "display.h"
#include "miner.h"
#include "sha256_hw.h"
#include "web_server.h"
#include "config.h"
#include <utility>

void wifi_note_swapped();   // main.cpp
#include "handover.h"
#include "settings.h"
#include "mining_job.h"
#include "stats.h"
#include "web_page.h"
#include "stratum_client.h"
#include "diag.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#if WEB_UPDATE_ENABLE
#include <Update.h>
#endif

static WebServer s_server(80);

static void handle_root()
{
    s_server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
}

static void handle_stats()
{
    JsonDocument doc;
    doc["hashrate"]    = g_stats.khCurrent;
    doc["average"]     = g_stats.khAverage;
    doc["uptime"]      = g_stats.uptimeS;
    doc["restarts"]    = handover::restarts();   // Neustarts seit der letzten Stromtrennung
    doc["totalHashes"] = (double)g_stats.totalHashes;
    doc["accepted"]    = g_stats.accepted;
    doc["submitted"]   = g_stats.submitted;
    doc["rejected"]    = g_stats.rejected;
    doc["hwErrors"]    = g_stats.hwErrors;
    doc["candidates"]  = g_stats.candidates;   // 16-Bit-Kandidaten (Echtheits-Check: ~Hashes/65536)
    doc["hwHashes"]    = (double)g_stats.hwHashes;
    doc["swHashes"]    = (double)g_stats.swHashes;
    doc["hwLoop"]      = hw_sha_loop_name();
    doc["hwCandidates"]= g_stats.hwCandidates;
    if (g_loopTest[0]) doc["loopTest"] = g_loopTest;
    doc["resetReason"] = g_resetReason;
    doc["chipTemp"]    = g_stats.chipTemp;
    doc["thermal"]     = thermal::text();
    doc["thermalLevel"]= thermal::level();
    if (g_variantBeforeReset >= 0) doc["variantBeforeReset"] = hw_sha_variant_name(g_variantBeforeReset);
    doc["bestDiff"]    = g_stats.bestDiff;
    doc["poolDiff"]    = jobs::difficulty();
    doc["netDiff"]     = g_stats.networkDiff;
    doc["blocks"]      = g_stats.blocksFound;
    doc["connected"]   = g_stats.poolConnected;
    doc["authorized"]  = g_stats.poolAuthorized;
    const PoolConfig& pc = g_settings.pools[g_stats.activePool];
    doc["pool"]        = pc.url;
    doc["poolName"]    = g_stats.activePool == 0 ? "Primary" : "Secondary";
    doc["user"]        = pc.user;
    doc["minerName"]   = g_settings.minerName;
    doc["hostname"]    = g_settings.hostname;
    doc["hasSecondary"]= g_settings.pools[1].url.length() > 0;
    doc["poolAltMin"]  = g_settings.poolAltMin;
    doc["portal"]      = web_portal_active();
    doc["ssid"]        = WiFi.isConnected() ? WiFi.SSID() : g_settings.wifiSsid;   // ab .003: das verbundene Netz
    doc["wifiNet"]     = g_stats.wifiNet;           // 1 oder 2
    doc["noPool"]      = g_settings.pools[0].url.isEmpty() && g_settings.pools[1].url.isEmpty();
    doc["ip"]          = WiFi.localIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    doc["channel"]     = WiFi.channel();
    doc["wifiDrops"]   = g_stats.wifiDrops;
    doc["wifiReconnects"] = g_stats.wifiReconnects;
    doc["wifiLastReason"] = g_stats.wifiLastReason;
    doc["dispUpdates"] = g_disp.updates;     // ab 1.3.1-d2: Messung Display-Kosten
    doc["dispFields"]  = g_disp.fields;
    doc["dispAvgUs"]   = g_disp.updates ? g_disp.busyUs / g_disp.updates : 0;
    doc["dispMaxUs"]   = g_disp.maxUs;
    doc["guardActive"] = hw_sha_guard_active();
    doc["guardPeriodS"]= g_stats.guardPeriodMs / 1000.0f;
    doc["guardNextInS"]= g_stats.guardNextInS;
    doc["guardBursts"] = g_stats.guardBursts;
    doc["guardWindows"]= g_stats.guardWindows;
    doc["guardErrors"] = g_stats.guardErrors;
    doc["waveErrors"]  = g_stats.waveErrors;    // Pre 4: Anteil von hwErrors aus WLAN-Wellen
    doc["loopErrors"]  = g_stats.hwErrors - g_stats.waveErrors;   // echte Fehler der Schleife
    doc["showWave"]    = SHOW_WAVE_ERRORS ? true : false;          // Dashboard: Wellen-Teil anzeigen?
    doc["heap"]        = ESP.getFreeHeap();
    doc["version"]     = MINER_NAME " v" MINER_VERSION;

    // Verlauf in zeitlicher Reihenfolge (aelteste zuerst)
    JsonArray h = doc["history"].to<JsonArray>();
    uint16_t n = g_stats.historyCount;
    uint16_t start = (g_stats.historyHead + STATS_HISTORY_LEN - n) % STATS_HISTORY_LEN;
    for (uint16_t i = 0; i < n; i++) h.add(roundf(g_stats.history[(start + i) % STATS_HISTORY_LEN] * 10) / 10);

    String out;
    serializeJson(doc, out);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", out);
}

// Einstellungen lesen. Das WLAN-Passwort wird nie ausgeliefert.
static void handle_config_get()
{
    JsonDocument doc;
    settings_to_json(doc.to<JsonObject>());
    String out;
    serializeJson(doc, out);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", out);
}

// WLAN-Netze in der Umgebung suchen (fuer das Einrichtungs-Portal).
static void handle_scan()
{
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray nets = doc["nets"].to<JsonArray>();
    for (int i = 0; i < n && nets.size() < 30; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        bool dup = false;
        for (JsonObject o : nets)
            if (ssid == o["ssid"].as<const char*>()) { dup = true; break; }
        if (dup) continue;
        JsonObject o = nets.add<JsonObject>();
        o["ssid"] = ssid;
        o["rssi"] = WiFi.RSSI(i);
        o["lock"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", out);
}

// Einstellungen speichern und neu starten. Leeres WLAN-Passwort = unveraendert.
static void handle_config_post()
{
    JsonDocument doc;
    if (deserializeJson(doc, s_server.arg("plain"))) {
        s_server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    Settings s = g_settings;
    settings_apply_json(doc.as<JsonVariantConst>(), s);
    if (const char* error = settings_validate(s)) {
        s_server.send(400, "text/plain", error);
        return;
    }
    if (!settings_save(s)) {
        s_server.send(500, "text/plain", "Saving failed");
        return;
    }

    s_server.send(200, "text/plain", "Saved - restarting ...");
    Serial.println("[Web] Settings saved, restarting ...");
    handover::forget();      // vom Nutzer gewollt -> kein Neustart-Zaehler
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
}

#if WEB_UPDATE_ENABLE
// Kennung, an der eine passende Firmware erkannt wird (steht so in jeder Firmware dieses Boards)
static const char FW_TAG[] __attribute__((used)) = FW_BOARD_TAG;
static bool   s_updOk = false;
static String s_updErr;
static size_t s_updTotal = 0;
static char   s_tail[48];          // Ende des vorigen Stuecks (Kennung kann ueber die Grenze gehen)
static size_t s_tailLen = 0;
static bool   s_tagSeen = false;

static bool find_tag(const uint8_t* buf, size_t len)
{
    static const size_t T = sizeof(FW_BOARD_TAG) - 1;
    uint8_t tmp[sizeof(s_tail) + 1460];
    size_t n = 0;
    memcpy(tmp, s_tail, s_tailLen); n = s_tailLen;
    const size_t take = len < sizeof(tmp) - n ? len : sizeof(tmp) - n;
    memcpy(tmp + n, buf, take); n += take;
    bool found = false;
    for (size_t i = 0; i + T <= n && !found; i++) found = memcmp(tmp + i, FW_BOARD_TAG, T) == 0;
    s_tailLen = n < sizeof(s_tail) ? n : sizeof(s_tail);
    memcpy(s_tail, tmp + n - s_tailLen, s_tailLen);
    return found;
}

static void handle_update_upload()
{
    HTTPUpload& up = s_server.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[Update] receiving %s - mining paused\n", up.filename.c_str());
        miner::g_paused = true;
        vTaskDelay(pdMS_TO_TICKS(300));       // laufende Charge zu Ende rechnen lassen
        s_updOk = false; s_updErr = ""; s_updTotal = 0; s_tailLen = 0; s_tagSeen = false;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) s_updErr = String("Update start failed: ") + Update.errorString();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!s_updErr.isEmpty()) return;
        if (s_updTotal == 0 && (up.currentSize == 0 || up.buf[0] != 0xE9)) {
            s_updErr = "Not an ESP32 firmware file (.bin)";
            Update.abort();
            return;
        }
        if (!s_tagSeen) s_tagSeen = find_tag(up.buf, up.currentSize);
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            s_updErr = String("Write failed: ") + Update.errorString();
            Update.abort();
            return;
        }
        s_updTotal += up.currentSize;
    } else if (up.status == UPLOAD_FILE_END) {
        if (!s_updErr.isEmpty()) { Serial.printf("[Update] failed: %s\n", s_updErr.c_str()); return; }
        if (!s_tagSeen) {
            s_updErr = "Wrong firmware: this file is not for this board (" FW_BOARD_TAG " missing)";
            Update.abort();
            Serial.println("[Update] rejected - board tag missing");
            return;
        }
        if (Update.end(true)) {
            s_updOk = true;
            Serial.printf("[Update] %u bytes written and verified\n", (unsigned)s_updTotal);
        } else {
            s_updErr = String("Update failed: ") + Update.errorString();
            Serial.printf("[Update] failed: %s\n", s_updErr.c_str());
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        s_updErr = "Upload aborted";
        miner::g_paused = false;
        Serial.println("[Update] aborted - mining continues");
    }
}
#endif

// ---------------------------------------------------------------------------
//  Einrichtungs-Portal (Access Point + Captive Portal)
// ---------------------------------------------------------------------------

static const IPAddress PORTAL_IP(192, 168, 4, 1);
static DNSServer       s_dns;
static volatile bool   s_portal = false;
static bool            s_portalKeepSta = false;

bool web_portal_active() { return s_portal; }

void web_start_portal(bool keepSta)
{
    uint64_t mac = ESP.getEfuseMac();
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", MINER_NAME, (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

    WiFi.mode(keepSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(PORTAL_IP, PORTAL_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid, PORTAL_AP_PASSWORD);
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", PORTAL_IP);
    s_portalKeepSta = keepSta;
    s_portal = true;

    Serial.println();
    Serial.println("==============================================");
    Serial.println("  SETUP PORTAL ACTIVE");
    Serial.printf("  Wi-Fi:    %s\n", ssid);
    Serial.printf("  Password: %s\n", PORTAL_AP_PASSWORD);
    Serial.println("  Page:     http://192.168.4.1/");
    Serial.println("  (or via serial console / tools/espressminer_setup.py)");
    Serial.println("==============================================");
}

static void stop_portal()
{
    s_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_portal = false;
    Serial.println("[Web] Wi-Fi connected - setup portal closed");
}

// Im Portal-Modus leiten unbekannte URLs (z.B. die Captive-Portal-Pruefung von
// Android/iOS/Windows) auf die Einstellungsseite um, damit sie sich selbst oeffnet.
static void handle_not_found()
{
    if (s_portal && s_server.hostHeader() != PORTAL_IP.toString()) {
        s_server.sendHeader("Location", "http://192.168.4.1/", true);
        s_server.send(302, "text/plain", "");
        return;
    }
    s_server.send(404, "text/plain", "Not found");
}

static void start_mdns()
{
    if (MDNS.begin(g_settings.hostname.c_str())) MDNS.addService("http", "tcp", 80);
    Serial.printf("[Web] Dashboard: http://%s/  or  http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), g_settings.hostname.c_str());
}

void web_task(void*)
{
    // Warten, bis entweder das WLAN steht oder das Portal laeuft
    while (WiFi.status() != WL_CONNECTED && !s_portal) vTaskDelay(pdMS_TO_TICKS(200));

    s_server.on("/", handle_root);
    s_server.on("/api/stats", handle_stats);
    s_server.on("/api/diag", HTTP_GET, [] {
        s_server.send(200, "application/json", diag_json(s_server.arg("since").toInt()));
    });
#if DIAG_NET
    // Test des Schutzfensters: gespielter Fehlerschub (zaehlt nicht in hwErrors)
    s_server.on("/api/guardsim", HTTP_GET, [] {
        const int n = s_server.hasArg("n") ? s_server.arg("n").toInt() : 3;
        guard::g_simErrors += n > 0 && n < 50 ? n : 3;
        s_server.send(200, "text/plain", "ok");
    });
#endif
    s_server.on("/api/config", HTTP_GET, handle_config_get);
    s_server.on("/api/config", HTTP_POST, handle_config_post);
    s_server.on("/api/scan", HTTP_GET, handle_scan);
    // ab .003: Wi-Fi 1 und Wi-Fi 2 dauerhaft tauschen (ohne Neustart, Verbindung bleibt)
    s_server.on("/api/swapwifi", HTTP_POST, [] {
        if (g_settings.wifiSsid2.isEmpty()) {
            s_server.send(400, "text/plain", "No Wi-Fi 2 configured");
            return;
        }
        Settings s = g_settings;
        std::swap(s.wifiSsid, s.wifiSsid2);
        std::swap(s.wifiPass, s.wifiPass2);
        if (!settings_save(s)) {
            s_server.send(500, "text/plain", "Saving failed");
            return;
        }
        wifi_note_swapped();
        Serial.printf("[Web] Wi-Fi swapped - Wi-Fi 1 is now \"%s\"\n", s.wifiSsid.c_str());
        s_server.send(200, "text/plain", "Wi-Fi 1 and 2 swapped and saved");
    });
    // ab 1.3.1.002: nur Wi-Fi 2 speichern, ohne Neustart (wirkt beim naechsten Verbindungsversuch)
    s_server.on("/api/wifi2", HTTP_POST, [] {
        JsonDocument in;
        if (deserializeJson(in, s_server.arg("plain"))) {
            s_server.send(400, "text/plain", "Invalid JSON");
            return;
        }
        Settings s = g_settings;
        s.wifiSsid2 = (const char*)(in["wifiSsid2"] | "");
        s.wifiSsid2.trim();
        const char* pw = in["wifiPass2"] | "";
        if (pw[0]) s.wifiPass2 = pw;
        if (s.wifiSsid2.isEmpty()) s.wifiPass2 = "";
        if (!settings_save(s)) {
            s_server.send(500, "text/plain", "Saving failed");
            return;
        }
        Serial.printf("[Web] Wi-Fi 2 saved: \"%s\" (no restart)\n", s.wifiSsid2.c_str());
        s_server.send(200, "text/plain", s.wifiSsid2.isEmpty() ? "Wi-Fi 2 removed" : "Wi-Fi 2 saved");
    });
#if WEB_UPDATE_ENABLE
    // ab 1.3.1.002-test: Firmware-Upload (.bin). Mining pausiert, Datei wird geprueft
    // (ESP-Image + Board-Kennung FW_BOARD_TAG), erst dann umgeschaltet und neu gestartet.
    s_server.on("/api/update", HTTP_POST, [] {
        const bool ok = !Update.hasError() && s_updOk;
        s_server.send(ok ? 200 : 400, "text/plain", ok ? "Update OK - restarting ..." : s_updErr);
        if (ok) {
            Serial.println("[Update] done - restarting");
            handover::forget();
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }
        miner::g_paused = false;
    }, handle_update_upload);
#endif
    // Klapp-Zustand der Settings-Abschnitte (Bitmuster), im NVS - gilt fuer Handy und PC gleich
    s_server.on("/api/fold", HTTP_POST, [] {
        settings_save_fold((uint16_t)s_server.arg("b").toInt());
        s_server.send(200, "text/plain", "ok");
    });
    s_server.on("/api/switchpool", HTTP_POST, [] {
        if (g_settings.pools[1].url.isEmpty()) {
            s_server.send(400, "text/plain", "No secondary pool configured");
            return;
        }
        // Ab 1.1.1.002: echter Tausch ohne Neustart - Primary und Secondary werden in den Settings
        // vertauscht, gespeichert und der neue Primary sofort verbunden (erledigt der Stratum-Task)
        stratum_request_swap();
        s_server.send(200, "text/plain", "Pools swapped and saved");
    });
#if HAS_DISPLAY
    // ab 1.3.1-d3: Display-Einstellungen, wirken sofort (kein Neustart)
    s_server.on("/api/display", HTTP_GET, [] {
        JsonDocument d;
        display_cfg_json(d);
        String out;
        serializeJson(d, out);
        s_server.send(200, "application/json", out);
    });
    s_server.on("/api/display", HTTP_POST, [] {
        JsonDocument j;
        if (deserializeJson(j, s_server.arg("plain"))) { s_server.send(400, "text/plain", "Invalid JSON"); return; }
        if (const char* err = display_cfg_set(j)) { s_server.send(400, "text/plain", err); return; }
        s_server.send(200, "text/plain", "Saved");
    });
    s_server.on("/api/display/preview", HTTP_GET, [] {      // Vorlage: /brightness?value=
        display_preview(s_server.arg("value").toInt());
        s_server.send(200, "text/plain", "OK");
    });
#endif
    s_server.onNotFound(handle_not_found);
    s_server.begin();

    bool mdnsStarted = false;
    for (;;) {
        s_server.handleClient();
        if (s_portal) s_dns.processNextRequest();

        if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
            start_mdns();
            mdnsStarted = true;
        }
        if (s_portal && s_portalKeepSta && WiFi.status() == WL_CONNECTED) stop_portal();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
