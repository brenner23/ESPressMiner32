// Display-Ausgabe (ab 1.3.1-d1)
// D1: Startbild im Design "Espresso" (PLAN.md Abschnitt 4).
// D2: Fortschrittsbalken beim Start, danach Dashboard 240x240 (NM-TV). Jedes Feld merkt sich seinen
//     letzten Text; neu gezeichnet wird nur bei Aenderung: Rechteck in Blasenfarbe + neuer Text.
//     Die Blasen sind glatte Farbflaechen, darunter muss nichts neu gemalt werden.

#include "display.h"

DisplayStats g_disp = {};

#if HAS_DISPLAY

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"
#include "settings.h"
#include "stats.h"
#include "miner.h"
#include "utils.h"
#include <Preferences.h>
#include <driver/ledc.h>
#include <driver/touch_pad.h>
#if !__has_include("fonts/ArchivoNB10pt7b.h")
#error "Display fonts missing: see include/fonts/README.md"
#endif
#include "fonts/ArchivoNB10pt7b.h"
#include "fonts/ArchivoNB12pt7b.h"
#include "fonts/ArchivoNB13pt7b.h"
#include "fonts/ArchivoNB14pt7b.h"
#include "fonts/ArchivoNB15pt7b.h"
#include "fonts/ArchivoNB16pt7b.h"
#include "fonts/ArchivoNB17pt7b.h"
#include "fonts/ArchivoNB18pt7b.h"
#include "fonts/ArchivoNB20pt7b.h"
#include "fonts/ArchivoNB8pt7b.h"
#include "fonts/DSEG7B9pt7b.h"       // d10 Uhr-Seite: DSEG7/DSEG14 Classic Bold (keshikan, OFL)
#include "fonts/DSEG7B12pt7b.h"
#include "fonts/DSEG7B26pt7b.h"
#include "fonts/DSEG14B9pt7b.h"
#include "earth_mask.h"
#include "persist.h"
#include "sha256_hw.h"
#include <ArduinoJson.h>

#if defined(BOARD_CYD)
static SPIClass s_spi(HSPI);                       // Vorlage CYD: eigener HSPI-Bus (main.cpp Z. 90)
static Adafruit_ST7789 tft(&s_spi, TFT_CS, TFT_DC, TFT_RST);
#define TFT_SPI s_spi
#else
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
#define TFT_SPI SPI
#endif

// Designs (PLAN.md Abschnitt 4, RGB565): 0 Vorlage (Duino), 1 Espresso, 2 Nachtblau
//                               BG      PANEL   PANEL2  LINE    ACCENT  GREEN   BLUE    TEXT    MUTED
static const uint16_t DESIGNS[3][9] = {
    { 0x0841, 0x10A2, 0x18E3, 0x2945, 0xF5C0, 0x4E68, 0x3D9F, 0xFFFF, 0xAD55 },
    { 0x1061, 0x28E2, 0x3123, 0x49A4, 0xED8D, 0x8EB1, 0xF6B6, 0xFFBD, 0xBCF0 },
    { 0x0083, 0x1107, 0x118A, 0x222D, 0xFE89, 0x470F, 0x56BF, 0xFFFF, 0x8D58 },
};
static uint16_t C_BG, C_PANEL, C_PANEL2, C_LINE, C_ACCENT, C_GREEN, C_BLUE, C_TEXT, C_MUTED;

static void apply_design(uint8_t d)
{
    if (d > 2) d = 1;
    const uint16_t* c = DESIGNS[d];
    C_BG = c[0]; C_PANEL = c[1]; C_PANEL2 = c[2]; C_LINE = c[3]; C_ACCENT = c[4];
    C_GREEN = c[5]; C_BLUE = c[6]; C_TEXT = c[7]; C_MUTED = c[8];
}

// ---- Display-Einstellungen (ab 1.3.1-d3, NVS "espdisp") ---------------------------------------
// NM-TV: Tag-/Nachthelligkeit nach Uhrzeit (PLAN.md "Festgelegt 16.10."). Aenderungen wirken sofort.
// NM-TV-Touch an GPIO 32 (gemessen 17.11. an .45); CYD nutzt den Touchscreen-Meldepin
#if defined(BOARD_CYD)
#define TOUCH_DEFAULT -1
#else
#define TOUCH_DEFAULT 32
#endif
struct DispCfg {
    uint8_t  design;         // 0..2
    uint8_t  dayBri;         // 0..100 %
    uint8_t  nightBri;       // 0..100 %
    bool     nightOn;
    uint16_t nightFrom;      // Minuten ab 0:00
    uint16_t nightTo;
    uint8_t  rotation;       // tft.setRotation-Index 0..3 (nur CYD)
    bool     autoLight;      // LDR (nur CYD): Regler = Minimum, Licht -> bis 100 %
    uint8_t  page;           // d10: 0 Dashboard, 1 Uhr
    uint8_t  ckRight;        // d10 Uhr oben rechts: 0 Bloecke, 1 Dollarkurs
    uint8_t  ckBottom;       // d10 Uhr unten rechts: 0 IP, 1 Pool
    int8_t   touchPin;       // d10 NM-TV: Touch-Pin (-1 = aus, sonst 27/32/33)
    uint8_t  touchPct;       // d10: Schaltschwelle = Abfall in % vom Ruhewert (Kalibrierung je Gehaeuse)
};
static DispCfg s_cfg = { 1, 100, 20, false, 22 * 60, 7 * 60, TFT_ROTATION, false, 0, 0, 0, TOUCH_DEFAULT, 12 };
static volatile bool     s_rotate     = false;   // Drehung geaendert -> neu aufbauen
static int               s_ldrRaw     = -1;      // letzter LDR-Mittelwert (CYD)
static volatile bool     s_redraw     = false;   // Design gewechselt -> Dashboard komplett neu
static volatile int16_t  s_previewBri = -1;      // Vorschau beim Ziehen (-1 = aus)
static volatile uint32_t s_previewEnd = 0;
static float             s_curBri     = 100;     // aktuelle Helligkeit (sanftes Ueberblenden)
static volatile bool     s_bootFull   = true;    // Startbild immer 100 % (User 16.10.), ab Dashboard normale Regel

static void cfg_load()
{
    Preferences p;
    if (p.begin("espdisp", true)) {
        s_cfg.design    = p.getUChar("design", 1);
        s_cfg.dayBri    = p.getUChar("dayBri", 100);
        s_cfg.nightBri  = p.getUChar("nightBri", 20);
        s_cfg.nightOn   = p.getBool("nightOn", false);
        s_cfg.nightFrom = p.getUShort("nFrom", 22 * 60);
        s_cfg.nightTo   = p.getUShort("nTo", 7 * 60);
        s_cfg.rotation  = p.getUChar("rot", TFT_ROTATION);
        s_cfg.autoLight = p.getBool("autoLight", false);
        s_cfg.page      = p.getUChar("page", 0);
        s_cfg.ckRight   = p.getUChar("ckR", 0);
        s_cfg.ckBottom  = p.getUChar("ckB", 0);
        s_cfg.touchPin  = p.getChar("tPin", TOUCH_DEFAULT);
        s_cfg.touchPct  = p.getUChar("tPct", 12);   // MiniTV: TOUCH_DROP_PCT 12
        p.end();
    }
    if (s_cfg.design > 2) s_cfg.design = 1;
    if (s_cfg.page > 1) s_cfg.page = 0;
    if (s_cfg.ckRight > 1) s_cfg.ckRight = 0;
    if (s_cfg.ckBottom > 1) s_cfg.ckBottom = 0;
    if (s_cfg.touchPct < 3 || s_cfg.touchPct > 70) s_cfg.touchPct = 12;
    if (s_cfg.dayBri == 0) s_cfg.dayBri = 100;      // nie dunkel starten (wie Vorlage)
    if (s_cfg.dayBri > 100) s_cfg.dayBri = 100;
    if (s_cfg.nightBri > 100) s_cfg.nightBri = 100;
    if (s_cfg.nightFrom >= 1440) s_cfg.nightFrom = 22 * 60;
    if (s_cfg.nightTo >= 1440) s_cfg.nightTo = 7 * 60;
#if defined(BOARD_CYD)
    if (s_cfg.rotation > 3) s_cfg.rotation = TFT_ROTATION;
#else
    s_cfg.rotation = TFT_ROTATION;                  // NM-TV: keine Drehung
#endif
}

