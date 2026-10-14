#!/usr/bin/env python3
"""ESPressMiner32 Test-Logger - Langzeitprotokoll ueber /api/stats (nur lesend)

Fragt in festen Abstaenden /api/stats eines oder mehrerer Miner ab und schreibt je Miner
  <name>.log   lesbare Zeile (Hashrate, Mittelwert, Rechenfehler, Shares, Temperatur, Schleife, Neustarts)
  <name>.csv   alle Werte (Trennzeichen ;) zum Auswerten von Mittelwerten, Fehlerquote und Neustarts
Gedacht fuer einen Rechner, der ohnehin durchlaeuft (z.B. Server/Raspberry Pi), damit der PC aus sein kann.
Nur Python-Standardbibliothek.

  python3 miner_logger.py NAME=IP [NAME=IP ...] [--intervall 120] [--stunden 18] [--ordner .]

Beispiel (im Hintergrund per screen):
  screen -dmS testlogger python3 miner_logger.py USB-C=<ESP-IP> MicroUSB=<ESP-IP>
  screen -r testlogger      (wieder ansehen, Ctrl-A D = loesen)

Auswertung, z.B. Mittelwert ueber die Nacht: Spalte "average" der letzten Zeile (Mittel seit Start
des Miners), solange "neustart" ueberall 0 ist; Fehlerquote = hwErrors / hwCandidates.
"""
import argparse, csv, json, os, time, urllib.request

FELDER = ["zeit", "uptime_s", "hashrate", "average", "totalHashes", "hwHashes", "swHashes",
          "hwCandidates", "hwErrors", "accepted", "rejected", "chipTemp", "thermal",
          "hwLoop", "version", "resetReason", "rssi", "heap", "neustart"]


def abfragen(name, url, ordner, letzte_uptime):
    zeit = time.strftime("%Y-%m-%d %H:%M:%S")
    log_pfad = os.path.join(ordner, name + ".log")
    csv_pfad = os.path.join(ordner, name + ".csv")
    try:
        d = json.load(urllib.request.urlopen(url, timeout=10))
    except Exception as ex:
        with open(log_pfad, "a", encoding="utf-8") as f:
            f.write(f"{zeit} FEHLER Abfrage: {ex}\n")
        return
    up = d.get("uptime", 0)
    neustart = name in letzte_uptime and up < letzte_uptime[name]
    letzte_uptime[name] = up
    row = {k: d.get(k, "") for k in FELDER}
    row.update(zeit=zeit, uptime_s=up, neustart=int(neustart))
    neu = not os.path.exists(csv_pfad)
    with open(csv_pfad, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FELDER, delimiter=";")
        if neu:
            w.writeheader()
        w.writerow(row)
    c, e = d.get("hwCandidates", 0), d.get("hwErrors", 0)
    line = (f"{zeit} up {up / 3600:6.2f}h {d.get('hashrate', 0):7.1f} kH/s avg {d.get('average', 0):7.1f}"
            f" | HW-Kand {c:8d} verworfen {e:5d} ({(100 * e / c if c else 0):.3f}%)"
            f" | Shares A/R {d.get('accepted')}/{d.get('rejected')} | {d.get('chipTemp', 0):.1f} C"
            f" | {d.get('hwLoop')} | {d.get('version')}")
    if neustart:
        line += f" | NEUSTART Grund={d.get('resetReason')}"
    with open(log_pfad, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def main():
    ap = argparse.ArgumentParser(description="ESPressMiner32 Test-Logger (/api/stats)")
    ap.add_argument("miner", nargs="+", help="NAME=IP oder NAME=http://host/api/stats")
    ap.add_argument("--intervall", type=int, default=120, help="Sekunden zwischen Abfragen (Standard 120)")
    ap.add_argument("--stunden", type=float, default=18, help="Laufzeit, danach Ende (Standard 18)")
    ap.add_argument("--ordner", default=".", help="Zielordner fuer .log/.csv")
    a = ap.parse_args()
    miner = {}
    for m in a.miner:
        name, _, ziel = m.partition("=")
        if not ziel:
            ap.error(f"'{m}': Format NAME=IP")
        miner[name] = ziel if ziel.startswith("http") else f"http://{ziel}/api/stats"
    os.makedirs(a.ordner, exist_ok=True)
    print("ESPressMiner32 Test-Logger:", ", ".join(f"{n} -> {u}" for n, u in miner.items()), flush=True)
    letzte = {}
    ende = time.time() + a.stunden * 3600
    while time.time() < ende:
        for name, url in miner.items():
            abfragen(name, url, a.ordner, letzte)
        time.sleep(a.intervall)


if __name__ == "__main__":
    main()
