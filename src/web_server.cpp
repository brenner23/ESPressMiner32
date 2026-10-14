#include "miner.h"
#include "sha256_hw.h"
#include "web_server.h"
#include "config.h"
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
    doc["ssid"]        = g_settings.wifiSsid;
    doc["ip"]          = WiFi.localIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    doc["channel"]     = WiFi.channel();
    doc["wifiDrops"]   = g_stats.wifiDrops;
    doc["wifiReconnects"] = g_stats.wifiReconnects;
    doc["wifiLastReason"] = g_stats.wifiLastReason;
    doc["guardActive"] = hw_sha_guard_active();
    doc["guardPeriodS"]= g_stats.guardPeriodMs / 1000.0f;
    doc["guardNextInS"]= g_stats.guardNextInS;
    doc["guardBursts"] = g_stats.guardBursts;
    doc["guardWindows"]= g_stats.guardWindows;
    doc["guardErrors"] = g_stats.guardErrors;
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
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
}

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
    s_server.on("/api/switchpool", HTTP_POST, [] {
        if (g_settings.pools[1].url.isEmpty()) {
            s_server.send(400, "text/plain", "No secondary pool configured");
            return;
        }
        stratum_request_switch();
        s_server.send(200, "text/plain", "Switching pool");
    });
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
