// display_style.cpp - shared drawing pieces of the new look, see display_style.h.
#include "display_style.h"
#include "display_character.h"

namespace disp
{
    void drawTopBar(TFT_eSPI &g, int16_t yOff, uint16_t accent)
    {
#if UI_STYLE
        const int16_t t = imax(2, u(2));
        const int16_t y0 = -yOff;
        g.fillRect(0, y0, W, t, tint(accent, 0.55f));
        g.fillRect(0, y0 + t, W, imax(1, u(1)), tint(accent, 0.16f));
        // the notch
        const int16_t sw = (int16_t)(W * 0.30f), sx = (W - sw) / 2, m = u(3);
        g.fillRect(sx - m, y0 + t, sw + 2 * m, imax(1, u(1)), tint(accent, 0.38f));
        g.fillRoundRect(sx, y0 - t, sw, 2 * t + 1, t, accent);
        g.fillRoundRect(sx + 1, y0 - t, sw - 2, t + 1, imax(1, t / 2), neonCore(accent));
#else
        g.fillRect(0, -yOff, W, imax(2, u(2)), accent);
        g.fillRect(0, imax(2, u(2)) - yOff, W, imax(1, u(1)), tint(accent, 0.235f));
#endif
    }

    void drawDivider(TFT_eSPI &g, int16_t x0, int16_t x1, int16_t y)
    {
#if UI_STYLE
        const int16_t cx = (x0 + x1) / 2, gap = u(6);
        const uint16_t c = tint(C_SOFT, 0.31f);
        g.drawFastHLine(x0, y, cx - gap - x0, c);
        g.drawFastHLine(cx + gap, y, x1 - cx - gap, c);
        drawSparkle(g, cx, y, 0.7f, tint(C_ACCENT, 0.7f));
#else
        g.drawFastHLine(x0, y, x1 - x0, tint(C_SOFT, 0.31f));
#endif
    }

    void drawNeonBar(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, float frac, uint16_t accent, bool tip)
    {
        g.fillRoundRect(x, y, w, h, h / 2, C_GHOST);
        int16_t fw = (int16_t)(w * clamp01(frac));
        if (fw <= 0)
            return;
        g.fillRoundRect(x, y, fw, h, imin(h / 2, fw / 2), accent);
#if UI_STYLE
        if (h >= 3 && fw > h)
            g.drawFastHLine(x + h / 2, y + h / 2 - 1, fw - h, neonCore(accent, 0.7f)); // bright core line
        if (tip && fw > h)
            g.fillCircle(x + fw - h / 2, y + h / 2, imax(2, h / 2 + 1), C_BLUSH);
#else
        (void)tip;
#endif
    }

    void drawRingDot(TFT_eSPI &g, int16_t x, int16_t y, bool active, uint16_t accent)
    {
#if UI_STYLE
        if (active)
        {
            int16_t r = imax(2, u(2.5f));
            g.fillCircle(x, y, r + 1, tint(accent, 0.25f)); // halo
            g.fillCircle(x, y, r, accent);
            g.fillCircle(x, y, imax(1, r - 1), neonCore(accent));
        }
        else
            g.fillCircle(x, y, imax(1, u(1.5f)), tint(accent, 0.27f));
#else
        if (active)
            g.fillCircle(x, y, imax(2, u(2.5f)), accent);
        else
            g.fillCircle(x, y, imax(1, u(1.5f)), tint(accent, 0.27f));
#endif
    }

    void drawSparkle(TFT_eSPI &g, int16_t x, int16_t y, float k, uint16_t col)
    {
        drawCharMono(g, CharMono::SPARK, x, y, k * sc, col);
    }

    void drawFrame(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t col)
    {
        g.drawRoundRect(x, y, w, h, r, col);
    }
} // namespace disp
