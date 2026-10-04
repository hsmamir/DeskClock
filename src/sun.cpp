#include <math.h>
#include <time.h>
#include "sun.h"
#include "config.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "settings.h"
#endif

static const double kPi = 3.14159265358979323846;
static const double kDeg = kPi / 180.0;

SunTimes sun_compute(int year, int yday, double latDeg, double lonDeg, int utcOffsetMin)
{
    SunTimes r = {false, false, false, 0, 0};
    if (!(latDeg >= -90.0 && latDeg <= 90.0 && lonDeg >= -180.0 && lonDeg <= 180.0))
        return r;

    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    // fractional year (rad), evaluated at local solar noon
    double g = 2.0 * kPi / (leap ? 366.0 : 365.0) * (double)yday;

    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g) - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);

    double lat = latDeg * kDeg;
    // 90.833 deg = sun centre 50' below the horizon (refraction + solar radius)
    double c = cos(90.833 * kDeg) / (cos(lat) * cos(decl)) - tan(lat) * tan(decl);
    if (c > 1.0)
    {
        r.valid = true;
        r.polarNight = true;
        return r;
    }
    if (c < -1.0)
    {
        r.valid = true;
        r.midnightSun = true;
        return r;
    }
    double haDeg = acos(c) / kDeg;

    double riseMin = 720.0 - 4.0 * (lonDeg + haDeg) - eqtime + utcOffsetMin;
    double setMin = 720.0 - 4.0 * (lonDeg - haDeg) - eqtime + utcOffsetMin;
    double rise = fmod(fmod(riseMin, 1440.0) + 1440.0, 1440.0) / 60.0;
    double set = fmod(fmod(setMin, 1440.0) + 1440.0, 1440.0) / 60.0;
    if (set <= rise)
        set += 24.0;

    r.valid = true;
    r.riseH = (float)rise;
    r.setH = (float)set;
    return r;
}

float moon_phase(time_t utc)
{
    const double kSynodic = 29.530588853;   // days
    const double kRefNewMoon = 947182440.0; // 2000-01-06 18:14 UTC, a new moon
    double days = ((double)utc - kRefNewMoon) / 86400.0;
    double p = fmod(days / kSynodic, 1.0);
    if (p < 0)
        p += 1.0;
    return (float)p;
}

#ifdef ARDUINO

// UTC offset (minutes) of the local timezone at this moment, DST included.
static int localUtcOffsetMin(time_t now)
{
    struct tm l, u;
    localtime_r(&now, &l);
    gmtime_r(&now, &u);
    int diff = (l.tm_hour - u.tm_hour) * 60 + (l.tm_min - u.tm_min);
    // different calendar day (local vs UTC)? compare (year, yday)
    int ld = l.tm_year * 366 + l.tm_yday, ud = u.tm_year * 366 + u.tm_yday;
    if (ld > ud)
        diff += 1440;
    else if (ld < ud)
        diff -= 1440;
    return diff;
}

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static SunTimes s_cache;
static bool s_cacheOk = false;
static int s_cYear, s_cYday, s_cOff;
static float s_cLat, s_cLon;

static uint32_t s_locMs = 0;
static bool s_locOk = false;
static float s_lat, s_lon;

// Coordinates are re-read from the settings at most every few seconds (settings_get
// takes a mutex and copies the whole struct; the display asks every frame).
static void currentLocation(float &lat, float &lon)
{
    uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    bool fresh = s_locOk && (now - s_locMs) < 5000;
    lat = s_lat;
    lon = s_lon;
    portEXIT_CRITICAL(&s_mux);
    if (fresh)
        return;
    Settings st = settings_get();
    portENTER_CRITICAL(&s_mux);
    s_lat = lat = st.latitude;
    s_lon = lon = st.longitude;
    s_locMs = now;
    s_locOk = true;
    portEXIT_CRITICAL(&s_mux);
}

SunTimes sun_today(const struct tm &local)
{
    float lat, lon;
    currentLocation(lat, lon);
    int year = local.tm_year + 1900, yday = local.tm_yday;
    int off = localUtcOffsetMin(time(nullptr));

    SunTimes r;
    bool hit;
    portENTER_CRITICAL(&s_mux);
    hit = s_cacheOk && s_cYear == year && s_cYday == yday && s_cOff == off && s_cLat == lat && s_cLon == lon;
    r = s_cache;
    portEXIT_CRITICAL(&s_mux);
    if (hit)
        return r;

    r = sun_compute(year, yday, lat, lon, off);
    portENTER_CRITICAL(&s_mux);
    s_cache = r;
    s_cacheOk = true;
    s_cYear = year;
    s_cYday = yday;
    s_cOff = off;
    s_cLat = lat;
    s_cLon = lon;
    portEXIT_CRITICAL(&s_mux);

    if (r.valid && !r.polarNight && !r.midnightSun)
        Serial.printf("Sun (%.3f, %.3f): rise %02d:%02d  set %02d:%02d\n", lat, lon,
                      (int)r.riseH, (int)((r.riseH - (int)r.riseH) * 60), ((int)r.setH) % 24,
                      (int)((r.setH - (int)r.setH) * 60));
    else
        Serial.printf("Sun (%.3f, %.3f): %s\n", lat, lon, r.valid ? (r.polarNight ? "polar night" : "midnight sun") : "n/a");
    return r;
}

float moon_phase_view()
{
    float lat, lon;
    currentLocation(lat, lon);
    float p = moon_phase(time(nullptr));
    return lat < 0 ? fmodf(1.0f - p, 1.0f) : p;
}

static float hourOf(const struct tm &t) { return t.tm_hour + t.tm_min / 60.0f + t.tm_sec / 3600.0f; }

// h in [from, to) on a 24 h circle (from > to means the window crosses midnight)
static bool inWindow(float h, float from, float to)
{
    from = fmodf(from + 48.0f, 24.0f);
    to = fmodf(to + 48.0f, 24.0f);
    if (from <= to)
        return h >= from && h < to;
    return h >= from || h < to;
}

bool sun_is_day(const struct tm &local)
{
    if (!SUN_ENABLED)
        return local.tm_hour >= SCENE_DAY_START_HOUR && local.tm_hour < SCENE_DAY_END_HOUR;
    SunTimes s = sun_today(local);
    if (!s.valid)
        return local.tm_hour >= SCENE_DAY_START_HOUR && local.tm_hour < SCENE_DAY_END_HOUR;
    if (s.polarNight)
        return false;
    if (s.midnightSun)
        return true;
    return inWindow(hourOf(local), s.riseH, s.setH);
}

bool sun_is_night(const struct tm &local)
{
    if (!NIGHT_FOLLOWS_SUN || !SUN_ENABLED)
        return isNightHour(local.tm_hour);
    SunTimes s = sun_today(local);
    if (!s.valid)
        return isNightHour(local.tm_hour);
    if (s.polarNight)
        return true;
    if (s.midnightSun)
        return false;
    return inWindow(hourOf(local), s.setH + SUN_NIGHT_START_AFTER_SUNSET_H, s.riseH + SUN_NIGHT_END_AFTER_SUNRISE_H);
}

#endif // ARDUINO