/* BH1750 isolation test - Classroom Environment Monitor
 * Standalone. No Wi-Fi, no MQTT, no TFT, no buzzer, no mic.
 *
 * Why this exists: the temp and light sensors keep dying, including
 * brand new replacements. This strips the node down to ONE sensor so a
 * failure cannot be blamed on anything else sharing the board.
 *
 * WIRE ONLY THIS - unplug the SHT3x, the MAX4466 and the buzzer:
 *   BH1750 VCC  -> ESP32 3V3   <-- 3V3, NOT 5V. See the warning below.
 *   BH1750 GND  -> ESP32 GND
 *   BH1750 SCL  -> GPIO9
 *   BH1750 SDA  -> GPIO8
 *   BH1750 ADDR -> GND         (or leave it, most boards pull it down)
 *
 * VOLTAGE WARNING, and this is the prime suspect for your dead parts:
 * the BH1750 runs on 2.4-3.6 V and its ABSOLUTE MAXIMUM is 4.5 V. Five
 * volts destroys it, sometimes slowly enough to work for a while first.
 * The SHT3x tolerates up to 5.5 V, which is exactly why it would outlive
 * the BH1750 on an over-volted rail before going itself. Put a meter on
 * this module's own VCC pin before trusting the wire colour.
 *
 * Never insert or remove the module while USB is plugged in.
 *
 * Leave it running at least 15 minutes. What you see tells you which
 * fault you have:
 *   readings forever            -> this sensor and this rail are fine
 *   READ FAILED but still ACKs  -> bus or timing, the part is alive
 *   DEVICE LOST then returns    -> power or a contact problem, NOT a
 *                                  dead part. Chase the 3V3 and GND wires.
 *   DEVICE LOST, never returns  -> the part actually died. Write down the
 *                                  T+ time and what you touched right then.
 *
 * IF THE SERIAL MONITOR STAYS BLANK:
 *   Tools > USB CDC On Boot  -> Enabled
 *   Serial monitor baud      -> 115200
 *   The DevKitC-1 has TWO usb sockets. With "USB CDC On Boot: Enabled"
 *   use the socket marked USB. With it Disabled, Serial leaves on GPIO
 *   43/44 and you must use the socket marked UART. The wrong socket
 *   prints nothing at all, which looks exactly like a dead sketch.
 *   Open the monitor first, then tap RST once.
 */

#include <Wire.h>
#include <BH1750.h>

#define I2C_SDA 8
#define I2C_SCL 9

const unsigned long READ_INTERVAL_MS = 2000;
const unsigned long STATS_EVERY      = 20;    // status block every N reads (~40 s)

// One object per possible address. The library fixes the address at
// construction, so declaring both and pointing at the live one keeps this
// working on any version of the library.
BH1750  meterLow(0x23);
BH1750  meterHigh(0x5C);
BH1750* meter = nullptr;

uint8_t  activeAddr  = 0;        // 0 = nothing detected yet
bool     ready       = false;
bool     lostBanner  = false;    // so a dead sensor does not spam the log

uint32_t okReads     = 0;
uint32_t failReads   = 0;
uint32_t consecFails = 0;
uint32_t lostEvents  = 0;

unsigned long lastRead   = 0;
uint32_t      quietTicks = 0;    // retry ticks with nothing on the bus

// Uptime as T+mmm:ss. The exact moment of death is the most useful
// number this sketch produces - it is what you correlate against.
void stamp() {
  unsigned long s = millis() / 1000;
  Serial.printf("T+%03lu:%02lu  ", s / 60, s % 60);
}

// Read the bus lines as plain inputs, WITHOUT the internal pull-ups.
// A powered, correctly wired breakout holds both lines high through its
// own pull-ups. This is the multimeter test, done in software.
void lineCheck() {
  pinMode(I2C_SDA, INPUT);      // no INPUT_PULLUP - that would mask the fault
  pinMode(I2C_SCL, INPUT);
  delay(10);
  int sda = digitalRead(I2C_SDA);
  int scl = digitalRead(I2C_SCL);

  Serial.printf("SDA (GPIO%d): %s\n", I2C_SDA, sda ? "HIGH" : "LOW");
  Serial.printf("SCL (GPIO%d): %s\n", I2C_SCL, scl ? "HIGH" : "LOW");

  if (sda && scl) {
    Serial.println(F("  -> both idle high: the module has power and both wires land."));
  } else {
    Serial.println(F("  -> LOW means no pull-up is holding that line up."));
    Serial.println(F("     Module unpowered, wire not landing, or line shorted to GND."));
    Serial.println(F("     Fix this first - nothing below will work."));
  }
}

