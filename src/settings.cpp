#include "settings.h"
#include "config.h"
#include "credentials.h"

#include <Preferences.h>

#ifndef POOL2_URL
#define POOL2_URL      ""
#define POOL2_USER     ""
#define POOL2_PASSWORD "x"
#endif

Settings g_settings;

static const char* NVS_NAMESPACE = "espressminer";
// Frueherer Projektname: vorhandene Einstellungen werden einmalig uebernommen
static const char* NVS_LEGACY    = "claudeminer";
static bool        s_configured  = false;

// Liest die Einstellungen aus einem Namespace; fehlende Werte kommen aus credentials.h
static bool load_from(const char* ns)
{
    Preferences p;
    p.begin(ns, true);
    bool configured          = p.getBool("cfg", false) || p.isKey("ssid");
    g_settings.minerName     = p.getString("mname", "ESP32 Miner");
    g_settings.hostname      = p.getString("host", WEB_MDNS_NAME);
    g_settings.wifiSsid      = p.getString("ssid", WIFI_SSID);
    g_settings.wifiPass      = p.getString("wpass", WIFI_PASSWORD);
    g_settings.pools[0].url  = p.getString("p1url", POOL_URL);
    g_settings.pools[0].user = p.getString("p1user", POOL_USER);
    g_settings.pools[0].pass = p.getString("p1pass", POOL_PASSWORD);
    g_settings.pools[1].url  = p.getString("p2url", POOL2_URL);
    g_settings.pools[1].user = p.getString("p2user", POOL2_USER);
    g_settings.pools[1].pass = p.getString("p2pass", POOL2_PASSWORD);
    g_settings.poolAltMin    = p.getUShort("paltm", 0);
    p.end();
    return configured;
}

static void clear_ns(const char* ns)
{
    Preferences p;
    if (p.begin(ns, false)) {
        p.clear();
        p.end();
    }
}

bool settings_load()
{
    s_configured = load_from(NVS_NAMESPACE);
    if (!s_configured && load_from(NVS_LEGACY)) {
        settings_save(g_settings);
        clear_ns(NVS_LEGACY);
        Serial.println("[Settings] Settings taken over from old namespace");
    } else if (!s_configured) {
        load_from(NVS_NAMESPACE);   // Vorbelegung aus credentials.h
    }
    return s_configured;
}

bool settings_configured() { return s_configured; }

bool settings_save(const Settings& s)
{
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false)) return false;
    p.putString("mname", s.minerName);
    p.putString("host", s.hostname);
    p.putString("ssid", s.wifiSsid);
    p.putString("wpass", s.wifiPass);
    p.putString("p1url", s.pools[0].url);
    p.putString("p1user", s.pools[0].user);
    p.putString("p1pass", s.pools[0].pass);
    p.putString("p2url", s.pools[1].url);
    p.putString("p2user", s.pools[1].user);
    p.putString("p2pass", s.pools[1].pass);
    p.putUShort("paltm", s.poolAltMin);
    p.putBool("cfg", true);
    p.end();
    g_settings = s;
    s_configured = true;
    return true;
}

void settings_clear()
{
    clear_ns(NVS_NAMESPACE);
    clear_ns(NVS_LEGACY);
    s_configured = false;
}

static void assign_trimmed(String& dst, JsonVariantConst v)
{
    if (v.isNull()) return;
    dst = v.as<const char*>() ? v.as<const char*>() : "";
    dst.trim();
}

void settings_apply_json(JsonVariantConst j, Settings& s)
{
    assign_trimmed(s.minerName, j["minerName"]);
    assign_trimmed(s.hostname, j["hostname"]);
    assign_trimmed(s.wifiSsid, j["wifiSsid"]);
    if (!j["poolAltMin"].isNull()) s.poolAltMin = j["poolAltMin"].as<uint16_t>();
    const char* wpass = j["wifiPass"] | "";
    if (wpass[0]) s.wifiPass = wpass;
    for (int i = 0; i < 2; i++) {
        JsonVariantConst o = j["pools"][i];
        if (o.isNull()) continue;
        assign_trimmed(s.pools[i].url, o["url"]);
        assign_trimmed(s.pools[i].user, o["user"]);
        assign_trimmed(s.pools[i].pass, o["pass"]);
        if (s.pools[i].pass.isEmpty()) s.pools[i].pass = "x";
    }
}

static bool valid_pool_url(const String& url)
{
    return url.isEmpty() || (url.startsWith("stratum+tcp://") && url.lastIndexOf(':') > 14);
}

const char* settings_validate(const Settings& s)
{
    if (s.minerName.isEmpty())                      return "Display name missing";
    if (s.hostname.isEmpty())                       return "Hostname missing";
    if (s.wifiSsid.isEmpty())                       return "Wi-Fi SSID missing";
    if (s.pools[0].url.isEmpty())                   return "Primary pool URL missing";
    if (!valid_pool_url(s.pools[0].url) || !valid_pool_url(s.pools[1].url))
        return "Pool URL must look like: stratum+tcp://host:port";
    if (s.pools[0].user.isEmpty())                  return "Wallet address (primary) missing";
    if (s.pools[1].url.length() && s.pools[1].user.isEmpty())
        return "Wallet address (secondary) missing";
    return nullptr;
}

void settings_to_json(JsonObject o)
{
    o["minerName"]   = g_settings.minerName;
    o["hostname"]    = g_settings.hostname;
    o["wifiSsid"]    = g_settings.wifiSsid;
    o["wifiPassSet"] = g_settings.wifiPass.length() > 0;
    o["configured"]  = s_configured;
    o["poolAltMin"]  = g_settings.poolAltMin;
    JsonArray pools = o["pools"].to<JsonArray>();
    for (const PoolConfig& pc : g_settings.pools) {
        JsonObject po = pools.add<JsonObject>();
        po["url"]  = pc.url;
        po["user"] = pc.user;
        po["pass"] = pc.pass;
    }
}