static void cfg_save()
{
    Preferences p;
    if (!p.begin("espdisp", false)) return;
    p.putUChar("design", s_cfg.design);
    p.putUChar("dayBri", s_cfg.dayBri);
    p.putUChar("nightBri", s_cfg.nightBri);
    p.putBool("nightOn", s_cfg.nightOn);
    p.putUShort("nFrom", s_cfg.nightFrom);
    p.putUShort("nTo", s_cfg.nightTo);
    p.putUChar("rot", s_cfg.rotation);
    p.putBool("autoLight", s_cfg.autoLight);
    p.putUChar("page", s_cfg.page);
    p.putUChar("ckR", s_cfg.ckRight);
    p.putUChar("ckB", s_cfg.ckBottom);
    p.putChar("tPin", s_cfg.touchPin);
    p.putUChar("tPct", s_cfg.touchPct);
    p.end();
}

// Backlight per LEDC-PWM (Hardware, kostet keine CPU). NM-TV LOW-aktiv -> invertieren.
static void backlight_write(float percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)(percent * 255.0f / 100.0f + 0.5f);
    if (TFT_BL_ON == LOW) duty = 255 - duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// -1 = Uhrzeit unbekannt, 0 = Tag, 1 = Nacht
static int night_state(uint16_t* minuteNow = nullptr)
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 120) return -1;
    const uint16_t m = t.tm_hour * 60 + t.tm_min;
    if (minuteNow) *minuteNow = m;
    if (!s_cfg.nightOn) return 0;
    const uint16_t a = s_cfg.nightFrom, b = s_cfg.nightTo;
    if (a == b) return 0;
    return (a < b ? (m >= a && m < b) : (m >= a || m < b)) ? 1 : 0;
}

#ifdef LDR_PIN
// CYD-LDR wie Vorlage (src/main.cpp nm_update_auto_brightness): 12 bit, ADC_11db, erster Wert verworfen, 10er-Mittel.
// Pull-up: hell = niedrig. Kalibrierung Vorlage: HELL ~0, DUNKEL ~900. Alle 200 ms (User: schneller), ohne delay.
static const int LDR_HELL = 0, LDR_DUNKEL = 1200;   // CYD 47 gemessen 16.10.: Raumlicht 0, Lappen 1240..1650 (Vorlage 900)
static void ldr_read()
{
    (void)analogRead(LDR_PIN);
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) sum += analogRead(LDR_PIN);
    s_ldrRaw = sum / 10;
}
#endif

static float target_bri()
{
    if (s_bootFull) return 100;
    if (s_previewBri >= 0 && (int32_t)(s_previewEnd - millis()) > 0) return s_previewBri;
    s_previewBri = -1;
#ifdef LDR_PIN
    // CYD (User 16.10.): nur Grundhelligkeit; Auto light an -> Grundhelligkeit = Minimum, Licht zieht bis 100 %
    if (!s_cfg.autoLight || s_ldrRaw < 0) return s_cfg.dayBri;
    float f = 1.0f - (float)(s_ldrRaw - LDR_HELL) / (LDR_DUNKEL - LDR_HELL);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return s_cfg.dayBri + (100 - s_cfg.dayBri) * f;
#else
    return night_state() == 1 ? s_cfg.nightBri : s_cfg.dayBri;
#endif
}

// alle 50 ms aus dem Display-Task: Vorschau sofort, heller 10 %/Schritt, dunkler 3 %/Schritt
static void brightness_tick()
{
#ifdef LDR_PIN
    static uint32_t lastLdr = 0;
    if (s_cfg.autoLight && millis() - lastLdr >= 200) { lastLdr = millis(); ldr_read(); }   // User: schneller reagieren
#endif
    const float t = target_bri();
    if (s_previewBri >= 0) s_curBri = t;
    else if (s_curBri < t) s_curBri = (t - s_curBri > 10) ? s_curBri + 10 : t;   // heller: schnell (Licht an -> < 1 s)
    else if (s_curBri > t) s_curBri = (s_curBri - t > 3) ? s_curBri - 3 : t;     // dunkler: sanft (kein Flackern bei Schatten)
    static float last = -1;
    if (s_curBri != last) { backlight_write(s_curBri); last = s_curBri; }
}
static const uint16_t C_OK     = 0x07E0;   // Statuspunkt
static const uint16_t C_WARN   = 0xFFE0;
static const uint16_t C_BAD    = 0xF800;

static int16_t s_w, s_h;

static void text_center(const char* s, int16_t y, uint8_t size, uint16_t fg, uint16_t bg)
{
    int16_t x1, y1; uint16_t w, h;
    tft.setFont(nullptr);
    tft.setTextSize(size);
    tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    tft.setTextColor(fg, bg);
    tft.setCursor((s_w - (int16_t)w) / 2, y);
    tft.print(s);
}

// ---- Startbild ------------------------------------------------------------------------------
static const int16_t BAR_X = 20, BAR_H = 10;

void display_begin()
{
#if TFT_EN_PIN >= 0
    pinMode(TFT_EN_PIN, OUTPUT);
    digitalWrite(TFT_EN_PIN, LOW);
#endif
    cfg_load();
    apply_design(s_cfg.design);
    // PWM 1 kHz / 8 bit wie Vorlage, aber direkt per ESP-IDF: Arduinos ledcAttach() zieht Code in den IRAM,
    // der durch HW_LOOP_PIN + schnellen SW-Scan voll ist (Build 16.10.: iram0_0_seg 56 Byte uebergelaufen)
    {
        ledc_timer_config_t tc = {};
        tc.speed_mode = LEDC_LOW_SPEED_MODE; tc.duty_resolution = LEDC_TIMER_8_BIT;
        tc.timer_num = LEDC_TIMER_0; tc.freq_hz = 1000; tc.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&tc);
        ledc_channel_config_t cc = {};
        cc.gpio_num = TFT_BL_PIN; cc.speed_mode = LEDC_LOW_SPEED_MODE; cc.channel = LEDC_CHANNEL_0;
        cc.intr_type = LEDC_INTR_DISABLE; cc.timer_sel = LEDC_TIMER_0; cc.duty = 0; cc.hpoint = 0;
        ledc_channel_config(&cc);
    }
    s_curBri = 100;                            // Startbild mit voller Helligkeit
    backlight_write(s_curBri);

    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    TFT_SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.init(TFT_W, TFT_H);
    tft.setSPISpeed(20000000);
    tft.setRotation(s_cfg.rotation);
#if defined(BOARD_CYD)
    tft.invertDisplay(false);                  // Vorlage CYD "FIX01": Panel zeigt RGB565 sonst vertauscht
#endif
#ifdef LDR_PIN
    analogReadResolution(12);
    analogSetPinAttenuation(LDR_PIN, ADC_11db);
    ldr_read();
