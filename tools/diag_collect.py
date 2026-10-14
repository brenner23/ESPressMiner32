#!/usr/bin/env python3
# Sammelt den Netz-Diagnose-Puffer des ESP (Test-Build 1.3.0.007-diag) alle 3 s
# und schreibt ihn mit echter Uhrzeit nach diag_<ip>.log (+ .jsonl).
# Eigene Abfragen (TCP von dieser Maschine auf Port 80) werden markiert.
import json, sys, time, urllib.request, socket
if len(sys.argv) < 2:
    sys.exit("usage: diag_collect.py <ESP-IP> [own IP, marked as collector]")
IP = sys.argv[1]
ME = socket.gethostbyname(socket.gethostname()) if len(sys.argv) < 3 else sys.argv[2]
seq = 0
log = open(f"diag_{IP}.log", "a", buffering=1)
raw = open(f"diag_{IP}.jsonl", "a", buffering=1)
log.write(f"# start {time.strftime('%F %T')} collector={ME}\n")
while True:
    try:
        t_req = time.time()
        d = json.load(urllib.request.urlopen(f"http://{IP}/api/diag?since={seq}", timeout=5))
        for e in d["entries"]:
            wall = t_req - (d["uptimeMs"] - e["ms"]) / 1000.0
            e["wall"] = round(wall, 3)
            raw.write(json.dumps(e) + "\n")
            ts = time.strftime("%H:%M:%S", time.localtime(wall)) + f".{int(wall % 1 * 1000):03d}"
            if e["p"] == "HWERR":
                log.write(f"{ts}  ***** HW ERRORS +{e['x']} *****\n")
            else:
                own = "  (collector)" if e["p"] == "TCP" and e["src"] == ME and e["dp"] == 80 else ""
                extra = f" icmp-type {e['x']}" if e["p"] == "ICMP" else f" flags 0x{e['x']:02x}" if e["p"] == "TCP" else ""
                log.write(f"{ts}  {e['p']:5} {e['src']:>15}:{e['sp']:<5} -> {e['dst']:>15}:{e['dp']:<5} len {e['len']:4} x{e['n']}{extra}{own}\n")
            seq = max(seq, e["seq"])
        if d["seq"] < seq:            # ESP neu gestartet
            seq = 0
            log.write(f"# ESP restarted {time.strftime('%T')}\n")
    except Exception as ex:
        log.write(f"{time.strftime('%T')}  poll error: {ex}\n")
    time.sleep(3)
