# ESPressMiner32 – Messprotokoll Master 004 (Weg Richtung 1000+ kH/s)

Ausgangslage: v1.2.1 (Master 003) = HW 959,6 + SW 30,4 = **~990 kH/s**, 247 Takte/Nonce.
Hardware: ESP32-D0WD-V3 (rev 3.1), DevKit USB-C, COM22, IP .100, eigener Pool .49.
Regel: pro Schritt EINE kleine Aenderung -> messen -> Backup `backups/ESPressMiner32_v1.3.0.NNN_<kHs>khs.zip`
-> Eintrag hier. Faellt die Fehlerquote, wird der Schritt verworfen.

Messmethode Fehlerquote: ein falscher Hash ist eine Zufallszahl. Der Anteil der HW-Kandidaten
(untere 16 Bit von H7 = 0), den die Software verwirft, ist daher direkt die Fehlerquote.
Effektive Hashrate = Hashrate x (1 - Fehlerquote).

| Schritt | Aenderung | Takte | kH/s HW (Mining) | Fehler | Ergebnis |
|---|---|---|---|---|---|
| 1.2.1 | Ausgangslage (v2-Schleife A) | 248,0 | 959,6 | 0 | Referenz, gesamt ~990 |
| 1.3.0.001 | Messbetrieb: Varianten reihum (Runde 1+2) | – | siehe unten | – | J sauber, H/K/N kaputt |
| 1.3.0.002 | Variante J fest (Register, Abstand 2, pl1 ruhig) | 240 | 989 | – | Start 1019,5 gesamt |
| 1.3.0.003 | + Chip-Temperatur (grob) im Dashboard/API/Konsole | 240 | 989 | 1 in ~1000 Kand. (erste Minute) | Nachtlauf 13./14.10. |
| 1.3.0.004 | + Ueberhitzungsschutz (90/100/80 C) | 240 | 989 | J: 0,06 %, nach 1,9 h Rueckfall auf A | Nacht: 0 Neustarts, ~992 auf A |
| 1.3.0.005 | Laufzeit-Kontrolle mit 2 Fenstern (25 %/256, 1 %/4096) | 240 | 989 | – | geflasht 14.10. 09:17, Langzeitlauf |
| 1.3.0.008 | WLAN-Waechter + Schutzfenster | 240 | 989 | Welle 0 statt 6–7 | ~1019–1020 gesamt |
| 1.3.0.009 | Schleife O (p2 39) | 239,9 | ~995 | 0 | **~1024 gesamt** |
| 1.3.0.009 | Langzeitlauf 29 h | 239,9 | ~995 | 0,016 % (fast nur Stichprobe) | 1024 Anzeige, ~1050 am Pool |
| 1.3.0.010 | Anzeige + schneller SW-Scan (Versuch) | 239,9 | ~995 + SW 34 | ~10–12 % | verworfen |

## 1.3.0.001 – Messbetrieb (Firmware zeigt noch "1.3.0-dev")

Neue Varianten in `src/hw_scan_v2.S` (Makro `PL_MINE_R`): Header-Woerter 0..7 dauerhaft in
Registern (a5, a9..a15), Budget dafuer im Speicher (in W1 gezaehlt, statt der 5 Fuell-nops).
Nur die Rueckschreib-Phase nach dem letzten LOAD (Engine steht) wird kuerzer.

Taktmessung beim Start (200 000 Nonces, ohne Last) + Known-Answer-Test (ohne Last):

