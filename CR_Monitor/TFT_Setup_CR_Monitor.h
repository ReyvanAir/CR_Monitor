/* TFT_eSPI setup - Classroom Environment Monitor
 * Panel : 3.5" 320x480 ILI9481, 8-bit 8080 parallel, 4-wire resistive touch
 *         (Arduino Uno shield form factor, hand-wired to the ESP32-S3)
 *         Pin map below is CONFIRMED against the shield's own silkscreen,
 *         not assumed: J1/J2 carry LCD_D0..D7, J3 carries LCD_RD/WR/RS/CS/RST
 *         plus F_CS (W25Q32 flash), J4 is RESET/3V3/5V/GND/GND.
 * Board : ESP32-S3 DevKitC-1 N16R8
 *
 * Copy the body of this file into TFT_eSPI/User_Setup.h, or point
 * User_Setup_Select.h at it. Kept in the repo so the pin map is versioned.
 *
 * Pin choice is constrained, in this order:
 *   - GPIO 26-32 are SPI flash, 33-37 are OPI PSRAM (the "R8"). All dead.
 *   - GPIO 43/44 carry the Serial CSV output, 19/20 are USB, 0/45/46 strap.
 *   - GPIO 5/7/8/9 are already mic / buzzer / I2C. Not moved.
 *   - CS and DC double as the touch panel's YP/XM and get analogRead(),
 *     so both MUST be ADC1 (GPIO1-10). ADC2 dies when Wi-Fi is on, same
 *     reason MIC_PIN is pinned to GPIO5.
 *   - D0-D7 sit contiguous in GPIO10-17 so the port write is one register.
 */

// ---- PANEL CONTROLLER --------------------------------------------
// Exactly ONE of these may be active. The pin map below was confirmed
// against the shield silkscreen, but the controller was NOT - and a
// wrong driver gives a lit backlight with a permanently white screen,
// because the init sequence means nothing to the chip.
//
// If the screen is white, work down this list, reflashing each time.
// CONFIRMED 2026-09-13: this board is ILI9481. It was assumed to be
// HX8357D, which is why the screen stayed white - the pin map was
// right all along, the init sequence was not.
//#define HX8357D_DRIVER
//#define ILI9486_DRIVER      // try this one FIRST if the screen is white
//#define ILI9488_DRIVER
#define ILI9481_DRIVER
//#define R61581_DRIVER
// ------------------------------------------------------------------
#define TFT_PARALLEL_8_BIT

#define TFT_WIDTH   320
#define TFT_HEIGHT  480

// Data bus - contiguous, all in the low GPIO output register
#define TFT_D0  10
#define TFT_D1  11
#define TFT_D2  12
#define TFT_D3  13
#define TFT_D4  14
#define TFT_D5  15
#define TFT_D6  16
#define TFT_D7  17

// Control lines
#define TFT_CS   1    // ADC1 - shared with touch YP
#define TFT_DC   2    // ADC1 - shared with touch XM  (shield labels this RS)
#define TFT_WR  38
#define TFT_RD  39
#define TFT_RST 42

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_GFXFF
#define SMOOTH_FONT

// ---- Optional, not required for the display ----------------------
// The shield also breaks out a microSD slot (J1) and a W25Q32 flash
// footprint (F_CS on J3). Wire the SD if you want the node to buffer
// readings to disk while MQTT is down; skip it otherwise.
//   SD_SCK -> 18   SD_DO (MISO) -> 21
//   SD_DI  -> 47   SD_SS        -> 41
//   F_CS   -> unused (footprint is bare on this board)
// SD_SS is 41, not 48: GPIO48 drives the DevKitC-1 onboard RGB LED.

// Leaves free after LCD only : GPIO 3, 4, 6, 18, 21, 40, 41, 47, 48
// Leaves free after LCD + SD : GPIO 3, 4, 6, 40, 48
