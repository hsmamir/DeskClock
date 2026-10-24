#pragma once
// Extra weather drawing (display_wx.cpp): the "Next days" and "Air quality" parts of the Weather page, and the
// accessories on the face (umbrella, sunglasses, and the face mask, which is worn in every face state). The data and the tunables are in weather_extras.h.
#include "display_internal.h"
#include "weather_extras.h"

namespace disp
{
    // One whole screen of the tall Weather page (it is page-height tall: faceH + infoH). oy = the canvas y of its top
    // edge (it can be negative or reach below the canvas, the sprite clips what sticks out).
    void drawWxDailySection(TFT_eSPI &g, const WeatherPage &p, int16_t oy);
    void drawWxAirSection(TFT_eSPI &g, const WeatherPage &p, int16_t oy);

    // Draws the accessories that belong to the weather: umbrella with a countdown (rain soon) OR sunglasses (high UV in
    // a bright sky). The yes / no answers come from weather_mood.h, so they have the same hysteresis as the faces.
    // drawFace() calls it after the eyes, before the extras (fly, spray ...). now = millis(); scn = what display_tick()
    // found out (daytime, weather code).
    //   umbrellaOk  the umbrella may be up: it is above the eyes, so it stays through a sneeze, a surprise, a fly, a yawn ...
    //   shadesOk    the sunglasses may be on: they sit on the eyes, so only while the face is calm (if it stops being calm
    //               while they are on, they lift off instead of vanishing)
    void drawWeatherAccessory(TFT_eSPI &g, uint32_t now, const SceneInfo &scn, bool umbrellaOk, bool shadesOk);
} // namespace disp