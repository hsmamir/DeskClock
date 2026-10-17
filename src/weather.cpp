#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "weather.h"
#include "weather_extras.h"
#include "config.h"
#include "settings.h"
#include "state.h"
#include "tls_lock.h"

// After a failed fetch (no internet, server error, bad JSON) the next attempt comes
// after this, not after a whole WEATHER_REFRESH_INTERVAL_MS. The weather task polls
// every 30 s, so the real retry gap is rounded up to a multiple of that.
#ifndef WEATHER_RETRY_INTERVAL_MS
#define WEATHER_RETRY_INTERVAL_MS (60UL * 1000)
#endif

static uint32_t s_lastFetchMs = 0;
static bool s_lastFetchFailed = false;
static volatile bool s_forceRefresh = false;

// Air quality has its own schedule (it is a second request, to another host).
static uint32_t s_aqLastMs = 0;
static bool s_aqFailed = false;
static bool s_aqNoPollen = false; // the server refused the pollen variables: ask without them from now on
static volatile bool s_aqForce = false;

// Rain warning, UV, 7-day forecast and air quality (weather_extras.h). Guarded by g_stateMutex.
static WeatherExtra s_extra = {};

const char *weather_code_to_label(int code)
{
    // WMO weather codes — see https://open-meteo.com/en/docs
    if (code == 0)
        return "Clear";
    if (code == 1 || code == 2)
        return "P.Cloudy";
    if (code == 3)
        return "Cloudy";
    if (code == 45 || code == 48)
        return "Fog";
    if (code >= 51 && code <= 57)
        return "Drizzle";
    if (code >= 61 && code <= 67)
        return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) // 85/86 = snow showers
        return "Snow";
    if (code >= 80 && code <= 82)
        return "Showers";
    if (code >= 95)
        return "Storm";
    return "N/A";
}

bool weather_extra_get(WeatherExtra &out)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    out = s_extra;
    xSemaphoreGive(g_stateMutex);
    return out.forecastValid || out.aqValid;
}

// It is already raining / snowing / storming right now (the eyes show that by themselves; no "soon" warning then).
static bool isWetCode(int c)
{
    return (c >= 51 && c <= 67) || (c >= 71 && c <= 77) || (c >= 80 && c <= 86) || c >= 95;
}

