/* Preflight self-test - Classroom Environment Monitor
 *
 * Runs before the sensors, display and network are brought up, and prints
 * a PASS/FAIL report for each subsystem. Nothing here halts the boot: a
 * classroom node should still come up and report what it can, so a failure
 * is printed and the sketch carries on.
 *
 * Included AFTER TFT_Panel.h so the tft object exists for the display test.
 *
 * The mic thresholds encode a real failure we hit on this board: with the
 * 3V3 wire off, the sensor rail still floats to ~2.5 V, back-fed through the
 * chips ESD protection diodes from the I2C lines. Everything looks powered
 * and nothing works. The MAX4466 idles at VCC/2, so its DC bias is the
 * cheapest voltmeter we have on that rail - see PF_MIC_PHANTOM_* below.
 */

// Set to 0 to compile the self-test out entirely. If the board stops
// booting after a change here, flip this to 0 and re-upload: if it still
// will not boot, the fault is not in this file.
#define PREFLIGHT_ENABLE 1

// The panel ID readback in [5] drives the parallel bus and reads it back.
// Set to 0 if the display is not wired yet, or if [5] is the last line you
// see before a hang.
#define PREFLIGHT_DISPLAY 1

// Expected MAX4466 DC bias, in raw 12-bit counts.
//   VCC 3.3 V -> 1.65 V -> 2048 counts ideal.
//   The S3 uncalibrated ADC at 11 dB reads a few % low, so ~1900-2050 real.
#define PF_MIC_OK_LO        1800
#define PF_MIC_OK_HI        2150
//   VCC 2.5 V -> 1.25 V -> ~1550 counts. The diode-drop signature.
#define PF_MIC_PHANTOM_LO   1400
#define PF_MIC_PHANTOM_HI   1700
#define PF_MIC_DEAD          100   // below this, nothing is driving the pin

enum PfResult { PF_PASS, PF_WARN, PF_FAIL, PF_SKIP };

static PfResult pfBus = PF_SKIP, pfSensors = PF_SKIP,
                pfMicR = PF_SKIP, pfDisp = PF_SKIP;

// What the mic measurement looked like, kept raw so the cross-check pass
// can reinterpret it once the sensor result is known.
enum PfMicCat { MIC_OK, MIC_DEAD, MIC_PHANTOM, MIC_LOW, MIC_ODD };
static PfMicCat pfMicCat   = MIC_OK;
static uint16_t pfMicBias  = 0;
static float    pfMicVolts = 0;
static bool     pfMicIsMic = false;   // fault is the mic, not the shared rail

static const char* pfName(PfResult r) {
  switch (r) {
    case PF_PASS: return "PASS";
    case PF_WARN: return "WARN";
    case PF_FAIL: return "FAIL";
    default:      return "SKIP";
  }
}

// ---------------------------------------------------------------
// [1] Bus lines, read BEFORE Wire.begin() so we see the real idle state.
// No internal pull-ups: a powered, correctly wired breakout holds both
// lines high through its own ~10k. Enabling the internal pull-ups here
// would mask exactly the fault we are looking for.
// ---------------------------------------------------------------
static void pfCheckLines() {
  Serial.println(F("[1] I2C bus lines"));

  // Pull DOWN internally (~45k) and see if something outside still wins.
  // A plain INPUT floats, and a floating CMOS input reads HIGH as often as
  // not - which reports a pull-up that is not there. Against the internal
  // pulldown, a real 10k pull-up divides to ~2.7 V and reads HIGH; nothing
  // externally connected reads LOW. That distinction is the whole test.
  pinMode(I2C_SDA, INPUT_PULLDOWN);
  pinMode(I2C_SCL, INPUT_PULLDOWN);
  delay(10);
  int sda = digitalRead(I2C_SDA);
  int scl = digitalRead(I2C_SCL);

  Serial.printf("    SDA (GPIO%d): %s\n", I2C_SDA, sda ? "HIGH" : "LOW");
  Serial.printf("    SCL (GPIO%d): %s\n", I2C_SCL, scl ? "HIGH" : "LOW");

  if (sda && scl) {
    pfBus = PF_PASS;
    Serial.println(F("    PASS - external pull-ups present, both lines land"));
  } else {
    pfBus = PF_FAIL;
    Serial.println(F("    FAIL - a LOW line has no pull-up holding it up."));
    Serial.println(F("      -> module unpowered, wire not landing, or line"));
    Serial.println(F("         shorted to GND. Fix this first; a scan cannot"));
    Serial.println(F("         succeed while a bus line is stuck low."));
  }
}

