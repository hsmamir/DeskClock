#pragma once
// Extra weather drawing (display_wx.cpp): the "Next days" and "Air quality" parts of the (portrait) Weather page.
// The data and the tunables are in weather_extras.h.
#include "display_internal.h"
#include "weather_extras.h"

namespace disp
{
    // One whole screen of the tall Weather page (it is page-height tall: faceH + infoH). oy = the canvas y of its top
    // edge (it can be negative or reach below the canvas, the sprite clips what sticks out).
    void drawWxDailySection(TFT_eSPI &g, const WeatherPage &p, int16_t oy);
    void drawWxAirSection(TFT_eSPI &g, const WeatherPage &p, int16_t oy);
} // namespace disp