/* I2C bus diagnostic - Classroom Environment Monitor
 * Standalone. No TFT, no Wi-Fi, no MQTT, no sensor libraries.
 * If devices show up here but not in CR_Monitor.ino, the fault is in
 * the sketch. If they show up in neither, the fault is in the wiring.
 */

#include <Wire.h>

#define I2C_SDA 8
#define I2C_SCL 9

// Read the bus lines as plain inputs, WITHOUT the internal pull-ups.
// A powered, correctly wired breakout holds both lines high through its
// own ~10k pull-ups. This is the multimeter test, done in software.
void lineCheck() {
  pinMode(I2C_SDA, INPUT);      // no INPUT_PULLUP - that would mask the fault
  pinMode(I2C_SCL, INPUT);
  delay(10);
  int sda = digitalRead(I2C_SDA);
  int scl = digitalRead(I2C_SCL);

  Serial.printf("SDA (GPIO%d): %s\n", I2C_SDA, sda ? "HIGH" : "LOW");
  Serial.printf("SCL (GPIO%d): %s\n", I2C_SCL, scl ? "HIGH" : "LOW");

  if (sda && scl) {
    Serial.println(F("  -> both idle high: external pull-ups present."));
    Serial.println(F("     At least one module has power and both lines land."));
  } else {
    Serial.println(F("  -> LOW means no pull-up is holding that line up."));
    Serial.println(F("     Module unpowered, wire not landing, or line shorted to GND."));
    Serial.println(F("     Fix this before reading anything below."));
  }
}

void scan() {
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  device at 0x%02X", addr);
      if      (addr == 0x44 || addr == 0x45) Serial.print(F("  <- SHT3x"));
      else if (addr == 0x23 || addr == 0x5C) Serial.print(F("  <- BH1750"));
      Serial.println();
      found++;
    }
  }
  Serial.printf("  %u device(s)\n", found);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println(F("\n=== I2C diagnostic ==="));

  lineCheck();

  Serial.println(F("\n--- scan @ 100 kHz ---"));
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(50);
  scan();

  // Long wires and weak pull-ups fail at 100 kHz but work at 50 kHz.
  Serial.println(F("--- scan @ 50 kHz ---"));
  Wire.setClock(50000);
  delay(50);
  scan();
}

void loop() {
  delay(3000);
  Serial.println(F("\n--- rescan ---"));
  scan();          // leave it running and wiggle wires; watch for changes
}