| Var | Aenderung | Takte | rechnerisch kH/s | KAT |
|---|---|---|---|---|
| A | v2 wie 1.2.1 | 248,0 | 967,9 | OK |
| B | ruhige Wartezeiten (p1 3, p2 39, pl1 9, p3 26, pl2 9) | 242,9 | 987,9 | OK |
| C | Register, Abstand 3 (Kontrolle) | 249,0 | 964,0 | OK – kein Gewinn |
| D | Register, Abstand 2 | 241,9 | 992,0 | OK |
| E | Register, Abstand 1 | 235,9 | 1017,3 | **FEHLER** |
| F | D + alle ruhig | 236,9 | 1013,0 | **FEHLER** |
| G | E + alle ruhig | 230,9 | 1039,4 | **FEHLER** |
| H | D + p1 ruhig | 240,9 | 996,1 | OK |
| I | D + p2 ruhig | 240,9 | 996,1 | OK |
| J | D + pl1 ruhig | 240,9 | 996,1 | OK |
| K | D + p3 ruhig | 240,9 | 996,1 | OK |
| L | D + pl2 ruhig | 240,9 | 996,1 | **FEHLER** |
| M | D + ruhig ausser pl2 | 237,9 | 1008,7 | OK |

Erkenntnisse:
- Abstand 1 Takt zwischen SHA-Writes scheitert schon OHNE Busverkehr: die Register nehmen
  hoechstens alle 2 Takte einen Write an (harte Grenze).
- pl2 (Wartezeit nach dem letzten LOAD) darf nicht kuerzer: mit Registern faellt das l32i vor
  dem ersten Write weg, pl2 ist dadurch effektiv schon 1 Takt kuerzer.

### Runde 1 unter Last (Pool, WLAN, SW-Miner an), je 4 min, 13.10. 17:10-17:35

| Var | kH/s HW | Kandidaten | verworfen | Fehler | effektiv |
|---|---|---|---|---|---|
| A | 960,8 | 3484 | 0 | 0 % | 960,8 |
| B | 980,9 | 3540 | 0 | 0 % | 980,9 |
| D | 985,1 | 3719 | 3 | 0,08 % | 984,3 |
| H | 989,2 | 4371 | 597 | **13,7 %** | 854,1 |
| I | 989,4 | 3653 | 0 | 0 % | **989,4** |
| J | (abgebrochen, bis dahin 0 Fehler) | | | | |

- Dashboard zeigte bei H trotzdem ~1019 kH/s -> Zaehler allein beweist nichts!
- Fehlerverlauf pro Minute: H gleichmaessig ~150/min, sofort 0 nach Umschalten auf I.
  Keine Startfehler (Kaltstart per Stecker: 182 Kandidaten, 0 verworfen).
- D: 3 Fehler alle in einer Minute (17:18-17:19), danach keine -> kurze Stoerung? Nachmessen.
- p1 darf unter Last NICHT ruhig sein (ohne Last bestand H den KAT noch!). M enthaelt p1=3 -> verworfen.
- Neustart in Runde 1 ungeklaert (vor Einbau der Reset-Diagnose). Seitdem: Reset-Grund und
  Variante vor Neustart in Konsole und /api/stats.

### Runde 2 (je 5 min): I, J, K, N, D, A
N = D + p2, pl1, p3 ruhig (p1 und pl2 sicher): 238,9 Takte = 1004,5 kH/s rechnerisch, KAT OK.

| Var | kH/s HW | Kandidaten | verworfen | Fehler | effektiv HW | Dashboard zeigte |
|---|---|---|---|---|---|---|
| I | 989,3 | 4615 | 6 | 0,13 % | 988,0 | ~1019 |
| **J** | 989,3 | 4509 | **0** | **0 %** | **989,3** | ~1019 |
| K | 989,3 | 4864 | 226 | 4,6 % | 943,3 | ~1019 |
| N | 997,2 | 5374 | 801 | **14,9 %** | 848,6 | **~1028** (!) |
| D | 985,2 | 4507 | 7 | 0,16 % | 983,6 | ~1015 |
| A | Referenz, ~960 | | | | | ~990 |

- Fehler pro Minute: K ~45/min, N ~170/min gleichmaessig; I und D je ein kurzer Schub
  (6 bzw. 3 Fehler in einer Minute), dann wieder 0.
- p3 ruhig klappt OHNE Register (B), aber nicht MIT Registern (K): ohne das l32i vor dem ersten
  Rueckschreib-Write ist der Puffer nach LOAD 2 einen Takt kleiner. Ersparnisse sind nicht stapelbar.
