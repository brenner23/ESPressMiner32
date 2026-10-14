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

## Ideen fuer spaeter (nach dem Ausreizen)

- **Update ueber die Weboberflaeche** (Seite /update, Binary hochladen): kostet im Betrieb nichts, weil der Webserver
  ohnehin laeuft; braucht aber 2 App-Partitionen (Firmware muss in ~1,9 MB passen) -> einmalig per USB umstellen.
  Danach Hashrate gegen den Stand ohne Update-Seite messen ("wie viel kostet es wirklich?").
  Kandidat: HAL 9000 (.48, Micro-USB v1.0.0, Aussengehaeuse, hmpool) - muss sonst zum Flashen rein.
- Blumentopf / GhostPod (.42, ~780 kH/s echt): neue Schleife einbauen, LEDs + Entropie behalten (eigener Chat).
- Pool-Diff fuer eine Nacht auf 0,001 -> 10x mehr Shares, Vergleich NerdMiner/ESPressMiner auf ~1 % genau.