#endif
    tft.setTextWrap(false);
    tft.cp437(true);                           // 0xF8 = Gradzeichen
    s_w = tft.width();
    s_h = tft.height();

    tft.fillScreen(C_BG);
    tft.fillRoundRect(10, 14, s_w - 20, 58, 12, C_ACCENT);
    text_center("ESPRESSMINER32", 26, 2, C_BG, C_ACCENT);
    text_center("v" MINER_VERSION, 52, 1, C_BG, C_ACCENT);

    tft.fillRoundRect(10, 88, s_w - 20, 60, 12, C_PANEL);
    text_center(BOARD_NAME, 102, 2, C_TEXT, C_PANEL);
    text_center("Bitcoin Stratum Miner", 128, 1, C_MUTED, C_PANEL);

    tft.drawRoundRect(BAR_X, s_h - 64, s_w - 2 * BAR_X, BAR_H, 5, C_LINE);
    display_progress(5, "booting");
    Serial.printf("[Display] %s %dx%d ready\n", BOARD_NAME, s_w, s_h);
}

void display_progress(int percent, const char* line)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const int16_t inner = s_w - 2 * BAR_X - 4;
    const int16_t fill  = inner * percent / 100;
    if (fill > 0) tft.fillRoundRect(BAR_X + 2, s_h - 62, fill, BAR_H - 4, 3, C_GREEN);
    tft.fillRect(0, s_h - 46, s_w, 12, C_BG);
    text_center(line, s_h - 44, 1, C_TEXT, C_BG);
}

// ---- Dashboard ------------------------------------------------------------------------------
// Schriften: ab 1.3.1-d6 Archivo Narrow Bold (OFL, include/fonts), vorher Liberation Narrow. Groessen und Geometrie aus der Duino-Vorlage des Users
// (duinocoin-NM-TV-154-Miner/include/NMDisplay.h): Blasen 112x51, Werte 20 pt, kleine Felder 15 pt (ab 5 Zeichen 12 pt),
// Shares nach Laenge 20/18/17/16/15 pt, Kopf 15 pt, Labels eingebaute Schrift.
// GFX-Schriften zeichnen keinen Hintergrund -> je Feld erst Rechteck in Blasenfarbe, dann Text (nur bei Aenderung).

struct Field {
    int16_t  x, y, w, h;     // Loeschrechteck
    int16_t  tx, base;       // Text: x links bzw. rechter Rand (align 2), Grundlinie
    uint16_t bg;
    uint8_t  align;          // 0 links, 2 rechts
    char     last[40];
};

// Layout: Werte aus den Vorlagen. 240 breit = NM-TV (y0 0) bzw. CYD hochkant (y0 40, Vorlage nm_y()),
// 320 breit = CYD quer (Vorlage "wide": Blasen 152 px). Wird bei Start und Drehung neu berechnet.
static Field F_DOT, F_HASH, F_BEST, F_NET, F_SHARE, F_BLOCK, F_CHIP, F_CLOCK, F_DATE, F_UP, F_POOL;
static int16_t L_W, L_Y0, L_CW, L_C2, L_DY;   // L_DY: Versatz der Zeilen unter Reihe 1 (hochkant: Netz-Diff-Zeile)
static uint8_t L_MODE;                         // 0 = 240x240 (NM-TV), 1 = hochkant 240x320, 2 = quer 320x240
static bool    L_WIDE;

static void fset(Field& f, int16_t x, int16_t y, int16_t w, int16_t h, int16_t tx, int16_t base, uint8_t align)
{
    f.x = x; f.y = y + L_Y0; f.w = w; f.h = h; f.tx = tx; f.base = base + L_Y0; f.align = align; f.last[0] = 0;
}

static void layout_apply()
{
    // Netz-Difficulty (User 16.10.): quer 3. Blase oben, hochkant eigene Zeile, NM-TV klein in der Best-Diff-Labelzeile
    L_W = s_w; L_WIDE = s_w > s_h;
    L_MODE = L_WIDE ? 2 : (s_h > s_w ? 1 : 0);
    L_Y0 = L_MODE == 1 ? 20 : 0;            // hochkant: 279 px Inhalt mittig in 320
    L_DY = L_MODE == 1 ? 41 : 0;
    L_CW = (L_W - 16) / 2;                 // 112 bzw. 152
    L_C2 = 5 + L_CW + 6;                   // 123 bzw. 163
    fset(F_DOT, L_W - 26, 5, 20, 18, 0, 0, 0);
    if (L_MODE == 2) {                     // quer: Hashrate 110 | Best 95 | Netz 95
        fset(F_HASH, 8, 44, 104, 35, 12, 74, 0);
        fset(F_BEST, 123, 44, 89, 35, 127, 74, 0);
        fset(F_NET,  223, 44, 89, 35, 227, 74, 0);
    } else {
        fset(F_HASH, 8, 44, L_CW - 6, 35, 12, 74, 0);
        fset(F_BEST, L_C2 + 3, 44, L_CW - 6, 35, L_C2 + 7, 74, 0);
        if (L_MODE == 1) fset(F_NET, 100, 88, L_W - 108, 32, 104, 115, 0);       // eigene Zeile
        else             fset(F_NET, L_C2 + 40, 34, L_CW - 46, 10, L_W - 12, 35, 2); // Labelzeile, eingebaute Schrift
    }
    const int16_t d = L_DY;
    fset(F_SHARE, 80, 88 + d, L_W - 88, 32, 84, 115 + d, 0);
    fset(F_BLOCK, 56, 129 + d, L_CW - 54, 32, 58, 154 + d, 0);
    fset(F_CHIP,  L_C2 + 35, 129 + d, L_CW - 38, 32, L_C2 + 37, 154 + d, 0);
    fset(F_CLOCK, 10, 171 + d, 100, 33, 14, 200 + d, 0);
    fset(F_DATE,  100, 177 + d, L_W - 108, 27, L_W - 12, 200 + d, 2);
    fset(F_UP,    10, 209 + d, L_WIDE ? 110 : 80, 27, 14, 230 + d, 0);
    fset(F_POOL,  L_WIDE ? 120 : 90, 209 + d, L_W - (L_WIDE ? 128 : 98), 27, L_W - 12, 230 + d, 2);
}

static int16_t gfx_width(const char* s, const GFXfont* f)
{
    int16_t x1, y1; uint16_t w, h;
    tft.setFont(f);
    tft.setTextSize(1);
    tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    return x1 + (int16_t)w;
}

// Zeichnet das Feld nur, wenn sich der Text geaendert hat.
// unit = kleiner Zusatz (eingebaute Schrift) direkt hinter der Zahl; degree = Gradzeichen + "C" in der Feldschrift
static bool field(Field& f, const char* txt, const GFXfont* font, uint16_t fg,
                  const char* unit = nullptr, bool degree = false)
{
    char key[40];
    snprintf(key, sizeof(key), "%s|%s|%p", txt, unit ? unit : "", (const void*)font);
    if (!strcmp(key, f.last)) return false;
    strlcpy(f.last, key, sizeof(f.last));

    tft.fillRect(f.x, f.y, f.w, f.h, f.bg);
    const int16_t tw = gfx_width(txt, font);
    const int16_t cw = degree ? gfx_width("C", font) + 7 : 0;
    const int16_t uw = unit ? (int16_t)strlen(unit) * 6 + 3 : 0;
    const int16_t x  = f.align == 2 ? f.tx - tw - cw - uw : f.tx;
    tft.setFont(font);
    tft.setTextColor(fg);
    tft.setCursor(x, f.base);
    tft.print(txt);
    if (degree) {
        const int16_t cx  = tft.getCursorX() + 3;
        const int16_t top = f.base - (font->yAdvance * 6 / 10);
        tft.drawCircle(cx, top + 2, 2, fg);
        tft.setCursor(cx + 4, f.base);
        tft.print("C");
    }
    if (unit) {
        const int16_t ux = tft.getCursorX() + 3;
        tft.setFont(nullptr);
        tft.setTextColor(fg, f.bg);
        tft.setCursor(ux, f.base - 7);
        tft.print(unit);
    }
    return true;
}

