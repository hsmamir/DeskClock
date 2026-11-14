#pragma once
#include <stdint.h>

// 4-pin RGB LED (common anode: one shared + pin, one - pin per colour), see "RGB LED" in config.h and pins.h.
// It shows what the clock is doing even when you are not looking at the screen:
//   alarm ringing ........ red, fast flashing                 (always, also at night)
//   firmware update ...... purple breathing                   (always)
//   WiFi setup portal .... blue breathing                     (always)
//   reminder on screen ... cyan double blink                  (not at night)
//   pet reaction ......... pink glow (purring = slow breathing; annoyed = orange, dizzy = violet,
//                          hot = warm orange, cold = ice blue)  (not at night)
//   otherwise ............ the robot's own colour: warm white breathing slowly (RGB_LED_IDLE_MODE; also weather colour,
//                          a steady colour or off; faint at night)
//   power-on ............. the robot "opens its eyes": warm white fades up, one blink (RGB_LED_BOOT_TEST)

// Sets the three pins to "LED off" and attaches PWM. Call at the very start of setup().
void rgb_led_init();

// Call about every 20 ms from its own task (see main.cpp).
void rgb_led_tick();

// Settings the web page changes at run time (saved in flash). config.h (RGB_LED_*) only gives the first values.
struct RgbLedSettings
{
    bool enabled;         // false = LED completely off (also no alarm light)
    uint8_t maxPct;       // overall brightness cap, 1-100 %
    uint8_t idleMode;     // 0 off, 1 steady colour, 2 weather / temperature colour, 3 breathing warm white
    uint8_t idlePct;      // idle brightness by day, 0-100 %
    uint8_t idleNightPct; // idle brightness at night, 0-100 % (0 = off)
    uint8_t sunriseMin;   // minutes of "sunrise" before the alarm, 0 = off, max 60
    bool showWifi;        // amber blink while the WiFi is gone
    bool showUpdate;      // green pulse while an update waits
};

RgbLedSettings rgb_led_settings_get();
// false + *err (a short English message, like the other settings) when a value is out of range
bool rgb_led_settings_set(const RgbLedSettings &s, const char **err);