// ---------------------------------------------------------------
// [2] Who is actually on the bus, and [3] are they where we expect.
// ---------------------------------------------------------------
static void pfCheckDevices() {
  Serial.println(F("[3] I2C device scan"));

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(50);

  bool sht = false, bh = false, shtAlt = false, bhAlt = false;
  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    found++;
    Serial.printf("    0x%02X", addr);
    if      (addr == 0x44) { Serial.print(F("  SHT3x"));              sht    = true; }
    else if (addr == 0x45) { Serial.print(F("  SHT3x (ADR high)"));   shtAlt = true; }
    else if (addr == 0x23) { Serial.print(F("  BH1750"));             bh     = true; }
    else if (addr == 0x5C) { Serial.print(F("  BH1750 (ADDR high)")); bhAlt  = true; }
    Serial.println();
  }

  if (found == 0) Serial.println(F("    nothing responded"));
  Serial.printf("    %u device(s)\n", found);

  Serial.println(F("[4] Expected sensors"));
  Serial.printf("    SHT3x  @0x%02X : %s\n", SHT3X_ADDR,
                sht ? "PRESENT" : (shtAlt ? "WRONG ADDRESS (0x45)" : "MISSING"));
  Serial.printf("    BH1750 @0x%02X : %s\n", BH1750_ADDR,
                bh  ? "PRESENT" : (bhAlt  ? "WRONG ADDRESS (0x5C)" : "MISSING"));

  if (sht && bh) {
    pfSensors = PF_PASS;
    Serial.println(F("    PASS"));
  } else if (shtAlt || bhAlt) {
    pfSensors = PF_WARN;
    Serial.println(F("    WARN - device found at the alternate address."));
    Serial.println(F("      -> tie that module ADDR/ADR pin to GND, or change"));
    Serial.println(F("         the address in the sketch to match."));
  } else {
    pfSensors = PF_FAIL;
    Serial.println(F("    FAIL - expected sensors did not answer."));
    if (pfMicR == PF_FAIL || pfMicR == PF_WARN) {
      // Power is the more likely story. Do not send anyone hunting for a
      // swapped wire while the rail is below what the parts need to run:
      // BH1750 needs 2.4 V, SHT3x 2.15 V.
      Serial.println(F("      -> the rail check above failed. Below ~2.4 V these"));
      Serial.println(F("         parts cannot run, but their pull-ups still hold"));
      Serial.println(F("         the bus high - which is what you are seeing."));
      Serial.println(F("         Fix the supply first; this is likely downstream."));
    } else if (found == 0 && pfBus == PF_PASS) {
      Serial.println(F("      -> lines idle high but nobody answers: check that"));
      Serial.println(F("         SDA and SCL are not swapped (SDA=8, SCL=9)."));
    }
  }
}

// ---------------------------------------------------------------
// [4] Sensor rail health, measured through the MAX4466 DC bias.
// ---------------------------------------------------------------
static void pfCheckMic() {
  Serial.println(F("[2] Sensor rail (via MAX4466 bias)"));

  const uint16_t N = 400;
  uint32_t sum = 0;
  for (uint16_t i = 0; i < N; i++) { sum += analogRead(MIC_PIN); delayMicroseconds(100); }
  uint16_t bias  = sum / N;
  float    volts = bias * ADC_VREF / ADC_MAX;

  Serial.printf("    GPIO%d DC bias: %u counts (%.2f V) -> source ~%.1f V\n",
                MIC_PIN, bias, volts, volts * 2.0f);

  pfMicBias  = bias;
  pfMicVolts = volts;

  if      (bias <  PF_MIC_DEAD)                                    pfMicCat = MIC_DEAD;
  else if (bias >= PF_MIC_PHANTOM_LO && bias <= PF_MIC_PHANTOM_HI) pfMicCat = MIC_PHANTOM;
  else if (bias <  PF_MIC_PHANTOM_LO)                              pfMicCat = MIC_LOW;
  else if (bias >= PF_MIC_OK_LO      && bias <= PF_MIC_OK_HI)      pfMicCat = MIC_OK;
  else                                                             pfMicCat = MIC_ODD;

  pfMicR = (pfMicCat == MIC_OK)  ? PF_PASS
         : (pfMicCat == MIC_ODD) ? PF_WARN
                                 : PF_FAIL;

  // No diagnosis here, on purpose. This runs before the I2C scan, so it
  // cannot yet tell "the rail is low" from "the mic is lying about the
  // rail" - only the sensor result settles that. See pfDiagnose().
  if (pfMicCat == MIC_OK) Serial.println(F("    PASS - rail is at 3.3 V"));
  else                    Serial.println(F("    out of range - diagnosis in [6]"));
}