// Vorlage: Wert zu breit -> eine Stufe kleiner
static const GFXfont* fit(const char* s, int16_t maxW, std::initializer_list<const GFXfont*> fonts)
{
    const GFXfont* last = nullptr;
    for (const GFXfont* f : fonts) { last = f; if (gfx_width(s, f) <= maxW) return f; }
    return last;
}

static void label(int16_t x, int16_t y, const char* s, uint16_t bg)
{
    tft.setFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(C_MUTED, bg);
    tft.setCursor(x, y);
    tft.print(s);
}

static void draw_frame()
{
    s_bootFull = false;                        // Dashboard da -> eingestellte Helligkeit (sanft runter)
    layout_apply();
    const int16_t y0 = L_Y0, d = L_DY;
    tft.fillScreen(C_BG);
    tft.fillRect(0, y0, L_W, 27, C_PANEL2);          // Kopfband wie Vorlage
    tft.setFont(&ArchivoNB14pt7b);              // 15 pt zu breit (Foto 16.10.: "2" im Statuspunkt)
    tft.setTextColor(C_ACCENT);
    tft.setCursor(7, y0 + 23);
    tft.print("ESPRESSMINER32");

    if (L_MODE == 2) {
        tft.fillRoundRect(5, y0 + 30, 110, 51, 7, C_PANEL);
        tft.fillRoundRect(120, y0 + 30, 95, 51, 7, C_PANEL);
        tft.fillRoundRect(220, y0 + 30, 95, 51, 7, C_PANEL);
        label(12, y0 + 35, "HASHRATE", C_PANEL);
        label(127, y0 + 35, "BEST DIFF", C_PANEL);
        label(227, y0 + 35, "NETWORK DIFF", C_PANEL);
    } else {
        tft.fillRoundRect(5, y0 + 30, L_CW, 51, 7, C_PANEL);
        tft.fillRoundRect(L_C2, y0 + 30, L_CW, 51, 7, C_PANEL);
        label(12, y0 + 35, "HASHRATE", C_PANEL);
        label(L_C2 + 7, y0 + 35, L_MODE == 1 ? "BEST DIFF" : "BEST", C_PANEL);
        if (L_MODE == 1) {
            tft.fillRoundRect(5, y0 + 86, L_W - 10, 36, 7, C_PANEL);
            label(12, y0 + 100, "NETWORK DIFF", C_PANEL);
        }
    }

    tft.fillRoundRect(5, y0 + 86 + d, L_W - 10, 36, 7, C_PANEL);
    label(12, y0 + 100 + d, "SHARES R/A", C_PANEL);

    tft.fillRoundRect(5, y0 + 127 + d, L_CW, 36, 7, C_PANEL);
    tft.fillRoundRect(L_C2, y0 + 127 + d, L_CW, 36, 7, C_PANEL);
    label(12, y0 + 141 + d, "BLOCKS", C_PANEL);
    label(L_C2 + 7, y0 + 141 + d, "CHIP", C_PANEL);

    tft.fillRoundRect(5, y0 + 168 + d, L_W - 10, 70, 14, C_PANEL2);
    tft.drawFastHLine(14, y0 + 206 + d, L_W - 28, C_LINE);

    for (Field* f : { &F_HASH, &F_BEST, &F_NET, &F_SHARE, &F_BLOCK, &F_CHIP }) f->bg = C_PANEL;   // Design kann wechseln
    for (Field* f : { &F_DOT, &F_CLOCK, &F_DATE, &F_UP, &F_POOL }) f->bg = C_PANEL2;
}

// Difficulty kompakt (User 16.10.): max. 4 Nachkommastellen, ABGESCHNITTEN (nie aufrunden), Nullen am Ende weg;
// ab 1000 mit K/M/G/T/P und 1 Nachkommastelle (132.7T)
static void fmt_diff(double v, char* out, size_t n)
{
    static const char* U[] = { "", "K", "M", "G", "T", "P", "E" };
    int u = 0;
    while (v >= 1000.0 && u < 6) { v /= 1000.0; u++; }
    int dec = u ? 1 : (v < 1 ? 4 : v < 10 ? 3 : v < 100 ? 2 : 1);
    double m = 1; for (int i = 0; i < dec; i++) m *= 10;
    v = floor(v * m) / m;
    snprintf(out, n, "%.*f", dec, v);
    char* e = out + strlen(out) - 1;
    if (strchr(out, '.')) { while (*e == '0') *e-- = 0; if (*e == '.') *e = 0; }
    strlcat(out, U[u], n);
}

// Pool-Name aus der URL: ohne "stratum+tcp://" und ohne Port, zu breit -> ".."
static void pool_name(char* out, size_t len, const GFXfont* font, int16_t maxW)
{
    String u = g_settings.pools[g_stats.activePool].url;
    int p = u.indexOf("://");
    if (p >= 0) u = u.substring(p + 3);
    p = u.indexOf(':');
    if (p >= 0) u = u.substring(0, p);
    if (gfx_width(u.c_str(), font) > maxW) {
        while (u.length() > 3 && gfx_width((u + "..").c_str(), font) > maxW) u.remove(u.length() - 1);
        u += "..";
    }
    strlcpy(out, u.c_str(), len);
}

