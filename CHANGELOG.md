# ESPressMiner32 – Versionen / Changelog

Messwerte: ESP32-D0WD-V3 (rev 3.1), 240 MHz, DevKit USB-C, eigener Stratum-Pool.

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
