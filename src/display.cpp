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
#if !__has_include("fonts/LibNarrowB10pt7b.h")
#error "Display fonts missing: see include/fonts/README.md (python tools/ttf2gfx.py ...)"
#endif
#include "fonts/LibNarrowB10pt7b.h"
#include "fonts/LibNarrowB12pt7b.h"
#include "fonts/LibNarrowB13pt7b.h"
#include "fonts/LibNarrowB14pt7b.h"
#include "fonts/LibNarrowB15pt7b.h"
#include "fonts/LibNarrowB16pt7b.h"
#include "fonts/LibNarrowB17pt7b.h"
#include "fonts/LibNarrowB18pt7b.h"
#include "fonts/LibNarrowB20pt7b.h"

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
struct DispCfg {
    uint8_t  design;         // 0..2
    uint8_t  dayBri;         // 0..100 %
    uint8_t  nightBri;       // 0..100 %
    bool     nightOn;
    uint16_t nightFrom;      // Minuten ab 0:00
    uint16_t nightTo;
    uint8_t  rotation;       // tft.setRotation-Index 0..3 (nur CYD)
    bool     autoLight;      // LDR (nur CYD): Regler = Minimum, Licht -> bis 100 %
};
static DispCfg s_cfg = { 1, 100, 20, false, 22 * 60, 7 * 60, TFT_ROTATION, false };
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
        p.end();
    }
    if (s_cfg.design > 2) s_cfg.design = 1;
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
// 1.3.1-d2f: Liberation Sans Narrow Bold im GFX-Format, Groessen und Geometrie aus der Duino-Vorlage des Users
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
static Field F_DOT, F_HASH, F_BEST, F_SHARE, F_BLOCK, F_CHIP, F_CLOCK, F_DATE, F_UP, F_POOL;
static int16_t L_W, L_Y0, L_CW, L_C2;
static bool    L_WIDE;

static void fset(Field& f, int16_t x, int16_t y, int16_t w, int16_t h, int16_t tx, int16_t base, uint8_t align)
{
    f.x = x; f.y = y + L_Y0; f.w = w; f.h = h; f.tx = tx; f.base = base + L_Y0; f.align = align; f.last[0] = 0;
}