// "2026-10-17" -> weekday, 0 = Sunday (Sakamoto's method). 0 if the text is not a date.
static uint8_t dowFromDate(const char *s)
{
    if (!s || strlen(s) < 10)
        return 0;
    int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
    if (m < 1 || m > 12 || d < 1 || d > 31)
        return 0;
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
        y--;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

// Returns true when a complete reading was stored in g_state.
static bool fetchWeather()
{
    TlsGuard tlsGuard; // one TLS connection at a time (see tls_lock.h); also covers the whole function
    bool stored = false;

    WiFiClientSecure client;
    client.setInsecure(); // skip cert validation — fine for a read-only public API here

    HTTPClient http;
    Settings cfg = settings_get();
    // current reading + humidity/wind/feels-like/pressure + today's high/low/UV + the next hours'
    // temperature, chance of rain and UV. timezone=auto makes "today" and the hourly times local to
    // the coordinates.
    // Extras (weather_extras.h): the daily arrays cover WX_MAX_DAYS days (weather code and chance of rain for the
    // 7-day part) and minutely_15 gives the next two hours of precipitation for the rain warning. forecast_hours
    // limits the hourly arrays on its own, forecast_days only the daily ones; the code below finds the current
    // hour by its time anyway.
    char minutely[64] = "";
    if (WX_RAIN_SOON_ENABLED)
        snprintf(minutely, sizeof(minutely), "&minutely_15=precipitation&forecast_minutely_15=%d", WX_RAIN_LOOKAHEAD_SLOTS);
    char url[720];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,wind_direction_10m,"
             "apparent_temperature,pressure_msl"
             "&daily=temperature_2m_max,temperature_2m_min,uv_index_max,weather_code,precipitation_probability_max"
             "&hourly=temperature_2m,precipitation_probability,uv_index&forecast_hours=%d&forecast_days=%d%s&timezone=auto",
             cfg.latitude, cfg.longitude, WEATHER_BARS, WX_DAILY_ENABLED ? WX_MAX_DAYS : 1, minutely);

    if (!http.begin(client, String(url)))
        return false;

    http.setConnectTimeout(8000); // never hang the weather task on a dead connection
    http.setTimeout(8000);
    // HTTP/1.0 = the server answers without chunked transfer encoding, so the raw stream below is plain JSON
    // (update_check.cpp needs its BodyStream wrapper only because GitHub answers chunked).
    http.useHTTP10(true);
    int status = http.GET();
    if (status == HTTP_CODE_OK)
    {
        // Parse straight from the socket instead of http.getString(): no ~1.5 KB String that grows in steps
        // (heap fragmentation next to the 40 KB sprites), and the filter keeps only the fields used below.
        JsonDocument filter;
        filter["current"] = true;
        filter["daily"]["temperature_2m_max"] = true;
        filter["daily"]["temperature_2m_min"] = true;
        filter["daily"]["uv_index_max"] = true;
        filter["hourly"]["time"] = true;
        filter["hourly"]["temperature_2m"] = true;
        filter["hourly"]["precipitation_probability"] = true;

        JsonDocument doc; // ArduinoJson v7 — auto-sized
        if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) == DeserializationError::Ok)
        {
            float temp = doc["current"]["temperature_2m"] | NAN;
            int code = doc["current"]["weather_code"] | -1;

            // ---- details (optional: the basic reading is still used without them) ----
            float tMax = doc["daily"]["temperature_2m_max"][0] | NAN;
            float tMin = doc["daily"]["temperature_2m_min"][0] | NAN;
            float hum = doc["current"]["relative_humidity_2m"] | -1.0f;
            float wind = doc["current"]["wind_speed_10m"] | -1.0f;
            float windDir = doc["current"]["wind_direction_10m"] | -1.0f;
            float feels = doc["current"]["apparent_temperature"] | NAN;
            float pres = doc["current"]["pressure_msl"] | -1.0f;
            float uv = doc["daily"]["uv_index_max"][0] | -1.0f;

            // Hourly arrays should start at the current hour (forecast_hours); find that
            // entry by its time anyway. No match = no bars rather than wrong ones.
            uint8_t bars = 0;
            uint8_t bHour[AppState::WEATHER_MAX_BARS];
            int8_t bTemp[AppState::WEATHER_MAX_BARS], bRain[AppState::WEATHER_MAX_BARS];
            float uvNow = -1.0f; // UV index of the current hour (sunglasses)
            JsonArray hTime = doc["hourly"]["time"].as<JsonArray>();
            JsonArray hTemp = doc["hourly"]["temperature_2m"].as<JsonArray>();
            JsonArray hProb = doc["hourly"]["precipitation_probability"].as<JsonArray>();
            JsonArray hUv = doc["hourly"]["uv_index"].as<JsonArray>();
            const char *curT = doc["current"]["time"] | "";
            if (!hTime.isNull() && !hTemp.isNull() && curT[0])
            {
                int start = -1;
                for (int i = 0; i < (int)hTime.size(); i++)
                {
                    const char *ht = hTime[i] | "";
                    if (strncmp(ht, curT, 13) == 0) // same "YYYY-MM-DDTHH"
                    {
                        start = i;
                        break;
                    }
                }
                if (start >= 0 && !hUv.isNull() && start < (int)hUv.size())
                    uvNow = hUv[start] | -1.0f;
                for (int k = 0; start >= 0 && k < WEATHER_BARS && start + k < (int)hTime.size() && start + k < (int)hTemp.size(); k++)
                {
                    float t = hTemp[start + k] | NAN;
                    const char *ht = hTime[start + k] | "";
                    if (isnan(t) || strlen(ht) < 13)
                        break;
                    int pr = hProb.isNull() ? -1 : (hProb[start + k] | -1);
                    bHour[bars] = (uint8_t)(atoi(ht + 11) % 24);
                    bTemp[bars] = (int8_t)constrain((int)lroundf(t), -127, 127);
                    bRain[bars] = (int8_t)constrain(pr, -1, 100);
                    bars++;
                }
            }

            // ---- 7-day forecast (weather_extras.h) ----
            WxDay days[WX_MAX_DAYS] = {};
            uint8_t dayCount = 0;
            if (WX_DAILY_ENABLED)
            {
                JsonArray dTime = doc["daily"]["time"].as<JsonArray>();
                JsonArray dMax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
                JsonArray dMin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
                JsonArray dCode = doc["daily"]["weather_code"].as<JsonArray>();
                JsonArray dProb = doc["daily"]["precipitation_probability_max"].as<JsonArray>();
                if (!dTime.isNull() && !dMax.isNull() && !dMin.isNull() && !dCode.isNull())
                {
                    for (int i = 0; i < WX_MAX_DAYS && i < (int)dTime.size() && i < (int)dMax.size() && i < (int)dMin.size() &&
                                    i < (int)dCode.size();
                         i++)
                    {
                        float hi = dMax[i] | NAN;
                        float lo = dMin[i] | NAN;
                        int c = dCode[i] | -1;
                        if (isnan(hi) || isnan(lo) || c < 0)
                            break; // a hole: show the days before it rather than a wrong one
                        int pr = (dProb.isNull() || i >= (int)dProb.size()) ? -1 : (dProb[i] | -1);
                        days[dayCount].maxC = (int8_t)constrain((int)lroundf(hi), -127, 127);
                        days[dayCount].minC = (int8_t)constrain((int)lroundf(lo), -127, 127);
                        days[dayCount].code = (uint8_t)constrain(c, 0, 255);
                        days[dayCount].rainPct = (int8_t)constrain(pr, -1, 100);
                        days[dayCount].dow = dowFromDate(dTime[i] | "");
                        dayCount++;
                    }
                }
            }

            // ---- rain warning: the first 15-minute step with rain in the next two hours ----
            // The "current" time is on a 15-minute step, the minutely times are too: the first entry that is not
            // before it is "now". Nothing is announced while it is already wet (the eyes show that), or when the
            // minutely data is missing.
            bool rainSoon = false, rainSnow = false;
            int rainSlot = 0;
            if (WX_RAIN_SOON_ENABLED && !isnan(temp) && code >= 0 && !isWetCode(code))
            {
                JsonArray mTime = doc["minutely_15"]["time"].as<JsonArray>();
                JsonArray mPrec = doc["minutely_15"]["precipitation"].as<JsonArray>();
                if (!mTime.isNull() && !mPrec.isNull() && curT[0])
                {
                    int start = -1;
                    for (int i = 0; i < (int)mTime.size(); i++)
                    {
                        const char *mt = mTime[i] | "";
                        if (strcmp(mt, curT) >= 0) // ISO times compare like text
                        {
                            start = i;
                            break;
                        }
                    }
                    for (int k = 0; start >= 0 && k < WX_RAIN_LOOKAHEAD_SLOTS && start + k < (int)mTime.size() && start + k < (int)mPrec.size(); k++)
                    {
                        float mm = mPrec[start + k] | 0.0f;
                        if (mm >= WX_RAIN_SOON_MM)
                        {
                            rainSoon = true;
                            rainSlot = k;
                            rainSnow = temp <= 1.0f;
                            break;
                        }
                    }
                }
            }

            if (!isnan(temp) && code >= 0)
            {
                xSemaphoreTake(g_stateMutex, portMAX_DELAY);
                g_state.detailValid = !isnan(tMax) && !isnan(tMin);
                g_state.tMaxC = tMax;
                g_state.tMinC = tMin;
                g_state.humidityPct = hum >= 0 ? (int)lroundf(hum) : -1;
                g_state.windKmh = wind;
                g_state.windDirDeg = windDir >= 0 ? ((int)lroundf(windDir)) % 360 : -1;
                g_state.apparentC = feels;
                g_state.pressureHpa = pres > 0 ? (int)lroundf(pres) : -1;
                g_state.uvMax = uv;
                g_state.barCount = bars;
                for (uint8_t i = 0; i < bars; i++)
                {
                    g_state.barHour[i] = bHour[i];
                    g_state.barTempC[i] = bTemp[i];
                    g_state.barRainPct[i] = bRain[i];
                }
                g_state.weatherTempC = temp;
                g_state.weatherCode = code;
                g_state.weatherValid = true;
                g_state.weatherUpdatedMs = millis();

                // the extras ride along with the same reading (the air quality fields are not touched here)
                s_extra.forecastValid = true;
                s_extra.forecastMs = g_state.weatherUpdatedMs;
                s_extra.dayCount = dayCount;
                for (uint8_t i = 0; i < dayCount; i++)
                    s_extra.day[i] = days[i];
                s_extra.uvNow = uvNow;
                s_extra.rainSoon = rainSoon;
                s_extra.rainSoonSnow = rainSnow;
                s_extra.rainSoonAtMs = g_state.weatherUpdatedMs + (uint32_t)rainSlot * 15UL * 60UL * 1000UL;
                xSemaphoreGive(g_stateMutex);
                stored = true;
            }
        }
    }
    // NOTE: on any failure above (bad status, bad JSON, missing fields) we
    // just leave the previous cached reading in place rather than clearing
    // weatherValid — a stale reading is more useful than "N/A" for one
    // missed refresh cycle.

    http.end();
    return stored;
}

