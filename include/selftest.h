#pragma once
#include <stdint.h>

// Prueft Software- und Hardware-SHA anhand des Bitcoin-Genesis-Blocks
// sowie tausender zufaelliger Nonces. Liefert false bei Fehler.
bool selftest_run();

// Misst die reine Hashrate von HW- und SW-Pfad (ca. 2 Sekunden)
void selftest_benchmark();

// Sucht die kleinsten fehlerfreien Wartezeiten der HW-Pipeline und stellt sie
// mit Sicherheitsabstand ein (HW_CAL_MARGIN_* in config.h).
bool selftest_calibrate();

// Entwicklung: Fehlerrate und Tempo fuer Reserve 0..4 Takte messen
void selftest_margin_sweep();
void selftest_long_test(uint32_t margin, uint32_t minutes);
