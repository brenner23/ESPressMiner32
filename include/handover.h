#pragma once
#include <stdint.h>

// Werte ueber einen Neustart retten (ab 1.1.1.002 / Ghost Pod 3.1.0.002)
//
// Startet der ESP32 neu, OHNE vom Strom getrennt zu sein (WLAN-Waechter, Absturz, Watchdog,
// Update, EN-Taste), laufen Betriebszeit, Best Diff und Blockfunde weiter. Shares beginnen bei 0.
// Nach einer Stromtrennung (power-on, brownout) beginnt alles bei 0 - daran erkennt man,
// ob das Geraet die ganze Zeit in Betrieb war.
//
// Ablage im RTC-Speicher (RTC_NOINIT): uebersteht jeden Neustart, wird bei Stromtrennung
// ungueltig, kein Schreiben in den Flash. Beim Start: auslesen, loeschen, weiterverwenden.
namespace handover {

// Frueh in setup(), nach esp_reset_reason(): gerettete Werte in g_stats uebernehmen
void restore();
// Jede Sekunde vom Statistik-Task: aktuelle Werte ablegen (nur RAM)
void save(uint32_t uptimeS);
// Ab 1.3.1.002: Neustart, den der Nutzer ausloest (Speichern & Neustart, Update, Konsole):
// nichts uebergeben - danach beginnt alles bei 0. Nur Abbrueche (Watchdog, Absturz, WLAN-Waechter) zaehlen.
void forget();

uint32_t uptime_offset();   // Betriebszeit vor dem letzten Neustart (s)
uint32_t restarts();        // Neustarts seit der letzten Stromtrennung

}  // namespace handover