- **N zeigte die hoechste Zahl des Tages (1028) und lieferte effektiv am wenigsten (~879 gesamt).**

### Gegenprobe Pool .49 (Sandkiste, data/shares.json per_hour_worker, 0,01 Diff, 1 Share/h = 11,93 kH/s)

119 volle Stunden (08.-13.10.), echt beim Pool angekommen, +-1 %:
NM-CYD-43 1026, NM-TV-OR-46 1020, NM-TV-45 1001, NM-TV-38 1001, NM-CYD-47 1001, GhostPod 776.
-> NMMiner liefern echt ~1010 im Schnitt (Display ~1030, also ~2 % darunter). Kein grosser
Schwindel. Ziel fuer uns: >= 1010 echt am Pool.
ChatGPT (dieser ESP) heute nur gestoerte Stunden (Flashen/Rotation): 16:00 und 17:00 je 80 Shares
= 954 +- 105 kH/s - passt, aber zu ungenau.

## 1.3.0.002 – Variante J fest (Nachtlauf)

Normalbetrieb (kein Messbetrieb). Rueckfallkette: J -> A (v2 schnell) -> +2 -> breit -> klassisch.
SW-Miner an. Ziel: ueber Nacht ungestoert, dann Stunden-Shares aus der Sandkiste vergleichen.

Erkenntnis Nebenbei (fuer einen spaeteren Schritt): Arduino-sdkconfig ueberwacht den IDLE-Task
von Core 1 NICHT (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 nicht gesetzt). Die Pause
`vTaskDelay(1)` alle 500 ms im HW-Miner (~0,2 % = ~2 kH/s) ist dafuer also nicht noetig.

## 1.3.0.003 – Chip-Temperatur (nur Anzeige)

Interner Sensor des klassischen ESP32 (temperatureRead, undokumentiert, Versatz bis +-10 Grad,
Stufen ~0,6 Grad). Gelesen bei jedem neuen Pool-Job (mining.notify, alle 30-60 s), weil Core 0
dann ohnehin mit Netzwerk beschaeftigt ist (Idee: brenner23). Anzeige: Dashboard-Zeile
"Chip-Temperatur (grob)" unter WLAN, /api/stats "chipTemp", Konsole in der Minutenzeile.
Gemessen: 63,9-65 Grad unter Volllast. Hashrate unveraendert 1019 kH/s.

Nachtlauf ab 13.10. ~18:25: Fehlerquote J ueber Stunden + Gegenprobe Pool (Sandkiste).

## 1.3.0.004 – Ueberhitzungsschutz (vorbereitet, Idee brenner23)

Fuer GitHub-Nutzer, die den ESP z.B. in eine geschlossene Dose stecken. Stufen mit Hysterese
(config.h TEMP_*): >= 90 C SW-Miner pausiert, >= 100 C Mining gestoppt, < 80 C alles wieder an.
Werte ausserhalb -20..150 C = Sensorfehler, ignoriert. Gelesen bei jedem Pool-Job, Rueckfall:
Statistik-Task liest selbst, wenn 60 s nichts kam (auch ohne Pool ueberwacht).
Anzeige Dashboard (hinter der Temperatur), API thermal/thermalLevel, Konsole bei Stufenwechsel.
Stand 13.10. abends: kompiliert, NICHT geflasht (Nachtlauf 1.3.0.003 laeuft). Test geplant:
Grenzwerte testweise niedrig setzen (z.B. 60/65/55) und Umschalten beobachten.

Test 13.10. 18:46-18:53 (env thermaltest, Grenzen 66/68/63 C): 67,2 C -> SW-Miner pausiert
(HW stieg dabei 989 -> 992-994!), 68,3 C -> Mining gestoppt, abgekuehlt auf 62,8 C -> normal,
wieder warm -> erneut gestoppt bzw. SW pausiert. Kreislauf funktioniert. Danach finale Werte
90/100/80 C geflasht (ueber 20 Grad Luft zur Volllast-Temperatur ~68 C). Kein Einfluss auf die
Hashrate im Normalbetrieb (1 int-Abfrage je 8192 Nonces). Nachtlauf ab ~19:00 am USB-Solarladeregler.

