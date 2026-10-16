# Display fonts / Display-Schriften

The display build (`nmtv154`, `cyd`) needs Adafruit-GFX font headers `LibNarrowB<pt>pt7b.h`
in this folder. They are **not included** in the repository (font licence). Create them once:

```
pip install freetype-py
python tools/ttf2gfx.py LiberationSansNarrow-Bold.ttf LibNarrowB 10 12 13 14 15 16 17 18 20 include/fonts
```

Font used for the photos: **Liberation Sans Narrow Bold** 1.07 (Liberation Fonts license =
GPLv2 with font exception; e.g. Windows: `C:/Windows/Fonts/LiberationSansNarrow-Bold.ttf`,
Debian/Ubuntu: package `fonts-liberation`). Any other narrow bold TTF works too; keep the file
names, sizes may then need a check on the device. The plain `esp32dev` build needs no fonts.

---

Der Display-Build (`nmtv154`, `cyd`) braucht die Adafruit-GFX-Schriftdateien `LibNarrowB<pt>pt7b.h`
in diesem Ordner. Sie liegen wegen der Schriftlizenz **nicht im Repo** – einmal mit dem Befehl oben
erzeugen. Der normale Build `esp32dev` braucht keine Schriften.
