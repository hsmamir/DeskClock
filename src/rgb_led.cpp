#include <Arduino.h>
#include <math.h>
#include <time.h>
#include "rgb_led.h"
#include "pins.h"
#include "config.h"
#include "state.h"
#include "pet.h"
#include "reminders.h"
#include "sun.h"

#if RGB_LED_ENABLED

namespace
{
    struct Rgb
    {
        uint8_t r, g, b;
    };

    const uint8_t PINS[3] = {RGB_LED_R_PIN, RGB_LED_G_PIN, RGB_LED_B_PIN};
    const Rgb OFF = {0, 0, 0};

    uint8_t gammaTable[256];

    // ---- PWM (LEDC) ----
    // Core 3.x addresses a channel by pin, core 2.x by channel number (9..11: the backlight uses 8, the servo
    // has its own low channels).
    void pwmWrite(uint8_t i, uint8_t duty)
    {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(PINS[i], duty);
#else
        ledcWrite(RGB_LED_PWM_CHANNEL0 + i, duty);
#endif
    }

    // value 0..255 = how bright the colour is. A common-anode LED lights up when the pin goes LOW, so the duty
    // is inverted: 255 (pin always HIGH) = off.
    void show(const Rgb &c)
    {
        const uint8_t v[3] = {c.r, c.g, c.b};
        for (uint8_t i = 0; i < 3; i++)
        {
            const uint8_t out = gammaTable[v[i]];
#if RGB_LED_COMMON_ANODE
            pwmWrite(i, 255 - out);
#else
            pwmWrite(i, out);
#endif
        }
    }

    // colour * k (0..1) * the overall brightness cap
    Rgb scaled(const Rgb &c, float k)
    {
        if (k < 0.f)
            k = 0.f;
        if (k > 1.f)
            k = 1.f;
        k *= RGB_LED_MAX_PCT / 100.0f;
        return {(uint8_t)(c.r * k + 0.5f), (uint8_t)(c.g * k + 0.5f), (uint8_t)(c.b * k + 0.5f)};
    }

    // 0..1..0 smooth breathing
    float breathe(uint32_t now, uint32_t periodMs)
    {
        const float ph = (float)(now % periodMs) / (float)periodMs;
        return 0.5f - 0.5f * cosf(ph * 6.2831853f);
    }

    // ---- night (the same rule as the backlight and the sound) ----
    bool s_night = false;
    uint32_t s_nightCheckMs = 0;

    bool nightNow(uint32_t now, bool timeSynced)
    {
        if ((uint32_t)(now - s_nightCheckMs) < 5000UL && s_nightCheckMs != 0)
            return s_night;
        s_nightCheckMs = now ? now : 1;
        struct tm ti;
        s_night = timeSynced && getLocalTime(&ti, 0) && sun_is_night(ti);
        return s_night;
    }

    // ---- pet reaction colour ----
    Rgb petColour(PetKind k)
    {
        switch (k)
        {
        case PetKind::ANNOYED:
            return {255, 70, 0};
        case PetKind::DIZZY:
            return {150, 40, 255};
        case PetKind::HOT:
            return {255, 110, 0};
        case PetKind::COLD:
            return {60, 170, 255};
        case PetKind::RARE_SURPRISE:
            return {255, 200, 40};
        default:
            return {255, 40, 120}; // pink
        }
    }

    uint32_t s_lastPetSeq = 0;
    bool s_petSeqKnown = false;
    uint32_t s_petStartMs = 0;
    PetKind s_petKind = PetKind::HAPPY;
    uint32_t s_petDurMs = 0;
}

