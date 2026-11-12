/*
  Mavrik OS 0.2 — firmware for the Mavrik Pocket prototype (ESP32-S3 N16R8)
  --------------------------------------------------------------------------
  Home, Record idea, Ideas, To-do, Music, Settings and About, on a 2.8" 240x320
  ILI9341 screen, with real audio:
    - Ideas are recorded from the INMP441 mic to /ideas/idea_<n>.wav on the SD card
    - Ideas and music play through the MAX98357A amp and speaker (WAV files)
    - Scroll with the KY-040 encoder, or the copper touch wheel (see mavrik_config.h)
    - Haptic ticks from a coin vibration motor
    - Optional Wi-Fi: real clock, and ideas sent off for speech-to-text

  Build (Arduino IDE 2, "esp32 by Espressif" 3.x): board "ESP32S3 Dev Module",
  Flash Size 16MB, PSRAM "OPI PSRAM", USB CDC On Boot "Enabled",
  Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)". Libraries: Adafruit GFX, Adafruit BusIO.
  Pins and options: mavrik_config.h. Wiring and SD card layout: README.md.

  Status: compiles for ESP32-S3; not yet run on the real hardware.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <esp_chip_info.h>
#include "mavrik_config.h"
#include "mavrik_audio.h"
#include "mavrik_net.h"
#if MAVRIK_WHEEL_TOUCH
#include "mavrik_touchwheel.h"
#endif
#include <math.h>
#include <string.h>
#include <stdio.h>

// Pins: see mavrik_config.h

// ============================================================================
// Types (all types first, so the Arduino builder's prototypes compile)
// ============================================================================
enum Screen : uint8_t {
  SCR_HOME, SCR_IDEAS, SCR_IDEA, SCR_TODO, SCR_MUSIC, SCR_PLAYER,
  SCR_SETTINGS, SCR_ABOUT, SCR_RECORD, SCR_COUNT
};

enum Icon : uint8_t {
  IC_NONE, IC_MIC, IC_BULB, IC_CHECK, IC_NOTE, IC_GEAR, IC_INFO,
  IC_PLAY, IC_PAUSE, IC_TRASH, IC_WAVE, IC_TODO_OPEN, IC_TODO_DONE
};

enum Ev : uint8_t { EV_NONE, EV_UP, EV_DOWN, EV_CW, EV_CCW, EV_SELECT, EV_BACK, EV_ACTION };

struct Palette {
  uint16_t bg, platter, platterFocus, outline, text, sub, accent, accentText,
           onAccent, track, danger, glyph;
};

struct TypeSet {
  const GFXfont *body, *sub, *hero;
  int16_t rowMin, icon, padV;
};

struct Settings {
  uint8_t theme;        // 0 dark, 1 light, 2 auto (dark 7pm-7am)
  uint8_t textSize;     // 0 standard, 1 large, 2 extra large
  uint8_t bold, contrast, reduceMotion, haptics, wheelRev;
};

struct Row {
  char label[40];
  char sub[48];
  uint8_t icon;
  uint16_t iconColor;
  int8_t toggle;        // -1 none, 0 off, 1 on
  bool chevron, accent, danger, strike;
  // layout (filled by layoutRows)
  char l1[40], l2[40];
  uint8_t lines;
  int16_t y, h;
};

struct ListState { int16_t focus; float scroll, hl, hlH; };

struct Idea { uint16_t num, secs, minute; };

#define MAX_IDEAS 24
struct IdeaStore { uint8_t count; uint16_t next; Idea list[MAX_IDEAS]; };

struct Track { char title[32]; char artist[28]; uint16_t secs; char path[40]; };

struct Button { uint8_t pin; Ev ev; bool repeat; bool stable, lastRaw; uint32_t changedAt, pressedAt, lastRepeat; };

struct Transition { bool active; Screen from, to; int8_t dir; uint32_t t0; };

// The screen is drawn in horizontal strips ("bands") so it fits in whatever
// memory is free. Drawing uses whole-screen coordinates; the band offset
// (oy) is subtracted here, at the three places every shape and letter pass through.
class BandCanvas : public GFXcanvas16 {
 public:
  int16_t oy = 0;
  BandCanvas(uint16_t w, uint16_t h) : GFXcanvas16(w, h, false) {}
  void attach(uint16_t *buf, int16_t rows) {
    buffer = buf;
    WIDTH = _width = 240;
    HEIGHT = _height = rows;
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override { GFXcanvas16::drawPixel(x, y - oy, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { GFXcanvas16::drawFastVLine(x, y - oy, h, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { GFXcanvas16::drawFastHLine(x, y - oy, w, c); }
  // Letters are clipped against the strip height before drawing, so draw
  // each one in strip coordinates instead.
  size_t write(uint8_t c) override {
    int16_t saved = oy;
    cursor_y -= saved; oy = 0;
    size_t n = GFXcanvas16::write(c);
    oy = saved; cursor_y += saved;
    return n;
  }
};

// ============================================================================
// Globals
// ============================================================================
static const int16_t W = 240, H = 320, HEADER_H = 38;
static const uint32_t TR_MS = 200;
#define MAX_ROWS 24
#define MAX_TRACKS 20

BandCanvas canvas(W, H);   // buffer attached in setup()
int16_t bandH = 0;
Preferences prefs;

Palette pal;
TypeSet ty;
Settings cfg = {0, 0, 0, 0, 0, 1, 0};

Screen navStack[8];
int8_t navDepth = 0;
ListState lists[SCR_COUNT];
Transition tr = {false, SCR_HOME, SCR_HOME, 1, 0};

Row rowsA[MAX_ROWS], rowsB[MAX_ROWS];

IdeaStore ideas;
int16_t openIdea = -1;          // index into ideas.list
bool ideaPlaying = false;
uint32_t ideaPlayStart = 0;

const char *todoText[] = {
  "Solder mic and amp", "Print the case", "Tune the touch wheel",
  "Load music on SD card", "Record a demo video"
};
const uint8_t TODO_COUNT = 5;
uint16_t todoDone = 0b00001;

Track tracks[MAX_TRACKS];
uint8_t trackCount = 0;
bool sdOk = false;
int16_t playIdx = -1;
bool playing = false;
uint32_t playStartedAt = 0, playOffset = 0;   // ms
int16_t volume = 60;
uint32_t volShownAt = 0;

bool recording = false;
uint32_t recStart = 0;
bool trackReal = false;       // true when the track is playing from a real file
uint32_t hapticOffAt = 0;
char ideaText[48] = "";      // first words of the open idea's transcript
float levels[15];

char toastMsg[32] = "";
uint32_t toastUntil = 0;

volatile int32_t encSteps = 0;
int32_t encSeen = 0;
Button buttons[4];
Ev evQueue[16];
uint8_t evHead = 0, evTail = 0;

bool dirty = true;
uint32_t lastFrame = 0, lastRender = 0;
int lastMinute = -1;
static const int START_MINUTE = 10 * 60 + 24;   // simulated clock starts 10:24

// ============================================================================
// Prototypes
// ============================================================================
void drawScreen(Screen s, int16_t ox, uint32_t now);
int buildRows(Screen s, Row *r);
void layoutRows(Row *r, int n);
void saveIdeas();
void push(Screen s);
void pop();
Screen top();

// ============================================================================
// Colour helpers
// ============================================================================
static inline uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}

// t = 0 gives a, t = 255 gives b
static uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int bl = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

int minuteOfDay() {
  int m = netMinuteOfDay();   // internet time when Wi-Fi is on and synced
  return m >= 0 ? m : (int)((START_MINUTE + millis() / 60000UL) % 1440);
}

bool isDarkNow() {
  if (cfg.theme == 0) return true;
  if (cfg.theme == 1) return false;
  int m = minuteOfDay();
  return m >= 19 * 60 || m < 7 * 60;
}

void applyTheme() {
  bool hc = cfg.contrast;
  if (isDarkNow()) {
    pal.bg           = rgb(0x000000);              // true black: OLED pixels off
    pal.platter      = hc ? rgb(0x2A2826) : rgb(0x1C1B19);
    pal.platterFocus = hc ? rgb(0x3A3733) : rgb(0x2C2A27);
    pal.outline      = hc ? rgb(0x8A857D) : pal.platter;
    pal.text         = hc ? rgb(0xFFFFFF) : rgb(0xF4F1EC);
    pal.sub          = hc ? rgb(0xD6D1C8) : rgb(0x9C978E);
    pal.accent       = hc ? rgb(0xFF7A2E) : rgb(0xFF6A1A);
    pal.accentText   = pal.accent;
    pal.onAccent     = rgb(0x1A0E06);
    pal.track        = hc ? rgb(0x5A5650) : rgb(0x3A3835);
    pal.danger       = rgb(0xFF5A4A);
  } else {
    pal.bg           = hc ? rgb(0xFFFFFF) : rgb(0xF4F1EC);
    pal.platter      = rgb(0xFFFFFF);
    pal.platterFocus = hc ? rgb(0xFFFFFF) : rgb(0xFFFDF9);
    pal.outline      = hc ? rgb(0x000000) : rgb(0xE3DED6);
    pal.text         = hc ? rgb(0x000000) : rgb(0x1A1917);
    pal.sub          = hc ? rgb(0x2E2B27) : rgb(0x6B665E);
    pal.accent       = hc ? rgb(0xB23C00) : rgb(0xE8590C);
    pal.accentText   = hc ? rgb(0x8A2E00) : rgb(0xC2410C);
    pal.onAccent     = hc ? rgb(0xFFFFFF) : rgb(0x1A0E06);
    pal.track        = hc ? rgb(0x8A857D) : rgb(0xD9D3C9);
    pal.danger       = hc ? rgb(0xB3261E) : rgb(0xD93A2B);
  }
  pal.glyph = rgb(0x141312);
}

void applyType() {
  switch (cfg.textSize) {
    case 0:
      ty.body = cfg.bold ? &FreeSansBold12pt7b : &FreeSans12pt7b;
      ty.sub = &FreeSans9pt7b; ty.hero = &FreeSansBold18pt7b;
      ty.rowMin = 54; ty.icon = 30; ty.padV = 10; break;
    case 1:
      ty.body = cfg.bold ? &FreeSansBold18pt7b : &FreeSans18pt7b;
      ty.sub = &FreeSans12pt7b; ty.hero = &FreeSansBold18pt7b;
      ty.rowMin = 72; ty.icon = 32; ty.padV = 12; break;
    default:
      ty.body = cfg.bold ? &FreeSansBold24pt7b : &FreeSans24pt7b;
      ty.sub = &FreeSans12pt7b; ty.hero = &FreeSansBold24pt7b;
      ty.rowMin = 86; ty.icon = 0; ty.padV = 12; break;   // icons hidden to make room
  }
}

// ============================================================================
// Text helpers
// ============================================================================
int16_t textWidth(const char *s, const GFXfont *f) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setFont(f);
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)(w + (x1 > 0 ? x1 : 0));
}

int16_t capH(const GFXfont *f) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setFont(f);
  canvas.getTextBounds("H", 0, 0, &x1, &y1, &w, &h);
  return (int16_t)h;
}

int16_t lineH(const GFXfont *f) { return capH(f) * 16 / 10; }

void drawText(const char *s, int16_t x, int16_t y, const GFXfont *f, uint16_t col) {
  canvas.setFont(f);
  canvas.setTextColor(col);
  canvas.setCursor(x, y);
  canvas.print(s);
}

void drawTextCentered(const char *s, int16_t cx, int16_t y, const GFXfont *f, uint16_t col) {
  drawText(s, cx - textWidth(s, f) / 2, y, f, col);
}

// Shortens text with "..." so it fits maxW
void fitText(const char *s, const GFXfont *f, int16_t maxW, char *out, size_t outSz) {
  strncpy(out, s, outSz - 1); out[outSz - 1] = 0;
  if (textWidth(out, f) <= maxW) return;
  char tmp[64];
  size_t n = strlen(out);
  while (n > 0) {
    out[--n] = 0;
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (textWidth(tmp, f) <= maxW) { strncpy(out, tmp, outSz - 1); out[outSz - 1] = 0; return; }
  }
  strncpy(out, "...", outSz - 1);
}

// Splits text into at most two lines that fit maxW. Returns the line count.
uint8_t wrapText(const char *s, const GFXfont *f, int16_t maxW, char *l1, char *l2, size_t sz) {
  l2[0] = 0;
  if (textWidth(s, f) <= maxW) { strncpy(l1, s, sz - 1); l1[sz - 1] = 0; return 1; }
  int len = (int)strlen(s);
  for (int i = len - 1; i > 0; i--) {
    if (s[i] != ' ') continue;
    char head[64];
    int n = i < 63 ? i : 63;
    memcpy(head, s, n); head[n] = 0;
    if (textWidth(head, f) <= maxW) {
      strncpy(l1, head, sz - 1); l1[sz - 1] = 0;
      fitText(s + i + 1, f, maxW, l2, sz);
      return 2;
    }
  }
  // one long word: break it with a hyphen (like large accessibility text)
  for (int k = len - 1; k >= 3; k--) {
    char head[64];
    int n = k < 62 ? k : 62;
    memcpy(head, s, n); head[n] = '-'; head[n + 1] = 0;
    if (textWidth(head, f) <= maxW) {
      strncpy(l1, head, sz - 1); l1[sz - 1] = 0;
      fitText(s + n, f, maxW, l2, sz);
      return 2;
    }
  }
  fitText(s, f, maxW, l1, sz);
  return 1;
}

void fmtClock(char *buf, size_t sz) {
  int m = minuteOfDay();
  snprintf(buf, sz, "%d:%02d", m / 60, m % 60);
}

void fmtDur(uint32_t secs, char *buf, size_t sz) {
  snprintf(buf, sz, "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
}

// ============================================================================
// Shapes and icons
// ============================================================================
void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  float hw = w / 2.0f;
  if (len < 0.5f) { canvas.fillCircle(lroundf(x0), lroundf(y0), lroundf(hw), c); return; }
  float nx = -dy / len * hw, ny = dx / len * hw;
  canvas.fillTriangle(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x0 - nx), lroundf(y0 - ny),
                      lroundf(x1 + nx), lroundf(y1 + ny), c);
  canvas.fillTriangle(lroundf(x0 - nx), lroundf(y0 - ny), lroundf(x1 - nx), lroundf(y1 - ny),
                      lroundf(x1 + nx), lroundf(y1 + ny), c);
  canvas.fillCircle(lroundf(x0), lroundf(y0), lroundf(hw), c);
  canvas.fillCircle(lroundf(x1), lroundf(y1), lroundf(hw), c);
}

void drawGlyph(uint8_t icon, int16_t cx, int16_t cy, float s, uint16_t g, uint16_t bgc) {
  switch (icon) {
    case IC_MIC: {
      canvas.fillRoundRect(cx - s * 0.12f, cy - s * 0.32f, s * 0.24f, s * 0.40f, s * 0.12f, g);
      float r = s * 0.21f, w = s * 0.07f;
      for (int a = 0; a < 180; a += 30) {
        float a0 = a * 0.01745f, a1 = (a + 30) * 0.01745f;
        thickLine(cx + cosf(a0) * r, cy - s * 0.04f + sinf(a0) * r,
                  cx + cosf(a1) * r, cy - s * 0.04f + sinf(a1) * r, w, g);
      }
      thickLine(cx, cy + s * 0.17f, cx, cy + s * 0.30f, w, g);
      thickLine(cx - s * 0.12f, cy + s * 0.30f, cx + s * 0.12f, cy + s * 0.30f, w, g);
      break;
    }
    case IC_BULB:
      canvas.fillCircle(cx, cy - s * 0.07f, s * 0.22f, g);
      canvas.fillRect(cx - s * 0.10f, cy + s * 0.10f, s * 0.20f, s * 0.10f, g);
      canvas.fillRoundRect(cx - s * 0.07f, cy + s * 0.22f, s * 0.14f, s * 0.07f, 2, g);
      break;
    case IC_CHECK:
      thickLine(cx - s * 0.22f, cy + s * 0.01f, cx - s * 0.06f, cy + s * 0.17f, s * 0.10f, g);
      thickLine(cx - s * 0.06f, cy + s * 0.17f, cx + s * 0.23f, cy - s * 0.18f, s * 0.10f, g);
      break;
    case IC_NOTE:
      canvas.fillCircle(cx - s * 0.08f, cy + s * 0.17f, s * 0.12f, g);
      canvas.fillRect(cx + s * 0.00f, cy - s * 0.29f, s * 0.07f, s * 0.47f, g);
      canvas.fillTriangle(cx + s * 0.07f, cy - s * 0.29f, cx + s * 0.27f, cy - s * 0.15f,
                          cx + s * 0.07f, cy - s * 0.07f, g);
      break;
    case IC_GEAR:
      for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f;
        canvas.fillCircle(cx + cosf(a) * s * 0.26f, cy + sinf(a) * s * 0.26f, s * 0.08f, g);
      }
      canvas.fillCircle(cx, cy, s * 0.24f, g);
      canvas.fillCircle(cx, cy, s * 0.09f, bgc);
      break;
    case IC_INFO:
      canvas.fillCircle(cx, cy - s * 0.21f, s * 0.06f, g);
      canvas.fillRoundRect(cx - s * 0.05f, cy - s * 0.09f, s * 0.10f, s * 0.33f, 2, g);
      break;
    case IC_PLAY:
      canvas.fillTriangle(cx - s * 0.13f, cy - s * 0.22f, cx - s * 0.13f, cy + s * 0.22f,
                          cx + s * 0.23f, cy, g);
      break;
    case IC_PAUSE:
      canvas.fillRoundRect(cx - s * 0.17f, cy - s * 0.21f, s * 0.11f, s * 0.42f, 2, g);
      canvas.fillRoundRect(cx + s * 0.06f, cy - s * 0.21f, s * 0.11f, s * 0.42f, 2, g);
      break;
    case IC_TRASH:
      canvas.fillRoundRect(cx - s * 0.17f, cy - s * 0.12f, s * 0.34f, s * 0.38f, 3, g);
      canvas.fillRect(cx - s * 0.24f, cy - s * 0.22f, s * 0.48f, s * 0.06f, g);
      canvas.fillRect(cx - s * 0.07f, cy - s * 0.29f, s * 0.14f, s * 0.07f, g);
      break;
    case IC_WAVE: {
      const float hts[5] = {0.18f, 0.38f, 0.26f, 0.44f, 0.22f};
      for (int k = 0; k < 5; k++) {
        float x = cx + (k - 2) * s * 0.12f, hh = hts[k] * s;
        canvas.fillRoundRect(x - s * 0.035f, cy - hh / 2, s * 0.07f + 1, hh, 2, g);
      }
      break;
    }
    default: break;
  }
}

void drawIcon(uint8_t icon, int16_t cx, int16_t cy, int16_t d, uint16_t circle, uint8_t fade) {
  if (icon == IC_TODO_OPEN) {
    uint16_t c = blend(pal.sub, pal.bg, fade);
    canvas.drawCircle(cx, cy, d / 2 - 2, c);
    canvas.drawCircle(cx, cy, d / 2 - 3, c);
    return;
  }
  if (icon == IC_TODO_DONE) {
    canvas.fillCircle(cx, cy, d / 2 - 2, blend(pal.accent, pal.bg, fade));
    drawGlyph(IC_CHECK, cx, cy, d, blend(pal.onAccent, pal.bg, fade), pal.accent);
    return;
  }
  uint16_t cc = blend(circle, pal.bg, fade);
  canvas.fillCircle(cx, cy, d / 2, cc);
  drawGlyph(icon, cx, cy, d, blend(pal.glyph, pal.bg, fade), cc);
}

void drawToggle(int16_t xr, int16_t cy, bool on, uint8_t fade) {
  int16_t w = 42, h = 26, x = xr - w, y = cy - h / 2;
  canvas.fillRoundRect(x, y, w, h, h / 2, blend(on ? pal.accent : pal.track, pal.bg, fade));
  int16_t kx = on ? x + w - h / 2 : x + h / 2;
  canvas.fillCircle(kx, cy, h / 2 - 3, blend(rgb(0xFFFFFF), pal.bg, fade));
}

void drawChevron(int16_t xr, int16_t cy, uint16_t c) {
  thickLine(xr - 7, cy - 6, xr - 1, cy, 2.4f, c);
  thickLine(xr - 1, cy, xr - 7, cy + 6, 2.4f, c);
}

// Soft fade where the list slides under the header and off the bottom
void edgeFade(int16_t ox, int16_t top, int16_t height, bool down) {
  uint16_t *buf = canvas.getBuffer();
  int16_t x0 = ox < 0 ? 0 : ox, x1 = ox + W > W ? W : ox + W;
  for (int16_t i = 0; i < height; i++) {
    int16_t y = (down ? top + i : top - i) - canvas.oy;   // row inside this band
    if (y < 0 || y >= bandH) continue;
    uint8_t t = (uint8_t)(255 - (255 * i) / height);
    for (int16_t x = x0; x < x1; x++) buf[y * W + x] = blend(buf[y * W + x], pal.bg, t);
  }
}

// ============================================================================
// Haptics: short pulses of the vibration motor
// ============================================================================
void haptic(uint8_t kind) {
  if (!cfg.haptics) return;
  uint16_t ms = kind == 0 ? HAPTIC_TICK_MS : kind == 1 ? HAPTIC_SELECT_MS : HAPTIC_BUMP_MS;
  digitalWrite(PIN_HAPTIC, HIGH);            // motor on; loop() switches it off
  hapticOffAt = millis() + ms;
#if MAVRIK_HAS_BUZZER
  if (kind == 0) tone(PIN_BUZZER, 2600, 4);
  else if (kind == 1) tone(PIN_BUZZER, 1500, 10);
  else tone(PIN_BUZZER, 700, 18);
#endif
}

void toast(const char *msg) {
  strncpy(toastMsg, msg, sizeof toastMsg - 1);
  toastMsg[sizeof toastMsg - 1] = 0;
  toastUntil = millis() + 1600;
  dirty = true;
}

// ============================================================================
// Storage
// ============================================================================
void saveSettings() {
  prefs.putUChar("theme", cfg.theme);
  prefs.putUChar("text", cfg.textSize);
  prefs.putUChar("bold", cfg.bold);
  prefs.putUChar("contrast", cfg.contrast);
  prefs.putUChar("motion", cfg.reduceMotion);
  prefs.putUChar("haptics", cfg.haptics);
  prefs.putUChar("wheel", cfg.wheelRev);
}

void saveIdeas() { prefs.putBytes("ideas", &ideas, sizeof ideas); }

void loadAll() {
  prefs.begin("mavrik", false);
  cfg.theme = prefs.getUChar("theme", 0);
  cfg.textSize = prefs.getUChar("text", 0);
  cfg.bold = prefs.getUChar("bold", 0);
  cfg.contrast = prefs.getUChar("contrast", 0);
  cfg.reduceMotion = prefs.getUChar("motion", 0);
  cfg.haptics = prefs.getUChar("haptics", 1);
  cfg.wheelRev = prefs.getUChar("wheel", 0);
  todoDone = prefs.getUShort("todo", 0b00001);
  if (prefs.getBytesLength("ideas") == sizeof ideas) {
    prefs.getBytes("ideas", &ideas, sizeof ideas);
  } else {   // first boot: no ideas yet
    memset(&ideas, 0, sizeof ideas);
    ideas.next = 1;
    saveIdeas();
  }
}

void addTrack(const char *t, const char *a, uint16_t secs, const char *path = nullptr) {
  if (trackCount >= MAX_TRACKS) return;
  Track &k = tracks[trackCount++];
  strncpy(k.title, t, sizeof k.title - 1); k.title[sizeof k.title - 1] = 0;
  strncpy(k.artist, a, sizeof k.artist - 1); k.artist[sizeof k.artist - 1] = 0;
  k.secs = secs;
  if (path && *path) { strncpy(k.path, path, sizeof k.path - 1); k.path[sizeof k.path - 1] = 0; }
  else snprintf(k.path, sizeof k.path, "/music/%d.wav", trackCount);   // default: /music/1.wav, 2.wav...
}

void ideaPath(uint16_t num, const char *ext, char *out, size_t sz) {
  snprintf(out, sz, "/ideas/idea_%u.%s", (unsigned)num, ext);
}

void loadIdeaText() {
  ideaText[0] = 0;
  if (openIdea < 0 || !sdOk) return;
  char p[40]; ideaPath(ideas.list[openIdea].num, "txt", p, sizeof p);
  SD_LOCK();
  File f = SD.open(p, FILE_READ);
  if (f) {
    int n = f.read((uint8_t *)ideaText, sizeof ideaText - 1);
    ideaText[n > 0 ? n : 0] = 0;
    for (char *c = ideaText; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
    f.close();
  }
  SD_UNLOCK();
}

// Line format: Title|Artist|m:ss|/music/file.wav   (file is optional; # lines are ignored)
void parseTrackLine(char *line) {
  while (*line == ' ') line++;
  if (!*line || *line == '#') return;
  char *a = strchr(line, '|'); if (!a) return; *a++ = 0;
  char *d = strchr(a, '|'); if (!d) return; *d++ = 0;
  char *p = strchr(d, '|'); if (p) *p++ = 0;
  int mm = 0, ss = 0;
  if (sscanf(d, "%d:%d", &mm, &ss) != 2) return;
  if (p) while (*p == ' ') p++;
  addTrack(line, a, (uint16_t)(mm * 60 + ss), p);
}

void loadLibrary() {
  trackCount = 0;
  if (sdOk) {
    SD_LOCK();
    File f = SD.open("/library.txt");
    if (f) {
      char line[96]; int len = 0;
      while (f.available()) {
        int c = f.read();
        if (c == '\r') continue;
        if (c == '\n' || len == 95) { line[len] = 0; parseTrackLine(line); len = 0; }
        else line[len++] = (char)c;
      }
      if (len) { line[len] = 0; parseTrackLine(line); }
      f.close();
    }
    SD_UNLOCK();
  }
  if (trackCount == 0) {   // no SD card or no library file: built-in demo list
    addTrack("Morning Static", "Demo Library", 201);
    addTrack("Slow Orbit", "Demo Library", 247);
    addTrack("Folded Paper", "Demo Library", 183);
  }
}

// ============================================================================
// Display driver: minimal ILI9341 over SPI, pushes the whole frame
// (init values adapted from Adafruit_ILI9341, BSD licence)
// ============================================================================
SPISettings tftSpi(40000000, MSBFIRST, SPI_MODE0);

void tftCmd(uint8_t c, const uint8_t *data, uint8_t n) {
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.write(c);
  digitalWrite(PIN_TFT_DC, HIGH);
  for (uint8_t i = 0; i < n; i++) SPI.write(data[i]);
}

void tftBegin() {
  pinMode(PIN_TFT_CS, OUTPUT); digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_TFT_DC, OUTPUT); digitalWrite(PIN_TFT_DC, HIGH);
  static const uint8_t init[] = {
    0xEF, 3, 0x03, 0x80, 0x02,   0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,   0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,   0xF7, 1, 0x20,   0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x23,   0xC1, 1, 0x10,   0xC5, 2, 0x3E, 0x28,   0xC7, 1, 0x86,
    0x36, 1, 0x48,   0x37, 1, 0x00,   0x3A, 1, 0x55,   0xB1, 2, 0x00, 0x18,
    0xB6, 3, 0x08, 0x82, 0x27,   0xF2, 1, 0x00,   0x26, 1, 0x01,
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    0x00
  };
  SPI.beginTransaction(tftSpi);
  digitalWrite(PIN_TFT_CS, LOW);
  tftCmd(0x01, nullptr, 0);   // software reset
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
  delay(150);
  SPI.beginTransaction(tftSpi);
  digitalWrite(PIN_TFT_CS, LOW);
  const uint8_t *p = init;
  while (*p) { uint8_t c = *p++, n = *p++; tftCmd(c, p, n); p += n; }
  tftCmd(0x11, nullptr, 0);   // sleep out
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
  delay(120);
  SPI.beginTransaction(tftSpi);
  digitalWrite(PIN_TFT_CS, LOW);
  tftCmd(0x29, nullptr, 0);   // display on
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

void tftPushBand(const uint16_t *buf, int16_t y0, int16_t rows) {
  static uint8_t chunk[W * 2 * 8];
  int16_t y1 = y0 + rows - 1;
  const uint8_t ca[4] = {0, 0, (uint8_t)((W - 1) >> 8), (uint8_t)((W - 1) & 0xFF)};
  const uint8_t pa[4] = {(uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF), (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)};
  SPI.beginTransaction(tftSpi);
  digitalWrite(PIN_TFT_CS, LOW);
  tftCmd(0x2A, ca, 4);
  tftCmd(0x2B, pa, 4);
  tftCmd(0x2C, nullptr, 0);
  for (int16_t y = 0; y < rows; y += 8) {
    const uint16_t *src = buf + y * W;
    for (int i = 0; i < W * 8; i++) { chunk[2 * i] = src[i] >> 8; chunk[2 * i + 1] = src[i] & 0xFF; }
    SPI.writeBytes(chunk, sizeof chunk);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

// Sends the frame in 32-row pieces, so the audio task can reach the SD card in between.
void tftPushFrame(const uint16_t *buf, int16_t y0, int16_t rows) {
  for (int16_t y = 0; y < rows; y += 32) {
    int16_t n = rows - y < 32 ? rows - y : 32;
    SD_LOCK();
    tftPushBand(buf + (int32_t)y * W, y0 + y, n);
    SD_UNLOCK();
  }
}

// ============================================================================
// Input: wheel (interrupt), buttons (debounced, arrows auto-repeat)
// ============================================================================
void IRAM_ATTR onWheel() {
  if (digitalRead(PIN_ENC_DT) == HIGH) encSteps = encSteps + 1; else encSteps = encSteps - 1;
}

void queueEv(Ev e) {
  uint8_t next = (evHead + 1) % 16;
  if (next == evTail) return;
  evQueue[evHead] = e; evHead = next;
}

void initButtons() {
  const uint8_t pins[4] = {PIN_ENC_SW, PIN_BACK, PIN_ACTION, PIN_UP};
  const Ev evs[4] = {EV_SELECT, EV_BACK, EV_ACTION, EV_UP};
  for (int i = 0; i < 4; i++) {
    buttons[i] = {pins[i], evs[i], i == 3, true, true, 0, 0, 0};
    pinMode(pins[i], INPUT_PULLUP);
  }
}

Button downBtn = {PIN_DOWN, EV_DOWN, true, true, true, 0, 0, 0};

void pollButton(Button &b, uint32_t now) {
  bool raw = digitalRead(b.pin);   // HIGH = released (pull-up)
  if (raw != b.lastRaw) { b.lastRaw = raw; b.changedAt = now; }
  if (now - b.changedAt >= 20 && raw != b.stable) {
    b.stable = raw;
    if (!raw) { queueEv(b.ev); b.pressedAt = now; b.lastRepeat = now; }
  }
  if (b.repeat && !b.stable && now - b.pressedAt > 400 && now - b.lastRepeat > 110) {
    queueEv(b.ev); b.lastRepeat = now;
  }
}

void pollInput(uint32_t now) {
#if MAVRIK_WHEEL_TOUCH
  static uint32_t lastTouch = 0;
  if (now - lastTouch >= 8) { lastTouch = now; encSteps = encSteps + touchWheelPoll(); }
#endif
  int32_t s = encSteps;
  int32_t d = s - encSeen;
  encSeen = s;
  while (d > 0) { queueEv(cfg.wheelRev ? EV_CCW : EV_CW); d--; }
  while (d < 0) { queueEv(cfg.wheelRev ? EV_CW : EV_CCW); d++; }
  for (int i = 0; i < 4; i++) pollButton(buttons[i], now);
  pollButton(downBtn, now);
}

// ============================================================================
// Content: rows for each list screen
// ============================================================================
Row &addRow(Row *r, int &n, const char *label, const char *sub, uint8_t icon, uint16_t iconColor) {
  Row &x = r[n++];
  memset(&x, 0, sizeof x);
  strncpy(x.label, label, sizeof x.label - 1);
  strncpy(x.sub, sub ? sub : "", sizeof x.sub - 1);
  x.icon = icon; x.iconColor = iconColor; x.toggle = -1;
  return x;
}

int todoLeft() {
  int left = 0;
  for (int i = 0; i < TODO_COUNT; i++) if (!(todoDone & (1 << i))) left++;
  return left;
}

uint32_t trackPosMs() {
  if (playIdx < 0) return 0;
  if (trackReal) return audioPosMs();
  uint32_t pos = playOffset + (playing ? millis() - playStartedAt : 0);
  uint32_t len = tracks[playIdx].secs * 1000UL;
  return pos > len ? len : pos;
}

const char *themeName() { return cfg.theme == 0 ? "Dark" : cfg.theme == 1 ? "Light" : "Auto (dark at night)"; }
const char *sizeName() { return cfg.textSize == 0 ? "Standard" : cfg.textSize == 1 ? "Large" : "Extra large"; }

int buildRows(Screen s, Row *r) {
  int n = 0;
  char sub[48];
  switch (s) {
    case SCR_HOME: {
      Row &rec = addRow(r, n, "Record idea", "From any screen", IC_MIC, pal.onAccent);
      rec.accent = true;
      snprintf(sub, sizeof sub, "%d saved", ideas.count);
      addRow(r, n, "Ideas", sub, IC_BULB, rgb(0xFFB020)).chevron = true;
      int left = todoLeft();
      if (left) snprintf(sub, sizeof sub, "%d left", left); else snprintf(sub, sizeof sub, "All done");
      addRow(r, n, "To-do", sub, IC_CHECK, rgb(0x3CCB6A)).chevron = true;
      if (playIdx >= 0 && playing) snprintf(sub, sizeof sub, "Playing: %s", tracks[playIdx].title);
      else snprintf(sub, sizeof sub, "%d tracks", trackCount);
      addRow(r, n, "Music", sub, IC_NOTE, rgb(0xFF5C7A)).chevron = true;
      snprintf(sub, sizeof sub, "%s, %s", isDarkNow() ? "Dark" : "Light", sizeName());
      addRow(r, n, "Settings", sub, IC_GEAR, rgb(0x9A958D)).chevron = true;
      break;
    }
    case SCR_IDEAS: {
      if (ideas.count == 0) { addRow(r, n, "No ideas yet", "Press the orange button to record", IC_NONE, 0); break; }
      for (int i = ideas.count - 1; i >= 0 && n < MAX_ROWS; i--) {
        const Idea &d = ideas.list[i];
        char label[24], dur[12];
        snprintf(label, sizeof label, "Idea %u", (unsigned)d.num);
        fmtDur(d.secs, dur, sizeof dur);
        snprintf(sub, sizeof sub, "%s  |  %d:%02d", dur, d.minute / 60, d.minute % 60);
        addRow(r, n, label, sub, IC_WAVE, rgb(0xFFB020)).chevron = true;
      }
      break;
    }
    case SCR_IDEA: {
      if (openIdea < 0) break;
      const Idea &d = ideas.list[openIdea];
      uint32_t el = ideaPlaying ? audioPosMs() / 1000 : 0;
      if (el > d.secs) el = d.secs;
      char a[12], b[12];
      fmtDur(el, a, sizeof a); fmtDur(d.secs, b, sizeof b);
      snprintf(sub, sizeof sub, "%s of %s", a, b);
      addRow(r, n, ideaPlaying ? "Stop" : "Play", sub, ideaPlaying ? IC_PAUSE : IC_PLAY, pal.accent);
      if (ideaText[0]) addRow(r, n, "Text", ideaText, IC_NONE, 0);
      else addRow(r, n, "Text", MAVRIK_WIFI ? "Sent when Wi-Fi is on" : "Transcription is off", IC_NONE, 0);
      Row &del = addRow(r, n, "Delete", "Removes this idea", IC_TRASH, pal.danger);
      del.danger = true;
      break;
    }
    case SCR_TODO: {
      for (int i = 0; i < TODO_COUNT; i++) {
        bool done = todoDone & (1 << i);
        Row &t = addRow(r, n, todoText[i], done ? "Done" : "", done ? IC_TODO_DONE : IC_TODO_OPEN, 0);
        t.strike = done;
      }
      addRow(r, n, "Add tasks from the phone app", "Coming with sync", IC_NONE, 0);
      break;
    }
    case SCR_MUSIC: {
      if (playIdx >= 0) {
        snprintf(sub, sizeof sub, "%s%s", playing ? "" : "Paused: ", tracks[playIdx].title);
        addRow(r, n, "Now Playing", sub, playing ? IC_PLAY : IC_PAUSE, pal.accent).chevron = true;
      }
      for (int i = 0; i < trackCount && n < MAX_ROWS; i++) {
        char dur[12];
        fmtDur(tracks[i].secs, dur, sizeof dur);
        snprintf(sub, sizeof sub, "%s  |  %s", tracks[i].artist, dur);
        Row &t = addRow(r, n, tracks[i].title, sub, (i == playIdx && playing) ? IC_PAUSE : IC_NOTE, rgb(0xFF5C7A));
        (void)t;
      }
      break;
    }
    case SCR_SETTINGS: {
      addRow(r, n, "Appearance", themeName(), IC_NONE, 0);
      addRow(r, n, "Text size", sizeName(), IC_NONE, 0);
      addRow(r, n, "Bold text", "", IC_NONE, 0).toggle = cfg.bold;
      addRow(r, n, "High contrast", "", IC_NONE, 0).toggle = cfg.contrast;
      addRow(r, n, "Reduce motion", "", IC_NONE, 0).toggle = cfg.reduceMotion;
      addRow(r, n, "Haptics", "Clicks on every wheel step", IC_NONE, 0).toggle = cfg.haptics;
      addRow(r, n, "Wheel direction", cfg.wheelRev ? "Reversed" : "Natural", IC_NONE, 0);
      addRow(r, n, "About", "Mavrik OS " MAVRIK_VERSION, IC_INFO, rgb(0x7AA2FF)).chevron = true;
      break;
    }
    case SCR_ABOUT: {
      esp_chip_info_t info; esp_chip_info(&info);
      snprintf(sub, sizeof sub, "%s, rev v%d.%d", ESP.getChipModel(), info.revision / 100, info.revision % 100);
      addRow(r, n, "Chip", sub, IC_NONE, 0);
      snprintf(sub, sizeof sub, "%lu MHz, %d cores", (unsigned long)ESP.getCpuFreqMHz(), info.cores);
      addRow(r, n, "Processor", sub, IC_NONE, 0);
      snprintf(sub, sizeof sub, "%lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024));
      addRow(r, n, "Memory", sub, IC_NONE, 0);
      snprintf(sub, sizeof sub, "%lu KB", (unsigned long)(ESP.getPsramSize() / 1024));
      addRow(r, n, "PSRAM", sub, IC_NONE, 0);
      if (sdOk) snprintf(sub, sizeof sub, "Mounted, %d tracks", trackCount);
      else snprintf(sub, sizeof sub, "Not found, demo tracks");
      addRow(r, n, "SD card", sub, IC_NONE, 0);
      addRow(r, n, "Screen", "2.8in ILI9341, 240x320", IC_NONE, 0);
      addRow(r, n, "Audio", audioMicReady() ? "INMP441 mic, MAX98357 amp" : "Mic not found", IC_NONE, 0);
      addRow(r, n, "Wheel", MAVRIK_WHEEL_TOUCH ? "Touch wheel" : "Rotary encoder", IC_NONE, 0);
      addRow(r, n, "Wi-Fi", !MAVRIK_WIFI ? "Off" : netConnected() ? "Connected" : "Not connected", IC_NONE, 0);
      break;
    }
    default: break;
  }
  return n;
}

int16_t rowTextWidth(const Row &r) {
  int16_t w = 224 - 28;                       // platter width minus side padding
  if (r.icon != IC_NONE && ty.icon) w -= ty.icon + 10;
  if (r.toggle >= 0) w -= 50;
  if (r.chevron) w -= 14;
  return w;
}

void layoutRows(Row *r, int n) {
  int16_t y = 0, bodyCap = capH(ty.body), bodyLine = lineH(ty.body), subCap = capH(ty.sub);
  for (int i = 0; i < n; i++) {
    Row &x = r[i];
    int16_t tw = rowTextWidth(x);
    x.lines = wrapText(x.label, ty.body, tw, x.l1, x.l2, sizeof x.l1);
    int16_t content = bodyCap + (x.lines - 1) * bodyLine + (x.sub[0] ? subCap * 17 / 10 + 2 : 0);
    int16_t h = content + ty.padV * 2 + 6;
    x.h = h < ty.rowMin ? ty.rowMin : h;
    x.y = y;
    y += x.h;
  }
}

int16_t listTop(Screen s) { return s == SCR_IDEA ? HEADER_H + 86 : HEADER_H; }

int16_t contentHeight(Row *r, int n) { return n ? r[n - 1].y + r[n - 1].h : 0; }

// ============================================================================
// Drawing
// ============================================================================
void drawHeader(int16_t ox, const char *title, bool battery) {
  canvas.fillRect(ox, 0, W, HEADER_H, pal.bg);
  char clk[8];
  fmtClock(clk, sizeof clk);
  int16_t cw = textWidth(clk, &FreeSansBold12pt7b);
  drawText(clk, ox + W - 12 - cw, 27, &FreeSansBold12pt7b, pal.text);
  int16_t right = W - 12 - cw - 10;
  if (battery) {
    int16_t bx = ox + right - 24, by = 13;
    canvas.drawRoundRect(bx, by, 22, 12, 3, pal.sub);
    canvas.fillRect(bx + 22, by + 4, 2, 4, pal.sub);
    canvas.fillRoundRect(bx + 2, by + 2, 15, 8, 2, pal.text);   // about 82%
    right -= 32;
  }
  char t[32];
  fitText(title, &FreeSansBold12pt7b, right - 12, t, sizeof t);
  drawText(t, ox + 12, 27, &FreeSansBold12pt7b, pal.accentText);
}

void drawRow(int16_t ox, int16_t top, const Row &r, bool focused, uint8_t fade) {
  int16_t x = ox + 8, w = 224, y = top + 3, h = r.h - 6;
  uint16_t fill = r.accent ? pal.accent : (focused ? pal.platterFocus : pal.platter);
  uint16_t txt = r.accent ? pal.onAccent : (r.danger ? pal.danger : pal.text);
  uint16_t sub = r.accent ? pal.onAccent : pal.sub;
  if (r.strike) txt = pal.sub;
  canvas.fillRoundRect(x, y, w, h, 18, blend(fill, pal.bg, fade));
  if (!r.accent && pal.outline != pal.platter)
    canvas.drawRoundRect(x, y, w, h, 18, blend(pal.outline, pal.bg, fade));

  int16_t tx = x + 14;
  int16_t cy = y + h / 2;
  if (r.icon != IC_NONE && ty.icon) {
    if (r.accent) {   // dark circle with an orange glyph on the orange row
      canvas.fillCircle(tx + ty.icon / 2, cy, ty.icon / 2, blend(pal.onAccent, pal.bg, fade));
      drawGlyph(r.icon, tx + ty.icon / 2, cy, ty.icon, blend(pal.accent, pal.bg, fade), pal.onAccent);
    } else {
      drawIcon(r.icon, tx + ty.icon / 2, cy, ty.icon, r.iconColor, fade);
    }
    tx += ty.icon + 10;
  }

  int16_t bodyCap = capH(ty.body), bodyLine = lineH(ty.body), subCap = capH(ty.sub);
  int16_t content = bodyCap + (r.lines - 1) * bodyLine + (r.sub[0] ? subCap * 17 / 10 + 2 : 0);
  int16_t base = y + (h - content) / 2 + bodyCap;
  drawText(r.l1, tx, base, ty.body, blend(txt, pal.bg, fade));
  if (r.strike) canvas.drawFastHLine(tx, base - bodyCap / 2, textWidth(r.l1, ty.body), blend(txt, pal.bg, fade));
  if (r.lines > 1) {
    base += bodyLine;
    drawText(r.l2, tx, base, ty.body, blend(txt, pal.bg, fade));
    if (r.strike) canvas.drawFastHLine(tx, base - bodyCap / 2, textWidth(r.l2, ty.body), blend(txt, pal.bg, fade));
  }
  if (r.sub[0]) {
    char s[48];
    fitText(r.sub, ty.sub, rowTextWidth(r), s, sizeof s);
    drawText(s, tx, base + subCap * 17 / 10, ty.sub, blend(sub, pal.bg, fade));
  }
  if (r.toggle >= 0) drawToggle(x + w - 14, cy, r.toggle == 1, fade);
  if (r.chevron) drawChevron(x + w - 14, cy, blend(r.accent ? pal.onAccent : pal.sub, pal.bg, fade));
}

void drawList(int16_t ox, Screen s, Row *rows, int n) {
  ListState &st = lists[s];
  int16_t top = listTop(s), viewH = H - top;
  float viewMid = top + viewH / 2.0f;
  for (int i = 0; i < n; i++) {
    int16_t rt = top + rows[i].y - (int16_t)lroundf(st.scroll);
    if (rt > H || rt + rows[i].h < top - 20) continue;
    uint8_t fade = 0;
    if (!cfg.contrast && i != st.focus) {
      float d = fabsf(rt + rows[i].h / 2.0f - viewMid) / (viewH / 2.0f);
      if (d > 0.72f) { float f = (d - 0.72f) / 0.5f; if (f > 1) f = 1; fade = (uint8_t)(f * 150); }
    }
    drawRow(ox, rt, rows[i], i == st.focus, fade);
  }
  if (n > 0 && st.focus < n) {   // focus ring glides between rows
    int16_t ry = top + (int16_t)lroundf(st.hl - st.scroll);
    int16_t rh = (int16_t)lroundf(st.hlH);
    uint16_t ring = rows[st.focus].accent ? pal.text : pal.accent;
    canvas.drawRoundRect(ox + 6, ry + 1, 228, rh - 2, 20, ring);
    canvas.drawRoundRect(ox + 7, ry + 2, 226, rh - 4, 19, ring);
    if (cfg.contrast) canvas.drawRoundRect(ox + 5, ry, 230, rh, 21, ring);
  }
}

void drawIdeaHero(int16_t ox) {
  if (openIdea < 0) return;
  const Idea &d = ideas.list[openIdea];
  char t[24], meta[40], dur[12];
  snprintf(t, sizeof t, "Idea %u", (unsigned)d.num);
  fmtDur(d.secs, dur, sizeof dur);
  snprintf(meta, sizeof meta, "%s long, recorded %d:%02d", dur, d.minute / 60, d.minute % 60);
  int16_t y = HEADER_H + 8;
  canvas.fillRect(ox, HEADER_H, W, 86, pal.bg);
  drawText(t, ox + 14, y + capH(&FreeSansBold18pt7b), &FreeSansBold18pt7b, pal.text);
  char m[40];
  fitText(meta, &FreeSans9pt7b, W - 28, m, sizeof m);
  drawText(m, ox + 14, y + capH(&FreeSansBold18pt7b) + 22, &FreeSans9pt7b, pal.sub);
  // progress bar
  float p = 0;
  if (ideaPlaying && d.secs) { p = audioPosMs() / (d.secs * 1000.0f); if (p > 1) p = 1; }
  int16_t by = HEADER_H + 70;
  canvas.fillRoundRect(ox + 14, by, W - 28, 6, 3, pal.track);
  if (p > 0) canvas.fillRoundRect(ox + 14, by, (int16_t)((W - 28) * p) + 1, 6, 3, pal.accent);
}

void drawPlayer(int16_t ox, uint32_t now) {
  canvas.fillRect(ox, 0, W, H, pal.bg);
  if (playIdx < 0) { drawHeader(ox, "Now Playing", false); return; }
  const Track &t = tracks[playIdx];
  int16_t art = cfg.textSize == 0 ? 120 : (cfg.textSize == 1 ? 96 : 78);
  int16_t ax = ox + (W - art) / 2, ay = HEADER_H + 6;
  uint32_t hsh = 2166136261u;
  for (const char *c = t.title; *c; c++) hsh = (hsh ^ (uint8_t)*c) * 16777619u;
  const uint32_t tones[5] = {0xFF6A1A, 0x6E9BFF, 0x3CCB6A, 0xFFB020, 0xFF5C7A};
  uint16_t c1 = rgb(tones[hsh % 5]), c2 = blend(c1, pal.bg, 120);
  canvas.fillRoundRect(ax, ay, art, art, 20, c2);
  canvas.fillCircle(ax + art * 2 / 3, ay + art / 3, art / 3, c1);
  drawGlyph(IC_NOTE, ax + art / 2, ay + art / 2 + 4, art * 0.7f, pal.text, c2);
  if (!playing) {
    canvas.fillCircle(ax + art / 2, ay + art / 2, art / 5 + 4, rgb(0x000000));
    drawGlyph(IC_PLAY, ax + art / 2 + 2, ay + art / 2, art * 0.4f, rgb(0xFFFFFF), 0);
  }
  const GFXfont *tf = cfg.textSize == 0 ? &FreeSansBold12pt7b : &FreeSansBold18pt7b;
  const GFXfont *sf = cfg.textSize == 0 ? &FreeSans9pt7b : &FreeSans12pt7b;
  char buf[40];
  int16_t y = ay + art + 12 + capH(tf);
  fitText(t.title, tf, W - 24, buf, sizeof buf);
  drawTextCentered(buf, ox + W / 2, y, tf, pal.text);
  y += capH(sf) + 12;
  fitText(t.artist, sf, W - 24, buf, sizeof buf);
  drawTextCentered(buf, ox + W / 2, y, sf, pal.sub);
  // progress
  uint32_t pos = trackPosMs();
  float p = t.secs ? pos / (t.secs * 1000.0f) : 0;
  y += 14;
  canvas.fillRoundRect(ox + 20, y, W - 40, 6, 3, pal.track);
  canvas.fillRoundRect(ox + 20, y, (int16_t)((W - 40) * p) + 1, 6, 3, pal.accent);
  char a[12], b[14];
  fmtDur(pos / 1000, a, sizeof a);
  uint32_t rem = t.secs - pos / 1000;
  b[0] = '-'; fmtDur(rem, b + 1, sizeof b - 1);
  y += 8 + capH(&FreeSans9pt7b);
  drawText(a, ox + 20, y, &FreeSans9pt7b, pal.sub);
  drawText(b, ox + W - 20 - textWidth(b, &FreeSans9pt7b), y, &FreeSans9pt7b, pal.sub);
  char hint[48];
  fitText(playing ? "Click: pause   Turn: vol" : "Click to play", &FreeSans9pt7b, W - 24, hint, sizeof hint);
  drawTextCentered(hint, ox + W / 2, H - 10, &FreeSans9pt7b, pal.sub);
  // volume pill, shown briefly after turning the wheel
  if (now - volShownAt < 1500) {
    int16_t vx = ox + W - 30, vy = HEADER_H + 14, vh = 110;
    canvas.fillRoundRect(vx, vy, 16, vh, 8, pal.track);
    int16_t fh = vh * volume / 100;
    if (fh > 0) canvas.fillRoundRect(vx, vy + vh - fh, 16, fh, 8, pal.accent);
  }
  drawHeader(ox, "Now Playing", false);
}

void drawRecord(int16_t ox, uint32_t now) {
  canvas.fillRect(ox, 0, W, H, pal.bg);
  uint32_t secs = (now - recStart) / 1000;
  int16_t cx = ox + W / 2, cy = 118;
  float pulse = cfg.reduceMotion ? 0 : sinf(now / 260.0f);
  int16_t r = 44 + (int16_t)(pulse * 5);
  canvas.fillCircle(cx, cy, r + 14, blend(pal.accent, pal.bg, 200));
  canvas.fillCircle(cx, cy, r, pal.accent);
  drawGlyph(IC_MIC, cx, cy, 64, pal.onAccent, pal.accent);
  char t[12];
  fmtDur(secs, t, sizeof t);
  drawTextCentered(t, cx, 222, &FreeSansBold24pt7b, pal.text);
  // live microphone level (updated in render())
  for (int i = 0; i < 15; i++) {
    int16_t hh = 4 + (int16_t)(levels[i] * 28);
    canvas.fillRoundRect(cx - 7 * 11 + i * 11 - 3, 252 - hh / 2, 6, hh, 3, i % 2 ? pal.sub : pal.text);
  }
  drawTextCentered("Orange button: save", cx, 290, &FreeSans9pt7b, pal.sub);
  drawTextCentered("Back: discard", cx, 310, &FreeSans9pt7b, pal.sub);
  drawHeader(ox, "Recording", false);
}

const char *screenTitle(Screen s) {
  switch (s) {
    case SCR_HOME: return "Mavrik";
    case SCR_IDEAS: return "Ideas";
    case SCR_IDEA: return "Idea";
    case SCR_TODO: return "To-do";
    case SCR_MUSIC: return "Music";
    case SCR_SETTINGS: return "Settings";
    case SCR_ABOUT: return "About";
    default: return "";
  }
}

void drawScreen(Screen s, int16_t ox, uint32_t now) {
  if (s == SCR_PLAYER) { drawPlayer(ox, now); return; }
  if (s == SCR_RECORD) { drawRecord(ox, now); return; }
  Row *rows = (s == tr.from && tr.active) ? rowsB : rowsA;
  canvas.fillRect(ox, 0, W, H, pal.bg);
  int n = buildRows(s, rows);
  layoutRows(rows, n);
  drawList(ox, s, rows, n);
  if (s == SCR_IDEA) drawIdeaHero(ox);
  edgeFade(ox, listTop(s), 10, true);
  edgeFade(ox, H - 1, 12, false);
  drawHeader(ox, screenTitle(s), s == SCR_HOME);
}

void drawToast(uint32_t now) {
  if (now >= toastUntil) return;
  int16_t tw = textWidth(toastMsg, &FreeSansBold9pt7b);
  int16_t w = tw + 32, h = 34, x = (W - w) / 2, y = H - h - 14;
  canvas.fillRoundRect(x, y, w, h, h / 2, pal.text);
  drawText(toastMsg, x + 16, y + 22, &FreeSansBold9pt7b, pal.bg);
}

void paintSplash(uint32_t) {
  canvas.fillScreen(rgb(0x000000));
  canvas.fillRoundRect(W / 2 - 34, 108, 68, 68, 18, rgb(0xFF6A1A));
  drawTextCentered("M", W / 2, 156, &FreeSansBold24pt7b, rgb(0x1A0E06));
  drawTextCentered("Mavrik OS", W / 2, 214, &FreeSansBold12pt7b, rgb(0xF4F1EC));
  drawTextCentered("prototype 0.1", W / 2, 238, &FreeSans9pt7b, rgb(0x9C978E));
}

// Paints the frame once per band and sends each band to the screen
void present(void (*paint)(uint32_t), uint32_t now) {
  for (int16_t y = 0; y < H; y += bandH) {
    canvas.oy = y;
    paint(now);
    tftPushFrame(canvas.getBuffer(), y, bandH);
  }
  canvas.oy = 0;
}

void drawSplash() { present(paintSplash, 0); }

void updateLevels() {   // scrolls the live mic level from right to left
  for (int i = 0; i < 14; i++) levels[i] = levels[i + 1];
  float l = audioLevel();
  levels[14] = 0.08f + l * 0.92f;
}

float easeOut(float t) { float u = 1 - t; return 1 - u * u * u; }

void paintFrame(uint32_t now) {
  if (tr.active) {
    float e = easeOut((now - tr.t0) / (float)TR_MS);
    if (tr.dir > 0) {
      drawScreen(tr.from, -(int16_t)(e * W * 0.3f), now);
      drawScreen(tr.to, (int16_t)((1 - e) * W), now);
    } else {
      drawScreen(tr.to, -(int16_t)((1 - e) * W * 0.3f), now);
      drawScreen(tr.from, (int16_t)(e * W), now);
    }
  } else {
    drawScreen(navStack[navDepth], 0, now);
  }
  drawToast(now);
}

void render(uint32_t now) {
  if (tr.active && now - tr.t0 >= TR_MS) tr.active = false;
  if (top() == SCR_RECORD) updateLevels();
  present(paintFrame, now);
  lastRender = now;
}

// ============================================================================
// Navigation and actions
// ============================================================================
Screen top() { return navStack[navDepth]; }

void startTransition(Screen from, Screen to, int8_t dir) {
  if (cfg.reduceMotion) { tr.active = false; dirty = true; return; }
  tr = {true, from, to, dir, millis()};
}

void push(Screen s) {
  if (navDepth >= 7) return;
  Screen from = top();
  navStack[++navDepth] = s;
  if (s == SCR_IDEA) lists[s] = {0, 0, 0, 0};
  startTransition(from, s, 1);
  dirty = true;
}

void pop() {
  if (navDepth == 0) { haptic(2); return; }
  Screen from = top();
  navDepth--;
  if (from == SCR_IDEA) ideaPlaying = false;
  startTransition(from, top(), -1);
  dirty = true;
}

// Stops music completely (used before recording or playing an idea).
void stopMusic() {
  if (playIdx < 0) return;
  if (trackReal) audioStop();
  playing = false; trackReal = false; playIdx = -1;
  if (lists[SCR_MUSIC].focus > 0) lists[SCR_MUSIC].focus--;   // the Now Playing row goes away
}

void startRecording() {
  if (top() == SCR_RECORD) return;
  if (!sdOk) { toast("Insert an SD card"); haptic(2); return; }
  if (!audioMicReady()) { toast("Microphone not found"); haptic(2); return; }
  stopMusic();
  if (ideaPlaying) { audioStop(); ideaPlaying = false; }
  char p[40]; ideaPath(ideas.next, "wav", p, sizeof p);
  audioRecStart(p);
  recording = true;
  recStart = millis();
  for (int i = 0; i < 15; i++) levels[i] = 0.2f;
  haptic(1);
  push(SCR_RECORD);
}

void finishRecording(bool keep) {
  if (!recording) return;
  recording = false;
  uint32_t secs = (audioRecStop(keep) + 500) / 1000;
  if (keep) {
    if (secs < 1) secs = 1;
    if (ideas.count == MAX_IDEAS) {   // drop the oldest from the list (its files stay on the card)
      memmove(&ideas.list[0], &ideas.list[1], sizeof(Idea) * (MAX_IDEAS - 1));
      ideas.count--;
    }
    netQueueIdea(ideas.next);
    ideas.list[ideas.count++] = {ideas.next++, (uint16_t)secs, (uint16_t)minuteOfDay()};
    saveIdeas();
    char msg[32];
    snprintf(msg, sizeof msg, "Idea %u saved", (unsigned)(ideas.next - 1));
    toast(msg);
    haptic(1);
  } else {
    toast("Discarded");
    haptic(2);
  }
  pop();
}

void startTrack(int16_t i) {
  playIdx = i; playing = true; playOffset = 0; playStartedAt = millis();
  if (ideaPlaying) ideaPlaying = false;
  trackReal = sdOk;
  if (trackReal) { audioSetVolume(volume); audioPlay(tracks[i].path); }
}

void togglePlay() {
  if (playIdx < 0) return;
  if (playing) { playOffset = trackPosMs(); playing = false; if (trackReal) audioPause(); }
  else { playStartedAt = millis(); playing = true; if (trackReal) audioResume(); }
  haptic(1);
}

void cycleSetting(int i) {
  switch (i) {
    case 0: cfg.theme = (cfg.theme + 1) % 3; break;
    case 1: cfg.textSize = (cfg.textSize + 1) % 3; break;
    case 2: cfg.bold ^= 1; break;
    case 3: cfg.contrast ^= 1; break;
    case 4: cfg.reduceMotion ^= 1; break;
    case 5: cfg.haptics ^= 1; break;
    case 6: cfg.wheelRev ^= 1; break;
    case 7: push(SCR_ABOUT); return;
  }
  applyTheme();
  applyType();
  saveSettings();
  haptic(1);
}

void activate(Screen s, int i) {
  switch (s) {
    case SCR_HOME:
      if (i == 0) startRecording();
      else if (i == 1) push(SCR_IDEAS);
      else if (i == 2) push(SCR_TODO);
      else if (i == 3) push(SCR_MUSIC);
      else if (i == 4) push(SCR_SETTINGS);
      haptic(1);
      break;
    case SCR_IDEAS:
      if (ideas.count == 0) { haptic(2); break; }
      openIdea = ideas.count - 1 - i;
      ideaPlaying = false;
      loadIdeaText();
      haptic(1);
      push(SCR_IDEA);
      break;
    case SCR_IDEA:
      if (i == 0) {
        if (ideaPlaying) { ideaPlaying = false; audioStop(); }
        else {
          char p[40]; ideaPath(ideas.list[openIdea].num, "wav", p, sizeof p);
          stopMusic();   // music stops while an idea plays
          audioSetVolume(volume);
          audioPlay(p);
          ideaPlaying = true; ideaPlayStart = millis();
        }
        haptic(1);
      } else if (i == 1) {
        haptic(2);   // the text row: nothing to do
      } else if (i == 2 && openIdea >= 0) {
        char p[40];
        if (ideaPlaying) { audioStop(); ideaPlaying = false; delay(30); }   // let the audio task close the file
        SD_LOCK();
        ideaPath(ideas.list[openIdea].num, "wav", p, sizeof p); SD.remove(p);
        ideaPath(ideas.list[openIdea].num, "txt", p, sizeof p); SD.remove(p);
        SD_UNLOCK();
        char msg[32];
        snprintf(msg, sizeof msg, "Idea %u deleted", (unsigned)ideas.list[openIdea].num);
        memmove(&ideas.list[openIdea], &ideas.list[openIdea + 1], sizeof(Idea) * (ideas.count - openIdea - 1));
        ideas.count--;
        saveIdeas();
        openIdea = -1;
        if (lists[SCR_IDEAS].focus >= ideas.count && ideas.count > 0) lists[SCR_IDEAS].focus = ideas.count - 1;
        toast(msg);
        haptic(2);
        pop();
      }
      break;
    case SCR_TODO:
      if (i < TODO_COUNT) {
        todoDone ^= (1 << i);
        prefs.putUShort("todo", todoDone);
        haptic(1);
      } else haptic(2);
      break;
    case SCR_MUSIC: {
      int off = playIdx >= 0 ? 1 : 0;
      if (off && i == 0) { haptic(1); push(SCR_PLAYER); break; }
      int k = i - off;
      if (k >= 0 && k < trackCount) {
        if (playIdx < 0) lists[SCR_MUSIC].focus += 1;   // a Now Playing row appears on top
        if (k != playIdx) startTrack(k); else if (!playing) togglePlay();
        haptic(1);
        push(SCR_PLAYER);
      }
      break;
    }
    case SCR_SETTINGS: cycleSetting(i); break;
    default: break;
  }
  dirty = true;
}

void moveFocus(Screen s, int d) {
  Row *rows = rowsA;
  int n = buildRows(s, rows);
  ListState &st = lists[s];
  int nf = st.focus + d;
  if (nf < 0 || nf >= n) { haptic(2); return; }
  st.focus = nf;
  haptic(0);
  dirty = true;
}

void handle(Ev e) {
  if (tr.active) tr.active = false;   // finish the slide instantly if you act during it
  Screen s = top();
  uint32_t now = millis();
  switch (e) {
    case EV_ACTION:
      if (s == SCR_RECORD) finishRecording(true); else startRecording();
      break;
    case EV_BACK:
      if (s == SCR_RECORD) finishRecording(false); else { haptic(1); pop(); }
      break;
    case EV_UP: case EV_DOWN: case EV_CW: case EV_CCW: {
      int listD = (e == EV_DOWN || e == EV_CW) ? 1 : -1;   // clockwise scrolls down
      int volD = (e == EV_UP || e == EV_CW) ? 5 : -5;      // clockwise or Up = louder
      if (s == SCR_PLAYER) {
        int v = volume + volD;
        if (v < 0 || v > 100) haptic(2); else { volume = v; audioSetVolume(v); haptic(0); }
        volShownAt = now;
      } else if (s != SCR_RECORD) moveFocus(s, listD);
      break;
    }
    case EV_SELECT:
      if (s == SCR_PLAYER) togglePlay();
      else if (s == SCR_RECORD) finishRecording(true);
      else activate(s, lists[s].focus);
      break;
    default: break;
  }
  dirty = true;
}

// Eases scroll and the focus ring toward their targets. Returns true while moving.
bool animate(uint32_t now) {
  Screen s = top();
  if (s == SCR_PLAYER || s == SCR_RECORD) return false;
  float dt = (float)(now - lastFrame);
  lastFrame = now;
  if (dt > 100) dt = 100;
  int n = buildRows(s, rowsA);
  if (n == 0) return false;
  layoutRows(rowsA, n);
  ListState &st = lists[s];
  if (st.focus >= n) st.focus = n - 1;
  int16_t viewH = H - listTop(s);
  const Row &f = rowsA[st.focus];
  float maxScroll = contentHeight(rowsA, n) - viewH + 8;
  if (maxScroll < 0) maxScroll = 0;
  float target = f.y + f.h / 2.0f - viewH / 2.0f;
  if (target < 0) target = 0;
  if (target > maxScroll) target = maxScroll;
  bool moving = false;
  if (cfg.reduceMotion) {
    st.scroll = target; st.hl = f.y; st.hlH = f.h;
  } else {
    float k = 1 - expf(-dt / 55.0f);
    st.scroll += (target - st.scroll) * k;
    st.hl += (f.y - st.hl) * k;
    st.hlH += (f.h - st.hlH) * k;
    if (fabsf(target - st.scroll) < 0.5f) st.scroll = target; else moving = true;
    if (fabsf(f.y - st.hl) < 0.5f) st.hl = f.y; else moving = true;
    if (fabsf(f.h - st.hlH) < 0.5f) st.hlH = f.h; else moving = true;
  }
  return moving;
}

// ============================================================================
// Setup and loop
// ============================================================================
// Finds the tallest strip of screen that fits in free memory
bool allocScreen() {
  const int16_t tries[5] = {320, 160, 80, 40, 16};
  for (int i = 0; i < 5; i++) {
    uint16_t *b = (uint16_t *)malloc((size_t)W * tries[i] * 2);
    if (b) { bandH = tries[i]; canvas.attach(b, bandH); return true; }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[Mavrik] 1/5 starting");
  Serial.printf("[Mavrik] free memory %lu KB, largest block %lu KB\n",
                (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getMaxAllocHeap() / 1024));
  if (!allocScreen()) {
    Serial.println("[Mavrik] ERROR: not enough memory for the screen. Stopping.");
    while (true) delay(1000);
  }
  Serial.printf("[Mavrik] 2/5 screen memory ok, drawing %d rows at a time\n", bandH);
  randomSeed(1234);
  canvas.setTextWrap(false);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  sdMutex = xSemaphoreCreateMutex();   // screen and SD card share the SPI bus
  tftBegin();
  drawSplash();
  Serial.println("[Mavrik] 3/5 display ok");

  loadAll();
  applyTheme();
  applyType();
  Serial.println("[Mavrik] 4/5 settings loaded");

  audioBegin();                       // creates the SD lock, starts the mic, amp and audio task
  SD_LOCK();
  sdOk = SD.begin(PIN_SD_CS, SPI, 20000000);
  SD_UNLOCK();
  loadLibrary();
  netBegin();
  for (int i = 0; i < ideas.count; i++) netQueueIdea(ideas.list[i].num);   // any not yet transcribed
  Serial.printf("[Mavrik] 5/5 ready | SD %s | %d tracks | %d ideas\n", sdOk ? "ok" : "missing", trackCount, ideas.count);

#if MAVRIK_WHEEL_TOUCH
  touchWheelBegin();
#else
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), onWheel, FALLING);
#endif
  initButtons();
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_HAPTIC, OUTPUT); digitalWrite(PIN_HAPTIC, LOW);
#if MAVRIK_HAS_BUZZER
  pinMode(PIN_BUZZER, OUTPUT);
#endif

  navStack[0] = SCR_HOME;
  for (int i = 0; i < SCR_COUNT; i++) lists[i] = {0, 0, 0, (float)ty.rowMin};
  delay(cfg.reduceMotion ? 200 : 700);
  lastFrame = millis();
  dirty = true;
}

void loop() {
  uint32_t now = millis();
  pollInput(now);
  while (evTail != evHead) { Ev e = evQueue[evTail]; evTail = (evTail + 1) % 16; handle(e); }

  int m = minuteOfDay();
  if (m != lastMinute) { lastMinute = m; if (cfg.theme == 2) applyTheme(); dirty = true; }
  if (hapticOffAt && (int32_t)(now - hapticOffAt) >= 0) { digitalWrite(PIN_HAPTIC, LOW); hapticOffAt = 0; }
  bool ended = audioTakeEnded();
  if (audioTakeFailed()) {
    if (ideaPlaying) { ideaPlaying = false; toast("Audio file missing"); }
    else if (trackReal) { trackReal = false; toast("No file, demo timer"); }   // keep the UI going
    dirty = true;
  }
  if (ideaPlaying && ended) { ideaPlaying = false; dirty = true; }
  if (playing && playIdx >= 0 && ((trackReal && ended) ||
                                  (!trackReal && trackPosMs() >= tracks[playIdx].secs * 1000UL))) {
    startTrack((playIdx + 1) % trackCount);   // next track
    dirty = true;
  }
  if (netTextReady) { netTextReady = false; if (top() == SCR_IDEA) { loadIdeaText(); dirty = true; } }

  bool moving = animate(now);
  Screen s = top();
  bool live = (s == SCR_RECORD && now - lastRender > (cfg.reduceMotion ? 250u : 60u)) ||
              (s == SCR_PLAYER && now - lastRender > 250) ||
              (s == SCR_IDEA && ideaPlaying && now - lastRender > 250) ||
              (s == SCR_HOME && playing && now - lastRender > 1000);
  if (toastUntil && now >= toastUntil) { toastUntil = 0; dirty = true; }
  if (dirty || moving || tr.active || live) {
    dirty = false;
    render(now);
    delay(1);   // let background tasks (and the watchdog) run
  } else {
    delay(5);
  }
}
