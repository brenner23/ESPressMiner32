# ESPressMiner32 – Versionen / Changelog

Messwerte: ESP32-D0WD-V3 (rev 3.1), 240 MHz, DevKit USB-C, eigener Stratum-Pool.

## v1.3.1-d4 – Display-Version, Branch `display` (2026-10-16) – Teststand
- Neue Envs `nmtv154` (NM-TV 154, ST7789 240x240) und `cyd` (CYD, ST7789 320x240), `include/board.h` mit Pins.
- Dashboard: nur geaenderte Felder neu (alle 5 s, Task auf Core 0, ~29 ms), Startbalken mit Status, IP 3 s,
  Hashrate ohne Nachkomma (abgeschnitten). HW-Schleifen an denselben Adressen wie v1.3.1 (`hw_*` gleich),
  gemessen 1033–1036 kH/s, 0 Schleifenfehler.
- Einstellungen „Display“: 3 Designs, Helligkeit mit Vorschau, NM-TV Tag/Nacht, CYD Drehung + Auto light (LDR GPIO 34).
- Backlight per ESP-IDF-LEDC (Arduino-`ledcAttach` sprengte das volle IRAM); Messschleifen P/Q/R nur noch mit
  `HW_LOOP_TEST` (liegen hinter O, keine genutzte Schleife verschiebt sich).
- Schriften (Liberation Sans Narrow Bold, GPLv2 + Font-Ausnahme) nicht im Repo: `tools/ttf2gfx.py` erzeugt sie.

## v1.3.1 – Master 005 (2026-10-16) – getestet, ~1034 kH/s
- Code = 1.3.0.011-pre4 nach Nachtlauf (9,6 h am USB-Solar-Laderegler): Ø **1033,7 kH/s** (nie unter 1032),
  0 Neustarts, 0 WLAN-Abbrueche, Shares 851/0, **2 Fehler der Schleife** + 73 aus 10 WLAN-Wellen (9 davon Stichprobe),
  alle 48 geschuetzten Fenster 0 Fehler. Pool 8 h: 1045 ±39 kH/s.
- Neu: Schalter `SHOW_WAVE_ERRORS` (ganz oben in config.h, Standard 0): Dashboard/Konsole zeigen nur die Fehler der
  Hash-Schleife; mit 1 zusaetzlich „(+73 from 10 Wi-Fi waves)“. /api/stats zaehlt immer (`showWave` neu).
- Screenshot docs/dashboard_v1.3.1.png (Werte nach 9 h 44 min, geschwaerzt).

## v1.3.0.011 – Master 005 (2026-10-15) – Vorabversion (pre4), ~1035 kH/s
Schritte pre1–pre4 im Detail mit Messwerten: MESSPROTOKOLL.md (Abschnitt Master 005).
- **pre1:** Interrupts auf Core 1 waehrend jeder HW-Charge (8192 Nonces, ~8 ms) gesperrt (`HW_IRQ_LOCK`). +~2 kH/s.
- **pre3:** Schneller Software-Scan auf Core 0 (Runden 0..2 vorberechnet, feste W-Woerter eingerechnet, Hash 2 nach
  Runde 60 beendet; beim Start gegen den Referenzpfad geprueft): SW 30 -> **35 kH/s**.
- **pre3: HW-Schleifen festgenagelt** (`HW_LOOP_PIN` in hw_scan_v2.S). Erkenntnis: Die HW-Schleifen reagieren auf ihre
  Lage im IRAM. Zusaetzlicher Code davor verschob sie und verursachte ~10 % HW-Fehler (so scheiterte auch 1.3.0.010) –
  bewiesen mit einer Gegenprobe (schneller Pfad aus, Fehler blieben). Jetzt liegt der Block immer auf demselben
  Versatz wie in der gemessenen Version.
- **pre4:** Fehler aus WLAN-Wellen werden getrennt gezaehlt und angezeigt (Messhilfe, noch in Arbeit):
  Dashboard „2 discarded (+8 from 1 Wi-Fi wave)“, /api/stats `waveErrors`, `loopErrors`; `hwErrors` bleibt die Gesamtzahl.
- Gemessen (Hauptboard, 10 min): **1034,7 kH/s** (HW 999,8 + SW 34,9), 0 Fehler der Schleife, 1 WLAN-Welle (8)
  vor dem Lernen des Takts. Pool-Gegenprobe 24 h mit 1.3.0.009: 1062 ±23 kH/s angekommen bei 1024 Anzeige.

## v1.3.0.009 – Master 004 (2026-10-14) – Schleife O, ~1024 kH/s
- Hash-Schleife **O** (J mit einem Takt weniger vor dem LOAD von Block 2, 240 Takte) als Hauptschleife,
  Rueckfall O -> J -> A -> +2 -> breit. **~1024 kH/s** (HW ~995 + SW ~30).
- Messbetrieb J/O/P/Q/R: O 0 eigene Fehler; P, Q, R (je ein weiterer Takt weniger) scheitern schon am
  Selbsttest. Damit stehen alle Wartezeiten der SHA-Engine am Limit (Tabelle im MESSPROTOKOLL).
- Messbetrieb trennt jetzt Fehler der Schleife von Fehlern der WLAN-Welle.