void rgb_led_init()
{
    for (int i = 0; i < 256; i++)
        gammaTable[i] = (uint8_t)(powf(i / 255.0f, 2.2f) * 255.0f + 0.5f);

    // First make sure the LED is OFF before anything else (a floating pin can make it glow faintly).
    for (uint8_t i = 0; i < 3; i++)
    {
        pinMode(PINS[i], OUTPUT);
        digitalWrite(PINS[i], RGB_LED_COMMON_ANODE ? HIGH : LOW);
    }
    for (uint8_t i = 0; i < 3; i++)
    {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(PINS[i], RGB_LED_PWM_FREQ, 8);
#else
        ledcSetup(RGB_LED_PWM_CHANNEL0 + i, RGB_LED_PWM_FREQ, 8);
        ledcAttachPin(PINS[i], RGB_LED_PWM_CHANNEL0 + i);
#endif
    }
    show(OFF);

#if RGB_LED_BOOT_TEST
    // Red, green, blue one after the other: if the colours come in another order the wires are swapped.
    const Rgb seq[3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    for (uint8_t i = 0; i < 3; i++)
    {
        show(scaled(seq[i], 1.0f));
        delay(250);
    }
    show(OFF);
#endif
}

void rgb_led_tick()
{
    const uint32_t now = millis();

    // snapshot of the shared state
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    const bool alarm = g_state.alarmActive;
    const int ota = g_state.otaPercent;
    const bool portal = g_state.wifiPortalActive;
    const Mood mood = g_state.mood;
    const bool synced = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);

#if RGB_LED_DEBUG
    {
        static bool first = true, lastAlarm = false;
        if (first)
        {
            first = false;
            Serial.println("RGB LED: task running");
        }
        if (alarm != lastAlarm)
        {
            lastAlarm = alarm;
            Serial.printf("RGB LED: alarm %s\n", alarm ? "ON" : "off");
        }
    }
#endif

    // 1. alarm: always, also at night
    if (alarm)
    {
        show(((now / 250) & 1) ? OFF : Rgb{255, 0, 0}); // full brightness: the cap RGB_LED_MAX_PCT does not apply to the alarm
        return;
    }
    // 2. firmware update
    if (ota >= 0)
    {
        show(scaled({150, 0, 255}, 0.15f + 0.85f * breathe(now, 1200)));
        return;
    }
    // 3. WiFi setup portal
    if (portal)
    {
        show(scaled({0, 60, 255}, 0.15f + 0.85f * breathe(now, 1800)));
        return;
    }

    // everything below is quiet at night
    if (nightNow(now, synced))
    {
        show(OFF);
        return;
    }

    // 4. reminder on screen: two short cyan blinks, pause
    ReminderView rv;
    if (reminder_get_active(rv) && rv.active)
    {
        const uint32_t t = now % 1600;
        const bool on = t < 150 || (t >= 300 && t < 450);
        show(on ? scaled({0, 220, 255}, 1.0f) : OFF);
        return;
    }

    // 5. pet reaction: starts when a NEW tap shows up (the one that was there at boot is ignored)
    const PetEvent pe = pet_get_event();
    if (!s_petSeqKnown)
    {
        s_lastPetSeq = pe.seq;
        s_petSeqKnown = true;
    }
    else if (pe.seq != s_lastPetSeq)
    {
        s_lastPetSeq = pe.seq;
        s_petStartMs = now;
        s_petKind = pe.kind;
        uint32_t d = pet_hold_ms(pe.kind);
        s_petDurMs = d < 800 ? 800 : (d > 3000 ? 3000 : d);
    }
    if (mood == Mood::PET_PURR)
    {
        show(scaled(petColour(PetKind::HAPPY), 0.15f + 0.85f * breathe(now, 1500)));
        return;
    }
    if (s_petDurMs && (uint32_t)(now - s_petStartMs) < s_petDurMs)
    {
        const float k = (float)(now - s_petStartMs) / (float)s_petDurMs; // 0..1
        const float fade = k < 0.4f ? 1.0f : 1.0f - (k - 0.4f) / 0.6f;    // full, then fades out
        show(scaled(petColour(s_petKind), fade));
        return;
    }

    // 6. idle
#if RGB_LED_IDLE_MODE == 1
    show(scaled({RGB_LED_IDLE_R, RGB_LED_IDLE_G, RGB_LED_IDLE_B}, 1.0f));
#else
    show(OFF);
#endif
}

#else // RGB_LED_ENABLED == 0

void rgb_led_init() {}
void rgb_led_tick() {}

#endif
