#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Laufzeit-Einstellungen, gespeichert im Flash (NVS, Namespace "espressminer").
// Solange nichts gespeichert ist, startet der Miner das Einrichtungs-Portal; die
// Werte aus credentials.h dienen dann nur als Vorbelegung des Formulars.

struct PoolConfig {
    String url;    // stratum+tcp://host:port  (leer = nicht konfiguriert)
    String user;   // Wallet-Adresse[.Worker]
    String pass;
};

struct Settings {
    String     minerName;  // Anzeigename oben auf dem Dashboard
    String     hostname;   // Netzwerkname (mDNS)
    String     wifiSsid;
    String     wifiPass;
    String     wifiSsid2;  // ab .003: zweites WLAN (optional, leer = keins), wird mitgeprueft wenn WLAN 1 fehlt
    String     wifiPass2;
    PoolConfig pools[2];   // [0] = Primary, [1] = Secondary
    uint16_t   poolAltMin; // 0 = Failover (Standard), sonst: alle N Minuten Pool wechseln
};

extern Settings g_settings;

// Laedt die Einstellungen. true = es liegt eine gespeicherte Konfiguration im NVS.
bool        settings_load();
bool        settings_configured();
bool        settings_save(const Settings& s);
void        settings_clear();
// Auf-/Zuklapp-Zustand der Abschnitte in den Settings (Bit i = Abschnitt i offen, 0xFFFF = Standard)
uint16_t    settings_fold();
void        settings_save_fold(uint16_t bits);

// Uebernimmt Felder aus JSON (Format wie /api/config). Ein leeres "wifiPass"
// laesst das bisherige Passwort unveraendert.
void        settings_apply_json(JsonVariantConst j, Settings& s);
// nullptr = gueltig, sonst eine Fehlermeldung
const char* settings_validate(const Settings& s);
// Schreibt die Einstellungen ohne WLAN-Passwort als JSON
void        settings_to_json(JsonObject o);
