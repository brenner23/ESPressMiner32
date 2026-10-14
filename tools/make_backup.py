#!/usr/bin/env python3
"""
Sichert den Projektordner als ZIP nach backups/.

  python tools/make_backup.py 842khs          -> backups/ESPressMiner32_842khs.zip
  python tools/make_backup.py 842khs "Kommentar zum Stand"

Ausgelassen werden Build-Ausgaben (.pio), die Sicherungen selbst und Python-Caches.
Achtung: include/credentials.h (WLAN-Passwort) ist mit drin.
"""

import sys
import zipfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BACKUP_DIR = ROOT / "backups"
SKIP_DIRS = {".pio", "backups", "__pycache__", ".git"}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    label = sys.argv[1]
    note = " ".join(sys.argv[2:])
    BACKUP_DIR.mkdir(exist_ok=True)
    target = BACKUP_DIR / f"ESPressMiner32_{label}.zip"
    if target.exists():
        sys.exit(f"{target.name} existiert bereits - anderen Namen waehlen.")

    count = 0
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(ROOT.rglob("*")):
            rel = path.relative_to(ROOT)
            if path.is_dir() or any(part in SKIP_DIRS for part in rel.parts):
                continue
            z.write(path, Path("ESPressMiner32") / rel)
            count += 1
        # Fertig gebaute Firmware dieses Stands (falls vorhanden) zum direkten Flashen
        # alle Build-Umgebungen (esp32dev, looptest, ...) je in eigenen Unterordner
        for build in sorted((ROOT / ".pio" / "build").glob("*")):
            for name in ("firmware.bin", "bootloader.bin", "partitions.bin"):
                if (build / name).exists():
                    z.write(build / name, Path("ESPressMiner32") / "firmware" / build.name / name)
                    count += 1
        info = f"Stand: {label}\nErstellt: {datetime.now():%Y-%m-%d %H:%M}\n"
        if note:
            info += f"Notiz: {note}\n"
        z.writestr("ESPressMiner32/BACKUP_INFO.txt", info)

    print(f"{target}  ({count} Dateien, {target.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