### Nachtlauf 1.3.0.004 (13.10. 19:00 – 14.10. 09:00, Solar-Akku)

- 19:01–20:46 Schleife **J**: ~1020 kH/s, 58 von 99.126 Kandidaten verworfen = **0,06 %**.
  Die Fehler kommen NICHT nur nach dem Start, sondern gleichmaessig in kleinen Schueben,
  auffaellig **alle ~10 min 4–8 Stueck** (Ursache vermutlich periodische Netzwerk-/WLAN-Aktivitaet auf Core 0).
- 20:51: Schub von 8 Fehlern in einem 256er-Fenster -> alte Regel (>= 8 von 256) wechselt auf **A „v2 schnell“** (247 Takte).
- Danach 14 h auf A: **~992 kH/s, 0 neue Fehler**, 0 Neustarts, 67–72 C, Schutz nie ausgeloest.
- Pool (shares.json, 22–09 Uhr, 11 h): ChatGPT 958 Shares = 1039 kH/s (Glueck, +5 %);
  NerdMiner 959–999; ChatMini (Micro-USB, Rev. 1) 965.
- Bewertung: J effektiv 1020 x (1 - 0,0006) = ~1019 > A 992. Die Regel hat fuer 0,06 % Fehler
  27 kH/s verschenkt -> zu empfindlich fuer kurze Schuebe. Falsche Hashes gehen ohnehin nie an den Pool.

## 1.3.0.005 – Laufzeit-Kontrolle mit zwei Fenstern

`hw_sha_report_candidate()` (src/sha256_hw.cpp):
- kurzes Fenster 256 Kandidaten (~35 s): Wechsel erst ab **64 falschen (25 %)** -> Schleife wirklich kaputt.
- langes Fenster 4096 Kandidaten (~9 min): Wechsel ab **41 falschen (> 1 %)**.
- Kurze Schuebe (4–8 Fehler) bleiben damit folgenlos, J bleibt drin. Kaputte Varianten wie H (13,7 %)
  oder N (15 %) fliegen weiterhin nach spaetestens ~9 min raus.
- Geflasht 14.10. 09:17, Start: KAT J OK, Benchmark 996,1 kH/s, Betrieb ~1020 kH/s.
- Sicherung: backups/ESPressMiner32_v1.3.0.005_1019khs.zip + Flash_Master004_v1.3.0.005_1019khs.bin.
- Langzeit-Logger: .49 /opt/test-logger/v005 (screen logger005, alle 2 min, 26 h).

## 1.3.0.006 – keine Zwangspause auf Core 1

- `HW_YIELD_MS 0`: der HW-Miner gibt Core 1 nie mehr per vTaskDelay ab. Plus ~1,3 kH/s -> **~1021 kH/s**.

## 1.3.0.007 – alles Englisch

- Weboberflaeche, API-Texte (Schleifennamen, Reset-Grund, Temperatur-Status) und Konsole auf Englisch.
- 30-min-Test: Ø **1020,8 kH/s**, 14 von 27 206 Kandidaten verworfen (0,051 %), 37/0 Shares, Schleife J.

## 1.3.0.007-diag – Netz-Diagnose: woher kommt der 10-Minuten-Schub?

Test-Build (`pio run -e diag`, `-DDIAG_NET=1`): lwIP-Raw-PCBs protokollieren jedes ankommende IPv4-Paket
(ICMP/IGMP/TCP/UDP, nach dem Entschluesseln, ohne es zu verbrauchen) und jeden HW-Fehler mit Zeitstempel in einen
Ringpuffer, Abruf `GET /api/diag?since=<seq>`. Sammler: `tools/diag_collect.py <ESP-IP> <eigene-IP>`.