// Does anything ACK at this address right now?
bool acks(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// 0x23 with ADDR low, 0x5C with ADDR high. Accept either so a floating
// ADDR pin shows up as a finding instead of a dead sensor.
uint8_t findSensor() {
  if (acks(0x23)) return 0x23;
  if (acks(0x5C)) return 0x5C;
  return 0;
}

bool startSensor() {
  activeAddr = findSensor();
  if (activeAddr == 0) { meter = nullptr; return false; }

  meter = (activeAddr == 0x23) ? &meterLow : &meterHigh;
  return meter->begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
}

// Re-printed on a timer so a serial monitor attached late still gets the
// whole picture instead of a bare column of numbers with no context.
void printStatus() {
  Serial.println();
  stamp();
  Serial.println(F("---- status ----"));
  Serial.printf("           sensor : %s\n", ready ? "READY" : "NOT RESPONDING");
  if (activeAddr) Serial.printf("           address: 0x%02X\n", activeAddr);
  Serial.printf("           reads  : ok=%lu  failed=%lu  lost-events=%lu\n",
                okReads, failReads, lostEvents);
  Serial.println(F("           ----------------"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  // The S3's native USB is a CDC port. It re-enumerates on every reset and
  // anything printed before the host re-attaches is simply lost - which is
  // why a plain delay() drops the whole header. Wait for the port to come
  // up, then give the monitor a moment to actually open its pipe.
  while (!Serial && millis() < 4000) delay(10);
  delay(400);
  Serial.println(F("\n\n=== BH1750 isolation test ==="));
  Serial.println(F("Only the BH1750 should be wired. No Wi-Fi in this sketch."));
  Serial.println();

  lineCheck();

  Serial.println(F("\n--- bringing up the sensor @ 100 kHz ---"));
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(50);

  ready = startSensor();

  if (ready) {
    Serial.printf("BH1750 READY at 0x%02X", activeAddr);
    if (activeAddr == 0x5C) Serial.print(F("   <-- ADDR pin is HIGH, tie it to GND"));
    Serial.println();
  } else if (activeAddr != 0) {
    Serial.printf("BH1750 ACKs at 0x%02X but begin() failed - part is damaged\n", activeAddr);
  } else {
    Serial.println(F("BH1750 NOT on the bus at 0x23 or 0x5C."));
    Serial.println(F("  -> check 3V3, GND, and that SDA=8 / SCL=9 are not swapped."));
    Serial.println(F("  -> if this module was ever fed 5 V, it is gone. Measure."));
  }

  Serial.println(F("\n--- logging. Leave this running. ---\n"));
  Serial.println(F("Cover the sensor with your hand: lux should drop to near 0."));
  Serial.println(F("A reading frozen at one value is a dying part, not a dark room.\n"));
}

// A failed read and a vanished device are different faults. Separating
// them is the whole point: one is a bus problem, the other is a dead or
// unpowered part.
void handleFailure() {
  failReads++;
  consecFails++;

  bool stillThere = acks(activeAddr ? activeAddr : 0x23);

  stamp();
  if (stillThere) {
    Serial.printf("READ FAILED (%lu in a row) - but the device still ACKs at 0x%02X\n",
                  consecFails, activeAddr);
    Serial.println(F("           bus or timing, not a dead sensor."));
  } else {
    if (!lostBanner) {
      lostEvents++;
      Serial.printf("DEVICE LOST - nothing ACKs at 0x%02X any more.\n", activeAddr);
      Serial.println(F("           This is the moment it died. Note what changed."));
      Serial.println(F("           Retrying every 2 s - if it comes back it is a"));
      Serial.println(F("           power/contact fault, not a failed part."));
      lostBanner = true;
    }
    ready = false;
  }
}

void tryRecover() {
  if (startSensor()) {
    ready       = true;
    lostBanner  = false;
    consecFails = 0;
    stamp();
    Serial.printf("RECOVERED at 0x%02X - the part is alive. This is a power or\n", activeAddr);
    Serial.println(F("           contact fault. Suspect the 3V3 wire, the GND wire,"));
    Serial.println(F("           or a tired breadboard hole."));
  } else {
    // Without this the sketch prints nothing at all while the sensor is
    // missing, which is indistinguishable from a crash or a dead port.
    quietTicks++;
    if (quietTicks % 10 == 1) {
      stamp();
      Serial.println(F("still nothing on the bus - retrying every 2 s"));
    }
    if (quietTicks % 30 == 0) printStatus();
  }
}

void loop() {
  if (millis() - lastRead < READ_INTERVAL_MS) return;
  lastRead = millis();

  if (!ready) { tryRecover(); return; }

  float lux = meter->readLightLevel();     // negative = library read error

  if (isnan(lux) || lux < 0) { handleFailure(); return; }

  okReads++;
  consecFails = 0;

  stamp();
  Serial.printf("%.1f lx\n", lux);

  if (okReads % STATS_EVERY == 0) printStatus();
}
