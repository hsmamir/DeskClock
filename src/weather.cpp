#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include "weather.h"
#include "config.h"
#include "settings.h"
#include "state.h"

static uint32_t s_lastFetchMs = 0;
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
    if (code >= 71 && code <= 77)
        return "Snow";
    if (code >= 80 && code <= 82)
        return "Showers";
    if (code >= 95)
        return "Storm";
    return "N/A";
}

static void fetchWeather()
{
    WiFiClientSecure client;
    client.setInsecure(); // skip cert validation — fine for a read-only public API here

    HTTPClient http;
    Settings cfg = settings_get();
    char url[160];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code",
             cfg.latitude, cfg.longitude);

    if (!http.begin(client, String(url)))
        return;

    int status = http.GET();
    if (status == HTTP_CODE_OK)
    {
        String body = http.getString();

        JsonDocument doc; // ArduinoJson v7 — auto-sized
        if (deserializeJson(doc, body) == DeserializationError::Ok)
        {
            float temp = doc["current"]["temperature_2m"] | NAN;
            int code = doc["current"]["weather_code"] | -1;

            if (!isnan(temp) && code >= 0)
            {
                xSemaphoreTake(g_stateMutex, portMAX_DELAY);
                g_state.weatherTempC = temp;
                g_state.weatherCode = code;
                g_state.weatherValid = true;
                xSemaphoreGive(g_stateMutex);
            }
        }
    }
    // NOTE: on any failure above (bad status, bad JSON, missing fields) we
    // just leave the previous cached reading in place rather than clearing
    // weatherValid — a stale reading is more useful than "N/A" for one
    // missed refresh cycle.

    http.end();
}

void weather_init()
{
    // Nothing to do — WiFi is brought up by time_sync_init().
}

void weather_refresh_now()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.weatherValid = false;
    xSemaphoreGive(g_stateMutex);
    s_forceRefresh = true;
}

void weather_tick()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    uint32_t now = millis();
    if (!s_forceRefresh && s_lastFetchMs != 0 && (now - s_lastFetchMs) < WEATHER_REFRESH_INTERVAL_MS)
        return;

    s_forceRefresh = false;
    fetchWeather();
    s_lastFetchMs = now;
}