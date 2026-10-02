# ☕ ESPressMiner32

🇬🇧 [English](README.md) · 🇩🇪 **Deutsch**

Ein Bitcoin-Stratum-Miner (V1) für den **klassischen ESP32**. Er nutzt den **SHA-256-Hardwarebeschleuniger**
per direktem Registerzugriff mit einer Pipeline: Die Daten für den nächsten Block werden geschrieben,
während die Engine noch rechnet.

Gemessen auf ESP32-D0WD-V3 @ 240 MHz: **~960 kH/s** (249 Takte pro Double-SHA256), Version 1.1.0.

🔗 **Projektseite:** https://github.com/brenner23/ESPressMiner32

> ⚠️ **Hobbyprojekt.** Mit einem ESP32 findet man realistisch keinen Bitcoin-Block. Es geht ums Lernen,
> Basteln und Ausreizen der Hardware. Ideal für Solo-Pools, Lotterie-Mining oder den eigenen Test-Pool.

## Versionen

| Version | kH/s | Takte/Hash | Was ist neu |
|---|---|---|---|
| **1.1.0** (aktuell) | **~960** | 249 | Taktgenaue Warteschleifen, Kalibrierung prüft die echte Mining-Schleife (behebt einen Fehler aus 1.0.0), robustere Kalibrierung |
| 1.0.0 | ~902 | 265 | Erste Version |

Jede Version ist ein fester Git-Tag. Ältere Versionen: oben im Auswahlfeld **„main ▾“ → Tags** wählen
(oder `git checkout v1.0.0`) und wie gewohnt flashen. Die Einstellungen (WLAN, Pools) bleiben beim Wechsel erhalten.
Es gibt bewusst keine fertigen Firmware-Dateien: Geflasht wird, was man im Quellcode nachlesen kann.

---

## Funktionen

- **~960 kH/s** auf einem klassischen ESP32 (Hardware-SHA, kalibrierte Pipeline)
- **Selbsttest** beim Start (Genesis-Block, 2000 Zufalls-Header, 5000 Nonces). Schlägt er fehl, wird nicht gemint.
- **Jeder Treffer wird in Software nachgerechnet**, bevor ein Share zum Pool geht
- **Web-Dashboard** unter `http://espressminer.local/` mit Hashrate-Verlauf, JSON unter `/api/stats`
- **Primary- und Secondary-Pool** mit automatischem Wechsel
- **Einrichtung ohne Programmieren:** WLAN-Portal, Setup-Programm mit Oberfläche oder Kommandozeile
- **Blaue LED (GPIO2)** leuchtet beim Minen und blinkt bei jedem Share

## Projektstruktur

```
ESPressMiner32/
├── platformio.ini            Build-Konfiguration (240 MHz, -O2)
├── include/
│   ├── credentials.h         Startwerte (leer, optional)
│   ├── config.h              Difficulty, Cores, LED, Nonce-Bereiche
│   ├── sha256_hw.h           Hardware-SHA-Pipeline
│   ├── sha256_sw.h           Software-SHA-256 (Coinbase, Merkle, Prüfung)
│   ├── stratum_client.h      Stratum-V1-Client
│   ├── mining_job.h          Job-/Share-Austausch zwischen den Tasks
│   └── ...
├── src/                      Implementierungen zu den Headern + main.cpp
├── src/hw_scan_asm.S         Hash-Schleife in Assembler (Versuch, abgeschaltet: HW_USE_ASM)
├── tools/                    Setup per USB: espressminer_setup_gui.py (Oberfläche), espressminer_setup.py
├── test/hw_probe/            Messprogramm für den SHA-Beschleuniger (Entwicklung)
└── test/pipe_probe/          Messprogramm für die Pipeline ab 1.1.0 (Entwicklung)
```

## Bauen & Flashen (VS Code + PlatformIO)

1. Ordner in VS Code öffnen (die PlatformIO-Erweiterung wird vorgeschlagen).
2. ESP32 per USB anschließen. PlatformIO findet den COM-Port automatisch.
3. Unten in der Statusleiste **→ Upload** (bzw. `pio run -t upload`).
4. **Serial Monitor** (115200 Baud) zeigt Selbsttest, Benchmark und alle 5 s die Hashrate.

Zugangsdaten müssen nicht in den Code: `include/credentials.h` ist absichtlich leer, eingerichtet wird nach dem Flashen.

## Ersteinrichtung (nach dem Flashen)

Solange keine Einstellungen gespeichert sind, startet der Miner **nicht** zum Minen, sondern wartet auf die Einrichtung.

**1. WLAN-Portal (Handy/Laptop)**
Mit dem WLAN `ESPressMiner32-XXXX` (Passwort `espressminer`) verbinden. Die Einrichtungsseite öffnet sich
automatisch, sonst `http://192.168.4.1/` aufrufen. Ausfüllen, „Speichern & Neustart“.
Das Portal startet auch, wenn das gespeicherte WLAN 20 s lang nicht erreichbar ist.

**2. Setup-Programm mit Oberfläche** (`pip install pyserial`)
```
python tools/espressminer_setup_gui.py
```
COM-Port wählen, Felder ausfüllen (oder „JSON laden …“), „Speichern & Neustart“.
Vorlage für mehrere Miner: `tools/credentials.example.json`.

