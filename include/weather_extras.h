#pragma once
#include <stdint.h>
#include "config.h"

// Extra weather features. No new hardware, everything comes from Open-Meteo (free, no API key):
//
//   Air quality + pollen   a new "Air quality" part at the end of the Weather page (US AQI with a gauge, PM2.5, PM10,
//                          pollen). When the air is bad he puts on a face mask and coughs now and then.
//   Rain warning           the minutely_15 forecast says rain (or snow) starts within WX_RAIN_WARN_MIN minutes:
//                          an umbrella opens over the eyes, with the countdown ("Rain in 25 min"). Outside Europe and
//                          North America Open-Meteo builds the 15-minute values from the hourly model, so the minute
//                          is a rough guess there.
//   UV sunglasses          while the UV index of the current hour is at least WX_UV_SHADES_MIN he wears sunglasses
//                          for a while, every few minutes (so his eyes are not hidden all day).
//   7-day forecast         a new "Next days" part on the Weather page: weekday, icon, chance of rain, high / low.
//
// The Weather page is one tall page that the knob scrolls. Every extra part is one more screen at its end, so the
// number of knob clicks from top to bottom grows with it: WX_SCROLL_STEPS_TOTAL (touch.cpp and display_pages.cpp).
//
// All tunables below can be overridden in config.h (the #ifndef lets config.h win), like in quick_menu.h.

// ---- 7-day forecast ----
#ifndef WX_DAILY_ENABLED
#define WX_DAILY_ENABLED 1 // 0 = no "Next days" part (and forecast_days stays 1)
#endif
#define WX_MAX_DAYS 7 // the Open-Meteo request asks for this many days; the part shows as many as arrive

// ---- Air quality / pollen ----
#ifndef WX_AIRQ_ENABLED
#define WX_AIRQ_ENABLED 1 // 0 = no "Air quality" part, no second request, no mask
#endif
#ifndef WX_AIRQ_REFRESH_MS
#define WX_AIRQ_REFRESH_MS (30UL * 60 * 1000) // the model is updated hourly, twice an hour is plenty
#endif
#ifndef WX_AIRQ_RETRY_MS
#define WX_AIRQ_RETRY_MS (5UL * 60 * 1000) // after a failed request
#endif
#ifndef WX_POLLEN_ENABLED
#define WX_POLLEN_ENABLED 1 // pollen is only modelled for Europe, elsewhere the row shows "n/a"
#endif
#ifndef WX_AIRQ_MASK_AQI
#define WX_AIRQ_MASK_AQI 151 // US AQI at or above this: face mask + cough (151 = "Unhealthy"). 0 = never
#endif
#ifndef WX_COUGH_PERIOD_MS
#define WX_COUGH_PERIOD_MS 20000UL // he coughs once per period while the mask is on
#endif

// ---- Rain warning ----
#ifndef WX_RAIN_SOON_ENABLED
#define WX_RAIN_SOON_ENABLED 1 // 0 = no minutely request, no umbrella
#endif
#ifndef WX_RAIN_SOON_MM
#define WX_RAIN_SOON_MM 0.10f // precipitation in one 15-minute step that counts as rain (mm)
#endif
#ifndef WX_RAIN_LOOKAHEAD_SLOTS
#define WX_RAIN_LOOKAHEAD_SLOTS 8 // 15-minute steps to look at: 8 = the next 2 hours
#endif
#ifndef WX_RAIN_WARN_MIN
#define WX_RAIN_WARN_MIN 45 // the umbrella appears when the rain is at most this many minutes away
#endif
#ifndef WX_RAIN_GRACE_MS
#define WX_RAIN_GRACE_MS (10UL * 60 * 1000) // keep the umbrella this long after the announced start if the reading still says dry
#endif

// ---- UV sunglasses ----
#ifndef WX_UV_SHADES_ENABLED
#define WX_UV_SHADES_ENABLED 1
#endif
#ifndef WX_UV_SHADES_MIN
#define WX_UV_SHADES_MIN 6 // UV index of the current hour at which he puts them on (6 = "High")
#endif
#ifndef WX_UV_SHADES_ON_MS
#define WX_UV_SHADES_ON_MS 25000UL // how long they stay on ...
#endif
#ifndef WX_UV_SHADES_PERIOD_MS
#define WX_UV_SHADES_PERIOD_MS (4UL * 60 * 1000) // ... in every period
#endif
static_assert(WX_UV_SHADES_PERIOD_MS > WX_UV_SHADES_ON_MS + 2000UL, "WX_UV_SHADES_PERIOD_MS must be longer than WX_UV_SHADES_ON_MS");
static_assert(WX_RAIN_LOOKAHEAD_SLOTS >= 1 && WX_RAIN_LOOKAHEAD_SLOTS <= 16, "WX_RAIN_LOOKAHEAD_SLOTS must be 1-16");

#define WX_ACCESSORIES (WX_AIRQ_ENABLED || WX_RAIN_SOON_ENABLED || WX_UV_SHADES_ENABLED)

// ---- Weather page: how many screens it has and how many knob clicks it takes to scroll through them ----
// 2 = now + hourly forecast (always), +1 for the 7-day part, +1 for the air quality part.
#define WX_SECTIONS (2 + (WX_DAILY_ENABLED ? 1 : 0) + (WX_AIRQ_ENABLED ? 1 : 0))
#define WX_SCROLL_STEPS_TOTAL (WEATHER_SCROLL_STEPS * (WX_SECTIONS - 1)) // same click size as before: WEATHER_SCROLL_STEPS per screen
static_assert(WX_SCROLL_STEPS_TOTAL <= 255, "scroll steps must fit g_state.weatherScrollStep");

// ---- The data (written by weather.cpp, read by the display) ----
struct WxDay
{
    int8_t maxC, minC; // rounded
    uint8_t code;      // WMO weather code
    int8_t rainPct;    // highest chance of rain that day, 0-100; -1 = unknown
    uint8_t dow;       // weekday, 0 = Sunday ... 6 = Saturday (like tm_wday)
};

struct WeatherExtra
{
    // from the forecast request
    bool forecastValid;
    uint32_t forecastMs; // millis() of the last successful forecast request
    uint8_t dayCount;
    WxDay day[WX_MAX_DAYS];
    float uvNow;           // UV index of the current hour; < 0 = unknown
    bool rainSoon;         // rain / snow is expected within the look-ahead window (not set while it is already wet)
    bool rainSoonSnow;     // ... and it will be snow (the temperature is at or below 1 C)
    uint32_t rainSoonAtMs; // millis() when it is expected to start (the countdown follows millis(), not the 15-minute refresh)

    // from the air quality request
    bool aqValid;
    uint32_t aqMs;      // millis() of the last successful air quality request
    int16_t usAqi;      // US AQI; -1 = unknown
    float pm25, pm10;   // ug/m3; < 0 = unknown
    int8_t pollenLevel; // -1 = no data, 0 none .. 4 very high (a rough scale, see weather.cpp)
    uint8_t pollenType; // which pollen is the highest: 0 alder, 1 birch, 2 grass, 3 mugwort, 4 olive, 5 ragweed
    float pollenPeak;   // grains per m3 of that one
};

// Copies the current values. false = nothing has arrived yet (neither request succeeded since boot / city change).
bool weather_extra_get(WeatherExtra &out);