Ergebnis (14.10., 12:12–14:00, 10 Schuebe):
- Im Fehlerfenster kommt **kein auffaelliges IP-Paket** an (kein Ping, kein DHCP, keine Anfrage von aussen).
- Der Router sendet zur vollen Zehnerminute einen Mesh-Such-Broadcast (UDP 53805, laut AVM Mesh-Erkennung) –
  der Abstand Broadcast -> Fehler waechst aber um 0,47 s je Runde -> nur Zeit-Nachbar, nicht die Ursache.
- Takt der Stoerung: **600,4–600,5 s**. Nach einer Neuanmeldung des ESP am WLAN springt der Zeitpunkt mit
  (14:00:24 -> 14:30:33) -> die Stoerung haengt an **der WLAN-Verbindung des ESP**, sehr wahrscheinlich ein
  periodischer Schluesselwechsel des Access Points (z. B. hostapd `wpa_ptk_rekey=600`).
- Gegentest: 5x nachgebautes 53805-Paket, 500 UDP-Pakete in 0,05 s, 300 Pings (Flut) -> **0 HW-Fehler**.
  IP-Last stoert die J-Schleife nicht.
- Fester Kanal statt Automatik aendert nichts.
- Nebenbefund: nach dem Kanalwechsel im Mesh blieb 1.3.0.007 **dauerhaft offline** (Auto-Reconnect versagt).

## 1.3.0.008 – WLAN-Waechter + Schutzfenster („Wecker“)

- **WLAN-Waechter** (`wifi_watch_task`, src/main.cpp): nach 10 s ohne WLAN selbst neu verbinden, alle Kanaele
  absuchen, im Mesh den staerksten Knoten nehmen, nichts im NVS zwischenspeichern; alle 20 s erneut; nach 3 min
  Neustart (nicht bei offenem Portal). Trenngrund, Abbrueche und Neuverbindungen in /api/stats.
- **Schutzfenster** (`guard::task`, src/miner.cpp): erkennt Fehlerschuebe (>= 2 Fehler in 2 s), lernt den Takt
  (1–30 min, bestaetigt bei zwei passenden Abstaenden +-1 %, auch Vielfache), schaltet 6 s vor bis 6 s nach dem
  erwarteten Zeitpunkt auf „v2 +2 margin“ (257 Takte) und zurueck auf J. Jedes 6. Fenster ist eine ungeschuetzte
  Stichprobe; bleibt die Welle zweimal aus, schaltet sich der Wecker ab. Kosten bei 10-min-Takt ~1,2 kH/s.
- Test 14.10. (erste Fassung des Weckers, lernt ab dem 2. Schub): Wellen 14:30:33 (6) und 14:40:33 (7) ungeschuetzt,
  Takt gelernt 600,43 s; **erstes Schutzfenster 14:50:28–40: 0 Fehler**, Ø 1019,2 kH/s.
- Danach (gebaut, noch nicht auf dem Board getestet): Wecker v2 (Startwert 600 s schon nach dem 1. Schub,
  beliebiger Takt, Stichproben) + Test-Eingang `/api/guardsim?n=3` nur im Diag-Build (gespielte Fehlerschuebe,
  zaehlen nicht in hwErrors). Geplant: Phasen 60 s / 120 s / unregelmaessig / Stille.

## 1.3.0.009 – Schleife O (240 Takte), Messbetrieb J/O/P/Q/R

Messbetrieb `1.3.0.009-looptest` (14.10. ab 15:09), je 10 min pro Variante. Neu: Fehler im Schutzfenster
und in erkannten Schueben zaehlen als „Welle“ und nicht gegen die Variante.

| Var | Aenderung (gegen J) | Takte | KAT | eigene Fehler | Welle |
|---|---|---|---|---|---|
| J | Referenz (p2 40, pl1 9) | 240,9 | OK | 0 | 3 (Start) |
| O | p2 39 | 239,9 | OK | **0** | 0 (Fenster 15:30 sauber) |
| P | O + p2 38 | – | **FAILED** | – | – |
| Q | O + pl1 8 | – | **FAILED** | – | – |
| R | O + d2 15 | – | **FAILED** | – | – |