**3. Kommandozeile über USB**
```
python tools/espressminer_setup.py                 # interaktiv, COM-Port wird gesucht
python tools/espressminer_setup.py --port COM3 --get
python tools/espressminer_setup.py --ssid MeinWLAN --password geheim ^
       --pool stratum+tcp://pool.example.com:3333 --wallet ADRESSE.Worker -y
python tools/espressminer_setup.py --reset         # Einstellungen löschen -> Portal
```

**4. Serielle Konsole direkt** (115200 Baud), eine JSON-Zeile pro Befehl, Antworten beginnen mit `@CM `:
```
{"cmd":"info"}   {"cmd":"get"}   {"cmd":"reset"}   {"cmd":"restart"}
{"cmd":"set","hostname":"espressminer","wifiSsid":"...","wifiPass":"...",
 "pools":[{"url":"stratum+tcp://host:3333","user":"ADRESSE.Worker","pass":"x"},{"url":""}]}
```

**Zurücksetzen ohne PC:** BOOT-Taste 5 s gedrückt halten → Einstellungen gelöscht, Neustart ins Portal.

## Ablauf

| Core | Aufgabe |
|------|---------|
| 1 | Hardware-SHA-Miner (exklusiv) |
| 0 | WLAN, Stratum-Client, Web-Dashboard, Statistik |

- **Einstellungen** (Zahnrad im Dashboard): Hostname, WLAN, Primary- und Secondary-Pool, gespeichert im Flash (NVS).
  Nach 3 Fehlversuchen wechselt der Miner zum anderen Pool, vom Secondary aus wird alle 10 Minuten der Primary erneut versucht.
- Das Dashboard läuft auf Core 0 und kostet keine Hashrate.

## Wie die Hashrate zustande kommt

| Schritt | kH/s |
|---|---|
| Einfacher Hardware-Weg (BUSY abfragen) | ~360 |
| Pipeline: nächsten Block schreiben, während die Engine rechnet | ~557 |
| Textregister gepuffert schreiben, ohne `memw` (Idee aus SparkMiner) | ~650 |
| Basisadresse im Register statt `l32r` pro Zugriff | ~700 |
| Feste Wartezeiten statt BUSY (das Flag hängt ~34 Takte nach) | ~800 |
| Kein `memw` nach LOAD | ~833 |
| Wartezeit je Phase einzeln kalibriert | ~842 |
| Software-Pipeline: nächsten Block 1 vor der Kandidatenprüfung starten | ~902 (v1.0.0) |
| Taktgenaue Zählschleife (`loopnez`) statt Abfrage der Taktuhr, LOAD-Zeiten getrennt | **~960** (v1.1.0) |

Die Wartezeiten werden bei jedem Start **kalibriert**: Der ESP32 sucht per Binärsuche die kleinste fehlerfreie
Wartezeit je Phase (auf 1 Takt genau) und schlägt `HW_CAL_MARGIN_*` Takte Reserve drauf. Geprüft wird dabei
genau die Schleife, die auch mint. Ein Prüfschritt gilt erst nach zweimaligem Durchfallen als zu knapp, und die
Kalibrierung hat bis zu drei Anläufe. Erst danach greifen langsame Notwerte. Im laufenden Betrieb werden die
Wartezeiten nicht mehr verändert.

Eine Hash-Schleife in Assembler liegt bei (`src/hw_scan_asm.S`), ist aber langsamer (255 Takte): Ab hier begrenzt
der Bus zum SHA-Baustein (rund 40 Zugriffe pro Hash), nicht mehr der Code.

Der klassische ESP32 kann keinen SHA-Zwischenstand (Midstate) in die Engine zurückladen. Pro Nonce sind deshalb
immer drei Hardware-Blöcke nötig.

Messwerkzeuge: `pio run -e hw_probe -t upload` (Registerzugriffe, Engine-Zeiten) und
`pio run -e pipe_probe -t upload` (Wartezeiten und Varianten der Pipeline ab 1.1.0).

## Rechenfehler, ehrlich gesagt

Um diese Geschwindigkeit zu erreichen, wird der SHA-Baustein **nicht nach Datenblatt** betrieben: Die Daten
werden geschrieben, während er noch rechnet, und statt auf sein BUSY-Signal zu warten, gelten fest gemessene
Wartezeiten. Dadurch rechnet er gelegentlich falsch.

- **Gemessen (v1.1.0, 17 Mio. geprüfte Hashes, WLAN aktiv): etwa 1 falscher Hash je 17.000, also ~0,006 %.**
  Die Quote blieb über die Zeit stabil. Mehr Reserve bei den Wartezeiten senkt sie nicht. Ursache ist vermutlich
  der WLAN-Verkehr auf dem gemeinsamen Bus.
- **Ein falscher Hash geht nie an den Pool.** Jeder Treffer wird vorher in Software nachgerechnet.
- Das Dashboard zeigt unter „verworfen (Rechenfehler)“ nur die falschen Hashes, die zufällig wie ein Treffer
  aussehen (etwa einer von 65.536), bei ~960 kH/s im Schnitt ungefähr einer alle 20 Minuten.
- Auswirkung auf den Ertrag: ~0,006 %, also nicht messbar.

## Entwicklung

Entwickelt mit Unterstützung von [Claude](https://claude.ai) (Anthropic) als KI-Programmierassistent. Entscheidungen, Tests auf echter Hardware und Messungen: brenner23.

## Lizenz

[MIT](LICENSE) © 2026 brenner23. Nutzung auf eigene Verantwortung.
Danksagungen und Bibliotheken: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
