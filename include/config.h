#pragma once
// ---------------------------------------------------------------------------
//  Allgemeine Einstellungen
// ---------------------------------------------------------------------------

#define MINER_NAME        "ESPressMiner32"
#if HW_LOOP_TEST
#define MINER_VERSION     "1.3.0.009-looptest"   // Messbetrieb: Varianten O/P/Q/R gegen J, mit Schutzfenster
#elif DIAG_NET
#define MINER_VERSION     "1.3.0.009-diag"   // Test-Build: Netz-Diagnose /api/diag (10-Minuten-Fehlerschub)
#else
#define MINER_VERSION     "1.3.0.011-pre4"   // Master 005, Pre 4: Pre 3 + Wellen-Fehler getrennt angezeigt (waveErrors)
#endif
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
// Master 002: aus, weil er den HW-Miner ueber den Bus um ~95 kH/s bremste.
// Master 003 (v1.2.1): an. Die v2-Schleife haelt >= 3 Takte zwischen SHA-Writes ein
// und verliert dadurch nur ~3 kH/s (gemessen: HW 959.6 + SW 30.4 = 990 kH/s).
#define ENABLE_SW_MINER          1

// Nonce-Aufteilung zwischen Hardware- und Software-Miner
#define HW_NONCE_START           0x00000000UL
#define HW_NONCE_END             0xE0000000UL   // exklusiv
#define SW_NONCE_START           0xE0000000UL
#define SW_NONCE_END             0xFFFFFFFFUL

// Nonces pro Durchlauf, bevor auf einen neuen Job geprueft wird
#define HW_BATCH_SIZE            8192
#define SW_BATCH_SIZE            1024

// HW-Miner (Core 1): Zwangspause von 1 Tick alle HW_YIELD_MS. 0 = keine Pause (ab 1.3.0.006).
// Auf Core 1 laeuft sonst nichts (loopTask beendet, Watchdog prueft IDLE1 nicht), die Pause
// kostete ~0,2 % (1 ms je 500 ms). Zurueck: 500.
#ifndef HW_YIELD_MS
#define HW_YIELD_MS              0
#endif

// Master 005 Pre 1: Interrupts auf Core 1 waehrend einer HW-Charge (HW_BATCH_SIZE Nonces, ~8 ms)
// sperren. Ziel: Tick-/IPC-Unterbrechungen kosten ~0,5 % (rechnerisch 1000,4, gemessen ~995).
// Danach kurz frei, damit wartende Interrupts laufen. 0 = aus (wie 1.3.0.009).
#ifndef HW_IRQ_LOCK
#define HW_IRQ_LOCK              1
#endif

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

// Master 004 Messbetrieb: >0 = Varianten der Hash-Schleife reihum je so viele Minuten
// minen und Hashrate / verworfene Kandidaten je Variante protokollieren (Konsole, /api/stats).
// 0 = Normalbetrieb.
#ifndef HW_LOOP_TEST
#define HW_LOOP_TEST             0
#endif

// Ueberhitzungsschutz (ab 1.3.0.004), Werte des internen Sensors (grob, +-10 Grad Versatz).
// Ab SW_PAUSE pausiert der Software-Miner, ab STOP das ganze Mining; unter RESUME laeuft
// alles wieder. Werte ausserhalb PLAUSIBLE gelten als Sensorfehler und werden ignoriert.
#ifndef TEMP_SW_PAUSE_C
#define TEMP_SW_PAUSE_C          90.0f
#endif
#ifndef TEMP_STOP_C
#define TEMP_STOP_C              100.0f
#endif
#ifndef TEMP_RESUME_C
#define TEMP_RESUME_C            80.0f
#endif
#define TEMP_PLAUSIBLE_MIN_C     (-20.0f)
#define TEMP_PLAUSIBLE_MAX_C     150.0f

// mDNS-Name (Standard fuer den Hostnamen)  ->  http://espressminer.local
#define WEB_MDNS_NAME            "espressminer"

// Einrichtungs-Portal: WLAN "ESPressMiner32-XXXX" (min. 8 Zeichen Passwort)
#define PORTAL_AP_PASSWORD       "espressminer"
// So lange wird beim Start auf das gespeicherte WLAN gewartet, dann Portal zusaetzlich an
#define WIFI_CONNECT_TIMEOUT_MS  20000UL

// WLAN-Waechter (ab 1.3.0.008): Nach einem Kanalwechsel im Mesh blieb 1.3.0.007 dauerhaft
// offline. Ist das WLAN so lange weg, wird neu verbunden (Suche ueber alle Kanaele) ...
#define WIFI_RECONNECT_AFTER_MS  10000UL
#define WIFI_RECONNECT_EVERY_MS  20000UL
// ... und hilft das nicht, startet der Miner neu.
#define WIFI_REBOOT_AFTER_MS     (3UL * 60UL * 1000UL)

// Schutzfenster (ab 1.3.0.008): Ein Geraet im Netz (Mesh/Router) stoert den ESP alle
// ~600 s fuer einen Augenblick (4-8 HW-Fehler auf einen Schlag). Der Miner lernt diesen
// Takt und rechnet um den erwarteten Zeitpunkt herum mit einer vorsichtigeren Schleife.
#ifndef HW_GUARD_ENABLE
#define HW_GUARD_ENABLE          1
#endif
#define HW_GUARD_BURST_ERRORS    2        // so viele HW-Fehler ...
#define HW_GUARD_BURST_MS        2000UL   // ... innerhalb dieser Zeit = Schub
#define HW_GUARD_PERIOD_MIN_MS   60000UL  // erlaubter Takt 1 .. 30 min (hier gemessen: 600,47 s)
#define HW_GUARD_PERIOD_MAX_MS   1800000UL
#define HW_GUARD_BEFORE_MS       6000UL   // Fenster vor ...
#define HW_GUARD_AFTER_MS        6000UL   // ... und nach dem erwarteten Schub
#ifndef HW_GUARD_PROBE_EVERY
#if HW_LOOP_TEST
#define HW_GUARD_PROBE_EVERY     1000000  // Messbetrieb: keine Stichprobe (Welle soll nie ins Ergebnis)
#else
#define HW_GUARD_PROBE_EVERY     6        // jedes 6. Fenster ungeschuetzt (Stichprobe: Welle noch da?)
#endif
#endif

// Blaue Onboard-LED: leuchtet, solange gemint wird; kurz aus bei jedem Share. -1 = aus
#define LED_PIN                  2