Damit stehen alle fuenf Wartezeiten der Engine am Limit (d2/d3 verschieben nur Arbeit innerhalb
CONTINUE->LOAD bzw. START->LOAD und bringen keinen eigenen Takt):

| Abschnitt | Grenze | Beleg |
|---|---|---|
| CONTINUE -> LOAD Block 2 (d2 + p2) | O | P, R KAT FAILED |
| LOAD -> START Hash 2 (pl1) | 9 | Q KAT FAILED |
| START -> LOAD Hash 2 (d3 + p3) | 27 | K 4,6 %, N 14,9 % |
| LOAD -> Header-Writes (pl2) | 10 | L Fehler |
| START Block 1 -> CONTINUE (p1) | 4 | H 13,7 % unter Last |
| Abstand SHA-Writes | 2 Takte | E Fehler |

1.3.0.009 = O als Hauptschleife, Rueckfallkette O -> J -> A -> +2 -> breit. Geflasht 15:39 (Display ~1024–1025),
nach dem Versuch 1.3.0.010 erneut 15:56: 0 Fehler bei 816 Kandidaten in der ersten Minute, ~1024 kH/s.
Backup `ESPressMiner32_v1.3.0.009_O_1024khs.zip` + `firmware_v1.3.0.009.bin`.

### Langzeitlauf 1.3.0.009 (14.10. 16:00 – 15.10. ~21:30, ausgewertet 15.10.)

Quellen: Logger .49 `/opt/test-logger/v005/USB-C_v1.3.0.005.csv` (Name veraltet, ab 14.10. 15:10 laeuft
dort 1.3.0.009; alle 2 min bis 15.10. 11:16), /api/stats am Abend, Pool shares.json.

- Stand nach 29,4 h (API): Ø **1024,0 kH/s**, 259 von 1.654.448 Kandidaten verworfen = **0,016 %**,
  Shares 2598/5 (0,2 % abgelehnt = normale veraltete Shares), 0 WLAN-Abbrueche, 69 C, guardErrors 0 in 147 Fenstern.
- Logger 19,3 h ab Neustart 16:00: 152 Fehler, davon **131 in 17 Schueben zu je ~8 Fehlern, ziemlich genau
  jede volle Betriebsstunde** (bei 03:00 und 08:00 kein Schub), dazu 21 Einzelfehler (~1 pro Stunde, ~0,002 %).
- Ursache Stundentakt: **kein Rechenfehler und keine neue Stoerung**, sondern die gewollte Stichprobe des
  Schutzfensters: `HW_GUARD_PROBE_EVERY 6` -> jedes 6. Fenster bleibt ungeschuetzt, 6 x 600,47 s = 3602,8 s.
  Die WLAN-Welle trifft dann die schnelle Schleife O voll (~8 Fehler). Die uebrigen 5 Fenster pro Stunde: 0 Fehler.
- Einzelfehler ~0,002 % = echte zufaellige Rechenfehler, unter dem alten Wert von v1.1.0 (0,006 %).
- Pool (27 volle Stunden ab 14.10. 17 Uhr): ~88 Shares/h = **~1050 +- 21 kH/s** -> Anzeige 1024 ist echt.
- Kosten der Stichprobe: ~8 verworfene Kandidaten pro Stunde = rechnerisch ~0,15 kH/s. Entscheidung brenner23:
  **so lassen** (Stichprobe bleibt jedes 6. Fenster).

## 1.3.0.010 – Versuch, verworfen (nicht auf GitHub)

