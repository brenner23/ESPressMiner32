#pragma once
// Dauerhafte Zaehler im NVS (ab 1.3.1-d10):
//  - Best Difficulty bleibt ueber Stromausfall erhalten (Haken "Keep highest difficulty", Standard an)
//  - Betriebszeit gesamt seit dem ersten Start dieser Firmware-Familie (alle 10 min gespeichert)
// Flash wird nur bei neuem Rekord (hoechstens 1x pro Minute) und alle 10 min fuer die Betriebszeit beschrieben.
#include <stdint.h>
#include <ArduinoJson.h>

namespace persist {
void     begin();                    // nach handover::restore(): gespeicherten Rekord uebernehmen
void     tick(uint32_t uptimeS);     // aus stats_task (jede Sekunde ok)
bool     keep_best();
uint32_t op_seconds();               // Betriebszeit gesamt
void     to_json(JsonDocument& d);
const char* set(JsonDocument& j);    // {"keepBest":bool} {"resetBest":true} {"best":42.98} (nur hoeher)
}
