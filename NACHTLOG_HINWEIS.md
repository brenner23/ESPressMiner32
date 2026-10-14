# Hinweis: Nachtprotokoll auf der .49 (13./14.10.2026)

Für beide ESPressMiner läuft auf dem FunCoin-Server **192.168.0.49** ein Logger, unabhängig vom PC:

- Skript: `/opt/test-logger/miner_logger.py`, screen-Sitzung **`testlogger`** (`screen -r testlogger`)
- alle 2 Minuten `/api/stats`, 18 Stunden lang (bis ca. 14.10. 15:45), danach Selbst-Ende
- Dateien:
  - `/opt/test-logger/USB-C_Master004.log` / `.csv`  → dieses Board (.100, v1.3.0.004)
  - `/opt/test-logger/MicroUSB_ChatMini.log` / `.csv` → Micro-USB „ChatMini“ (.101, v1.1.0.004)
- Zugang: `ssh -i ~/.ssh/id_ed25519_funcoin root@192.168.0.49`

Auffällig vor der Nacht: Dieses Board lief nicht mehr mit Variante J, sondern mit „v2 schnell (247 Takte)“
(66 verworfene Kandidaten → Laufzeit-Kontrolle hat umgeschaltet).

Angelegt aus dem Chat zur Micro-USB-Variante. Der Pool in `/opt/SHC-Coin` wurde nicht angefasst.