static int update_fields()
{
    int n = 0;
    char b[40];

    // Statuspunkt: gruen = online + mining, gelb = verbindet / Ueberhitzungsschutz, rot = kein WLAN/Pool
    uint16_t dot = C_OK; const char* dk = "g";
    if (WiFi.status() != WL_CONNECTED || !g_stats.poolConnected) { dot = C_BAD;  dk = "r"; }
    else if (!g_stats.poolAuthorized || thermal::level() > 0)     { dot = C_WARN; dk = "y"; }
    if (strcmp(dk, F_DOT.last)) {
        strlcpy(F_DOT.last, dk, sizeof(F_DOT.last));
        tft.fillRect(F_DOT.x, F_DOT.y, F_DOT.w, F_DOT.h, F_DOT.bg);
        tft.fillCircle(F_DOT.x + 10, F_DOT.y + 9, 6, dot);
        n++;
    }

    // Hashrate ohne Nachkomma, abgeschnitten (nie aufrunden); Web/API behalten die Nachkommastelle
    snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.khCurrent);
    n += field(F_HASH, b, fit(b, F_HASH.w - 8 - 27, { &ArchivoNB20pt7b, &ArchivoNB18pt7b, &ArchivoNB16pt7b, &ArchivoNB15pt7b }),
               C_GREEN, "kH/s");
    fmt_diff(g_stats.bestDiff, b, sizeof(b));
    n += field(F_BEST, b, fit(b, F_BEST.w - 8, { &ArchivoNB20pt7b, &ArchivoNB18pt7b, &ArchivoNB16pt7b, &ArchivoNB15pt7b }), C_BLUE);
    {   // Netz-Difficulty: quer/hochkant grosse Zahl, NM-TV klein "NET 75" in der Labelzeile
        char v[16];
        if (g_stats.networkDiff > 0) fmt_diff(g_stats.networkDiff, v, sizeof(v)); else strlcpy(v, "--", sizeof(v));
        if (L_MODE == 0) {
            snprintf(b, sizeof(b), "NET %s", v);
            n += field(F_NET, b, nullptr, C_MUTED);
        } else {
            n += field(F_NET, v, fit(v, F_NET.w - 8, { &ArchivoNB20pt7b, &ArchivoNB18pt7b, &ArchivoNB16pt7b, &ArchivoNB15pt7b }), C_TEXT);
        }
    }

    snprintf(b, sizeof(b), "%lu/%lu", (unsigned long)g_stats.rejected, (unsigned long)g_stats.accepted);
    {   // Vorlage nm_share_font_for(): <= 11 Zeichen 20 pt, 12 -> 18, 13 -> 17, 14 -> 16, laenger 15
        const size_t len = strlen(b);
        const GFXfont* f = len <= 11 ? &ArchivoNB20pt7b : len == 12 ? &ArchivoNB18pt7b :
                           len == 13 ? &ArchivoNB17pt7b : len == 14 ? &ArchivoNB16pt7b : &ArchivoNB15pt7b;
        n += field(F_SHARE, b, f, C_TEXT);
    }

    snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.blocksFound);
    n += field(F_BLOCK, b, (strlen(b) >= 5 || gfx_width(b, &ArchivoNB15pt7b) > 54) ? &ArchivoNB12pt7b : &ArchivoNB15pt7b,
               C_TEXT);
    if (g_stats.chipTemp > 0) {
        snprintf(b, sizeof(b), "%.0f", g_stats.chipTemp);
        n += field(F_CHIP, b, &ArchivoNB15pt7b, C_TEXT, nullptr, true);
    } else {
        n += field(F_CHIP, "--", &ArchivoNB15pt7b, C_MUTED);
    }

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year >= 120) {
        static const char* WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        n += field(F_CLOCK, b, &ArchivoNB20pt7b, C_TEXT);
        // Datum klein wie beim NerdMiner "FRI 16-10-2026" (User, Foto 16.10.: Uhr ist wichtig, Datum Nebensache), fest 10 pt
        snprintf(b, sizeof(b), "%s %02d-%02d-%04d", WD[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        n += field(F_DATE, b, L_WIDE ? &ArchivoNB13pt7b : &ArchivoNB10pt7b, C_MUTED);   // quer mehr Platz
    } else {
        n += field(F_CLOCK, "--:--", &ArchivoNB20pt7b, C_MUTED);
        n += field(F_DATE, "time ...", &ArchivoNB10pt7b, C_MUTED);
    }

    const uint32_t up = g_stats.uptimeS;
    snprintf(b, sizeof(b), "%lud %02lu:%02lu", (unsigned long)(up / 86400),
             (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60));
    const GFXfont* small = L_WIDE ? &ArchivoNB12pt7b : &ArchivoNB10pt7b;   // 10 pt wie Datum (User), quer 12 pt
    n += field(F_UP, b, small, C_MUTED);
    pool_name(b, sizeof(b), small, F_POOL.w - 6);
    n += field(F_POOL, b, small, C_ACCENT);
    return n;
}

// ---- Uhr-Seite (ab 1.3.1-d10) ---------------------------------------------------------------
// Layout vom User abgenommen (Erde-Punkte/IDEE_Uhr-Erde-Bilder.md): oben links kH/s (DSEG7) + "kH/s" (Archivo),
// oben rechts Bloecke oder Dollarkurs, Mitte Uhrzeit DSEG7 (Ziffern gleich breit -> springt nicht), Datum
// DSEG14 (Wochentag) + DSEG7, unten Version (nur Ziffern) und IP oder Pool (Archivo, zu lang -> "...").
// Hintergrund: eigene Punkte-Erde (global_land_mask), blass in der Stilfarbe.

static uint16_t rgb565_mix(uint16_t a, uint16_t b, float k)
{
    const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    const int r = ar + (br - ar) * k + 0.5f, g = ag + (bg - ag) * k + 0.5f, bl = ab + (bb - ab) * k + 0.5f;
    return (r << 11) | (g << 5) | bl;
}

static uint16_t s_dot, s_dotEdge;                 // Punktfarben der Erde (aus dem Stil)
static int16_t  E_X, E_Y, E_COLS, E_ROWS, E_BPR;   // Lage der Erde
static const uint8_t* E_MASK;
static int16_t  K_OX, K_OY;                       // Ursprung des 240x240-Uhr-Layouts

static void earth_colors()
{
    // Night blue = Tuerkis wie die Vorlage (User), Classic/Espresso = Akzentfarbe, beides blass ueber dem Hintergrund
    const uint16_t base = s_cfg.design == 2 ? 0x1186 : rgb565_mix(C_BG, C_ACCENT, 0.28f);
    s_dot     = base;
    s_dotEdge = rgb565_mix(C_BG, base, 0.45f);    // Ecken halb hell = leicht abgerundet
}

static bool earth_bit(int c, int r) { return pgm_read_byte(&E_MASK[r * E_BPR + (c >> 3)]) & (0x80 >> (c & 7)); }

// Rechteck mit Hintergrund + Erdpunkten fuellen (zum Loeschen eines Uhr-Feldes ohne Loch in der Karte)
static void earth_rect(int16_t rx, int16_t ry, int16_t rw, int16_t rh)
{
    tft.fillRect(rx, ry, rw, rh, C_BG);
    const int c0 = max(0, (rx - E_X) / 4), c1 = min((int)E_COLS - 1, (rx + rw - E_X) / 4);
    const int r0 = max(0, (ry - E_Y) / 4), r1 = min((int)E_ROWS - 1, (ry + rh - E_Y) / 4);
    if (c1 < c0 || r1 < r0) return;
    tft.startWrite();
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            if (!earth_bit(c, r)) continue;
            for (int dy = 0; dy < 3; dy++)
                for (int dx = 0; dx < 3; dx++) {
                    const int16_t x = E_X + c * 4 + dx, y = E_Y + r * 4 + dy;
                    if (x < rx || y < ry || x >= rx + rw || y >= ry + rh) continue;
                    tft.writePixel(x, y, (dx != 1 && dy != 1) ? s_dotEdge : s_dot);
                }
        }
    tft.endWrite();
}

struct CkField { int16_t x, y, w, h; char last[40]; };
static CkField K_HASH, K_RIGHT, K_TIME, K_DATE, K_VER, K_BOT;

static void ck_set(CkField& f, int16_t x, int16_t y, int16_t w, int16_t h)
{
    f.x = K_OX + x; f.y = K_OY + y; f.w = w; f.h = h; f.last[0] = 0;
}

static void draw_clock_frame()
{
    s_bootFull = false;
    earth_colors();
    if (s_w > s_h) { E_COLS = EARTH320_COLS; E_ROWS = EARTH320_ROWS; E_BPR = EARTH320_BPR; E_MASK = earth320_mask; }
    else           { E_COLS = EARTH240_COLS; E_ROWS = EARTH240_ROWS; E_BPR = EARTH240_BPR; E_MASK = earth240_mask; }
    K_OX = (s_w - 240) / 2; K_OY = (s_h - 240) / 2;
    E_X = (s_w - E_COLS * 4) / 2;
    E_Y = K_OY + 66 + (108 - E_ROWS * 4) / 2;
    tft.fillScreen(C_BG);
    earth_rect(E_X, E_Y, E_COLS * 4, E_ROWS * 4);
    // Ecktexte in die echten Bildschirmecken (CYD quer/hochkant, User 17.11.), Uhrzeit + Datum mittig
    ck_set(K_TIME, 0, 80, 240, 58);
    ck_set(K_DATE, 0, 146, 240, 26);
    K_HASH  = { 0, 0, (int16_t)(s_w - 70), 30, "" };
    K_RIGHT = { (int16_t)(s_w - 70), 0, 70, 30, "" };
    K_VER   = { 0, (int16_t)(s_h - 32), 90, 32, "" };
    K_BOT   = { 90, (int16_t)(s_h - 32), (int16_t)(s_w - 90), 32, "" };
}

