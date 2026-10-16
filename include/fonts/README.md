# Display fonts / Display-Schriften

The headers `ArchivoNB<pt>pt7b.h` in this folder are **included and ready to use** – just build and flash.

- Font: **Archivo Narrow** (Bold, weight 700) by Omnibus-Type, from the Google Fonts archive
  (https://github.com/google/fonts/tree/main/ofl/archivonarrow)
- Licence: **SIL Open Font License 1.1** – see `tools/fonts/OFL-ArchivoNarrow.txt`.
  The headers are a converted (bitmap) form of the font and are distributed under the same licence.
- Source TTF: `tools/fonts/ArchivoNarrow[wght].ttf`

Recreate or change sizes (e.g. after swapping the font):

```
pip install freetype-py
python tools/ttf2gfx.py --wght 700 "tools/fonts/ArchivoNarrow[wght].ttf" ArchivoNB 10 12 13 14 15 16 17 18 20 include/fonts
```

---

Die Schriftdateien `ArchivoNB<pt>pt7b.h` liegen **fertig bei** – einfach bauen und flashen. Schrift: Archivo Narrow
Bold (OFL 1.1, Lizenztext in `tools/fonts/OFL-ArchivoNarrow.txt`). Neu erzeugen nur bei Änderungen, Befehl siehe oben.
