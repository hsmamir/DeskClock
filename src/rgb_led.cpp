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
#include "settings.h"
#include "update_check.h"
#include <WiFi.h>
#include <Preferences.h>

#if RGB_LED_ENABLED

namespace
{
    struct Rgb
    {
        uint8_t r, g, b;
    };

    const uint8_t PINS[3] = {RGB_LED_R_PIN, RGB_LED_G_PIN, RGB_LED_B_PIN};
    const Rgb OFF = {0, 0, 0};

    // ---- runtime settings (the web page changes them; saved in flash). config.h only holds the defaults. ----
    RgbLedSettings s_cfg = {RGB_LED_DEFAULT_ENABLED, RGB_LED_MAX_PCT, RGB_LED_IDLE_MODE, RGB_LED_IDLE_PCT,
                            RGB_LED_IDLE_NIGHT_PCT, RGB_LED_SUNRISE_MIN, RGB_LED_SHOW_WIFI, RGB_LED_SHOW_UPDATE};
    portMUX_TYPE s_cfgMux = portMUX_INITIALIZER_UNLOCKED;
    RgbLedSettings s_c = s_cfg; // the copy the LED task works with (taken at the start of every tick)

    void cfgSnapshot()
    {
        portENTER_CRITICAL(&s_cfgMux);
        s_c = s_cfg;
        portEXIT_CRITICAL(&s_cfgMux);
    }

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
    uint8_t s_lastDuty[3] = {255, 255, 255}; // what was last written to the pins (debug log)

    void show(const Rgb &c)
    {
        const uint8_t v[3] = {c.r, c.g, c.b};
        for (uint8_t i = 0; i < 3; i++)
        {
            const uint8_t out = gammaTable[v[i]];
#if RGB_LED_COMMON_ANODE
            s_lastDuty[i] = 255 - out;
#else
            s_lastDuty[i] = out;
#endif
            pwmWrite(i, s_lastDuty[i]);
        }
    }

