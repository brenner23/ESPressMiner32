#!/usr/bin/env python3
"""TTF -> Adafruit-GFX-Font (.h), wie Adafruits fontconvert (141 dpi, 1 bit, Zeichen 0x20..0x7E).
Aufruf: python ttf2gfx.py [--wght 700] <schrift.ttf> <name> <pt> [pt ...] <ausgabeordner>
  --wght: Gewicht bei variablen Schriften (z. B. ArchivoNarrow[wght].ttf, 700 = Bold)
Benoetigt: pip install freetype-py"""
import os, sys, freetype

def convert(path, name, pt, outdir, first=0x20, last=0x7E, wght=None):
    face = freetype.Face(path)
    if wght: face.set_var_design_coords((wght,))
    face.set_char_size(pt << 6, 0, 141, 0)
    bits, glyphs = bytearray(), []
    for c in range(first, last + 1):
        face.load_char(chr(c), freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_RENDER)
        g = face.glyph; bm = g.bitmap
        off = len(bits); acc = 0; n = 0
        for y in range(bm.rows):
            for x in range(bm.width):
                bit = (bm.buffer[y * bm.pitch + (x >> 3)] >> (7 - (x & 7))) & 1
                acc = (acc << 1) | bit; n += 1
                if n == 8: bits.append(acc); acc = 0; n = 0
        if n: bits.append(acc << (8 - n))
        glyphs.append((off, bm.width, bm.rows, g.advance.x >> 6, g.bitmap_left, 1 - g.bitmap_top, chr(c)))
    var = f"{name}{pt}pt7b"
    ya = face.size.height >> 6
    lic = face.get_sfnt_name(next(i for i in range(face.sfnt_name_count) if face.get_sfnt_name(i).name_id == 13)).string
    try: lic = lic.decode("utf-16-be")
    except Exception: lic = lic.decode("latin1")
    out = [f"// {var} - erzeugt mit tools/ttf2gfx.py aus {os.path.basename(path)}, {pt} pt @ 141 dpi",
           f"// Schrift-Lizenz: {lic.splitlines()[0][:150]}", "#pragma once", "#include <Adafruit_GFX.h>", "",
           f"const uint8_t {var}Bitmaps[] PROGMEM = {{"]
    for i in range(0, len(bits), 16):
        out.append("  " + ", ".join(f"0x{b:02X}" for b in bits[i:i + 16]) + ",")
    out += ["};", "", f"const GFXglyph {var}Glyphs[] PROGMEM = {{"]
    for o, w, h, a, x, y, ch in glyphs:
        out.append(f"  {{ {o:5d}, {w:3d}, {h:3d}, {a:3d}, {x:4d}, {y:4d} }},   // 0x{ord(ch):02X} {ch if ch != chr(92) else 'backslash'}")
    out += ["};", "", f"const GFXfont {var} PROGMEM = {{ (uint8_t*){var}Bitmaps, (GFXglyph*){var}Glyphs, 0x{first:02X}, 0x{last:02X}, {ya} }};", ""]
    p = os.path.join(outdir, var + ".h")
    open(p, "w", encoding="utf-8").write("\n".join(out))
    print(f"{p}: {len(bits)} Bytes Bitmap, yAdvance {ya}")

if __name__ == "__main__":
    args = sys.argv[1:]
    wght = None
    if args[:1] == ["--wght"]: wght = int(args[1]); args = args[2:]
    ttf, name, *pts, outdir = args
    for pt in pts: convert(ttf, name, int(pt), outdir, wght=wght)
