#pragma once
// ---------------------------------------------------------------------------
//  Allgemeine Einstellungen
// ---------------------------------------------------------------------------

#define MINER_NAME        "ESPressMiner32"
#define MINER_VERSION     "1.2.0"   // Master 003: v2-Hash-Schleife
#define MINER_REPO_URL    "https://github.com/brenner23/ESPressMiner32"

// Difficulty, die beim Pool angefragt wird (mining.suggest_difficulty).
// Bei ~1 MH/s ergibt 0.0005 etwa alle 8-9 Sekunden einen Share.
#define POOL_SUGGEST_DIFFICULTY  0.0005

// Pool-Verbindung gilt als tot, wenn so lange nichts empfangen wurde
#define STRATUM_RX_TIMEOUT_MS    (5UL * 60UL * 1000UL)
#define STRATUM_RECONNECT_MS     5000UL

// Statusausgabe auf der seriellen Konsole
#define STATS_INTERVAL_MS        5000UL

// Core-Zuordnung: Hardware-SHA-Miner exklusiv auf Core 1,
// WLAN/Stratum + Software-Miner auf Core 0
#define HW_MINER_CORE            1
#define SW_MINER_CORE            0
// Aus: Der SW-Miner bringt nur ~24 kH/s, bremst aber den HW-Miner ueber den
// gemeinsamen Bus um ~95 kH/s (gemessen mit der Schleife aus Master 002).
#define ENABLE_SW_MINER          0

// Nonce-Aufteilung zwischen Hardware- und Software-Miner
#define HW_NONCE_START           0x00000000UL
#define HW_NONCE_END             0xE0000000UL   // exklusiv
#define SW_NONCE_START           0xE0000000UL
#define SW_NONCE_END             0xFFFFFFFFUL

// Nonces pro Durchlauf, bevor auf einen neuen Job geprueft wird
#define HW_BATCH_SIZE            8192
#define SW_BATCH_SIZE            1024

// Sicherheitsabstand (CPU-Takte) auf die beim Start gemessenen Grenzwerte
// der Hardware-Pipeline. Kleiner = schneller, groesser = mehr Reserve.
#define HW_CAL_MARGIN_BLOCK      1
#define HW_CAL_MARGIN_LOAD       1

// Hash-Schleife in Assembler (src/hw_scan_asm.S) statt C. 0 = C-Version
// Gemessen: Assembler 255 Takte/Hash, C 249. Die dichter gepackten Writes stauen
// sich am Bus (jeder Takt Pause nach START kostet genau einen Takt) -> C bleibt aktiv.
// Hinweis Master 003: hw_scan_asm.S zaehlt die Nonce noch byte-getauscht und passt
// nicht mehr zur T[3]-Wort-Zaehlung -> nicht einschalten.
#define HW_USE_ASM               0
// Zusaetzliche Takte zwischen START von Block 1 und dem ersten Block-2-Write
// (die Engine liest T[0..7] erst nach START ein). Die Kalibrierung prueft das mit.
#define HW_ASM_LOOP_GUARD        0

// mDNS-Name (Standard fuer den Hostnamen)  ->  http://espressminer.local
#define WEB_MDNS_NAME            "espressminer"

// Einrichtungs-Portal: WLAN "ESPressMiner32-XXXX" (min. 8 Zeichen Passwort)
#define PORTAL_AP_PASSWORD       "espressminer"
// So lange wird beim Start auf das gespeicherte WLAN gewartet, dann Portal zusaetzlich an
#define WIFI_CONNECT_TIMEOUT_MS  20000UL

// Blaue Onboard-LED: leuchtet, solange gemint wird; kurz aus bei jedem Share. -1 = aus
#define LED_PIN                  2