// ---- Air quality + pollen (Open-Meteo air quality API, same coordinates) ----
static const char *const kPollenKeys[6] = {"alder_pollen", "birch_pollen", "grass_pollen", "mugwort_pollen", "olive_pollen", "ragweed_pollen"};

// Grains per m3 of the highest pollen -> 0 none .. 4 very high. A rough, general scale (the real limits differ per
// plant); it is only there to colour a word.
static int8_t pollenLevelOf(float peak)
{
    if (peak < 0)
        return -1;
    if (peak < 1.0f)
        return 0;
    if (peak < 20.0f)
        return 1;
    if (peak < 100.0f)
        return 2;
    if (peak < 500.0f)
        return 3;
    return 4;
}

// Returns true when a reading was stored.
static bool fetchAirQuality()
{
    TlsGuard tlsGuard;
    bool stored = false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    Settings cfg = settings_get();
    const bool askPollen = WX_POLLEN_ENABLED && !s_aqNoPollen;
    char pollenVars[128] = "";
    if (askPollen)
        snprintf(pollenVars, sizeof(pollenVars), ",%s,%s,%s,%s,%s,%s", kPollenKeys[0], kPollenKeys[1], kPollenKeys[2],
                 kPollenKeys[3], kPollenKeys[4], kPollenKeys[5]);
    char url[400];
    snprintf(url, sizeof(url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&current=us_aqi,pm10,pm2_5%s&timezone=auto",
             cfg.latitude, cfg.longitude, pollenVars);

    if (!http.begin(client, String(url)))
        return false;

    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    int status = http.GET();
    if (status == HTTP_CODE_OK)
    {
        String body = http.getString();
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok)
        {
            JsonObject cur = doc["current"].as<JsonObject>();
            if (!cur.isNull())
            {
                float aqi = cur["us_aqi"] | -1.0f;
                float pm25 = cur["pm2_5"] | -1.0f;
                float pm10 = cur["pm10"] | -1.0f;

                // pollen: null outside Europe and out of season; take the highest of the six
                float peak = -1.0f;
                int peakType = 0;
                if (askPollen)
                {
                    for (int i = 0; i < 6; i++)
                    {
                        JsonVariant v = cur[kPollenKeys[i]];
                        if (v.isNull())
                            continue;
                        float f = v.as<float>();
                        if (f > peak)
                        {
                            peak = f;
                            peakType = i;
                        }
                    }
                }

                if (aqi >= 0 || pm25 >= 0 || pm10 >= 0)
                {
                    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
                    s_extra.aqValid = true;
                    s_extra.aqMs = millis();
                    s_extra.usAqi = aqi >= 0 ? (int16_t)constrain((int)lroundf(aqi), 0, 999) : (int16_t)-1;
                    s_extra.pm25 = pm25;
                    s_extra.pm10 = pm10;
                    s_extra.pollenLevel = pollenLevelOf(peak);
                    s_extra.pollenType = (uint8_t)peakType;
                    s_extra.pollenPeak = peak;
                    xSemaphoreGive(g_stateMutex);
                    stored = true;
                }
            }
        }
    }
    else if (status == 400 && askPollen)
    {
        // an unknown variable makes the whole request fail: drop the pollen ones and try again at the next retry
        s_aqNoPollen = true;
    }

    http.end();
    return stored;
}

