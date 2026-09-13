/* Rail voltmeter - Classroom Environment Monitor
 * Turns the ESP32-S3 into a DC voltmeter for 0 - 3.3 V, for when there
 * is no multimeter on the bench. Same idea as the MAX4466 bias check in
 * Preflight.h, but with the probe brought out to a free pin.
 *
 * BUILT FOR ONE QUESTION: does the 3.5" shield have its own 3.3 V
 * regulator? A lit backlight with a white screen is what you get when
 * the panel controller has no logic supply, and this settles it.
 *
 * ---------------------------------------------------------------------
 * WIRING
 *   ESP32 GPIO4  <-- 10k resistor -->  the point you want to measure
 *   ESP32 GND    <---------------->    that circuit's GND  (must share)
 *
 * GPIO4 is ADC1 and is free in this project (see TFT_Setup_CR_Monitor.h).
 *
 * THE 10k SERIES RESISTOR IS PROTECTION, NOT PART OF THE MEASUREMENT.
 * The ADC input draws almost nothing, so it does not change the reading.
 * What it does is limit current through the chip's ESD clamp if you ever
 * touch the probe to something above 3.3 V - that is the mistake that
 * kills a GPIO. Any value from 1k to 100k works. If you genuinely have
 * no resistor you can probe directly, but then you must be certain the
 * point is at or below 3.3 V. NEVER probe 5V directly, with or without
 * the resistor - this sketch cannot measure it and the pin may not
 * survive it.
 * ---------------------------------------------------------------------
 *
 * TO ANSWER THE SHIELD QUESTION:
 *   1. Wire ONLY J4 5V and J4 GND from the ESP32 to the shield.
 *      Leave all 13 signal lines disconnected.
 *   2. Probe GPIO4 (through the resistor) to the shield's J4 3.3V pin.
 *   3. Read the verdict below.
 */

#include <Arduino.h>

#define PROBE_PIN 4          // ADC1, free in this project

const unsigned long READ_MS = 500;
const uint16_t      SAMPLES = 64;    // averaging kills ADC jitter

unsigned long lastRead = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) delay(10);
  delay(400);

  Serial.println(F("\n\n=== ESP32 as a voltmeter ==="));
  Serial.printf("Probe on GPIO%d, through a series resistor, GND shared.\n", PROBE_PIN);
  Serial.println(F("Range 0 - 3.3 V only. Readings saturate near 3.1 V:"));
  Serial.println(F("that is the ADC topping out, not a low rail.\n"));

  analogReadResolution(12);
  // 12 dB attenuation gives the widest input span this chip offers.
  analogSetPinAttenuation(PROBE_PIN, ADC_11db);
}

// analogReadMilliVolts() applies the factory ADC calibration, so this is
// a real voltage rather than raw counts scaled by a guess.
uint32_t readMilliVolts() {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < SAMPLES; i++) {
    sum += analogReadMilliVolts(PROBE_PIN);
    delayMicroseconds(200);
  }
  return sum / SAMPLES;
}

void verdict(uint32_t mv) {
  if (mv < 150) {
    Serial.println(F("   -> ZERO. Nothing is driving this point."));
    Serial.println(F("      On the shield 3.3V pin this means NO onboard"));
    Serial.println(F("      regulator. The panel controller has no logic"));
    Serial.println(F("      supply, which is why the screen is white."));
    Serial.println(F("      FIX: wire shield 3.3V -> ESP32 3V3."));
  } else if (mv < 1000) {
    Serial.println(F("   -> very low. Not a supply rail. Check the probe is"));
    Serial.println(F("      on the pin you think it is, and that GND is shared."));
  } else if (mv < 2400) {
    Serial.println(F("   -> partial. Something is feeding this point weakly -"));
    Serial.println(F("      a back-fed rail through pull-ups or ESD diodes"));
    Serial.println(F("      looks like this. Not a healthy 3.3 V supply."));
  } else {
    Serial.println(F("   -> VOLTAGE PRESENT, at or above ~2.4 V."));
    Serial.println(F("      Remember the ADC saturates around 3.1 V, so a"));
    Serial.println(F("      real 3.3 V rail reads a little low here. That is"));
    Serial.println(F("      expected and does NOT mean the rail is sagging."));
    Serial.println(F("      On the shield 3.3V pin this means the regulator"));
    Serial.println(F("      IS present: leave that pin unconnected and go"));
    Serial.println(F("      change the driver to ILI9486 instead."));
  }
}

void loop() {
  if (millis() - lastRead < READ_MS) return;
  lastRead = millis();

  uint32_t mv = readMilliVolts();
  unsigned long s = millis() / 1000;

  Serial.printf("T+%03lu:%02lu  %4lu mV  (%.2f V)\n",
                s / 60, s % 60, (unsigned long)mv, mv / 1000.0);

  // Only comment on every 10th reading, so the verdict does not bury the
  // numbers you are watching while you move the probe.
  static uint8_t n = 0;
  if (++n % 10 == 1) verdict(mv);
}
