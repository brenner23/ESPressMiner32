# ESPressMiner32 – Versionen / Changelog

Messwerte: ESP32-D0WD-V3 (rev 3.1), 240 MHz, DevKit USB-C, eigener Stratum-Pool.

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