void weather_init()
{
    // Nothing to do — WiFi is brought up by time_sync_init().
}

void weather_refresh_now()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.weatherValid = false;
    g_state.detailValid = false;
    s_extra = WeatherExtra{}; // the old city's rain warning, UV, 7-day forecast and air quality must not stay on screen
    xSemaphoreGive(g_stateMutex);
    s_forceRefresh = true;
    s_aqForce = true;
}

static void forecastTick()
{
    uint32_t now = millis();
    uint32_t interval = s_lastFetchFailed ? WEATHER_RETRY_INTERVAL_MS : WEATHER_REFRESH_INTERVAL_MS;
    if (!s_forceRefresh && s_lastFetchMs != 0 && (now - s_lastFetchMs) < interval)
        return;

    s_forceRefresh = false;
    s_lastFetchFailed = !fetchWeather();
    s_lastFetchMs = now;
}

static void airQualityTick()
{
    if (!WX_AIRQ_ENABLED)
        return;
    uint32_t now = millis();
    uint32_t interval = s_aqFailed ? WX_AIRQ_RETRY_MS : WX_AIRQ_REFRESH_MS;
    if (!s_aqForce && s_aqLastMs != 0 && (now - s_aqLastMs) < interval)
        return;

    s_aqForce = false;
    s_aqFailed = !fetchAirQuality();
    s_aqLastMs = now;
}

void weather_tick()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    forecastTick();
    airQualityTick();
}