static void put(const char* s, const GFXfont* f, int16_t x, int16_t base, uint16_t col)
{
    tft.setFont(f); tft.setTextSize(1); tft.setTextColor(col); tft.setCursor(x, base); tft.print(s);
}

// Feld nur bei Aenderung neu: Hintergrund (mit Erde) wiederherstellen, dann Text
static bool ck_changed(CkField& f, const char* key)
{
    if (!strcmp(key, f.last)) return false;
    strlcpy(f.last, key, sizeof(f.last));
    earth_rect(f.x, f.y, f.w, f.h);
    return true;
}

// Wie eine echte 7-Segment-Anzeige: nur geaenderte Zeichen neu malen (User 17.11.: ganzes Feld flackerte).
// DSEG-Ziffern sind gleich breit; bei anderer Laenge oder Lage -> ganzes Feld neu.
// x = linker Rand des Textes, Zelle = xAdvance des Zeichens, Hoehe aus yAdvance der Schrift.
static bool mono_diff(CkField& f, const char* txt, const GFXfont* font, int16_t x, int16_t base, uint16_t col)
{
    char key[40];
    snprintf(key, sizeof(key), "%s@%d", txt, x);
    const bool sameLayout = strlen(f.last) == strlen(key) && strrchr(f.last, '@') && !strcmp(strrchr(f.last, '@'), strrchr(key, '@'));
    if (!strcmp(key, f.last)) return false;
    if (!sameLayout) {
        strlcpy(f.last, key, sizeof(f.last));
        earth_rect(f.x, f.y, f.w, f.h);
        put(txt, font, x, base, col);
        return true;
    }
    const int16_t top = f.y, h = f.h;   // volle Zeilenhoehe: DSEG7 12 pt beginnt schon bei y 2 (Strichrest 17.11.)
    int16_t cx = x;
    for (size_t i = 0; txt[i]; i++) {
        const GFXglyph* g = &font->glyph[(uint8_t)txt[i] - font->first];
        const int16_t adv = pgm_read_byte(&g->xAdvance);
        if (txt[i] != f.last[i]) {
            earth_rect(cx, top, adv, h);
            char one[2] = { txt[i], 0 };
            put(one, font, cx, base, col);
        }
        cx += adv;
    }
    strlcpy(f.last, key, sizeof(f.last));
    return true;
}

static float s_usd = 0;                           // 1 EUR in USD (0 = noch unbekannt)

// Pool-Name fuer die Uhr: ohne Protokoll und Port, zu breit -> "..." (User 17.11.)
static void ck_pool(char* out, size_t len, const GFXfont* f, int16_t maxW)
{
    String u = g_settings.pools[g_stats.activePool].url;
    int p = u.indexOf("://");
    if (p >= 0) u = u.substring(p + 3);
    p = u.indexOf(':');
    if (p >= 0) u = u.substring(0, p);
    if (gfx_width(u.c_str(), f) > maxW) {
        while (u.length() > 3 && gfx_width((u + "...").c_str(), f) > maxW) u.remove(u.length() - 1);
        u += "...";
    }
    strlcpy(out, u.c_str(), len);
}

static int update_clock()
{
    int n = 0;
    char b[40];
    const int16_t X = K_OX, Y = K_OY;

    // oben links: kH/s ohne Nachkomma (abgeschnitten) + kleine Einheit in Archivo
    snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.khCurrent);
    {
        const size_t oldLen = strchr(K_HASH.last, '@') ? (size_t)(strchr(K_HASH.last, '@') - K_HASH.last) : 0;
        if (mono_diff(K_HASH, b, &DSEG7B12pt7b, 13, 25, C_GREEN)) {
            if (oldLen != strlen(b))   // Einheit nur neu, wenn sich die Stellenzahl aendert
                put("kH/s", &ArchivoNB8pt7b, 13 + gfx_width(b, &DSEG7B12pt7b) + 5, 25, C_GREEN);
            n++;
        }
    }
    // oben rechts: Bloecke (nur Zahl) oder Dollarkurs
    if (s_cfg.ckRight == 1) {
        if (s_usd > 0) snprintf(b, sizeof(b), "%.2f", s_usd); else strlcpy(b, "-.--", sizeof(b));
        char key[44]; snprintf(key, sizeof(key), "$%s", b);
        if (ck_changed(K_RIGHT, key)) {
            const int16_t w = gfx_width(b, &DSEG7B12pt7b), ws = gfx_width("$", &ArchivoNB12pt7b);
            put("$", &ArchivoNB12pt7b, s_w - 13 - w - ws - 2, 25, C_ACCENT);
            put(b, &DSEG7B12pt7b, s_w - 13 - w, 25, C_ACCENT);
            n++;
        }
    } else {
        snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.blocksFound);
        if (mono_diff(K_RIGHT, b, &DSEG7B12pt7b, s_w - 13 - gfx_width(b, &DSEG7B12pt7b), 25, C_ACCENT)) n++;
    }

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    const bool ok = t.tm_year >= 120;
    if (ok) snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min); else strlcpy(b, "--:--", sizeof(b));
    if (mono_diff(K_TIME, b, &DSEG7B26pt7b, X + (240 - gfx_width(b, &DSEG7B26pt7b)) / 2, Y + 134, ok ? C_TEXT : C_MUTED)) n++;

    static const char* WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    char d[16];
    if (ok) snprintf(d, sizeof(d), "%02d-%02d-%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900); else strlcpy(d, "--------", sizeof(d));
    snprintf(b, sizeof(b), "%s %s", ok ? WD[t.tm_wday] : "", d);
    if (ck_changed(K_DATE, b)) {
        const char* wd = ok ? WD[t.tm_wday] : "";
        const int16_t w1 = ok ? gfx_width(wd, &DSEG14B9pt7b) + 8 : 0, w2 = gfx_width(d, &DSEG7B9pt7b);
        const int16_t x = X + (240 - w1 - w2) / 2;
        if (ok) put(wd, &DSEG14B9pt7b, x, Y + 167, C_TEXT);
        put(d, &DSEG7B9pt7b, x + w1, Y + 167, C_TEXT);
        n++;
    }

    // unten links: Version nur aus Ziffern ("1.3.1-d10" -> "1.3.1.10", 7-Segment kann kein "d")
    String v = MINER_VERSION;
    v.replace("-d", ".");
    if (ck_changed(K_VER, v.c_str())) { put(v.c_str(), &ArchivoNB10pt7b, 13, s_h - 11, C_MUTED); n++; }
    // unten rechts: IP oder Pool
    const int16_t maxW = s_w - 26 - gfx_width(v.c_str(), &ArchivoNB10pt7b) - 12;
    if (s_cfg.ckBottom == 1) ck_pool(b, sizeof(b), &ArchivoNB10pt7b, maxW);
    else strlcpy(b, WiFi.localIP().toString().c_str(), sizeof(b));
    if (ck_changed(K_BOT, b)) { put(b, &ArchivoNB10pt7b, s_w - 13 - gfx_width(b, &ArchivoNB10pt7b), s_h - 11, C_MUTED); n++; }
    return n;
}