static void layout_apply()
{
    L_W = s_w; L_WIDE = s_w > s_h; L_Y0 = s_h > s_w ? 40 : 0;
    L_CW = (L_W - 16) / 2;                 // 112 bzw. 152
    L_C2 = 5 + L_CW + 6;                   // 123 bzw. 163
    fset(F_DOT,   L_W - 26, 5, 20, 18, 0, 0, 0);
    fset(F_HASH,  8, 44, L_CW - 6, 35, 12, 74, 0);
    fset(F_BEST,  L_C2 + 3, 44, L_CW - 6, 35, L_C2 + 7, 74, 0);
    fset(F_SHARE, 80, 88, L_W - 88, 32, 84, 115, 0);
    fset(F_BLOCK, 56, 129, L_CW - 54, 32, 58, 154, 0);
    fset(F_CHIP,  L_C2 + 35, 129, L_CW - 38, 32, L_C2 + 37, 154, 0);
    fset(F_CLOCK, 10, 171, 100, 33, 14, 200, 0);
    fset(F_DATE,  100, 177, L_W - 108, 27, L_W - 12, 200, 2);
    fset(F_UP,    10, 209, L_WIDE ? 110 : 80, 27, 14, 230, 0);
    fset(F_POOL,  L_WIDE ? 120 : 90, 209, L_W - (L_WIDE ? 128 : 98), 27, L_W - 12, 230, 2);
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
    const int16_t y0 = L_Y0;
    tft.fillScreen(C_BG);
    tft.fillRect(0, y0, L_W, 27, C_PANEL2);          // Kopfband wie Vorlage
    tft.setFont(&LibNarrowB14pt7b);              // 15 pt zu breit (Foto 16.10.: "2" im Statuspunkt)
    tft.setTextColor(C_ACCENT);
    tft.setCursor(7, y0 + 23);
    tft.print("ESPRESSMINER32");

    tft.fillRoundRect(5, y0 + 30, L_CW, 51, 7, C_PANEL);
    tft.fillRoundRect(L_C2, y0 + 30, L_CW, 51, 7, C_PANEL);
    label(12, y0 + 35, "HASHRATE", C_PANEL);
    label(L_C2 + 7, y0 + 35, L_WIDE ? "BEST DIFFICULTY" : "BEST DIFF", C_PANEL);

    tft.fillRoundRect(5, y0 + 86, L_W - 10, 36, 7, C_PANEL);
    label(12, y0 + 100, "SHARES R/A", C_PANEL);

    tft.fillRoundRect(5, y0 + 127, L_CW, 36, 7, C_PANEL);
    tft.fillRoundRect(L_C2, y0 + 127, L_CW, 36, 7, C_PANEL);
    label(12, y0 + 141, "BLOCKS", C_PANEL);
    label(L_C2 + 7, y0 + 141, "CHIP", C_PANEL);

    tft.fillRoundRect(5, y0 + 168, L_W - 10, 70, 14, C_PANEL2);
    tft.drawFastHLine(14, y0 + 206, L_W - 28, C_LINE);

    for (Field* f : { &F_HASH, &F_BEST, &F_SHARE, &F_BLOCK, &F_CHIP }) f->bg = C_PANEL;   // Design kann wechseln
    for (Field* f : { &F_DOT, &F_CLOCK, &F_DATE, &F_UP, &F_POOL }) f->bg = C_PANEL2;
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
    n += field(F_HASH, b, fit(b, F_HASH.w - 8 - 27, { &LibNarrowB20pt7b, &LibNarrowB18pt7b, &LibNarrowB16pt7b, &LibNarrowB15pt7b }),
               C_GREEN, "kH/s");
    format_difficulty(g_stats.bestDiff, b, sizeof(b));
    n += field(F_BEST, b, fit(b, F_BEST.w - 8, { &LibNarrowB20pt7b, &LibNarrowB18pt7b, &LibNarrowB16pt7b, &LibNarrowB15pt7b }), C_BLUE);

    snprintf(b, sizeof(b), "%lu/%lu", (unsigned long)g_stats.rejected, (unsigned long)g_stats.accepted);
    {   // Vorlage nm_share_font_for(): <= 11 Zeichen 20 pt, 12 -> 18, 13 -> 17, 14 -> 16, laenger 15
        const size_t len = strlen(b);
        const GFXfont* f = len <= 11 ? &LibNarrowB20pt7b : len == 12 ? &LibNarrowB18pt7b :
                           len == 13 ? &LibNarrowB17pt7b : len == 14 ? &LibNarrowB16pt7b : &LibNarrowB15pt7b;
        n += field(F_SHARE, b, f, C_TEXT);
    }

    snprintf(b, sizeof(b), "%lu", (unsigned long)g_stats.blocksFound);
    n += field(F_BLOCK, b, (strlen(b) >= 5 || gfx_width(b, &LibNarrowB15pt7b) > 54) ? &LibNarrowB12pt7b : &LibNarrowB15pt7b,
               C_TEXT);
    if (g_stats.chipTemp > 0) {
        snprintf(b, sizeof(b), "%.0f", g_stats.chipTemp);
        n += field(F_CHIP, b, &LibNarrowB15pt7b, C_TEXT, nullptr, true);
    } else {
        n += field(F_CHIP, "--", &LibNarrowB15pt7b, C_MUTED);
    }

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year >= 120) {
        static const char* WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        n += field(F_CLOCK, b, &LibNarrowB20pt7b, C_TEXT);
        // Datum klein wie beim NerdMiner "FRI 16-10-2026" (User, Foto 16.10.: Uhr ist wichtig, Datum Nebensache), fest 10 pt
        snprintf(b, sizeof(b), "%s %02d-%02d-%04d", WD[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        n += field(F_DATE, b, L_WIDE ? &LibNarrowB13pt7b : &LibNarrowB10pt7b, C_MUTED);   // quer mehr Platz
    } else {
        n += field(F_CLOCK, "--:--", &LibNarrowB20pt7b, C_MUTED);
        n += field(F_DATE, "time ...", &LibNarrowB10pt7b, C_MUTED);
    }

    const uint32_t up = g_stats.uptimeS;
    snprintf(b, sizeof(b), "%lud %02lu:%02lu", (unsigned long)(up / 86400),
             (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60));
    const GFXfont* small = L_WIDE ? &LibNarrowB12pt7b : &LibNarrowB10pt7b;   // 10 pt wie Datum (User), quer 12 pt
    n += field(F_UP, b, small, C_MUTED);
    pool_name(b, sizeof(b), small, F_POOL.w - 6);
    n += field(F_POOL, b, small, C_ACCENT);
    return n;
}

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
    draw_frame();

    uint32_t lastLog = millis(), lastUpd = 0;
    for (;;) {
        brightness_tick();
        if (s_rotate) {
            s_rotate = false;
            tft.setRotation(s_cfg.rotation);
            s_w = tft.width(); s_h = tft.height();
            s_redraw = true;
        }
        if (s_redraw) { s_redraw = false; draw_frame(); lastUpd = 0; }
        if (lastUpd && millis() - lastUpd < DISPLAY_UPDATE_MS) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        lastUpd = millis() | 1;
        const uint32_t t0 = micros();
        const int n = update_fields();
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
    const bool redraw = c.design != s_cfg.design;
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
