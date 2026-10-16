#pragma once
// ---------------------------------------------------------------------------
//  Board-Varianten (Display-Version, ab 1.3.1-d0)
//  Auswahl per platformio.ini:  esp32dev (ohne Display) | nmtv154 | cyd
//  Pins aus den getesteten Duino-Coin-Projekten (duinocoin-NM-TV-154-Miner, duinocoin-NM-CYD-ST7789)
// ---------------------------------------------------------------------------

#if defined(BOARD_NMTV154)
  #define HAS_DISPLAY     1
  #define BOARD_NAME      "NM-TV 154"
  #define TFT_MOSI        13
  #define TFT_MISO        12
  #define TFT_SCK         14
  #define TFT_CS          15
  #define TFT_DC          2        // GPIO 2 = DC -> keine Onboard-LED
  #define TFT_RST         4
  #define TFT_EN_PIN      21       // Displayversorgung, muss LOW sein
  #define TFT_BL_PIN      19       // Backlight
  #define TFT_BL_ON       LOW      // LOW-aktiv
  #define TFT_W           240
  #define TFT_H           240
  #define TFT_ROTATION    2        // wie im Duino-Projekt
#elif defined(BOARD_CYD)
  #define HAS_DISPLAY     1
  #define BOARD_NAME      "CYD ST7789"
  #define TFT_MOSI        13
  #define TFT_MISO        12
  #define TFT_SCK         14
  #define TFT_CS          15
  #define TFT_DC          2
  #define TFT_RST         -1
  #define TFT_EN_PIN      -1
  #define TFT_BL_PIN      21
  #define TFT_BL_ON       HIGH
  #define TFT_W           240      // tft.init(240, 320)
  #define TFT_H           320
  #define TFT_ROTATION    1        // quer (noch nicht getestet)
  #define LDR_PIN         34
#else
  #define HAS_DISPLAY     0
  #define BOARD_NAME      "DevKit"
#endif
