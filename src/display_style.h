#pragma once
// display_style.h - the look of the pages, taken from the clock character: deep navy, white "neon" cores with a
// blue glow, a pink blush accent, rounded pills and small sparkles. Shared by the clock and every page, so
// one change here changes them all.
//
//   UI_STYLE 1 = the new look (default), 0 = the old flat look (override with -D UI_STYLE=0)
#include "display_internal.h"

#ifndef UI_STYLE
#define UI_STYLE 1
#endif

namespace disp
{
    constexpr uint16_t C_BLUSH = rgb(252, 61, 218); // the pink of the character's cheeks

    // A bright, nearly white version of a colour: the "core" of a neon stroke (the colour itself is its glow).
    inline uint16_t neonCore(uint16_t c, float k = 0.82f) { return mixColor(c, C_WHITE, k); }

    // The line along the top edge of a page: thin glow line with a brighter notch in the middle (like the bell on
    // top of the character's eyes). yOff = screen y of this canvas' top edge (0 for the lower card / single canvas).
    void drawTopBar(TFT_eSPI &g, int16_t yOff, uint16_t accent);
    // Thin rule with a small sparkle in the middle, from x0 to x1.
    void drawDivider(TFT_eSPI &g, int16_t x0, int16_t x1, int16_t y);
    // Progress bar: dark pill, neon fill, optional pink dot at the end of the fill.
    void drawNeonBar(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, float frac, uint16_t accent, bool tip);
    // Page indicator dot: active = a small ring with a bright core, inactive = a dim dot.
    void drawRingDot(TFT_eSPI &g, int16_t x, int16_t y, bool active, uint16_t accent);
    // Four-point sparkle (the star in the character's eyes). k = size, 1 = about 7 px.
    void drawSparkle(TFT_eSPI &g, int16_t x, int16_t y, float k, uint16_t col);
    // Outline of a rounded rectangle, 1 px (selection frames).
    void drawFrame(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t col);
} // namespace disp