// ---------------------------------------------------------------
// [5] Display. Call AFTER tftBegin(). Reads the panel ID register back
// over the parallel bus, which only works if RD and the data lines are
// wired. Readback is not reliable on every panel, so a dead result is
// reported as inconclusive rather than a hard failure.
// ---------------------------------------------------------------
void preflightDisplay() {
#if !PREFLIGHT_ENABLE || !PREFLIGHT_DISPLAY
  return;
#else
  Serial.println(F("[5] Display"));
  Serial.printf("    ILI9481 parallel, D0-D7 = GPIO%d-%d, WR=%d RD=%d RS=%d CS=%d RST=%d\n",
                TFT_D0, TFT_D7, TFT_WR, TFT_RD, TFT_DC, TFT_CS, TFT_RST);

  uint8_t id1 = tft.readcommand8(0x04, 1);   // RDDID
  uint8_t id2 = tft.readcommand8(0x04, 2);
  Serial.printf("    ID readback: 0x%02X 0x%02X\n", id1, id2);

  // A floating bus holds the last value driven onto it, so an unconnected
  // panel commonly "answers" with the command byte itself. Treat that as
  // no answer, not a pass - it is the shape of a disconnected bus.
  bool echo = (id1 == 0x04 && id2 == 0x04);
  bool dead = ((id1 == 0x00 && id2 == 0x00) || (id1 == 0xFF && id2 == 0xFF));

  if (echo) {
    pfDisp = PF_WARN;
    Serial.println(F("    WARN - readback equals the command byte."));
    Serial.println(F("           That is bus capacitance holding the last"));
    Serial.println(F("           value, not a panel replying."));
    Serial.println(F("      -> treat the display as NOT connected until the"));
    Serial.println(F("         red-screen smoke test in WIRING_TFT.md passes."));
  } else if (!dead) {
    pfDisp = PF_PASS;
    Serial.println(F("    PASS - panel answered on the parallel bus"));
  } else {
#if defined(ILI9481_DRIVER)
    // Confirmed on this board: the ILI9481 fitted here does not implement
    // ID readback, so a null answer says nothing about the display. It was
    // reported as WARN while the panel worked perfectly - exactly the kind
    // of standing warning that trains you to ignore the row, and hides a
    // real fault when one finally appears.
    pfDisp = PF_SKIP;
    Serial.println(F("    SKIP - this panel does not support ID readback."));
    Serial.println(F("           Expected on the ILI9481 fitted here. It says"));
    Serial.println(F("           nothing about whether the display works -"));
    Serial.println(F("           the screen in front of you does."));
#else
    pfDisp = PF_WARN;
    Serial.println(F("    WARN - no readback. Either the panel is not connected,"));
    Serial.println(F("           or it does not support ID readback."));
    Serial.println(F("      -> confirm with the red-screen smoke test in"));
    Serial.println(F("         WIRING_TFT.md before trusting this line."));
#endif
  }
  Serial.flush();
#endif
}

// ---------------------------------------------------------------
// [6] Cross-check. Every check above sees only its own subsystem. This
// pass has all of them at once, which is the only way to tell a real
// fault from a measurement that merely looks like one.
// ---------------------------------------------------------------
static void pfDiagnose() {
  if (pfMicCat == MIC_OK) return;

  Serial.println(F("[6] Cross-check"));

  // Both I2C parts answering is hard proof the rail is healthy: a BH1750
  // needs 2.4 V and an SHT3x 2.15 V just to ACK. If they answered, a low
  // mic bias cannot be the shared rail - the mic is measuring itself.
  if (pfSensors == PF_PASS) {
    pfMicIsMic = true;
    Serial.println(F("    This is the MICROPHONE, not the rail."));
    Serial.println(F("    Both I2C sensors answered, which takes at least"));
    Serial.println(F("    2.4 V, so the shared rail is provably fine. A"));
    Serial.println(F("    MAX4466 idles at VCC/2 whatever its gain trimmer,"));
    Serial.printf ("    so %.2f V out means that module alone sees ~%.1f V.\n",
                   pfMicVolts, pfMicVolts * 2.0f);
    Serial.println(F("      -> reseat its VCC, OUT and GND first."));
    Serial.println(F("      -> still low: probe the module VCC with Test_Rail."));
    Serial.println(F("         ~3.1 V there means the module itself is dead."));
    Serial.println(F("    Sound is affected. Temp, humidity and light are not."));
    return;
  }

  Serial.println(F("    This points at the RAIL, not the mic: the sensors"));
  Serial.println(F("    did not answer either, which is what a collapsed"));
  Serial.println(F("    rail looks like from here."));
  if (pfMicCat == MIC_PHANTOM) {
    Serial.println(F("      -> bias sits a diode drop below half of 3.3 V."));
    Serial.println(F("         The rail is back-fed through the ESD diodes,"));
    Serial.println(F("         not supplied. The 3V3 wire is not landing."));
  } else if (pfMicCat == MIC_DEAD) {
    Serial.println(F("      -> pin at ground: MAX4466 OUT not connected, or"));
    Serial.println(F("         nothing on the rail is powered at all."));
  } else {
    Serial.println(F("      -> check the 3V3 feed and every ground before"));
    Serial.println(F("         suspecting any individual part."));
  }
}