Zwei Aenderungen auf einmal (gegen die eigene Regel): (1) Dashboard: 1-s-Wert ohne Komma, Mittel 10 min;
(2) schneller Software-Scan auf Core 0 (Runden 0..2 vorberechnet, feste W-Woerter eingerechnet, Hash 2 nach
Runde 60 beendet). Der schnelle Pfad ist in Python und auf dem Chip gegen die Referenz geprueft (0 Abweichungen)
und bringt **SW 30 -> 34 kH/s** (Benchmark 36,8), gesamt 1028–1031.
Aber: **~12 % HW-Fehler** (127 von 983 Kandidaten in 55 s). Mit abgeschaltetem schnellem Pfad (`SW_FAST_SCAN 0`)
weiter ~10 %, 1.3.0.009 direkt danach 0 Fehler bei gleicher Temperatur (67 C).
-> Ursache liegt nicht (nur) im SW-Scan. Verdacht: geaenderte IRAM-Lage der HW-Schleife oder der 1-s-Takt der
Statistik auf Core 0. Naechster Versuch getrennt: erst nur die Anzeige, dann der SW-Scan.
Stand gesichert: `backups/ESPressMiner32_v1.3.0.010-versuch_Fehler.zip`.

## Master 005 (ab 15.10.2026) – Pre-Versionen, zwei USB-C-Boards (A = Hauptboard, B = Board im Acrylgehaeuse)

Messmethode Master 005: /api/stats alle 30 s (scratch measure.py), kH/s aus Zuwachs hwHashes/swHashes
geteilt durch Laufzeit (unabhaengig von der Anzeige). Bewertung: Fehlerrate steigt stetig/schnell = gescheitert,
Schuebe von ~8 (WLAN-Welle, Schutzfenster lernt noch) zaehlen nicht.

| Schritt | Aenderung | HW | SW | gesamt (Zaehler) | Anzeige Ø | Fehler | Ergebnis |
|---|---|---|---|---|---|---|---|
| Basis | 1.3.0.009 unveraendert, 10 min | 989,1 | 30,3 | **1019,4** | 1024,2 | 10/10.241 (8 davon 1. WLAN-Welle 22:11, ungeschuetzt) = 2 echte | Referenz |

| ⭐ **Pre 1** | 1.3.0.011-pre1: Interrupts auf Core 1 je HW-Charge gesperrt (Board A) | **995,4** | 30,2 | **1025,6** | **1026,1** | 8/9.959 = nur 1. WLAN-Welle, **0 Einzelfehler** | **OK, +~2 kH/s** |

| Pre 2 | Pre 1 + schneller SW-Scan aus 1.3.0.010 (OHNE Anzeige-Aenderung) | ~997 | **34,5** | ~1031 | 1030–1033 | **244/2.507 = ~10 %, stetig steigend** | **verworfen**, sofort Pre 1 zurueck |

| Pre 2b | Gegenprobe: Pre-2-Code, aber SW_FAST_SCAN 0 (Lage verschoben, schneller Pfad aus) | – | ~30 | ~1025 | 1023–1026 | **173/1.627 = ~10 %** nach 90 s | verworfen -> **Beweis: Code-Lage ist schuld** |
| ⭐⭐ **Pre 3** | Pre 1 + schneller SW-Scan + **HW-Schleifen festgenagelt** (HW_LOOP_PIN: Block auf Versatz 0xc74 in 4 KB wie Pre 1) | **999,8** | **34,9** | **1034,7** | **1034,0** | 8/11.315 = nur 1. WLAN-Welle (23:01:20–50), **0 Einzelfehler** | **OK, +9 kH/s gegen Pre 1** |

Pre 2b (15.10. ~22:48): Fehler ~10 % von der ersten Sekunde an, gleich wie Pre 2 -> die verschobene Lage der
HW-Schleifen (Code-Lage im IRAM) verursacht die Fehler, NICHT der schnelle SW-Scan. Betrifft genauso 1.3.0.010.
Pre 3 (15.10. 22:51–23:03): Adressen gegen Pre 1 geprueft (alle 20 Schleifen gleicher Versatz in 4 KB, O: 0x4008d510 ->
0x40093510). 1033–1036, Selftest schnell==Referenz OK, Shares 16/0, 64–70 C. Die eine Welle (8 auf einmal, als
Schub erkannt) kam ungeschuetzt, weil das Schutzfenster nach dem Neustart erst lernt. HW stieg dabei auch (995,4 ->
999,8, Ursache offen). Backup `ESPressMiner32_v1.3.0.011-pre3_1035khs.zip` + `firmware_v1.3.0.011-pre3.bin`.

