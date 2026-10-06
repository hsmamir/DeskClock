// display_backlight.cpp - backlight: dimmed at night, full during the day / alarm / setup.
#include "display_internal.h"
#include <driver/gpio.h>

namespace disp
{

    // =====================================================================
    //  Backlight - dimmed at night, full during the day / alarm / setup
    // =====================================================================
    // Two ways to dim (BACKLIGHT_NIGHT_PWM in config.h):
    //   1 = PWM on the LEDC peripheral, smooth fade. Works on any module, but the
    //       fast current pulses can couple into the SPI lines as random pixels.
    //   0 = NO PWM at all: the pin stays steady HIGH and only its drive strength
    //       is lowered at night. No switching edges -> no switching noise. This
    //       only dims a module whose LED pin is fed straight from the GPIO.
    // In both modes the pin drive strength is BACKLIGHT_DAY_DRIVE_CAP by day and
    // BACKLIGHT_NIGHT_DRIVE_CAP at night (weaker = slower edges, less current).
    static bool blNight = false; // true once the night drive strength is applied

    static void backlightSetDrive(bool night)
    {
        gpio_set_drive_capability((gpio_num_t)TFT_BL_PIN,
                                  (gpio_drive_cap_t)(night ? BACKLIGHT_NIGHT_DRIVE_CAP
                                                           : BACKLIGHT_DAY_DRIVE_CAP));
        blNight = night;
    }

#if BACKLIGHT_NIGHT_PWM
    static uint8_t blDuty = BACKLIGHT_DAY;

    static void backlightWrite(uint8_t duty)
    {
        blDuty = duty;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(TFT_BL_PIN, duty); // core 3.x: addressed by pin
#else
        ledcWrite(BACKLIGHT_PWM_CHANNEL, duty); // core 2.x: addressed by channel
#endif
    }
#endif

    void backlightInit()
    {
#if BACKLIGHT_NIGHT_PWM
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(TFT_BL_PIN, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
#else
        ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
        ledcAttachPin(TFT_BL_PIN, BACKLIGHT_PWM_CHANNEL);
#endif
        backlightWrite(BACKLIGHT_DAY); // clock unknown at boot -> start fully lit
#else
        pinMode(TFT_BL_PIN, OUTPUT);
        digitalWrite(TFT_BL_PIN, HIGH); // plain DC: no PWM, no edges
#endif
        backlightSetDrive(false);
    }

    // night = true dims, false is full brightness.
    // jump = true goes there at once (alarm: must be bright immediately); only
    // matters in PWM mode, where the change is otherwise a smooth fade.
    void backlightUpdate(bool night, bool jump)
    {
#if BACKLIGHT_NIGHT_PWM
        uint8_t target = night ? BACKLIGHT_NIGHT : BACKLIGHT_DAY;

        if (!night && blNight)
            backlightSetDrive(false); // leaving night: full drive before brightening

        if (blDuty != target)
        {
            if (jump)
            {
                backlightWrite(target);
            }
            else
            {
                int next = (int)blDuty + (target > blDuty ? BACKLIGHT_FADE_STEP : -BACKLIGHT_FADE_STEP);
                if ((target > blDuty && next > target) || (target < blDuty && next < target))
                    next = target;
                backlightWrite((uint8_t)next);
            }
        }

        if (night && !blNight && blDuty == target)
            backlightSetDrive(true); // fade finished: now weaken the drive
#else
        (void)jump;
        if (night != blNight)
            backlightSetDrive(night);
#endif
    }

} // namespace disp