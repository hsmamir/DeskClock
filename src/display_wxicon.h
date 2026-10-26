#pragma once
// display_wxicon.h - the small animated weather icons of the clock faces (display_wxicon.cpp).
#include "display_internal.h"

namespace disp
{
    // One icon for every Open-Meteo WMO weather code (0 clear .. 99 thunderstorm with heavy hail; anything unknown is a
    // plain cloud). day = the sun is up (the sky shows the sun, otherwise the moon). (cx, cy) = centre, s = half the
    // width of the icon in px (about 6.5 * sc on the clock face). now = millis(): the icon animates with it, so redraw
    // about 10 times a second while it is on screen.
    void drawWxIcon(TFT_eSPI &g, int code, bool day, float cx, float cy, float s, uint32_t now);
} // namespace disp