void preflightSummary() {
  pfDiagnose();

  Serial.println(F("+------------- PREFLIGHT -------------+"));
  Serial.printf("|  %-16s%-4s               |\n", "I2C bus lines", pfName(pfBus));
  Serial.printf("|  %-16s%-4s               |\n", "Sensors",       pfName(pfSensors));
  Serial.printf("|  %-16s%-4s               |\n",
                pfMicIsMic ? "Microphone" : "Sensor rail", pfName(pfMicR));
  Serial.printf("|  %-16s%-4s               |\n", "Display",       pfName(pfDisp));
  Serial.println(F("+-------------------------------------+"));

  if (pfBus == PF_FAIL || pfMicR == PF_FAIL) {
    Serial.println(F("Power or wiring fault above. Readings below will be"));
    Serial.println(F("wrong or missing until it is fixed."));
  }
  Serial.println();
}

// Bus, sensors and mic. Call before tftBegin().
void preflightBus() {
#if !PREFLIGHT_ENABLE
  Wire.begin(I2C_SDA, I2C_SCL);   // still needed by the sensor begin() calls
  Wire.setClock(100000);
  return;
#else
  Serial.println(F("\n========== PREFLIGHT =========="));
  Serial.flush();                 // each stage flushes, so a hang names itself
  pfCheckLines();   Serial.flush();
  pfCheckMic();     Serial.flush();   // power before devices: a sagging rail
  pfCheckDevices(); Serial.flush();   // explains a silent bus, not vice versa
#endif
}

// ---------------------------------------------------------------
// The same report as preflightSummary(), on the TFT. Shown before the
// dashboard so a failure is visible to whoever is standing at the node -
// the Serial version is only useful to someone with a laptop attached.
//
// Nothing here halts the boot, and nothing blocks: the report holds
// briefly - longer when something failed - then the dashboard takes
// over as soon as it has a reading to show.
// ---------------------------------------------------------------
static uint16_t pfColour(PfResult r) {
  switch (r) {
    case PF_PASS: return COL_OK;
    case PF_WARN: return COL_WARN;
    case PF_FAIL: return COL_ALERT;
    default:      return COL_TRACK;
  }
}

static void pfRow(int16_t y, const char* label, PfResult r) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_VALUE, COL_SURFACE);
  tft.drawString(label, 30, y + 5, 4);

  uint16_t c = pfColour(r);
  tft.fillRoundRect(360, y, 86, 30, 6, c);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_BG, c);
  tft.drawString(pfName(r), 403, y + 15, 2);
  tft.setTextDatum(TL_DATUM);
}

void tftPreflight() {
#if !PREFLIGHT_ENABLE
  return;
#else
  PfResult worst = PF_PASS;
  PfResult all[4] = { pfBus, pfSensors, pfMicR, pfDisp };
  for (uint8_t i = 0; i < 4; i++) {
    if (all[i] == PF_FAIL) worst = PF_FAIL;
    else if (all[i] == PF_WARN && worst != PF_FAIL) worst = PF_WARN;
  }

  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, PANEL_W, 44, COL_SURFACE);
  tft.drawFastHLine(0, 44, PANEL_W, COL_EDGE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_VALUE, COL_SURFACE);
  tft.drawString(F("PREFLIGHT SELF-TEST"), 14, 12, 4);

  tft.fillRoundRect(8, 56, 464, 186, 8, COL_SURFACE);
  tft.drawRoundRect(8, 56, 464, 186, 8, COL_EDGE);

  pfRow(70,  "I2C bus lines", pfBus);
  pfRow(112, "Sensors",       pfSensors);
  pfRow(154, pfMicIsMic ? "Microphone" : "Sensor rail", pfMicR);
  pfRow(196, "Display",       pfDisp);

  // One line saying what it means, because PASS/FAIL alone does not tell
  // anyone standing at the node what to do about it.
  tft.setTextColor(pfColour(worst), COL_BG);
  const char* verdict;
  if      (worst == PF_FAIL) verdict = "FAULT - readings will be wrong or missing";
  else if (worst == PF_WARN) verdict = "WARNINGS - the node will run, check the log";
  else                       verdict = "all checks passed";
  tft.drawString(verdict, 14, 256, 2);

  // Non-blocking. The report stays up and drawPanel() takes the screen
  // once the dashboard has a real reading. Holding here with delay()
  // would stall Wi-Fi and NTP for the whole wait, which is the opposite
  // of what you want while a screen says "waiting".
  //
  // Longer hold when something is wrong: a 4 s flash of a FAIL screen is
  // not long enough for anyone standing at the node to read it.
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString(F("waiting for the first sensor reading..."), 14, 288, 2);
  panelHoldPreflight(worst == PF_PASS ? 4000 : 10000, 20000);
#endif
}
