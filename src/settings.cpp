#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include "settings.h"
#include "config.h"

static const char *NVS_NAMESPACE = "deskclock";

static Settings s_cur;
static SemaphoreHandle_t s_mutex = nullptr;

static void applyTimezone(const char *posix)
{
    setenv("TZ", posix, 1);
    tzset();
}

static void loadDefaults(Settings &s)
{
    memset(&s, 0, sizeof(s));
    s.alarmEnabled = ALARM_ENABLED;
    s.alarmHour = ALARM_HOUR;
    s.alarmMinute = ALARM_MINUTE;
    s.volume = AUDIO_VOLUME;
    s.latitude = (float)atof(WEATHER_LATITUDE);
    s.longitude = (float)atof(WEATHER_LONGITUDE);
    strlcpy(s.city, WEATHER_CITY_NAME, sizeof(s.city));
    strlcpy(s.tzName, DEFAULT_TZ_NAME, sizeof(s.tzName));
    strlcpy(s.tzPosix, DEFAULT_TZ_POSIX, sizeof(s.tzPosix));
}

static bool validate(const Settings &s, const char **err)
{
    if (s.alarmHour < 0 || s.alarmHour > 23)
    {
        *err = "alarm hour must be 0-23";
        return false;
    }
    if (s.alarmMinute < 0 || s.alarmMinute > 59)
    {
        *err = "alarm minute must be 0-59";
        return false;
    }
    if (s.volume < 0 || s.volume > 30)
    {
        *err = "volume must be 0-30";
        return false;
    }
    // written so that NaN fails too
    if (!(s.latitude >= -90.0f && s.latitude <= 90.0f) ||
        !(s.longitude >= -180.0f && s.longitude <= 180.0f))
    {
        *err = "invalid coordinates";
        return false;
    }

    size_t n = strnlen(s.tzPosix, sizeof(s.tzPosix));
    if (n == 0 || n >= sizeof(s.tzPosix))
    {
        *err = "timezone rule missing or too long";
        return false;
    }
    for (size_t i = 0; i < n; i++)
    {
        char c = s.tzPosix[i];
        if (!isalnum((unsigned char)c) && !strchr("<>+-,.:/", c))
        {
            *err = "timezone rule has invalid characters";
            return false;
        }
    }
    if (strnlen(s.tzName, sizeof(s.tzName)) >= sizeof(s.tzName) ||
        strnlen(s.city, sizeof(s.city)) >= sizeof(s.city))
    {
        *err = "text too long";
        return false;
    }
    return true;
}

void settings_init()
{
    s_mutex = xSemaphoreCreateMutex();

    Settings s;
    loadDefaults(s);

    Preferences p;
    if (p.begin(NVS_NAMESPACE, false))
    {
        // Each key is optional: a missing one keeps the default above.
        s.alarmEnabled = p.getBool("aEn", s.alarmEnabled);
        s.alarmHour = p.getUChar("aH", s.alarmHour);
        s.alarmMinute = p.getUChar("aM", s.alarmMinute);
        s.volume = p.getUChar("vol", s.volume);
        s.latitude = p.getFloat("lat", s.latitude);
        s.longitude = p.getFloat("lon", s.longitude);
        if (p.isKey("city"))
            p.getString("city", s.city, sizeof(s.city));
        if (p.isKey("tzN"))
            p.getString("tzN", s.tzName, sizeof(s.tzName));
        if (p.isKey("tzP"))
            p.getString("tzP", s.tzPosix, sizeof(s.tzPosix));
        p.end();
    }

    const char *err;
    if (!validate(s, &err))
        loadDefaults(s); // corrupt/odd values in flash -> factory defaults

    applyTimezone(s.tzPosix);
    s_cur = s;
}

Settings settings_get()
{
    Settings copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_cur;
    xSemaphoreGive(s_mutex);
    return copy;
}

bool settings_update(const Settings &in, const char **err)
{
    if (!validate(in, err))
        return false;

    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false))
    {
        *err = "could not open storage";
        return false;
    }
    bool ok = true;
    ok &= p.putBool("aEn", in.alarmEnabled) > 0;
    ok &= p.putUChar("aH", (uint8_t)in.alarmHour) > 0;
    ok &= p.putUChar("aM", (uint8_t)in.alarmMinute) > 0;
    ok &= p.putUChar("vol", (uint8_t)in.volume) > 0;
    ok &= p.putFloat("lat", in.latitude) > 0;
    ok &= p.putFloat("lon", in.longitude) > 0;
    // city / tzName may legitimately be empty (putString then returns 0),
    // so only the mandatory rule string counts toward "ok"
    p.putString("city", in.city);
    p.putString("tzN", in.tzName);
    ok &= p.putString("tzP", in.tzPosix) > 0;
    p.end();

    if (!ok)
    {
        *err = "saving to flash failed";
        return false;
    }

    applyTimezone(in.tzPosix);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cur = in;
    xSemaphoreGive(s_mutex);
    return true;
}