// Dollarkurs: open.er-api.com (Exchange Rate API, frei, ohne Schluessel, HTTP), alle 6 h, nur wenn eingestellt.
// Schlanker GET ueber WiFiClient: Arduinos HTTPClient zieht 1,2 KB in den vollen IRAM (Build 17.11.).
static void usd_fetch()
{
    static uint32_t next = 0;
    static uint32_t dueSince = 0;
    if (s_cfg.ckRight != 1 || WiFi.status() != WL_CONNECTED || (int32_t)(millis() - next) < 0) { dueSince = 0; return; }
    // Idee des Users: Abruf ins Schutzfenster der Fritzbox-WLAN-Welle legen (dort laeuft die HW-Schleife ohnehin
    // vorsichtig). Fenster kommt ~alle 10 min; spaetestens nach 15 min trotzdem holen (Wecker noch nicht eingelernt).
    if (!dueSince) dueSince = millis() | 1;
    if (!hw_sha_guard_active() && millis() - dueSince < 15UL * 60UL * 1000UL) return;
    dueSince = 0;
    next = millis() + 5UL * 60UL * 1000UL;             // Fehler -> in 5 min nochmal
    WiFiClient c;
    c.setTimeout(4);
    if (!c.connect("open.er-api.com", 80, 4000)) return;
    c.print("GET /v6/latest/USD HTTP/1.0\r\nHost: open.er-api.com\r\nUser-Agent: ESPressMiner32\r\nConnection: close\r\n\r\n");
    if (!c.find("\r\n\r\n")) { c.stop(); return; }   // Kopf ueberspringen
    JsonDocument filter; filter["rates"]["EUR"] = true;
    JsonDocument doc;
    if (!deserializeJson(doc, c, DeserializationOption::Filter(filter))) {
        const float eur = doc["rates"]["EUR"] | 0.0f;
        if (eur > 0.1f) { s_usd = 1.0f / eur; next = millis() + 6UL * 60UL * 60UL * 1000UL; }   // Kurs aendert sich 1x/Tag
    }
    c.stop();
}

// ---- Umschalten Dashboard <-> Uhr: Touch (CYD: XPT2046-Meldepin 36, NM-TV: Touch-Pin), BOOT kurz, Webseite
#if defined(BOARD_CYD)
static const int CYD_TOUCH_IRQ = 36;
#endif
static const int TOUCH_CAND[3] = { 27, 32, 33 };          // freie ESP32-Touch-Pins am NM-TV (Kalibrierung per Web)
static const touch_pad_t TOUCH_PAD[3] = { TOUCH_PAD_NUM7, TOUCH_PAD_NUM9, TOUCH_PAD_NUM8 };   // GPIO 27 / 32 / 33
static uint16_t s_touchRaw[3] = { 0, 0, 0 };
static uint16_t s_touchBase = 0;
static volatile uint32_t s_calEnd = 0;     // Kalibrierung laeuft bis (0 = aus)
static uint16_t s_calMin = 0;
static char     s_calMsg[64] = "";

static void page_toggle() { s_cfg.page ^= 1; cfg_save(); s_redraw = true; }

// Tasten/Touch: beim LOSLASSEN entscheiden (Regel des Users): kurz = Seite wechseln
static void press_logic(bool down, uint32_t& since, uint32_t now)
{
    if (down && !since) since = now;
    if (!down && since) {
        const uint32_t dur = now - since;
        since = 0;
        if (dur >= 40 && dur < 1000) page_toggle();
    }
}

// NM-TV-Touch: Logik 1:1 aus MiniTV-Hybrid-AllDevices/include/touch.h (Pin 32, Ruhewert laufend mitteln,
// TOUCH_DROP_PCT unter Ruhe = beruehrt, 3 Messungen bestaetigen, Ausloesen beim Loslassen nach 60 ms .. 3 s,
// danach 1 s Sperre, 10 s Dauerdruck -> neu einmessen). Gelesen per ESP-IDF mit kurzer Messzeit:
// Arduino touchRead() zieht Code in den vollen IRAM, und die Werks-Messzeit (~4 ms je Pin) bremste den
// SW-Miner auf Kern 0 um 2 kH/s (17.11.: ohne Touch 35,1, Werkszeit 33,0, kurze Messzeit 35,0).
static uint16_t touch_read_pad(int idx)
{
    static bool init = false;
    if (!init) {
        init = true;
        touch_pad_init();
        touch_pad_set_fsm_mode(TOUCH_FSM_MODE_SW);
        touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
        touch_pad_set_meas_time(0x1000, 0x400);          // 0x400 Takte @ 8 MHz = 0,13 ms je Messung
        for (int i = 0; i < 3; i++) touch_pad_config(TOUCH_PAD[i], 0);
    }
    uint16_t v = 0;
    touch_pad_read(TOUCH_PAD[idx], &v);
    s_touchRaw[idx] = v;
    return v;
}

static bool touch_pressed(int idx, uint32_t now)
{
    static uint32_t lastSample = 0, pressStart = 0, lastTrigger = 0;
    static int  below = 0;
    static bool pressed = false;
    static float base = 0;
    if (now - lastSample < 20) return false;
    lastSample = now;
    const uint16_t v = touch_read_pad(idx);
    if (!v) return false;
    if (base <= 0) { base = v; return false; }
    s_touchBase = (uint16_t)base;
    const float thresh  = base * (100 - s_cfg.touchPct) / 100.0f;
    const float release = base * (100 - s_cfg.touchPct / 2) / 100.0f;
    if (s_calEnd) {                                   // Kalibrierung: 5 s Finger drauf, tiefsten Wert merken
        if (v < s_calMin) s_calMin = v;
        if ((int32_t)(now - s_calEnd) >= 0) {
            s_calEnd = 0;
            const int drop = (int)((base - s_calMin) * 100 / base);
            if (drop >= 6) {
                s_cfg.touchPct = constrain(drop / 2, 3, 70);   // Schwelle = Mitte zwischen frei und beruehrt
                cfg_save();
                snprintf(s_calMsg, sizeof(s_calMsg), "OK: touch drops %d %%, threshold now %d %%", drop, s_cfg.touchPct);
            } else {
                snprintf(s_calMsg, sizeof(s_calMsg), "No touch seen (drop %d %%) - threshold kept at %d %%", drop, s_cfg.touchPct);
            }
        }
        pressed = false; below = 0;
        return false;
    }
    if (!pressed) {
        if (v < thresh) { if (++below >= 3) { pressed = true; pressStart = now; below = 0; } }
        else { below = 0; if (v > release) base = base * 0.98f + v * 0.02f; }   // Drift nachfuehren
        return false;
    }
    if (now - pressStart > 10000UL) { base = v; pressed = false; return false; }   // Dauerdruck -> neu einmessen
    if (v > release) {
        pressed = false;
        const uint32_t dur = now - pressStart;
        if (dur >= 60 && dur <= 3000 && now - lastTrigger >= 1000) { lastTrigger = now; return true; }
    }
    return false;
}

static void input_tick()
{
    static uint32_t bootSince = 0, touchSince = 0;
    const uint32_t now = millis();
    press_logic(digitalRead(0) == LOW, bootSince, now);          // BOOT (GPIO 0, falls vorhanden); 5 s = Werksreset
#if defined(BOARD_CYD)
    press_logic(digitalRead(CYD_TOUCH_IRQ) == LOW, touchSince, now);
#else
    (void)touchSince;
    int idx = -1;
    for (int i = 0; i < 3; i++) if (TOUCH_CAND[i] == s_cfg.touchPin) idx = i;
    if (idx >= 0 && touch_pressed(idx, now)) page_toggle();
    // die anderen beiden Pins nur selten (Anzeige auf der Webseite)
    static uint32_t lastOther = 0;
    if (now - lastOther >= 2000) { lastOther = now; for (int i = 0; i < 3; i++) if (i != idx) touch_read_pad(i); }
#endif
}

static void draw_page() { if (s_cfg.page == 1) draw_clock_frame(); else draw_frame(); }

