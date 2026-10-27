#pragma once
// display_landscape.h - the pages of the landscape UI (160 x 128, one full-screen page at a time). Each draws the
// whole screen into the one big canvas (faceCv) and pushes it. Used by display.cpp when LANDSCAPE_UI is 1.
#include "display_internal.h"

namespace disp
{
    // The clock page in the style picked in the quick menu (Simple / Analog / Minimal / Retro / LED).
    // Draws into g (the full-screen canvas); the caller pushes it.
    void drawClockScreen(TFT_eSPI &g, bool haveTime, const struct tm &ti, uint32_t now);
    // A cheap number that changes whenever drawClockScreen() would look different (so unchanged frames can be skipped).
    uint32_t clockScreenSig(bool haveTime, const struct tm &ti, uint32_t now);

    // While the alarm rings: the time in big red digits under the angry eyes (over the face).
    void drawAlarmOverlay(TFT_eSPI &g, bool haveTime, const struct tm &ti, bool flashOn);

    // The Date and Weather pages (display_page_side.cpp): the two side pages of the knob cycle
    // Face -> Clock -> Date -> Weather -> Face. mode is DisplayMode::DATE or DisplayMode::WEATHER; day = the sun is up
    // (the weather icon shows the sun or the moon). Draws into g; the caller pushes it.
    void drawSidePage(TFT_eSPI &g, DisplayMode mode, bool haveTime, const struct tm &ti, bool day, uint32_t now);
    // Like clockScreenSig(): changes whenever the page would look different (the Weather page: ten times a second, for the icon).
    uint32_t sidePageSig(DisplayMode mode, bool haveTime, const struct tm &ti, uint32_t now);

    // Quick menu, diagnostics and reminder pages (full screen).
    void drawMenuScreenL(uint32_t now);
    void drawDiagScreenL(uint32_t now);
    void drawReminderScreenL(const ReminderView &rv, uint32_t now);
} // namespace disp
