/* TFT panel - Classroom Environment Monitor
 * 3.5" 320x480 ILI9481, 8-bit parallel, on the ESP32-S3.
 *
 * Included AFTER printPanel() so the globals it reads (soundMaxDb, mqtt,
 * seq, the alert flags, the threshold constants) are already declared,
 * and BEFORE Preflight.h so that file can use tft and this palette.
 * Pin map lives in TFT_Setup_CR_Monitor.h.
 *
 * LAYOUT - rotation 1, 480 x 320 landscape:
 *
 *   +--------------------------------------------------+ 0
 *   |  13:45  Sat, 13 Sep                        bars  | header
 *   +----------------------+---------------------------+ 38
 *   | (th) TEMPERATURE     | (drop) HUMIDITY           |
 *   |  23.4 C              |  55 %                     | 46
 *   |  [=======-------]    |  [==========----]         |
 *   +----------------------+---------------------------+ 166
 *   | (sun) LIGHT          | (eq) SOUND                |
 *   |  420 lx              |  62 dB                    | 172
 *   |  [=====---------]    |  [=======-------]         |
 *   +----------------------+---------------------------+ 292
 *   |  MQTT ok  seq 41                                 |
 *   +--------------------------------------------------+ 320
 *
 * COLOUR CARRIES THE STATE. A teacher should read the room from the
 * doorway without parsing numbers:
 *   green  - comfortably inside the band
 *   amber  - within 8% of a limit, or the sensor read failed
 *   red    - outside the band, alarm active
 *
 * Redraw policy: cards, icons and labels are painted once by dashChrome().
 * A value repaints only when its text changes and a bar only when it moves
 * more than ~0.4%. Repainting everything each 2 s cycle flickers badly on
 * a panel this size.
 */

 #include <TFT_eSPI.h>
 #include "Logo_APU.h"   // APU mark, baked to RGB565 for the boot splash

 TFT_eSPI tft = TFT_eSPI();

 #define PANEL_W 480
 #define PANEL_H 320

 // ---- palette -----------------------------------------------------
 // A dark UI set rather than saturated primaries on pure black. Black
 // backgrounds make every colour on top of them vibrate, and full-bright
 // red/green reads as a test pattern; these sit still and stay legible
 // across a room. RGB565, source hex in the comment.
 #define COL_BG      0x0882      // #0D1117  near-black slate
 #define COL_SURFACE 0x10C4      // #161B22  raised card
 #define COL_EDGE    0x2987      // #2A313C  card border
 #define COL_TRACK   0x31A7      // #30363D  unfilled part of a bar
 #define COL_LABEL   0x8CB5      // #8B97A8  captions
 #define COL_VALUE   0xE79E      // #E6EDF3  primary text
 #define COL_OK      0x3DCA      // #3FB950  green
 #define COL_WARN    0xD4C4      // #D29922  amber
 #define COL_ALERT   0xFA89      // #F85149  red
 #define COL_DROP    0x5D3F      // #58A6FF  humidity blue

 // This panel powers up with its colours inverted - a near-black
 // background renders near-white, amber renders light blue. Flip it once
 // here rather than inverting all twelve constants above by hand.
 // If the colours ever look inverted the OTHER way, change this to false.
 #define PANEL_INVERT true

 // The preflight report owns the screen until the dashboard has something
 // real to show. Both set by panelHoldPreflight().
 static unsigned long preflightUntil    = 0;   // earliest handover
 static unsigned long preflightDeadline = 0;   // latest, so a dead sensor
                                               // cannot pin the screen
 static bool chromeDirty = true;

 // ---- helpers ------------------------------------------------------
 static float norm(float v, float lo, float hi) {
   if (hi <= lo) return 0;
   float f = (v - lo) / (hi - lo);
   return f < 0 ? 0 : (f > 1 ? 1 : f);
 }

 static uint16_t bandColour(float v, float lo, float hi) {
   if (v < lo || v > hi) return COL_ALERT;
   float span = hi - lo;
   if (v < lo + span * 0.08f || v > hi - span * 0.08f) return COL_WARN;
   return COL_OK;
 }

 // ---- icons, drawn with primitives so there are no image assets ----
 static void iconTherm(int16_t x, int16_t y, uint16_t col) {
   tft.fillRoundRect(x + 6, y, 6, 15, 3, col);
   tft.fillCircle(x + 9, y + 17, 5, col);
 }

 static void iconDrop(int16_t x, int16_t y, uint16_t col) {
   tft.fillTriangle(x + 9, y, x + 2, y + 11, x + 16, y + 11, col);
   tft.fillCircle(x + 9, y + 14, 7, col);
 }

 static void iconSun(int16_t x, int16_t y, uint16_t col) {
   tft.fillCircle(x + 9, y + 11, 5, col);
   for (uint8_t i = 0; i < 8; i++) {
     float a = i * PI / 4.0f;
     tft.drawLine(x + 9 + cos(a) * 7,  y + 11 + sin(a) * 7,
                  x + 9 + cos(a) * 10, y + 11 + sin(a) * 10, col);
   }
 }

 // A level meter reads as "sound" more immediately than a speaker cone,
 // and it survives being drawn at 20 px far better than a cone plus arcs.
 static void iconSound(int16_t x, int16_t y, uint16_t col) {
   const uint8_t h[5] = { 6, 13, 20, 11, 7 };
   for (uint8_t i = 0; i < 5; i++)
     tft.fillRect(x + i * 4, y + 20 - h[i], 3, h[i], col);
 }

 // Signal strength as four bars. Unlit bars stay visible in COL_TRACK -
 // an absent icon would read as "no display code", not "no Wi-Fi".
 static void iconWifi(int16_t x, int16_t y, bool up, int32_t rssi) {
   uint8_t lit = 0;
   if (up) {
     if      (rssi > -60) lit = 4;
     else if (rssi > -70) lit = 3;
     else if (rssi > -80) lit = 2;
     else                 lit = 1;
   }
   for (uint8_t i = 0; i < 4; i++) {
     int16_t bh = 5 + i * 4;
     tft.fillRect(x + i * 7, y + 17 - bh, 5, bh, (i < lit) ? COL_OK : COL_TRACK);
   }
 }

 // ============================ CARDS ===========================
 #define HEAD_H  38
 #define CARD_W 226
 #define CARD_H 120

 struct Card { int16_t x, y; };
 static const Card CARD[4] = { {8, 46}, {246, 46}, {8, 172}, {246, 172} };
 static const char* CARD_LABEL[4] = { "TEMPERATURE", "HUMIDITY", "LIGHT", "SOUND" };

 // Bar scale ends, deliberately wider than the alert band: a bar pinned
 // hard at one end says nothing about how far out of range the room is.
 static const float TEMP_SCALE_LO = 10.0,  TEMP_SCALE_HI = 40.0;
 static const float LUX_SCALE_HI  = 1200.0;
 static const float DB_SCALE_LO   = 30.0,  DB_SCALE_HI = 100.0;
 // Humidity raises no alarm in the sketch, but a room still reads wrong
 // outside this. Colour only - it never sets an alert flag.
 static const float HUM_COMFORT_LO = 30.0, HUM_COMFORT_HI = 70.0;

 static String lastVal[4];
 static float  lastFrac[4] = { -1, -1, -1, -1 };
 static String lastHead, lastFoot;
 static int8_t lastBars = -1;

 static void drawIcon(uint8_t i, int16_t x, int16_t y, uint16_t col) {
   switch (i) {
     case 0: iconTherm(x, y, col); break;
     case 1: iconDrop (x, y, col); break;
     case 2: iconSun  (x, y, col); break;
     default: iconSound(x, y, col); break;
   }
 }

 static void dashChrome() {
   tft.fillScreen(COL_BG);
   tft.fillRect(0, 0, PANEL_W, HEAD_H, COL_SURFACE);
   tft.drawFastHLine(0, HEAD_H, PANEL_W, COL_EDGE);

   for (uint8_t i = 0; i < 4; i++) {
     const Card& c = CARD[i];
     tft.fillRoundRect(c.x, c.y, CARD_W, CARD_H, 8, COL_SURFACE);
     tft.drawRoundRect(c.x, c.y, CARD_W, CARD_H, 8, COL_EDGE);
     drawIcon(i, c.x + 12, c.y + 10, COL_LABEL);
     tft.setTextDatum(TL_DATUM);
     tft.setTextColor(COL_LABEL, COL_SURFACE);
     tft.drawString(CARD_LABEL[i], c.x + 40, c.y + 14, 2);
     tft.fillRect(c.x + 12, c.y + 94, CARD_W - 24, 14, COL_TRACK);
     lastVal[i]  = "";
     lastFrac[i] = -1;
   }
   lastHead = lastFoot = "";
   lastBars = -1;
 }

 // Repaint one card only where it actually changed. The icon is recoloured
 // with the value, so the whole card reads as one state at a glance.
 static void drawReading(uint8_t i, const String& value, const String& unit,
                         float frac, uint16_t col) {
   const Card& c = CARD[i];

   if (value != lastVal[i]) {
     lastVal[i] = value;
     tft.fillRect(c.x + 12, c.y + 38, CARD_W - 24, 50, COL_SURFACE);
     tft.setTextDatum(TL_DATUM);
     tft.setTextColor(col, COL_SURFACE);
     tft.drawString(value, c.x + 12, c.y + 38, 6);
     tft.setTextColor(COL_LABEL, COL_SURFACE);
     tft.drawString(unit, c.x + 18 + tft.textWidth(value, 6), c.y + 64, 4);
     drawIcon(i, c.x + 12, c.y + 10, col);
   }

   if (fabs(frac - lastFrac[i]) > 0.004f) {
     lastFrac[i] = frac;
     int16_t bw = CARD_W - 24;
     int16_t fill = (int16_t)(bw * frac);
     tft.fillRect(c.x + 12, c.y + 94, fill, 14, col);
     tft.fillRect(c.x + 12 + fill, c.y + 94, bw - fill, 14, COL_TRACK);
   }
 }

 // ---- boot splash --------------------------------------------------
 // The university mark on white with the author's name under it, held
 // long enough to actually be read before the self-test report takes the
 // screen. Blocking is fine here: nothing else has been started yet, and
 // a splash that flashes past is not a splash.
 const uint16_t SPLASH_MS = 2000;

 static void tftSplash() {
   tft.fillScreen(TFT_WHITE);

   // Logo_APU.h is a plain uint16_t RGB565 array, which pushImage sends
   // high byte first by default - the order this panel wants. If the mark
   // ever comes up with red and blue traded, flip this to true.
   tft.setSwapBytes(false);
   tft.pushImage((PANEL_W - APU_LOGO_W) / 2, 12, APU_LOGO_W, APU_LOGO_H, apuLogo);

   tft.setTextDatum(MC_DATUM);
   tft.setTextColor(TFT_BLACK, TFT_WHITE);
   tft.drawString(F("Suvin Raj K."), PANEL_W / 2, 282, 4);

   delay(SPLASH_MS);
   tft.setTextDatum(TL_DATUM);   // rest of this file assumes top-left
 }

 // ============================ PUBLIC API ======================
 void tftBegin() {
   tft.init();
   tft.setRotation(1);
   tft.invertDisplay(PANEL_INVERT);

   tftSplash();

   tft.fillScreen(COL_BG);
   tft.setTextDatum(TL_DATUM);
   tft.setTextColor(COL_VALUE, COL_BG);
   tft.drawString(F("CLASSROOM MONITOR"), 20, 130, 4);
   tft.setTextColor(COL_LABEL, COL_BG);
   tft.drawString(F("starting up"), 20, 166, 2);
   chromeDirty = true;
 }

 // Called by tftPreflight(). Keeps the report on screen for at least
 // minMs, and at most maxMs, after which drawPanel() takes over.
 void panelHoldPreflight(uint32_t minMs, uint32_t maxMs) {
   preflightUntil    = millis() + minMs;
   preflightDeadline = millis() + maxMs;
 }

 void drawPanel(bool tempOk, float temp, float hum,
                bool luxOk, float lux, bool wifiUp) {
   // Hand over from the preflight report only once the dashboard can
   // actually show something. Switching to a screen full of dashes reads
   // as a fault, so wait for a real reading - but not forever: a failed
   // sensor must not leave the preflight screen up for good.
   if (chromeDirty) {
     bool haveData = (tempOk || luxOk);
     if (millis() < preflightUntil) return;
     if (!haveData && millis() < preflightDeadline) return;
     chromeDirty = false;
     dashChrome();
   }

   bool anyAlert = (alertTemp || alertLight || alertSound);

   // --- header: clock and date, or the alarm text when something is wrong
   char hbuf[48];
   if (anyAlert) {
     snprintf(hbuf, sizeof hbuf, "%s", alarmBanner().c_str());
   } else if (timeValid()) {
     // Shift the UTC stamp and read it back as UTC: that yields local wall
     // time without touching the clock the telemetry is stamped with.
     time_t t = time(nullptr) + DISPLAY_TZ_OFFSET_SEC;
     struct tm tmv; gmtime_r(&t, &tmv);
     strftime(hbuf, sizeof hbuf, "%H:%M   %a, %d %b", &tmv);
   } else {
     snprintf(hbuf, sizeof hbuf, "clock not synced");
   }
   String head = hbuf;
   if (head != lastHead) {
     lastHead = head;
     uint16_t bg = anyAlert ? COL_ALERT : COL_SURFACE;
     tft.fillRect(0, 0, 410, HEAD_H, bg);
     tft.setTextColor(anyAlert ? COL_BG : COL_VALUE, bg);
     tft.setTextDatum(TL_DATUM);
     tft.drawString(head, 12, 10, anyAlert ? 4 : 2);
   }

   int32_t rssi = WiFi.RSSI();
   int8_t  bars = wifiUp ? (rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1) : 0;
   if (bars != lastBars) {
     lastBars = bars;
     tft.fillRect(424, 0, 48, HEAD_H, COL_SURFACE);
     iconWifi(432, 10, wifiUp, rssi);
   }

   // --- the four readings. A failed read shows amber dashes and an empty
   //     bar: a stale number is worse than none, it reads as a live sensor.
   if (tempOk) {
     drawReading(0, String(temp, 1), "C", norm(temp, TEMP_SCALE_LO, TEMP_SCALE_HI),
                 alertTemp ? COL_ALERT : bandColour(temp, TEMP_MIN_C, TEMP_MAX_C));
     drawReading(1, String(hum, 0), "%", norm(hum, 0, 100),
                 bandColour(hum, HUM_COMFORT_LO, HUM_COMFORT_HI));
   } else {
     drawReading(0, "--.-", "C", 0.0f, COL_WARN);
     drawReading(1, "--",   "%", 0.0f, COL_WARN);
   }

   if (luxOk) drawReading(2, String((int)lux), "lx", norm(lux, 0, LUX_SCALE_HI),
                          alertLight ? COL_ALERT : bandColour(lux, LUX_MIN, LUX_MAX));
   else       drawReading(2, "----", "lx", 0.0f, COL_WARN);

   drawReading(3, String(soundMaxDb, 0), "dB", norm(soundMaxDb, DB_SCALE_LO, DB_SCALE_HI),
               alertSound ? COL_ALERT : bandColour(soundMaxDb, DB_SCALE_LO, SOUND_MAX_DB));

   // --- footer: link state
   String foot;
   if (!wifiUp)                foot = F("Wi-Fi DOWN    MQTT -");
   else if (!mqtt.connected()) foot = F("Wi-Fi ok      MQTT down");
   else                        foot = "Wi-Fi ok      MQTT ok   seq " + String(seq);
   if (foot != lastFoot) {
     lastFoot = foot;
     tft.setTextColor(wifiUp ? COL_LABEL : COL_WARN, COL_BG);
     tft.setTextDatum(TL_DATUM);
     tft.setTextPadding(PANEL_W - 20);
     tft.drawString(foot, 12, 300, 2);
     tft.setTextPadding(0);
   }
 }
