/* TFT isolation test - Classroom Environment Monitor
 * 3.5" 320x480 ILI9481, 8-bit parallel, on the ESP32-S3 DevKitC-1.
 *
 * Standalone: no Wi-Fi, no MQTT, no sensors. Drives the panel with
 * simulated readings so the display can be proved out on its own.
 *
 * Run this BEFORE CR_Monitor.ino. It goes in stages and tells you which
 * stage failed, so "backlight on but nothing on screen" stops being one
 * symptom and becomes a specific fault.
 *
 * STAGE 1  ID readback   - is the parallel bus actually talking?
 * STAGE 2  solid fills   - red, green, blue, white. Any colour at all
 *                          means the panel is alive and the pin map works.
 * STAGE 3  gauges        - the real classroom layout, fed a slow sine so
 *                          every colour band (green/amber/red) gets shown.
 *
 * IF STAGE 2 STAYS BLANK WHILE THE BACKLIGHT IS ON:
 *   The backlight runs off 5V and lights up no matter what. The panel controller
 *   controller needs 3.3V. A lit screen with no image is the classic
 *   signature of a controller with no logic supply - do the regulator
 *   test in WIRING_TFT.md before anything else.
 */

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#define PANEL_W 480
#define PANEL_H 320

#define COL_BG     0x0000
#define COL_CARD   0x2124
#define COL_EDGE   0x39E7
#define COL_LABEL  0x9CF3
#define COL_VALUE  TFT_WHITE
#define COL_OK     0x07E0
#define COL_WARN   0xFD20
#define COL_ALERT  0xF800
#define COL_TRACK  0x18E3

// Same bands as CR_Monitor.ino, so what you see here is what the node shows.
const float TEMP_MIN_C = 18.0, TEMP_MAX_C = 30.0;
const float LUX_MIN = 300.0,   LUX_MAX = 800.0;
const float SOUND_MAX_DB = 70.0;

struct Card { int16_t x, y; };
static const int16_t CARD_W = 228, CARD_H = 116;
static const Card CARD[4] = { {6, 44}, {246, 44}, {6, 166}, {246, 166} };
static const char* CARD_LABEL[4] = { "TEMPERATURE", "HUMIDITY", "LIGHT", "SOUND" };

static String   lastValue[4];
static float    lastFrac[4] = { -1, -1, -1, -1 };

// Clamp a reading onto 0..1 for the bar.
static float norm(float v, float lo, float hi) {
  if (hi <= lo) return 0;
  float f = (v - lo) / (hi - lo);
  return f < 0 ? 0 : (f > 1 ? 1 : f);
}

// Green inside the band, amber in the outer 8%, red once it alarms.
static uint16_t bandColour(float v, float lo, float hi) {
  if (v < lo || v > hi) return COL_ALERT;
  float span = hi - lo;
  if (v < lo + span * 0.08f || v > hi - span * 0.08f) return COL_WARN;
  return COL_OK;
}

static void drawCardFrame(uint8_t i) {
  const Card& c = CARD[i];
  tft.fillRoundRect(c.x, c.y, CARD_W, CARD_H, 6, COL_CARD);
  tft.drawRoundRect(c.x, c.y, CARD_W, CARD_H, 6, COL_EDGE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_LABEL, COL_CARD);
  tft.drawString(CARD_LABEL[i], c.x + 12, c.y + 6, 2);
  tft.fillRect(c.x + 12, c.y + 88, CARD_W - 24, 16, COL_TRACK);
}

// Repaint a card only when its number or its bar actually moved. A full
// repaint every cycle flickers badly on a panel this size.
static void drawReading(uint8_t i, const String& value, const String& unit,
                        float frac, uint16_t col) {
  const Card& c = CARD[i];

  if (value != lastValue[i]) {
    lastValue[i] = value;
    tft.fillRect(c.x + 12, c.y + 26, CARD_W - 24, 52, COL_CARD);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(col, COL_CARD);
    tft.drawString(value, c.x + 12, c.y + 26, 6);
    int16_t vw = tft.textWidth(value, 6);
    tft.setTextColor(COL_LABEL, COL_CARD);
    tft.drawString(unit, c.x + 18 + vw, c.y + 52, 4);
  }

  if (fabs(frac - lastFrac[i]) > 0.004f) {
    lastFrac[i] = frac;
    int16_t bw = CARD_W - 24;
    int16_t fill = (int16_t)(bw * frac);
    tft.fillRect(c.x + 12, c.y + 88, fill, 16, col);
    tft.fillRect(c.x + 12 + fill, c.y + 88, bw - fill, 16, COL_TRACK);
  }
}

