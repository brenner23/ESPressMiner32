#pragma once
#include <stdint.h>
#include "board.h"

// Display (ab 1.3.1-d1)
//  D1: Init + statisches Startbild
//  D2: Startbild mit Fortschrittsbalken, danach Dashboard; gezeichnet werden nur Felder,
//      deren Text sich geaendert hat (Rechteck in Blasenfarbe + neuer Text). Eigene Task auf Core 0.

// Messwerte des Zeichnens (fuer /api/stats, Messung D2)
struct DisplayStats {
    volatile uint32_t updates;      // Durchlaeufe des Dashboard-Tasks
    volatile uint32_t fields;       // davon neu gezeichnete Felder
    volatile uint32_t busyUs;       // Summe Zeichenzeit (us)
    volatile uint32_t maxUs;        // laengster Durchlauf (us)
};
extern DisplayStats g_disp;

#if HAS_DISPLAY
void display_begin();                                   // Pins, SPI, Init, Startbild
void display_progress(int percent, const char* line);   // Startbild: Balken + Statuszeile
void display_start_task();                              // Dashboard-Task (wartet auf Pool, dann Dashboard)
// ab 1.3.1-d3: Einstellungen fuer die Weboberflaeche (/api/display)
#include <ArduinoJson.h>
void        display_cfg_json(JsonDocument& d);          // aktuelle Werte + Statuszeile
const char* display_cfg_set(JsonDocument& j);           // speichern + sofort anwenden, nullptr = OK sonst Fehlertext
void        display_preview(int percent);               // Helligkeit beim Ziehen sofort zeigen (5 s)
#else
inline void display_begin() {}
inline void display_progress(int, const char*) {}
inline void display_start_task() {}
#endif
