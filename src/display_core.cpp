// display_core.cpp - shared display state: the TFT object, palette, layout, colour helpers,
// canvases and the readers that copy values out of g_state (under its mutex).
#include "display_internal.h"

namespace disp
{

TFT_eSPI tft = TFT_eSPI();

// The five themed colours (values are the night look, also used until the clock knows the time).
uint16_t C_BG = rgb(6, 9, 26);           // background
uint16_t C_ACCENT = rgb(0, 225, 255);    // clock digits, accent lines, bars (was fixed cyan)
uint16_t C_EYE_TOP = rgb(140, 245, 255); // eye gradient, top
uint16_t C_EYE_BOT = rgb(0, 140, 235);   // eye gradient, bottom
uint16_t C_GHOST = rgb(16, 26, 52);      // unlit 7-seg segments, empty bars

// Layout - computed in display_init() from tft.width()/height()
int16_t W, H, faceH, infoH;
float sc = 1.0f; // scale relative to the 128 px wide real display

// ---- Colour helpers ----
uint16_t dimColor(uint16_t c, uint8_t v)
{
    uint16_t r = ((c >> 11) & 0x1F) * v / 255;
    uint16_t g = ((c >> 5) & 0x3F) * v / 255;
    uint16_t b = (c & 0x1F) * v / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t mixColor(uint16_t a, uint16_t b, float t)
{
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

// ---- Off-screen canvases (see Canvas in display_internal.h) ----
Canvas faceCv, infoCv;

// Small sprite for the clock-digit slide animation (display_digits.cpp). Defined here, right after
// tft, so it is constructed after it.
TFT_eSprite digSpr(&tft);

// ---- Alarm flash state (toggled by display_tick) ----
bool alarmFlashOn = false;

// ---- State readers ----
Mood readMood()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    Mood m = g_state.mood;
    xSemaphoreGive(g_stateMutex);
    return m;
}

DisplayMode readDisplayMode()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    DisplayMode d = g_state.displayMode;
    xSemaphoreGive(g_stateMutex);
    return d;
}

bool readTimeSynced()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool t = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    return t;
}

bool readPortalActive()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool p = g_state.wifiPortalActive;
    xSemaphoreGive(g_stateMutex);
    return p;
}

int readOtaPercent()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    int p = g_state.otaPercent;
    xSemaphoreGive(g_stateMutex);
    return p;
}

void readWeather(bool &valid, float &tempC, int &code, uint32_t &updatedMs)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    valid = g_state.weatherValid;
    tempC = g_state.weatherTempC;
    code = g_state.weatherCode;
    updatedMs = g_state.weatherUpdatedMs;
    xSemaphoreGive(g_stateMutex);
}


void readWeatherDetail(WeatherDetail &d)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    d.valid = g_state.weatherValid && g_state.detailValid;
    d.maxC = g_state.tMaxC;
    d.minC = g_state.tMinC;
    d.humidityPct = g_state.humidityPct;
    d.windKmh = g_state.windKmh;
    d.windDirDeg = g_state.windDirDeg;
    d.barCount = g_state.barCount;
    for (uint8_t i = 0; i < d.barCount && i < AppState::WEATHER_MAX_BARS; i++)
    {
        d.hour[i] = g_state.barHour[i];
        d.tempC[i] = g_state.barTempC[i];
        d.rainPct[i] = g_state.barRainPct[i];
    }
    xSemaphoreGive(g_stateMutex);
}

// "45s", "12m", "3h", "2d" — compact age for the small screen.
void fmtAge(char *out, size_t n, uint32_t ageMs)
{
    uint32_t s = ageMs / 1000;
    if (s < 60)
        snprintf(out, n, "%us", (unsigned)s);
    else if (s < 3600)
        snprintf(out, n, "%um", (unsigned)(s / 60));
    else if (s < 48UL * 3600)
        snprintf(out, n, "%uh", (unsigned)(s / 3600));
    else
        snprintf(out, n, "%ud", (unsigned)(s / 86400));
}

// Age of a millis() timestamp. Signed so a timestamp written by another task
// a moment after `now` was sampled reads as 0 instead of wrapping to ~49 days.
uint32_t ageSince(uint32_t now, uint32_t stamp)
{
    int32_t d = (int32_t)(now - stamp);
    return d < 0 ? 0 : (uint32_t)d;
}

uint16_t tempColor(float tempC)
{
    if (tempC < 5)
        return rgb(100, 170, 255); // cold
    if (tempC < 18)
        return rgb(110, 225, 220); // cool
    if (tempC < 28)
        return C_AMBER;       // mild
    return rgb(255, 120, 60); // hot
}

} // namespace disp
