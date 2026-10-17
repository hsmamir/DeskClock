#pragma once
// Extra weather drawing (display_wx.cpp): the "Next days" and "Air quality" parts of the Weather page, and the
// accessories on the face (umbrella, face mask, sunglasses). The data and the tunables are in weather_extras.h.
#include "display_internal.h"
#include "weather_extras.h"

namespace disp
{
    // One whole screen of the tall Weather page (it is page-height tall: faceH + infoH). oy = the canvas y of its top
    // edge (it can be negative or reach below the canvas, the sprite clips what sticks out).
    void drawWxDailySection(TFT_eSPI &g, const WeatherPage &p, int16_t oy);
    void drawWxAirSection(TFT_eSPI &g, const WeatherPage &p, int16_t oy);

    // Draws at most one of: face mask + cough (bad air), umbrella with a countdown (rain soon), sunglasses (high UV).
    // Call it after the eyes are drawn, only while the face is calm (no pet reaction, no sleep, no one-off act).
    // now = millis(); dayNow = the sun is up.
    void drawWeatherAccessory(TFT_eSPI &g, uint32_t now, bool dayNow);
} // namespace disp