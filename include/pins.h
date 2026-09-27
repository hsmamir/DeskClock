#pragma once

// ---- TFT (ST7735) — also mirrored in platformio.ini build_flags for TFT_eSPI ----
// SPI bus (VSPI): MOSI=23, SCLK=18 (ESP32 default, no need to redefine)
#define TFT_CS_PIN    5
#define TFT_DC_PIN    2
#define TFT_RST_PIN   4
#define TFT_BL_PIN    15   // backlight, PWM-capable for dimming

// ---- Buttons (mechanical microswitches, wired GPIO -> switch -> GND, INPUT_PULLUP) ----
#define TOUCH_PET_PIN    32   // btn 1: pet/react
#define TOUCH_MODE_PIN   33   // btn 2: switch display mode
#define TOUCH_ALARM_PIN  27   // btn 3: snooze / confirm / manual trigger

// ---- Servo ----
#define SERVO_PIN     13   // PWM-capable, avoid strapping pins (0,2,5,12,15)

// ---- Status LED (optional) ----
// Not wired in this skeleton. If you add one, avoid 0/2/5/12/15 (strapping/TFT pins).
