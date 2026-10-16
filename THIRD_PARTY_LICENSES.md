# Third-party components / Fremdkomponenten

```
The source code in this repository is MIT-licensed. It was written for this
project, except for the file listed under "Included third-party code".

Credits / Dank:
  - SparkMiner (MIT)  https://github.com/BitzyLabs/sparkminer
    Idea only: writing the SHA text registers as buffered stores without a
    `memw` barrier. Implemented independently in C++ (src/sha256_hw.cpp).

Included third-party code / Enthaltener Fremdcode (since v1.2.0):
  - NerdMiner Modded (MIT)  https://github.com/HenrysCat/NerdMiner-Modded
                            https://henryscat.github.io/NerdMiner-Modded/
    src/hw_scan_v2.S is a port of src/pipelined_hw_sha_classic_v2.S
    (pipelined SHA-256d loop for the classic ESP32, measured delays unchanged).
    Also taken from there: the known-answer test idea/vector (verified
    independently in Python) and the runtime fallback on unconfirmed
    candidates. Key insight from that project: stores to the SHA registers
    must be >= 3 cycles apart, otherwise they get lost while the other core
    uses the DPORT bus - which made the software miner on core 0 usable.

Libraries fetched at build time (not part of this repository):
  - ArduinoJson               MIT         https://github.com/bblanchon/ArduinoJson
  - Arduino core for ESP32    LGPL-2.1    https://github.com/espressif/arduino-esp32
  - ESP-IDF                   Apache-2.0  https://github.com/espressif/esp-idf
  - pioarduino platform       Apache-2.0  https://github.com/pioarduino/platform-espressif32

Display version (env nmtv154 / cyd), fetched at build time:
  - Adafruit GFX Library      BSD         https://github.com/adafruit/Adafruit-GFX-Library
  - Adafruit ST7735/ST7789    MIT         https://github.com/adafruit/Adafruit-ST7735-Library
  - Adafruit BusIO            MIT         https://github.com/adafruit/Adafruit_BusIO
  Fonts are NOT included: include/fonts/*.h are generated locally with
  tools/ttf2gfx.py (needs freetype-py, BSD/FreeType licence) from a TTF of
  your choice, e.g. Liberation Sans Narrow Bold (GPLv2 with font exception).
  Layout and font sizes follow the author's own Duino-Coin display projects.

Setup tools (tools/*.py) need:
  - pyserial                  BSD-3-Clause
```

---------------------------------------------------------------------------
NerdMiner Modded - license text (applies to src/hw_scan_v2.S)
---------------------------------------------------------------------------

MIT License

Copyright (c) 2023 Bitmaker
Copyright (c) 2026 HenrysCat (NerdMiner Modded modifications)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Previous work:
HAN Miner - Copyright (c) 2022 Valerio Vaccaro