static void display_task(void*)
{
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");

    // Startbild: auf den Pool warten (max. 20 s), dann Dashboard
    display_progress(85, "pool connecting");
    for (int i = 0; i < 200 && !g_stats.poolAuthorized; i++) vTaskDelay(pdMS_TO_TICKS(100));
    {   // IP 3 s stehen lassen (User: "damit ich weiss, welche IP")
        char line[48];
        snprintf(line, sizeof(line), "%s  %s", g_stats.poolAuthorized ? "pool OK" : "no pool yet",
                 WiFi.localIP().toString().c_str());
        display_progress(100, line);
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
#if defined(BOARD_CYD)
    pinMode(CYD_TOUCH_IRQ, INPUT);
#endif
    draw_page();

    uint32_t lastLog = millis(), lastUpd = 0;
    for (;;) {
        brightness_tick();
        input_tick();
        usd_fetch();
        if (s_rotate) {
            s_rotate = false;
            tft.setRotation(s_cfg.rotation);
            s_w = tft.width(); s_h = tft.height();
            s_redraw = true;
        }
        if (s_redraw) { s_redraw = false; draw_page(); lastUpd = 0; }
        if (lastUpd && millis() - lastUpd < DISPLAY_UPDATE_MS) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        lastUpd = millis() | 1;
        const uint32_t t0 = micros();
        const int n = s_cfg.page == 1 ? update_clock() : update_fields();
        const uint32_t dt = micros() - t0;
        g_disp.updates++;
        g_disp.fields += n;
        g_disp.busyUs += dt;
        if (dt > g_disp.maxUs) g_disp.maxUs = dt;
        if (millis() - lastLog >= 60000UL) {
            lastLog = millis();
            Serial.printf("[Display] %lu updates, %lu fields, avg %lu us, max %lu us\n",
                          (unsigned long)g_disp.updates, (unsigned long)g_disp.fields,
                          (unsigned long)(g_disp.updates ? g_disp.busyUs / g_disp.updates : 0),
                          (unsigned long)g_disp.maxUs);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void hhmm(char* out, size_t n, uint16_t m) { snprintf(out, n, "%02u:%02u", m / 60, m % 60); }

static bool parse_hhmm(const char* s, uint16_t& out)
{
    int h, m;
    if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
    out = h * 60 + m;
    return true;
}

void display_cfg_json(JsonDocument& d)
{
    char a[8], b[8];
    hhmm(a, sizeof(a), s_cfg.nightFrom);
    hhmm(b, sizeof(b), s_cfg.nightTo);
    d["board"]     = BOARD_NAME;
    d["design"]    = s_cfg.design;
    d["dayBri"]    = s_cfg.dayBri;
    d["nightBri"]  = s_cfg.nightBri;
    d["nightOn"]   = s_cfg.nightOn;
    d["nightFrom"] = a;
    d["nightTo"]   = b;
#ifdef LDR_PIN
    d["hasLdr"]    = true;
#else
    d["hasLdr"]    = false;
#endif
    d["hasRotation"] = TFT_W != TFT_H;
    d["rotation"]  = s_cfg.rotation;
    d["autoLight"] = s_cfg.autoLight;
    d["ldr"]       = s_ldrRaw;
#ifdef LDR_PIN
    d["hasNight"]  = false;            // CYD: keine Nachtzeiten (User 16.10.), dafuer LDR
#else
    d["hasNight"]  = true;
#endif
    d["nowBri"]    = (int)(s_curBri + 0.5f);
    d["page"]      = s_cfg.page;                   // d10: 0 Dashboard, 1 Uhr
    d["ckRight"]   = s_cfg.ckRight;
    d["ckBottom"]  = s_cfg.ckBottom;
    d["usd"]       = s_usd;
#if !defined(BOARD_CYD)
    d["touchPin"]  = s_cfg.touchPin;
    d["touchPct"]  = s_cfg.touchPct;
    d["touchBase"] = s_touchBase;
    d["touchCal"]  = s_calEnd ? "running" : s_calMsg;
    JsonArray tr = d["touch"].to<JsonArray>();     // Rohwerte Pin 27/32/33 (Beruehren = Wert faellt)
    for (int i = 0; i < 3; i++) tr.add(s_touchRaw[i]);
#endif
    // Statuszeile: "now: night until 07:00" / "now: day, night starts 22:00"
    char st[48];
    const int ns = night_state();
#ifdef LDR_PIN
    if (s_cfg.autoLight) snprintf(st, sizeof(st), "auto light on, sensor %d (0 bright .. 1200 dark)", s_ldrRaw);
    else strlcpy(st, "auto light off: fixed brightness", sizeof(st));
    if (false)
#endif
    if (!s_cfg.nightOn)  strlcpy(st, "night mode off", sizeof(st));
    else if (ns < 0)     strlcpy(st, "waiting for time (NTP)", sizeof(st));
    else if (ns == 1)    snprintf(st, sizeof(st), "now: night until %s", b);
    else                 snprintf(st, sizeof(st), "now: day, night starts %s", a);
    d["status"] = st;
}

const char* display_cfg_set(JsonDocument& j)
{
    DispCfg c = s_cfg;
    if (j["design"].is<int>())   c.design   = constrain((int)j["design"], 0, 2);
    if (j["dayBri"].is<int>())   c.dayBri   = constrain((int)j["dayBri"], 1, 100);
    if (j["nightBri"].is<int>()) c.nightBri = constrain((int)j["nightBri"], 0, 100);
    if (j["nightOn"].is<bool>()) c.nightOn  = j["nightOn"];
#if defined(BOARD_CYD)
    if (j["rotation"].is<int>()) c.rotation = constrain((int)j["rotation"], 0, 3);
#endif
#ifdef LDR_PIN
    if (j["autoLight"].is<bool>()) c.autoLight = j["autoLight"];
#endif
    if (j["nightFrom"].is<const char*>() && !parse_hhmm(j["nightFrom"], c.nightFrom)) return "Night from: use HH:MM";
    if (j["nightTo"].is<const char*>()   && !parse_hhmm(j["nightTo"], c.nightTo))     return "Night to: use HH:MM";
    if (j["page"].is<int>())     c.page     = constrain((int)j["page"], 0, 1);
    if (j["ckRight"].is<int>())  c.ckRight  = constrain((int)j["ckRight"], 0, 1);
    if (j["ckBottom"].is<int>()) c.ckBottom = constrain((int)j["ckBottom"], 0, 1);
#if !defined(BOARD_CYD)
    if (j["touchPin"].is<int>()) { const int t = j["touchPin"]; c.touchPin = (t == 27 || t == 32 || t == 33) ? t : -1; }
    if (j["touchPct"].is<int>()) c.touchPct = constrain((int)j["touchPct"], 3, 70);
    if (j["touchCal"].is<bool>() && j["touchCal"]) {            // 5 s Finger auf den Sensor
        s_calMin = 0xFFFF; s_calMsg[0] = 0; s_calEnd = millis() + 5000;
    }
#endif
    const bool redraw = c.design != s_cfg.design || c.page != s_cfg.page ||
                        c.ckRight != s_cfg.ckRight || c.ckBottom != s_cfg.ckBottom;
    const bool rotate = c.rotation != s_cfg.rotation;
    s_cfg = c;
    cfg_save();
    if (redraw) { apply_design(s_cfg.design); s_redraw = true; }
    if (rotate) s_rotate = true;
    s_previewEnd = millis() + 5000;    // nach dem Loslassen den Wert noch ~5 s zeigen, dann gueltiger Zustand
    return nullptr;
}

void display_preview(int percent)
{
    s_previewBri = constrain(percent, 0, 100);
    s_previewEnd = millis() + 5000;
}

void display_start_task()
{
    xTaskCreatePinnedToCore(display_task, "display", 4096, nullptr, 1, nullptr, 0);
}

#endif
