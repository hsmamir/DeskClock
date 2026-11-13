#pragma once

// ---- TFT (ST7735) — also mirrored in platformio.ini build_flags for TFT_eSPI ----
// SPI bus (VSPI): MOSI=23, SCLK=18 (ESP32 default, no need to redefine)
#define TFT_CS_PIN 5
#define TFT_DC_PIN 2
#define TFT_RST_PIN 4
#define TFT_BL_PIN 15 // backlight, PWM-capable for dimming

// ---- Touch pad (TTP223 module) ----
// OUT -> GPIO, VCC -> 3V3, GND -> GND. The module drives OUT HIGH while touched
// (default jumper setting: momentary, active-high), so no resistor is needed.
// This one pad is Pet AND Snooze/Off (see touch.h).
#define TOUCH_PAD_PIN 32 // TTP223 OUT

// ---- Rotary encoder (KY-040 module) ----
// "+" -> 3V3, GND -> GND. The module already has pull-ups on CLK / DT / SW.
// Rotate = change page, push = home / hold for diagnostics (see touch.h).
// All three pins are plain GPIOs (not strapping pins, not input-only).
#define ROTARY_CLK_PIN 33 // KY-040 CLK (A)
#define ROTARY_DT_PIN 27  // KY-040 DT  (B)
#define ROTARY_SW_PIN 26  // KY-040 SW  (push button, active LOW)

// ---- Servo ----
#define SERVO_PIN 13 // PWM-capable, avoid strapping pins (0,2,5,12,15)

// ---- JQ6500-16P MP3 module (UART2) ----
#define JQ6500_RX_PIN 16 // ESP32 RX2 <- JQ6500 TX
#define JQ6500_TX_PIN 17 // ESP32 TX2 -> JQ6500 RX

// ---- RGB LED (4 pins, common anode) ----
// The longest leg is the shared "+": connect it to 3V3. Every colour leg goes through its own resistor to a GPIO
// (the GPIO sinks the current: LOW = lit). Typical values at 3.3 V: red 150 ohm, green 100 ohm, blue 100 ohm
// (green / blue need a low value: their forward voltage is ~3 V). Free pins on the same side of the board as 18 / 5.
// Common cathode LED instead? Set RGB_LED_COMMON_ANODE to 0 in config.h and connect the long leg to GND.
#define RGB_LED_R_PIN 19
#define RGB_LED_G_PIN 21
#define RGB_LED_B_PIN 22
