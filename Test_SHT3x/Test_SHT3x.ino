/* SHT3x isolation test - Classroom Environment Monitor
 * Standalone. No Wi-Fi, no MQTT, no TFT, no buzzer, no mic.
 *
 * Why this exists: the temp and light sensors keep dying, including
 * brand new replacements. This strips the node down to ONE sensor so a
 * failure cannot be blamed on anything else sharing the board.
 *
 * WIRE ONLY THIS - unplug the BH1750, the MAX4466 and the buzzer:
 *   SHT3x VIN -> ESP32 3V3    <-- 3V3, NOT 5V. Read the silkscreen twice.
 *   SHT3x GND -> ESP32 GND
 *   SHT3x SCL -> GPIO9
 *   SHT3x SDA -> GPIO8
 *   SHT3x AD  -> GND          (optional, forces 0x44)
 *   SHT3x AL  -> leave unconnected
 *
 * Never insert or remove the module while USB is plugged in.
 *
 * Leave it running for at least 15 minutes and watch. What you see
 * tells you which fault you have:
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
#include <Adafruit_SHT31.h>

#define I2C_SDA 8
#define I2C_SCL 9

const unsigned long READ_INTERVAL_MS = 2000;
const unsigned long STATS_EVERY      = 20;    // status block every N reads (~40 s)

Adafruit_SHT31 sht = Adafruit_SHT31();

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

// The SHT3x answers at 0x44 with AD low, 0x45 with AD high. Accept either
// so a floating AD pin shows up as a finding instead of a dead sensor.
uint8_t findSensor() {
  if (acks(0x44)) return 0x44;
  if (acks(0x45)) return 0x45;
  return 0;
}

bool startSensor() {
  activeAddr = findSensor();
  if (activeAddr == 0) return false;

  if (!sht.begin(activeAddr)) return false;

  sht.heater(false);            // the heater self-heats the die, skewing temp
  return true;
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
  Serial.println(F("\n\n=== SHT3x isolation test ==="));
  Serial.println(F("Only the SHT3x should be wired. No Wi-Fi in this sketch."));
  Serial.println();

  lineCheck();

  Serial.println(F("\n--- bringing up the sensor @ 100 kHz ---"));
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(50);

  ready = startSensor();

  if (ready) {
    Serial.printf("SHT3x READY at 0x%02X", activeAddr);
    if (activeAddr == 0x45) Serial.print(F("   <-- AD pin is HIGH, tie it to GND"));
    Serial.println();
  } else if (activeAddr != 0) {
    Serial.printf("SHT3x ACKs at 0x%02X but begin() failed - part is damaged\n", activeAddr);
  } else {
    Serial.println(F("SHT3x NOT on the bus at 0x44 or 0x45."));
    Serial.println(F("  -> check 3V3, GND, and that SDA=8 / SCL=9 are not swapped."));
  }

  Serial.println(F("\n--- logging. Leave this running. ---\n"));
}

// A failed read and a vanished device are different faults. Separating
// them is the whole point: one is a bus problem, the other is a dead or
// unpowered part.
void handleFailure() {
  failReads++;
  consecFails++;

  bool stillThere = acks(activeAddr ? activeAddr : 0x44);

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
    ready      = true;
    lostBanner = false;
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

  float t = sht.readTemperature();
  float h = sht.readHumidity();

  if (isnan(t) || isnan(h)) { handleFailure(); return; }

  okReads++;
  consecFails = 0;

  stamp();
  Serial.printf("%.2f C   %.2f %%RH\n", t, h);

  if (okReads % STATS_EVERY == 0) printStatus();
}