void dumpId(const char* name, uint8_t cmd, uint8_t n) {
  Serial.printf("    %-8s cmd 0x%02X ->", name, cmd);
  for (uint8_t i = 1; i <= n; i++) Serial.printf(" 0x%02X", tft.readcommand8(cmd, i));
  Serial.println();
}

// Identify the controller instead of guessing it. Each family answers on a
// different register, so probe all of them and match the signature:
//   ILI9486  0xD3 -> .. .. 0x94 0x86
//   ILI9488  0xD3 -> .. .. 0x94 0x88
//   ILI9481  0xBF -> .. .. .. 0x94 0x81
//   R61581   0xBF -> .. 0x01 0x22 0x15 0x81
void stage1_readback() {
  Serial.println(F("\n[1] Controller identification"));
  Serial.printf("    D0-D7 = GPIO%d-%d, WR=%d RD=%d RS=%d CS=%d RST=%d\n",
                TFT_D0, TFT_D7, TFT_WR, TFT_RD, TFT_DC, TFT_CS, TFT_RST);

  dumpId("RDDID",  0x04, 3);
  dumpId("RDID4",  0xD3, 4);
  dumpId("RDID-BF", 0xBF, 6);
  dumpId("RDID1-3", 0xDA, 1);

  uint8_t a = tft.readcommand8(0xD3, 3);
  uint8_t b = tft.readcommand8(0xD3, 4);
  uint8_t e = tft.readcommand8(0xBF, 5);
  uint8_t f = tft.readcommand8(0xBF, 6);

  Serial.print(F("    verdict: "));
  if (a == 0x94 && b == 0x86) {
    Serial.println(F("ILI9486  -> use #define ILI9486_DRIVER"));
  } else if (a == 0x94 && b == 0x88) {
    Serial.println(F("ILI9488  -> use #define ILI9488_DRIVER"));
  } else if (e == 0x94 && f == 0x81) {
    Serial.println(F("ILI9481  -> use #define ILI9481_DRIVER"));
  } else if (e == 0x15 && f == 0x81) {
    Serial.println(F("R61581   -> use #define R61581_DRIVER"));
  } else {
    Serial.println(F("no usable ID."));
    Serial.println(F("    Readback needs LCD_RD wired AND a panel that supports"));
    Serial.println(F("    it - many of these shields support neither, so this is"));
    Serial.println(F("    not a failure. Fall back to trying each driver in turn:"));
    Serial.println(F("    ILI9486, then ILI9488, then ILI9481, then R61581."));
  }
}

void stage2_fills() {
  Serial.println(F("\n[2] Solid colour fills - watch the screen"));
  const uint16_t cols[] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE };
  const char*    names[] = { "RED", "GREEN", "BLUE", "WHITE" };
  for (uint8_t i = 0; i < 4; i++) {
    Serial.printf("    filling %s\n", names[i]);
    tft.fillScreen(cols[i]);
    delay(700);
  }
  Serial.println(F("    If the screen never changed colour, stop here and read"));
  Serial.println(F("    the 3.3V note at the top of this sketch."));
}

void stage3_begin() {
  Serial.println(F("\n[3] Gauge layout with simulated readings"));
  Serial.println(F("    Colours sweep green -> amber -> red as values leave band."));
  tft.fillScreen(COL_BG);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_VALUE, COL_BG);
  tft.drawString(F("CLASSROOM MONITOR - display test"), 10, 10, 4);
  tft.drawFastHLine(0, 42, PANEL_W, COL_EDGE);

  for (uint8_t i = 0; i < 4; i++) drawCardFrame(i);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) delay(10);
  delay(400);

  Serial.println(F("\n\n=== TFT isolation test ==="));

  tft.init();
  tft.setRotation(1);          // landscape, 480 x 320

  stage1_readback();
  stage2_fills();
  stage3_begin();
}

void loop() {
  // A slow sweep that deliberately walks each reading out of its band so
  // the amber and red states are visible without waiting for a real alarm.
  float p = millis() / 6000.0f;

  float temp = 24.0f + 9.0f * sinf(p);
  float hum  = 50.0f + 35.0f * sinf(p * 0.7f);
  float lux  = 550.0f + 500.0f * sinf(p * 0.5f);
  float db   = 55.0f + 22.0f * sinf(p * 1.3f);

  drawReading(0, String(temp, 1), "C",
              norm(temp, 10, 40),  bandColour(temp, TEMP_MIN_C, TEMP_MAX_C));
  drawReading(1, String(hum, 0),  "%",
              norm(hum, 0, 100),   bandColour(hum, 30, 70));
  drawReading(2, String((int)lux), "lx",
              norm(lux, 0, 1200),  bandColour(lux, LUX_MIN, LUX_MAX));
  drawReading(3, String(db, 0),   "dB",
              norm(db, 30, 100),   bandColour(db, 30, SOUND_MAX_DB));

  delay(60);
}
