# Wiring: 3.5" ILI9481 shield -> ESP32-S3 DevKitC-1 N16R8

Shield labels are read straight off its silkscreen (headers J1-J4).
ESP32 pins are the GPIO numbers printed on the DevKitC-1 silkscreen.

Both boards have MALE pins, so you need **female-to-female** jumpers.

## Required: the display (13 signals + power)

| Shield header | Label    | (Uno pin) | -> ESP32-S3 |
|---------------|----------|-----------|-------------|
| J1            | LCD_D0   | D8        | **10**      |
| J1            | LCD_D1   | D9        | **11**      |
| J2            | LCD_D2   | D2        | **12**      |
| J2            | LCD_D3   | D3        | **13**      |
| J2            | LCD_D4   | D4        | **14**      |
| J2            | LCD_D5   | D5        | **15**      |
| J2            | LCD_D6   | D6        | **16**      |
| J2            | LCD_D7   | D7        | **17**      |
| J3            | LCD_RD   | A0        | **39**      |
| J3            | LCD_WR   | A1        | **38**      |
| J3            | LCD_RS   | A2        | **2**       |
| J3            | LCD_CS   | A3        | **1**       |
| J3            | LCD_RST  | A4        | **42**      |
| J4            | 5V       | 5V        | **5V**      |
| J4            | 3.3V     | 3.3V      | **3V3**  (REQUIRED - no onboard regulator) |
| J4            | GND      | GND       | **GND**     |
| J4            | GND      | GND       | **GND** (2nd, recommended) |

LCD_RS and LCD_CS go to GPIO 2 and 1 specifically: they double as the
touch panel's XM/YP and get analogRead(), so they must be ADC1 (GPIO1-10).
ADC2 stops working when Wi-Fi is on - the same constraint that pins the
mic to GPIO5.

## Leave unconnected

| Shield pin        | Why |
|-------------------|-----|
| J3 `F_CS`         | chip-select for the W25Q32 flash footprint, which is bare |
| J4 `RESET`        | do not let the shield pull the ESP32 reset line |
| J1 top 2 (unlabelled) | Uno AREF / GND |
| J2 bottom 2 (unlabelled) | Uno D0/D1 = RX/TX |
| J4 `VIN`, `IOREF` | not used |

## Optional: the microSD slot (J1)

Only if you want the node to buffer readings while MQTT is down.

| Label         | (Uno pin) | -> ESP32-S3 |
|---------------|-----------|-------------|
| SD_SCK        | D13       | **18**      |
| SD_DO (MISO)  | D12       | **21**      |
| SD_DI (MOSI)  | D11       | **47**      |
| SD_SS         | D10       | **41**      |

## Already in use - do not disturb

| GPIO | Function |
|------|----------|
| 5    | MAX4466 mic (ADC1) |
| 7    | buzzer |
| 8/9  | I2C SDA/SCL - SHT3x + BH1750 |
| 43/44| UART0, the Serial CSV output |

## Regulator test - RESOLVED 2026-09-13: this board has NO regulator

1. Wire **5V and GND only**. Nothing else.
2. Power the ESP32 over USB.
3. Measure the shield's `3.3V` pin against GND.

- **~3.3 V** -> onboard regulator present. Leave `3.3V` unconnected. Done.
- **0 V** -> no regulator. Also wire shield `3.3V` -> ESP32 `3V3`.

**Result on this board: no regulator.** A visual check found no AMS1117 or
any other 3.3 V part - only U1/U2, both marked `SM245TC` (octal bus
transceivers). So J4 `3.3V` is an INPUT and must be wired, and the whole
shield logic domain sits at 3.3 V: the 245 buffers run off that rail, not
off 5 V. Symptom while it was unwired: backlight lit (it runs from 5 V),
screen permanently white, ID readback 0x00/0xFF - the controller and its
buffers had no logic supply at all.

Because the buffers run at 3.3 V, the `HC245` input-threshold concern
below does not apply to this board - the ESP32's 3.3 V drive is correct
by construction.

While you have it open, read the markings on U1/U2 (the two SOIC bus
buffers). `LVC245` or `ALC245` is fine driven from 3.3V logic. A plain
`HC245` running off 5V has a ~3.5 V input threshold, and the S3's 3.3 V
drive would be marginal - stop and reconsider if that is what is fitted.

## Wiring notes

- Keep the 8 data wires and especially `LCD_WR` short and similar length.
  It is a parallel bus; long floppy jumpers on WR cause glitchy pixels.
- Connect both GNDs if you can. 13 fast-switching lines need a return path.
- Backlight pulls ~120-150 mA. Fine over USB; leave headroom when you
  move to a wall adapter.

## Smoke test

Before flashing the full sketch, confirm the bus:

```cpp
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
void setup() { tft.init(); tft.setRotation(1); tft.fillScreen(TFT_RED); }
void loop() {}
```

Red screen = bus and pin map are correct. Then flash `CR_Monitor.ino`.

White/blank = check LCD_WR, LCD_CS, LCD_RST and the 3.3V question first.
