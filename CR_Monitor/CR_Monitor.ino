/* Classroom Environment Monitor - Stage 2 (real hardware)
 * ESP32-S3 DevKitC-1. Full design notes: CR_Monitor_NOTES.txt
 *
 * Sensors : SHT3x (temp/humidity), BH1750 (lux), MAX4466 (sound)
 * Outputs : buzzer, Serial (CSV + status panel), MQTT JSON telemetry
 * Libs    : Adafruit SHT31 (+ Adafruit BusIO), BH1750, PubSubClient
 */

 #include <Wire.h>
 #include <WiFi.h>
 #include <time.h>
 #include "secrets.h"      // untracked credentials
 #include <esp_mac.h>
 #include <PubSubClient.h>
 #include <Adafruit_SHT31.h>
 #include <BH1750.h>

 // ============================ PINS ============================
 #define I2C_SDA     8
 #define I2C_SCL     9
 #define MIC_PIN     5      // must be an ADC1 pin (ADC2 fails with Wi-Fi on)
 #define BUZZER_PIN  7

 #define SHT3X_ADDR   0x44  // 0x45 if ADDR pin high
 #define BH1750_ADDR  0x23  // 0x5C if ADDR pin high

 // ============================ WI-FI ===========================
 // SSID and password live in secrets.h - see secrets.example.h

 const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
 const unsigned long WIFI_RETRY_DELAY_MS     = 5000;

 // ============================ MQTT ============================
 // Topics: classroom/<room>/<node>/{telemetry,status,alert}
 const char*    MQTT_HOST      = "192.168.1.8";   // <-- broker LAN IP
 const uint16_t MQTT_PORT      = 1883;
 // MQTT_USER / MQTT_PASS live in secrets.h
 const char*    ROOM_ID        = "room-101";       // <-- set per room

 const unsigned long MQTT_RECONNECT_MS = 5000;
 const uint16_t      MQTT_KEEPALIVE_S  = 30;

 // ============================ TIME (NTP) ======================
 const char*  NTP_SERVER     = "pool.ntp.org";
 const long   GMT_OFFSET_SEC = 0;                  // store UTC
 const int    DST_OFFSET_SEC = 0;
 // Telemetry stays in UTC (GMT_OFFSET_SEC above) so stored data is
 // unambiguous across sites. This offset is applied to the PANEL CLOCK
 // only - changing GMT_OFFSET_SEC instead would silently retimestamp
 // every MQTT message.
 const long   DISPLAY_TZ_OFFSET_SEC = 8 * 3600;   // UTC+8, panel clock only
 const time_t TIME_VALID_MIN = 1700000000;         // clock is synced if past this

 // ============================ THRESHOLDS ======================
 // Placeholder limits - justify against a cited standard before collecting data.
 const float TEMP_MAX_C   = 30.0;
 const float TEMP_MIN_C   = 18.0;
 const float LUX_MIN      = 300.0;
 const float LUX_MAX      = 800.0;
 const float SOUND_MAX_DB = 70.0;

 // Hysteresis: how far back inside the band a reading must return before
 // the alarm clears (stops chatter on the line).
 const float TEMP_HYST  = 0.5;
 const float LUX_HYST   = 25.0;
 const float SOUND_HYST = 3.0;

 // ============================ SOUND ===========================
 // Relative level, NOT a calibrated SPL meter. See CR_Monitor_NOTES.txt
 // for the model and the MIC_CAL_DB calibration procedure.
 //   dB = 20*log10(Vrms / MIC_REF_V) + MIC_DB_OFFSET + MIC_CAL_DB
 const float    ADC_MAX       = 4095.0f;
 const float    ADC_VREF      = 3.3f;
 const float    MIC_REF_V     = 0.00631f;
 const float    MIC_DB_OFFSET = 94.0f;
 const float    MIC_CAL_DB    = -77.0f;    // from calibration 2026-08-28

 float          micBias       = ADC_MAX / 2.0f;  // DC offset, measured at boot
 float          soundRmsMv    = 0;                // last reading, for calibration

 const uint16_t MIC_WINDOW_MS = 100;
 float soundAvgDb = 0;
 float soundMaxDb = 0;

 #define SOUND_DEBUG  true   // print raw ADC stats each read (wiring sanity check)

 const unsigned long READ_INTERVAL_MS = 2000;

 // ============================ OBJECTS / STATE =================
 Adafruit_SHT31    sht3x = Adafruit_SHT31();
 BH1750            lightMeter(BH1750_ADDR);

 WiFiClient        netClient;
 PubSubClient      mqtt(netClient);

 String   nodeId;
 String   topicTelemetry, topicStatus, topicAlert;
 unsigned long mqttLastTry  = 0;
 uint32_t      mqttFails    = 0;
 uint32_t      seq          = 0;
 int           lastAlertMask = -1;

 bool timeValid() { return time(nullptr) > TIME_VALID_MIN; }

 bool shtReady    = false;
 bool bh1750Ready = false;

 unsigned long lastRead   = 0;
 uint32_t      shtFails   = 0;
 uint32_t      luxFails   = 0;

 bool alertTemp  = false;
 bool alertLight = false;
 bool alertSound = false;

 // ============================ BUZZER =========================
 // Non-blocking beeper - no delay() anywhere in the pattern.
 const uint16_t BEEP_ON_MS  = 150;
 const uint16_t BEEP_OFF_MS = 150;
 const uint16_t FREQ_TEMP  = 2200;   // 3 beeps
 const uint16_t FREQ_LIGHT = 1200;   // 2 beeps
 const uint16_t FREQ_SOUND = 1700;   // 4 beeps

 uint8_t       beepsLeft = 0;
 bool          beepOn    = false;
 uint16_t      beepFreq  = 2000;
 unsigned long beepNext  = 0;

 void startAlarm(uint8_t beeps, uint16_t freq) {
   if (beepsLeft > 0) return;          // let the current pattern finish
   beepsLeft = beeps * 2;
   beepFreq  = freq;
   beepOn    = false;
   beepNext  = 0;
 }

 void serviceAlarm() {
   if (beepsLeft == 0) return;
   if (millis() < beepNext) return;

   beepOn = !beepOn;
   if (beepOn) {
     tone(BUZZER_PIN, beepFreq);
     beepNext = millis() + BEEP_ON_MS;
   } else {
     noTone(BUZZER_PIN);
     beepNext = millis() + BEEP_OFF_MS;
   }
   beepsLeft--;
   if (beepsLeft == 0) noTone(BUZZER_PIN);
 }

 // ============================ HELPERS ========================

 void i2cScan() {
   Serial.println(F("--- I2C scan ---"));
   uint8_t found = 0;
   for (uint8_t addr = 1; addr < 127; addr++) {
     Wire.beginTransmission(addr);
     if (Wire.endTransmission() == 0) {
       Serial.printf("  device at 0x%02X", addr);
       if      (addr == 0x44 || addr == 0x45) Serial.print(F("  <- SHT3x"));
       else if (addr == 0x23 || addr == 0x5C) Serial.print(F("  <- BH1750"));
       else if (addr == 0x27 || addr == 0x3F) Serial.print(F("  <- LCD backpack (unused)"));
       Serial.println();
       found++;
     }
   }
   if (found == 0) {
     Serial.println(F("  nothing found."));
     Serial.println(F("  check: SDA=GPIO8 SCL=GPIO9, 3.3V present, common ground"));
   }
   Serial.println(F("----------------"));
 }

 // Measure the MAX4466 DC operating point once at boot. Keep the room quiet.
 void calibrateMicBias() {
   const uint16_t N = 2000;
   float sum = 0;
   for (uint16_t i = 0; i < N; i++) { sum += analogRead(MIC_PIN); delayMicroseconds(200); }
   micBias = sum / N;
 }

 static float countsToDb(float rmsCounts) {
   float vrms = (rmsCounts * ADC_VREF) / ADC_MAX;
   if (vrms < 1e-6f) vrms = 1e-6f;              // avoid log10(0)
   return 20.0f * log10f(vrms / MIC_REF_V) + MIC_DB_OFFSET + MIC_CAL_DB;
 }

 // Fills soundAvgDb / soundMaxDb from RMS sub-windows (DC bias removed).
 void readSound() {
   const uint16_t SUB_MS    = 10;
   const uint8_t  SUB_COUNT = MIC_WINDOW_MS / SUB_MS;

   float sumDb    = 0;
   float maxDb    = -200.0f;
   float sumSqAll = 0;
   uint32_t nAll  = 0;
   int rawMin = 4095, rawMax = 0;
   double rawSum = 0;

   for (uint8_t s = 0; s < SUB_COUNT; s++) {
     unsigned long start = millis();
     float  sumSq = 0;
     uint32_t n   = 0;

     while (millis() - start < SUB_MS) {
       int raw = analogRead(MIC_PIN);
       if (raw <= 0 || raw >= 4095) continue;   // drop ESP32 ADC rail glitches
       if (raw < rawMin) rawMin = raw;
       if (raw > rawMax) rawMax = raw;
       rawSum += raw;

       float ac = raw - micBias;
       sumSq += ac * ac;
       n++;
     }
     if (n == 0) continue;

     float rmsCounts = sqrtf(sumSq / n);
     float db        = countsToDb(rmsCounts);
     sumDb += db;
     if (db > maxDb) maxDb = db;

     sumSqAll += sumSq;
     nAll     += n;
   }

   soundAvgDb = sumDb / SUB_COUNT;
   soundMaxDb = maxDb;
   soundRmsMv = nAll ? (sqrtf(sumSqAll / nAll) * ADC_VREF / ADC_MAX) * 1000.0f : 0.0f;

   if (SOUND_DEBUG && nAll) {
     Serial.printf("# mic raw: mean=%.0f min=%d max=%d p-p=%d  (bias=%.0f)\n",
                   rawSum / nAll, rawMin, rawMax, rawMax - rawMin, micBias);
   }
 }

 // Returns lux, or -1.0 on failure.
 float readLux() {
   if (!bh1750Ready) return -1.0f;
   float lux = lightMeter.readLightLevel();
   return (lux < 0) ? -1.0f : lux;
 }

 void evaluateAlarms(bool tempOk, float temp, bool luxOk, float lux, float soundPeak) {
   if (tempOk) {
     bool breach = alertTemp
       ? (temp > TEMP_MAX_C - TEMP_HYST || temp < TEMP_MIN_C + TEMP_HYST)
       : (temp > TEMP_MAX_C || temp < TEMP_MIN_C);
     if (breach && !alertTemp) {
       Serial.print(F("# ALERT temperature: ")); Serial.println(temp, 2);
       startAlarm(3, FREQ_TEMP);
     } else if (!breach && alertTemp) {
       Serial.println(F("# CLEAR temperature"));
     }
     alertTemp = breach;
   }

   if (luxOk) {
     bool breach = alertLight
       ? (lux < LUX_MIN + LUX_HYST || lux > LUX_MAX - LUX_HYST)
       : (lux < LUX_MIN || lux > LUX_MAX);
     if (breach && !alertLight) {
       Serial.print(F("# ALERT light: ")); Serial.println(lux, 0);
       startAlarm(2, FREQ_LIGHT);
     } else if (!breach && alertLight) {
       Serial.println(F("# CLEAR light"));
     }
     alertLight = breach;
   }

   // Sound alarm only fires once MIC_CAL_DB is set (an uncalibrated dB
   // figure is tens of dB off and would alarm constantly). Still logs.
   if (MIC_CAL_DB != 0.0f) {
     bool breach = alertSound
       ? (soundPeak > SOUND_MAX_DB - SOUND_HYST)
       : (soundPeak > SOUND_MAX_DB);
     if (breach && !alertSound) {
       Serial.print(F("# ALERT sound: ")); Serial.println(soundPeak, 1);
       startAlarm(4, FREQ_SOUND);
     } else if (!breach && alertSound) {
       Serial.println(F("# CLEAR sound"));
     }
     alertSound = breach;
   }
 }

 String alarmBanner() {
   if (!alertTemp && !alertLight && !alertSound) return "Classroom Monitor";
   String s = "ALERT:";
   if (alertTemp)  s += " TMP";
   if (alertLight) s += " LGT";
   if (alertSound) s += " SND";
   return s;
 }

 // Readable status block on Serial (replaces the dead LCD).
 void printPanel(bool tempOk, float temp, float hum,
                 bool luxOk, float lux, bool wifiUp) {
   Serial.println(F("+------------------------------+"));
   Serial.print  (F("| ")); Serial.println(alarmBanner());

   if (tempOk) {
     Serial.print(F("| Temp : ")); Serial.print(temp, 1); Serial.println(F(" C"));
     Serial.print(F("| Humid: ")); Serial.print(hum, 1);  Serial.println(F(" %"));
   } else {
     Serial.println(F("| Temp : --.- C  ERR"));
     Serial.println(F("| Humid: --.- %"));
   }

   Serial.print(F("| Light: "));
   if (luxOk) { Serial.print((int)lux); Serial.println(F(" lx")); }
   else       { Serial.println(F("---- lx")); }

   Serial.print(F("| Sound: ")); Serial.print(soundMaxDb, 0);
   Serial.print(F(" dB peak (avg "));  Serial.print(soundAvgDb, 0);
   Serial.print(F(" dB, "));           Serial.print(soundRmsMv, 2);
   Serial.println(F(" mVrms)"));

   Serial.print(F("| Wi-Fi: "));
   Serial.println(wifiUp ? F("connected") : F("DOWN"));

   Serial.print(F("| MQTT : "));
   if      (!wifiUp)          Serial.println(F("- (no Wi-Fi)"));
   else if (mqtt.connected()) Serial.print(F("connected"));
   else                       Serial.println(F("down"));
   if (wifiUp && mqtt.connected()) {
     Serial.print(F(", seq ")); Serial.print(seq);
     Serial.println(timeValid() ? F(", time ok") : F(", time NOT synced"));
   }
   Serial.println(F("+------------------------------+"));
 }

 // Screen version of the block above. Included here, not at the top of the
 // file, because it reads globals declared further up (soundMaxDb, mqtt,
 // seq, alertTemp/Light/Sound).
 #include "TFT_Panel.h"

 // Boot self-test. After TFT_Panel.h so it can read the panel back.
 #include "Preflight.h"

 // ============================ WI-FI STATE MACHINE =============
 // SCAN -> CONNECTING -> CONNECTED, falling back to WAIT_RETRY on any
 // failure or link drop. Sensors and buzzer run regardless of network.
 enum WifiState { WIFI_ST_SCAN, WIFI_ST_CONNECTING, WIFI_ST_CONNECTED, WIFI_ST_WAIT_RETRY };
 WifiState     wifiState      = WIFI_ST_SCAN;
 unsigned long wifiStateSince = 0;
 uint32_t      wifiRetryCount = 0;

 bool wifiSsidInRange(const char* ssid) {
   int found = WiFi.scanNetworks();
   bool inRange = false;
   for (int i = 0; i < found; i++) {
     if (WiFi.SSID(i) == ssid) { inRange = true; break; }
   }
   WiFi.scanDelete();
   return inRange;
 }

 void handleWifi() {
   switch (wifiState) {

     case WIFI_ST_SCAN:
       Serial.print(F("Wi-Fi: scanning for \"")); Serial.print(WIFI_SSID);
       Serial.println(F("\"..."));
       if (wifiSsidInRange(WIFI_SSID)) {
         Serial.println(F("Wi-Fi: found, connecting..."));
         WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
         wifiState      = WIFI_ST_CONNECTING;
         wifiStateSince = millis();
       } else {
         Serial.println(F("Wi-Fi: not in range, will retry"));
         wifiState      = WIFI_ST_WAIT_RETRY;
         wifiStateSince = millis();
       }
       break;

     case WIFI_ST_CONNECTING:
       if (WiFi.status() == WL_CONNECTED) {
         Serial.print(F("Wi-Fi: connected, IP "));
         Serial.print(WiFi.localIP());
         Serial.print(F(", RSSI "));
         Serial.println(WiFi.RSSI());
         wifiRetryCount = 0;
         wifiState      = WIFI_ST_CONNECTED;
       } else if (millis() - wifiStateSince > WIFI_CONNECT_TIMEOUT_MS) {
         wifiRetryCount++;
         Serial.print(F("Wi-Fi: timed out, retry #")); Serial.println(wifiRetryCount);
         WiFi.disconnect(true);
         wifiState      = WIFI_ST_WAIT_RETRY;
         wifiStateSince = millis();
       }
       break;

     case WIFI_ST_CONNECTED:
       if (WiFi.status() != WL_CONNECTED) {
         Serial.println(F("Wi-Fi: lost, reconnecting"));
         wifiState      = WIFI_ST_WAIT_RETRY;
         wifiStateSince = millis();
       }
       break;

     case WIFI_ST_WAIT_RETRY:
       if (millis() - wifiStateSince > WIFI_RETRY_DELAY_MS) wifiState = WIFI_ST_SCAN;
       break;
   }
 }

 // ============================ MQTT ===========================
 // Runs downstream of Wi-Fi; never blocks more than the socket timeout.

 void mqttInit() {
   // Read the MAC from eFuse, not from the Wi-Fi driver. WiFi.macAddress()
   // returns all zeros until the driver has actually started, which gave
   // every node the id "000000" - a shared MQTT client id makes brokers
   // disconnect each node as the next one connects.
   uint8_t mac[6];
   esp_read_mac(mac, ESP_MAC_WIFI_STA);
   char id[7];
   snprintf(id, sizeof(id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
   nodeId = id;

   String base    = String("classroom/") + ROOM_ID + "/" + nodeId + "/";
   topicTelemetry = base + "telemetry";
   topicStatus    = base + "status";
   topicAlert     = base + "alert";

   mqtt.setServer(MQTT_HOST, MQTT_PORT);
   mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
   mqtt.setSocketTimeout(3);
   mqtt.setBufferSize(512);

   Serial.print(F("MQTT: node id ")); Serial.print(nodeId);
   Serial.print(F(", topic base classroom/")); Serial.print(ROOM_ID);
   Serial.print('/'); Serial.println(nodeId);
 }

 void mqttConnect() {
   const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
   const char* pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;

   Serial.print(F("MQTT: connecting to "));
   Serial.print(MQTT_HOST); Serial.print(':'); Serial.print(MQTT_PORT); Serial.print(F("... "));

   // Last Will -> broker publishes "offline" (retained) if this node drops.
   bool ok = mqtt.connect(nodeId.c_str(), user, pass,
                          topicStatus.c_str(), 0, true, "offline");
   if (ok) {
     Serial.println(F("connected"));
     mqtt.publish(topicStatus.c_str(), "online", true);
     lastAlertMask = -1;
   } else {
     mqttFails++;
     Serial.print(F("failed, rc=")); Serial.print(mqtt.state());
     Serial.print(F(" (total ")); Serial.print(mqttFails); Serial.println(F(")"));
   }
 }

 void mqttService() {
   if (wifiState != WIFI_ST_CONNECTED) return;
   if (mqtt.connected()) { mqtt.loop(); return; }
   if (millis() - mqttLastTry < MQTT_RECONNECT_MS) return;
   mqttLastTry = millis();
   mqttConnect();
 }

 static void numOrNull(char* out, size_t sz, bool ok, float v, int dp) {
   if (ok) snprintf(out, sz, "%.*f", dp, v);
   else    snprintf(out, sz, "null");          // JSON has no NaN
 }

 void publishTelemetry(bool tempOk, float temp, float hum, bool luxOk, float lux) {
   if (!mqtt.connected()) return;

   char tb[12], hb[12], lb[12];
   numOrNull(tb, sizeof(tb), tempOk, temp, 2);
   numOrNull(hb, sizeof(hb), tempOk, hum, 2);
   numOrNull(lb, sizeof(lb), luxOk, lux, 1);

   time_t now  = time(nullptr);
   bool   tsOk = now > TIME_VALID_MIN;

   char payload[320];
   snprintf(payload, sizeof(payload),
     "{\"ts\":%ld,\"ts_valid\":%s,\"uptime_ms\":%lu,\"seq\":%lu,"
     "\"temp_c\":%s,\"hum_pct\":%s,\"lux\":%s,"
     "\"sound_avg_db\":%.1f,\"sound_peak_db\":%.1f,\"sound_calibrated\":%s,"
     "\"alerts\":\"%s%s%s\",\"rssi\":%d}",
     (long)(tsOk ? now : 0), tsOk ? "true" : "false",
     millis(), (unsigned long)(++seq),
     tb, hb, lb,
     soundAvgDb, soundMaxDb, (MIC_CAL_DB != 0.0f) ? "true" : "false",
     alertTemp ? "T" : "-", alertLight ? "L" : "-", alertSound ? "S" : "-",
     WiFi.RSSI());

   if (!mqtt.publish(topicTelemetry.c_str(), payload)) {
     mqttFails++;
     Serial.println(F("# MQTT publish failed (telemetry)"));
   }
 }

 // Retained alert-state message, published only when the state changes.
 void publishAlertState() {
   int mask = (alertTemp ? 1 : 0) | (alertLight ? 2 : 0) | (alertSound ? 4 : 0);
   if (mask == lastAlertMask) return;
   if (!mqtt.connected()) return;
   lastAlertMask = mask;

   char payload[96];
   snprintf(payload, sizeof(payload),
     "{\"temp\":%s,\"light\":%s,\"sound\":%s}",
     alertTemp  ? "true" : "false",
     alertLight ? "true" : "false",
     alertSound ? "true" : "false");
   mqtt.publish(topicAlert.c_str(), payload, true);
 }

 // ============================ SETUP ==========================
 void setup() {
   Serial.begin(115200);
   delay(800);
   Serial.println();
   Serial.println(F("Classroom Environment Monitor - Stage 2"));
   Serial.flush();   // if this line never appears, the hang is before setup()

   pinMode(BUZZER_PIN, OUTPUT);
   noTone(BUZZER_PIN);

   analogReadResolution(12);
   analogSetPinAttenuation(MIC_PIN, ADC_11db);   // full 0-3.3V range
   // Touch shares LCD_RS/LCD_CS and is read on ADC1; same range as the mic.
   analogSetPinAttenuation(TFT_DC, ADC_11db);
   analogSetPinAttenuation(TFT_CS, ADC_11db);

   // Panel first, so the splash is up while the rest of the boot runs.
   // tftBegin() only drives the parallel bus pins, so it cannot disturb
   // the idle I2C levels preflightBus() reads next.
   tftBegin();

   // Self-test before anything is brought up. Brings Wire up as a side
   // effect, so the sensor begin() calls below can use it directly.
   preflightBus();

   preflightDisplay();
   preflightSummary();
   tftPreflight();        // same report on the TFT, then the dashboard

   calibrateMicBias();
   Serial.printf("Mic DC bias: %.0f counts (%.2f V)\n",
                 micBias, micBias * ADC_VREF / ADC_MAX);

   if (sht3x.begin(SHT3X_ADDR)) {
     shtReady = true;
     Serial.printf("SHT3x ready at 0x%02X\n", SHT3X_ADDR);
     sht3x.heater(false);                        // heater adds self-heating error
   } else {
     Serial.printf("SHT3x NOT found at 0x%02X - try 0x45\n", SHT3X_ADDR);
   }

   if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
     bh1750Ready = true;
     Serial.printf("BH1750 ready at 0x%02X\n", BH1750_ADDR);
   } else {
     Serial.printf("BH1750 NOT found at 0x%02X - try 0x5C\n", BH1750_ADDR);
   }

   WiFi.mode(WIFI_STA);
   WiFi.disconnect();
   wifiState      = WIFI_ST_SCAN;
   wifiStateSince = millis();

   mqttInit();
   configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);

   startAlarm(2, 1500);                          // boot chirp / buzzer self-test

   Serial.print(F("Thresholds: temp "));
   Serial.print(TEMP_MIN_C, 1); Serial.print(F("-")); Serial.print(TEMP_MAX_C, 1);
   Serial.print(F(" C | lux "));
   Serial.print(LUX_MIN, 0); Serial.print(F("-")); Serial.print(LUX_MAX, 0);
   Serial.print(F(" | sound peak max ")); Serial.print(SOUND_MAX_DB, 0);
   Serial.println(F(" dB"));

   delay(800);
   Serial.println(F("time_ms,temp_c,humidity_pct,lux,sound_avg_db,sound_peak_db,alerts"));
 }

 // ============================ LOOP ==========================
 void loop() {
   serviceAlarm();
   handleWifi();
   mqttService();

   if (millis() - lastRead < READ_INTERVAL_MS) return;
   lastRead = millis();

   // temp / humidity
   float temp = NAN, hum = NAN;
   bool tempOk = false;
   if (shtReady) {
     temp   = sht3x.readTemperature();
     hum    = sht3x.readHumidity();
     tempOk = !isnan(temp) && !isnan(hum);
     if (!tempOk) {
       shtFails++;
       Serial.print(F("# SHT3x read failed, total: ")); Serial.println(shtFails);
     }
   }

   // light
   float lux   = readLux();
   bool  luxOk = (lux >= 0);
   if (!luxOk && bh1750Ready) {
     luxFails++;
     Serial.print(F("# BH1750 read failed, total: ")); Serial.println(luxFails);
   }

   // sound
   readSound();

   evaluateAlarms(tempOk, temp, luxOk, lux, soundMaxDb);
   publishAlertState();

   bool wifiUp = (wifiState == WIFI_ST_CONNECTED);
   printPanel(tempOk, temp, hum, luxOk, lux, wifiUp);
   drawPanel (tempOk, temp, hum, luxOk, lux, wifiUp);

   // Serial CSV
   Serial.print(millis());                  Serial.print(',');
   if (tempOk) Serial.print(temp, 2); else Serial.print("NaN");
   Serial.print(',');
   if (tempOk) Serial.print(hum, 2);  else Serial.print("NaN");
   Serial.print(',');
   if (luxOk)  Serial.print(lux, 1);  else Serial.print("NaN");
   Serial.print(',');
   Serial.print(soundAvgDb, 1);             Serial.print(',');
   Serial.print(soundMaxDb, 1);             Serial.print(',');
   Serial.print(alertTemp  ? "T" : "-");
   Serial.print(alertLight ? "L" : "-");
   Serial.println(alertSound ? "S" : "-");

   // MQTT telemetry (no-op if broker link is down)
   publishTelemetry(tempOk, temp, hum, luxOk, lux);
 }
