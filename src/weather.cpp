#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "weather.h"
#include "config.h"
#include "settings.h"
#include "state.h"

// After a failed fetch (no internet, server error, bad JSON) the next attempt comes
// after this, not after a whole WEATHER_REFRESH_INTERVAL_MS. The weather task polls
// every 30 s, so the real retry gap is rounded up to a multiple of that.
#ifndef WEATHER_RETRY_INTERVAL_MS
#define WEATHER_RETRY_INTERVAL_MS (60UL * 1000)
#endif

static uint32_t s_lastFetchMs = 0;
static bool s_lastFetchFailed = false;
static volatile bool s_forceRefresh = false;

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

// Returns true when a complete reading was stored in g_state.
static bool fetchWeather()
{
    bool stored = false;

    WiFiClientSecure client;
    client.setInsecure(); // skip cert validation — fine for a read-only public API here

    HTTPClient http;
    Settings cfg = settings_get();
    // current reading + humidity/wind + today's high/low + the next hours' temperature
    // and chance of rain. timezone=auto makes "today" and the hourly times local to
    // the coordinates.
    char url[420];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,wind_direction_10m"
             "&daily=temperature_2m_max,temperature_2m_min"
             "&hourly=temperature_2m,precipitation_probability&forecast_hours=%d&forecast_days=1&timezone=auto",
             cfg.latitude, cfg.longitude, WEATHER_BARS);

    if (!http.begin(client, String(url)))
        return false;

    http.setConnectTimeout(8000); // never hang the weather task on a dead connection
    http.setTimeout(8000);
    int status = http.GET();
    if (status == HTTP_CODE_OK)
    {
        String body = http.getString();

        JsonDocument doc; // ArduinoJson v7 — auto-sized
        if (deserializeJson(doc, body) == DeserializationError::Ok)
        {
            float temp = doc["current"]["temperature_2m"] | NAN;
            int code = doc["current"]["weather_code"] | -1;

            // ---- details (optional: the basic reading is still used without them) ----
            float tMax = doc["daily"]["temperature_2m_max"][0] | NAN;
            float tMin = doc["daily"]["temperature_2m_min"][0] | NAN;
            float hum = doc["current"]["relative_humidity_2m"] | -1.0f;
            float wind = doc["current"]["wind_speed_10m"] | -1.0f;
            float windDir = doc["current"]["wind_direction_10m"] | -1.0f;

            // Hourly arrays should start at the current hour (forecast_hours); find that
            // entry by its time anyway. No match = no bars rather than wrong ones.
            uint8_t bars = 0;
            uint8_t bHour[AppState::WEATHER_MAX_BARS];
            int8_t bTemp[AppState::WEATHER_MAX_BARS], bRain[AppState::WEATHER_MAX_BARS];
            JsonArray hTime = doc["hourly"]["time"].as<JsonArray>();
            JsonArray hTemp = doc["hourly"]["temperature_2m"].as<JsonArray>();
            JsonArray hProb = doc["hourly"]["precipitation_probability"].as<JsonArray>();
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

            if (!isnan(temp) && code >= 0)
            {
                xSemaphoreTake(g_stateMutex, portMAX_DELAY);
                g_state.detailValid = !isnan(tMax) && !isnan(tMin);
                g_state.tMaxC = tMax;
                g_state.tMinC = tMin;
                g_state.humidityPct = hum >= 0 ? (int)lroundf(hum) : -1;
                g_state.windKmh = wind;
                g_state.windDirDeg = windDir >= 0 ? ((int)lroundf(windDir)) % 360 : -1;
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

void weather_init()
{
    // Nothing to do — WiFi is brought up by time_sync_init().
}

void weather_refresh_now()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.weatherValid = false;
    g_state.detailValid = false;
    xSemaphoreGive(g_stateMutex);
    s_forceRefresh = true;
}

void weather_tick()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    uint32_t now = millis();
    uint32_t interval = s_lastFetchFailed ? WEATHER_RETRY_INTERVAL_MS : WEATHER_REFRESH_INTERVAL_MS;
    if (!s_forceRefresh && s_lastFetchMs != 0 && (now - s_lastFetchMs) < interval)
        return;

    s_forceRefresh = false;
    s_lastFetchFailed = !fetchWeather();
    s_lastFetchMs = now;
}