    // colour * k (0..1) * the overall brightness cap
    Rgb scaled(const Rgb &c, float k, float capPct = -1.0f) // capPct < 0: the brightness cap of the settings
    {
        if (capPct < 0.f)
            capPct = s_c.maxPct;
        if (k < 0.f)
            k = 0.f;
        if (k > 1.f)
            k = 1.f;
        k *= capPct / 100.0f;
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

    // ---- helpers for the extra functions ----
    float lerpf(float a, float b, float t) { return a + (b - a) * t; }
    Rgb lerpRgb(const Rgb &a, const Rgb &b, float t)
    {
        if (t < 0.f)
            t = 0.f;
        if (t > 1.f)
            t = 1.f;
        return {(uint8_t)lerpf(a.r, b.r, t), (uint8_t)lerpf(a.g, b.g, t), (uint8_t)lerpf(a.b, b.b, t)};
    }

    // Temperature -> colour: violet (very cold) ... blue ... cyan ... green ... yellow ... orange ... red (very hot)
    Rgb tempColour(float c)
    {
        static const struct
        {
            float t;
            Rgb col;
        } stops[] = {{-10, {140, 60, 255}}, {0, {50, 90, 255}}, {10, {0, 190, 255}}, {18, {0, 255, 110}}, {24, {255, 220, 0}}, {30, {255, 100, 0}}, {38, {255, 0, 0}}};
        const int n = sizeof(stops) / sizeof(stops[0]);
        if (c <= stops[0].t)
            return stops[0].col;
        for (int i = 1; i < n; i++)
            if (c <= stops[i].t)
                return lerpRgb(stops[i - 1].col, stops[i].col, (c - stops[i - 1].t) / (stops[i].t - stops[i - 1].t));
        return stops[n - 1].col;
    }

    // ---- sunrise before the alarm (checked once a second) ----
    // progress 0..1 while the last (sunriseMin) minutes before the alarm run, else < 0
    float s_sunriseP = -1.f;
    uint32_t s_sunriseCheckMs = 0;

    void sunriseUpdate(uint32_t now, bool timeSynced)
    {
        if (s_sunriseCheckMs != 0 && (uint32_t)(now - s_sunriseCheckMs) < 1000UL)
            return;
        s_sunriseCheckMs = now ? now : 1;
        s_sunriseP = -1.f;
        if (s_c.sunriseMin == 0)
            return;
        struct tm ti;
        if (!timeSynced || !getLocalTime(&ti, 0))
            return;
        const Settings cfg = settings_get();
        if (!cfg.alarmEnabled)
            return;
        const long nowS = ti.tm_hour * 3600L + ti.tm_min * 60L + ti.tm_sec;
        const long alarmS = cfg.alarmHour * 3600L + cfg.alarmMinute * 60L;
        const long diff = (alarmS - nowS + 86400L) % 86400L; // seconds until the next alarm time
        const long win = (long)s_c.sunriseMin * 60L;
        if (diff == 0 || diff > win)
            return;
        const int alarmWday = (ti.tm_wday + ((nowS + diff) >= 86400L ? 1 : 0)) % 7; // the day the alarm will ring on
        if (!(cfg.alarmDays & (1 << alarmWday)))
            return;
        s_sunriseP = 1.0f - (float)diff / (float)win;
    }

    Rgb sunriseColour(float p)
    {
        // deep red -> orange -> warm white, and it gets brighter all the way
        const Rgb a = {255, 10, 0}, b = {255, 70, 0}, c = {255, 170, 60};
        const Rgb col = p < 0.5f ? lerpRgb(a, b, p * 2.f) : lerpRgb(b, c, (p - 0.5f) * 2.f);
        return scaled(col, 0.03f + 0.97f * p * p, RGB_LED_SUNRISE_PCT);
    }

    // ---- "update available" (asked every 10 s; the status copy holds Strings, so not every tick) ----
    bool s_updateAvail = false;
    uint32_t s_updateCheckMs = 0;

    bool updateAvailable(uint32_t now)
    {
#if UPDATE_ENABLED
        if (!s_c.showUpdate)
            return false;
        if (s_updateCheckMs == 0 || (uint32_t)(now - s_updateCheckMs) >= 10000UL)
        {
            s_updateCheckMs = now ? now : 1;
            s_updateAvail = update_status().state == UpdateState::AVAILABLE;
        }
        return s_updateAvail;
#else
        (void)now;
        return false;
#endif
    }

    // ---- WiFi lost for a while ----
    uint32_t s_wifiDownSinceMs = 0;

    bool wifiLost(uint32_t now)
    {
        if (!s_c.showWifi)
            return false;
        if (WiFi.status() == WL_CONNECTED)
        {
            s_wifiDownSinceMs = 0;
            return false;
        }
        if (s_wifiDownSinceMs == 0)
            s_wifiDownSinceMs = now ? now : 1;
        return (uint32_t)(now - s_wifiDownSinceMs) >= (uint32_t)RGB_LED_WIFI_LOST_S * 1000UL;
    }

    // The idle colour: from the weather (temperature gradient; rain / snow / storm make it breathe), or a steady colour.
    Rgb idleColour(uint32_t now, const AppState &st, bool night)
    {
        const float pct = night ? s_c.idleNightPct : s_c.idlePct;
        if (pct <= 0.f || s_c.idleMode == 0)
            return OFF;
        if (s_c.idleMode == 2)
        {
            const bool fresh = st.weatherValid && (uint32_t)(now - st.weatherUpdatedMs) < WEATHER_STALE_MS;
            if (fresh)
            {
                const int code = st.weatherCode;
                const bool thunder = code >= 95;
                const bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
                const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
                float k = 1.0f;
                if (thunder)
                    k = 0.45f + 0.55f * breathe(now, 1400);
                else if (rain || snow)
                    k = 0.55f + 0.45f * breathe(now, snow ? 5000 : 2800);
                return scaled(tempColour(st.weatherTempC), k, pct);
            }
        }
        // mode 1, or mode 2 without a fresh weather reading: the steady colour
        return scaled({RGB_LED_IDLE_R, RGB_LED_IDLE_G, RGB_LED_IDLE_B}, 1.0f, pct);
    }

    uint32_t s_lastPetSeq = 0;
    bool s_petSeqKnown = false;
    uint32_t s_petStartMs = 0;
    PetKind s_petKind = PetKind::HAPPY;
    uint32_t s_petDurMs = 0;
}

static void loadSettings()
{
    Preferences p;
    if (!p.begin("rgbled", true))
        return;
    RgbLedSettings c = s_cfg;
    c.enabled = p.getBool("en", c.enabled);
    c.maxPct = p.getUChar("max", c.maxPct);
    c.idleMode = p.getUChar("idm", c.idleMode);
    c.idlePct = p.getUChar("idp", c.idlePct);
    c.idleNightPct = p.getUChar("ngt", c.idleNightPct);
    c.sunriseMin = p.getUChar("sun", c.sunriseMin);
    c.showWifi = p.getBool("wifi", c.showWifi);
    c.showUpdate = p.getBool("upd", c.showUpdate);
    p.end();
    if (c.maxPct < 1 || c.maxPct > 100 || c.idleMode > 2 || c.idlePct > 100 || c.idleNightPct > 100 || c.sunriseMin > 60)
        return; // damaged values: keep the defaults
    portENTER_CRITICAL(&s_cfgMux);
    s_cfg = c;
    portEXIT_CRITICAL(&s_cfgMux);
}

RgbLedSettings rgb_led_settings_get()
{
    RgbLedSettings c;
    portENTER_CRITICAL(&s_cfgMux);
    c = s_cfg;
    portEXIT_CRITICAL(&s_cfgMux);
    return c;
}

bool rgb_led_settings_set(const RgbLedSettings &c, const char **err)
{
    if (c.maxPct < 1 || c.maxPct > 100)
    {
        if (err)
            *err = "led brightness must be 1-100";
        return false;
    }
    if (c.idleMode > 2)
    {
        if (err)
            *err = "led idle mode must be 0-2";
        return false;
    }
    if (c.idlePct > 100 || c.idleNightPct > 100)
    {
        if (err)
            *err = "led idle brightness must be 0-100";
        return false;
    }
    if (c.sunriseMin > 60)
    {
        if (err)
            *err = "sunrise minutes must be 0-60";
        return false;
    }
    portENTER_CRITICAL(&s_cfgMux);
    s_cfg = c;
    portEXIT_CRITICAL(&s_cfgMux);

    Preferences p;
    if (p.begin("rgbled", false))
    {
        p.putBool("en", c.enabled);
        p.putUChar("max", c.maxPct);
        p.putUChar("idm", c.idleMode);
        p.putUChar("idp", c.idlePct);
        p.putUChar("ngt", c.idleNightPct);
        p.putUChar("sun", c.sunriseMin);
        p.putBool("wifi", c.showWifi);
        p.putBool("upd", c.showUpdate);
        p.end();
    }
    return true;
}

void rgb_led_init()
{
    loadSettings();
    cfgSnapshot();
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
        // Explicit channels (RGB_LED_PWM_CHANNEL0 + i). The core gives every pair of channels one shared timer
        // (timer = channel / 2), and the servo (ESP32Servo, 50 Hz) takes the first free channel when it moves. With
        // an automatic channel the LED could end up on the servo's timer and be switched to 50 Hz / another
        // resolution = dark while the head moves (alarm, pet). Channels 12-14 are far from the servo's.
        ledcAttachChannel(PINS[i], RGB_LED_PWM_FREQ, 8, RGB_LED_PWM_CHANNEL0 + i);
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
    cfgSnapshot();
    if (!s_c.enabled)
    {
        show(OFF);
        return;
    }

    // snapshot of the shared state
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    const bool alarm = g_state.alarmActive;
    const int ota = g_state.otaPercent;
    const bool portal = g_state.wifiPortalActive;
    const Mood mood = g_state.mood;
    const bool synced = g_state.timeSynced;
    AppState weatherSnap; // only the weather part is used (idle colour)
    weatherSnap.weatherValid = g_state.weatherValid;
    weatherSnap.weatherTempC = g_state.weatherTempC;
    weatherSnap.weatherCode = g_state.weatherCode;
    weatherSnap.weatherUpdatedMs = g_state.weatherUpdatedMs;
    xSemaphoreGive(g_stateMutex);

#if RGB_LED_DEBUG
    {
        static bool first = true, lastAlarm = false;
        if (first)
        {
            first = false;
            Serial.printf("RGB LED: task running (Arduino core %d, LEDC channels %d-%d)\n",
#if defined(ESP_ARDUINO_VERSION_MAJOR)
                          (int)ESP_ARDUINO_VERSION_MAJOR,
#else
                          0,
#endif
                          (int)RGB_LED_PWM_CHANNEL0, (int)RGB_LED_PWM_CHANNEL0 + 2);
        }
        if (alarm != lastAlarm)
        {
            lastAlarm = alarm;
            Serial.printf("RGB LED: alarm %s at t=%lu ms\n", alarm ? "ON" : "off", (unsigned long)now);
        }
    }
#endif

#if RGB_LED_DEBUG
    // Wiring / PWM test, 20 s after boot (when WiFi, display and servo are all up): phase after phase, 1 s each.
    // Compare with the boot test: if red works at boot but not here, something started later takes the pin over.
    {
        static uint8_t phase = 0;
        static uint32_t phaseStartMs = 0;
        if (phase < 6 && now >= 20000)
        {
            if (phaseStartMs == 0)
                phaseStartMs = now;
            if ((uint32_t)(now - phaseStartMs) >= 1000)
            {
                phase++;
                phaseStartMs = now;
            }
            if (phase < 6)
            {
                static uint8_t lastPrinted = 255;
                if (phase != lastPrinted)
                {
                    lastPrinted = phase;
                    static const char *const names[6] = {"red, 40% like the boot test", "red, full brightness (duty 0)",
                                                         "red, blinking 2x per second", "green, full", "blue, full", "white, full"};
                    Serial.printf("RGB LED test %u/6: %s\n", (unsigned)phase + 1, names[phase]);
                }
                switch (phase)
                {
                case 0:
                    show(scaled({255, 0, 0}, 1.0f));
                    break;
                case 1:
                    show({255, 0, 0});
                    break;
                case 2:
                    show(((now / 250) & 1) ? OFF : Rgb{255, 0, 0});
                    break;
                case 3:
                    show({0, 255, 0});
                    break;
                case 4:
                    show({0, 0, 255});
                    break;
                default:
                    show({255, 255, 255});
                    break;
                }
                return;
            }
            show(OFF);
            Serial.println("RGB LED test done");
        }
    }
#endif

    // 1. alarm: always, also at night
    if (alarm)
    {
        show(((now / 250) & 1) ? OFF : Rgb{255, 0, 0}); // full brightness: the cap RGB_LED_MAX_PCT does not apply to the alarm
#if RGB_LED_DEBUG
        {
            static uint32_t lastDbgMs = 0;
            if ((uint32_t)(now - lastDbgMs) >= 500)
            {
                lastDbgMs = now;
                Serial.printf("RGB LED: alarm tick t=%lu ms, duty R/G/B = %u/%u/%u\n", (unsigned long)now,
                              (unsigned)s_lastDuty[0], (unsigned)s_lastDuty[1], (unsigned)s_lastDuty[2]);
            }
        }
#endif
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

    // 4. sunrise: the last minutes before the alarm, the LED slowly brightens (also at night: that is when it is for)
    sunriseUpdate(now, synced);
    if (s_sunriseP >= 0.f)
    {
        show(sunriseColour(s_sunriseP));
        return;
    }

    const bool night = nightNow(now, synced);
    // everything below is quiet at night (except an idle glow, if RGB_LED_IDLE_NIGHT_PCT > 0)
    if (night)
    {
        show(idleColour(now, weatherSnap, true));
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
        const float fade = k < 0.4f ? 1.0f : 1.0f - (k - 0.4f) / 0.6f;   // full, then fades out
        show(scaled(petColour(s_petKind), fade));
        return;
    }

    // 6. WiFi lost / update available: a short blink on top of the idle colour
    const Rgb idle = idleColour(now, weatherSnap, false);
    if (wifiLost(now))
    {
        const uint32_t t = now % 3000; // amber double blink every 3 s
        if (t < 150 || (t >= 300 && t < 450))
        {
            show(scaled({255, 140, 0}, 1.0f));
            return;
        }
    }
    else if (updateAvailable(now))
    {
        const uint32_t t = now % 8000; // one green pulse every 8 s
        if (t < 400)
        {
            show(scaled({0, 255, 60}, 1.0f));
            return;
        }
    }

    // 7. idle
    show(idle);
}

#else // RGB_LED_ENABLED == 0

void rgb_led_init() {}
void rgb_led_tick() {}
RgbLedSettings rgb_led_settings_get() { return RgbLedSettings{}; }
bool rgb_led_settings_set(const RgbLedSettings &, const char **) { return true; }

#endif