**Regel fuer alle Varianten (auch Micro-USB / Display-Version):** Die HW-Schleifen reagieren auf ihre Lage im IRAM.
Jede Aenderung, die IRAM-Code VOR den Schleifen hinzufuegt oder entfernt (IRAM_ATTR-Funktionen, neue Dateien),
kann sie verschieben -> immer HW_LOOP_PIN benutzen und nach dem Bauen mit `xtensa-esp32-elf-nm -n firmware.elf`
pruefen, dass `hw_*`-Adressen modulo 0x1000 gleich der bekannt guten Version sind. Welcher Versatz gut ist,
muss je Board/Variante einmal gemessen werden (USB-C: 0xc74 fuer hw_v2_fast).

Pre 2 (15.10. ~22:45): Selbsttest schnell == Referenz bestanden, SW 34,5 (schneller Pfad aktiv), Schleife blieb O,
kein Neustart, 63–67 C. Fehler von Anfang an gleichmaessig (~10/min-Takt: 35 nach 25 s, 244 nach 140 s).
Pre 1 zurueck (nur App bei 0x10000): sofort wieder 0 Fehler, 1027–1028.
Schluss mit 1.3.0.010: dort ~10 % auch mit SW_FAST_SCAN 0 (Anzeige-Aenderung drin), hier ~10 % ohne Anzeige-Aenderung.
Gemeinsam ist nur die neue sha256_sw.cpp -> **Verdacht Code-Lage**: der zusaetzliche IRAM-Code verschiebt die
HW-Schleifen (hw_v2_fast 0x4008ac74 -> 0x4008efe4). Gegenprobe offen: Pre 2 mit SW_FAST_SCAN 0 (Lage verschoben,
schneller Pfad aus). Backup `backups/ESPressMiner32_v1.3.0.011-pre2-versuch_Fehler.zip`. Quellcode wieder auf Pre 1.

Basis 15.10. 22:06–22:17: 67,8 -> 73,3 C (steigt; Board steckt im Acrylgehaeuse mit kleinem Lueftungsschlitz),
Shares 12/0, 1 API-Timeout genau zur Welle.
Achtung: Basis (Board B) und Pre 1 (Board A) sind verschiedene Boards. Fairer Vergleich auf Board A selbst:
1.3.0.009 lief dort 30 h mit Anzeige Ø 1024,0 (Logger: totalHashes/uptime 1024,0) -> Pre 1 **+1,6 bis +2,1 kH/s**.
Pre 1 15.10. 22:21–22:31: 62–66 C, Shares 7/0, wifiDrops 0, Schleife O blieb. Backup
`backups/ESPressMiner32_v1.3.0.011-pre1_1026khs.zip` + `firmware_v1.3.0.011-pre1.bin`.

## Ideen fuer spaeter (nach dem Ausreizen)

- **Update ueber die Weboberflaeche** (Seite /update, Binary hochladen): kostet im Betrieb nichts, weil der Webserver
  ohnehin laeuft; braucht aber 2 App-Partitionen (Firmware muss in ~1,9 MB passen) -> einmalig per USB umstellen.
  Danach Hashrate gegen den Stand ohne Update-Seite messen ("wie viel kostet es wirklich?").
  Kandidat: HAL 9000 (.48, Micro-USB v1.0.0, Aussengehaeuse, hmpool) - muss sonst zum Flashen rein.
- Blumentopf / GhostPod (.42, ~780 kH/s echt): neue Schleife einbauen, LEDs + Entropie behalten (eigener Chat).
- Pool-Diff fuer eine Nacht auf 0,001 -> 10x mehr Shares, Vergleich NerdMiner/ESPressMiner auf ~1 % genau.
- Miner-Monitor (Dashboard aller Miner im LAN, Pool-Wahrheit, Ereignisse): umgesetzt als eigenes internes Tool.