## v1.3.0.008 – Master 004 (2026-10-14) – Entwicklungsstand (noch kein Release v1.3.0)
Schritte 1.3.0.001–.008 im Detail mit Messwerten: MESSPROTOKOLL.md.
- Hash-Schleife **J** (Header-Woerter in Registern, 241 Takte) als Hauptschleife, Rueckfall J -> A -> +2 -> breit.
  **~1020 kH/s** (HW ~990 + SW ~30).
- Laufzeit-Kontrolle mit zwei Fenstern: kurze Fehlerschuebe werfen J nicht mehr auf eine langsamere Stufe.
- Chip-Temperatur + Ueberhitzungsschutz (SW-Miner pausiert / Mining stoppt / Wiederanlauf mit Hysterese).
- Core 1 ohne Zwangspause (+1,3 kH/s). Reset-Grund und Variante vor dem Neustart in /api/stats.
- Alle Ausgaben (Web, API, Konsole) auf Englisch.
- **WLAN-Waechter:** verbindet nach WLAN-Verlust selbst neu (alle Kanaele, staerkster Mesh-Knoten), Neustart
  nach 3 min offline. 1.3.0.007 blieb nach einem Kanalwechsel im Mesh dauerhaft offline.
- **Schutzfenster:** die verbleibenden HW-Fehler kamen als Schub alle ~600 s von der WLAN-Verbindung (periodische
  Aktion des Access Points, kein IP-Verkehr). Die Firmware lernt den Takt und rechnet um den erwarteten Zeitpunkt
  12 s lang mit der vorsichtigeren Schleife (~1,2 kH/s). Erstes Fenster im Test: 0 statt 6–7 Fehler.
  Falsche Hashes gingen auch vorher nie an den Pool – jeder Kandidat wird in Software nachgerechnet.
- Diagnose-Build `pio run -e diag` (/api/diag, tools/diag_collect.py) fuer eigene Netz-Untersuchungen.

## v1.2.1 – Master 003 (2026-10-13) – Software-Miner wieder an, ~990 kH/s
- Software-Miner auf Core 0 eingeschaltet (`ENABLE_SW_MINER 1`).
- Gemessen: HW 959,6 + SW 30,4 = **~990 kH/s** (Master 002 verlor mit SW-Miner ~95 kH/s,
  jetzt nur ~3 kH/s).
- Brenntest 26,7 min: Ø 989,6 kH/s, 0 Rechenfehler, 42/42 Shares akzeptiert,
  16-Bit-Kandidaten 99,6 % des Erwarteten (statistisch einwandfrei).
- /api/stats: neue Felder candidates, hwHashes, swHashes, hwLoop (Echtheits-Check).
- Screenshot docs/dashboard_v1.2.1.png (Pool, Wallet, WLAN geschwaerzt).

## v1.2.0 – Master 003 (2026-10-13) – neue Hash-Schleife „v2“, 963 kH/s
- Neue Hash-Schleife `src/hw_scan_v2.S`, portiert aus **NerdMiner Modded** (MIT,
  HenrysCat, https://github.com/HenrysCat/NerdMiner-Modded): 247 Takte/Nonce,
  gezaehlte Wartezeiten ohne BUSY-Abfrage, SHA-Register-Writes mind. 3 Takte auseinander.
- Auswahl beim Start per Known-Answer-Test (262 144 Nonces, 6 bekannte Kandidaten,
  in Python nachgerechnet); Stufen: schnell (247) -> +2 Reserve (257) -> breit (317)
  -> klassische kalibrierte Schleife aus Master 002. Chips vor Revision 3 bekommen
  immer die klassische Schleife.
- Laufzeit-Kontrolle: werden von 256 HW-Kandidaten >= 8 nicht in Software
  bestaetigt, wird automatisch auf die naechste, vorsichtigere Stufe gewechselt.
- Nonce-Zaehlung intern als T[3]-Wort (Big-Endian), echte Nonce = bswap32 –
  fuer HW- und SW-Miner einheitlich, Bereiche bleiben getrennt.
- Kalibrierung der klassischen Schleife entfaellt, wenn v2 besteht (schnellerer Start).
- Konsole: jede Minute Projekt-Kennung mit Version und aktiver Schleife.
- Gemessen: 963 kH/s (nur HW), Shares akzeptiert.

## v1.1.0 – Master 002 (2026-10-02) – 963 kH/s
- Warteschleifen mit `loopnez`/`nop` statt CCOUNT-Abfrage, Binaersuch-Kalibrierung,
  249 Takte/Hash.

## v1.0.x – Master 001 – 902 kH/s
- Software-Pipelining (naechster Block 1 startet vor der Kandidatenpruefung).

## Quellen / Sources
- NerdMiner Modded (MIT) – https://github.com/HenrysCat/NerdMiner-Modded ·
  https://henryscat.github.io/NerdMiner-Modded/ – Hash-Schleife v2 (siehe THIRD_PARTY_LICENSES.md)
- NerdMiner_v2 (MIT) – https://github.com/BitMaker-hub/NerdMiner_v2 (Upstream von NerdMiner Modded)
- SparkMiner (MIT) – https://github.com/BitzyLabs/sparkminer – nur die Idee der gepufferten Writes
