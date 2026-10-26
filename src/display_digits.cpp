// display_digits.cpp - seven-segment digits, the blinking colon and the clock-digit slide animation.
#include "display_internal.h"
#include "display_style.h"

namespace disp
{

    // 7-segment digit. d = 0..9, 10 = minus sign, <0 = blank.
    static const uint8_t SEG_MASK[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40};
    struct Seg
    {
        int16_t x, y, w, h;
    };

    // How strongly the bright "neon core" of the lit segments shows: 1 = full, less while a digit fades in / out
    // (the slide animation), so the core fades with the glow.
    static float s_neonFade = 1.0f;

    // Which parts of a digit to draw.
    enum : uint8_t
    {
        SEG_GHOST = 1, // the unlit "ghost" segments
        SEG_LIT = 2,   // the lit segments, with their glow
        SEG_ALL = 3
    };

    static void drawSegDigitEx(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int8_t d,
                               uint16_t on, uint16_t off, uint8_t parts)
    {
        int16_t t = imax(2, w / 5);
        int16_t vh = (h - 3 * t) / 2;
        uint8_t m = (d >= 0 && d <= 10) ? SEG_MASK[d] : 0; // d < 0: blank = only ghost segments
        Seg s[7] = {
            {(int16_t)(x + t), y, (int16_t)(w - 2 * t), t},                     // a top
            {(int16_t)(x + w - t), (int16_t)(y + t), t, vh},                    // b top-right
            {(int16_t)(x + w - t), (int16_t)(y + 2 * t + vh), t, vh},           // c bottom-right
            {(int16_t)(x + t), (int16_t)(y + h - t), (int16_t)(w - 2 * t), t},  // d bottom
            {x, (int16_t)(y + 2 * t + vh), t, vh},                              // e bottom-left
            {x, (int16_t)(y + t), t, vh},                                       // f top-left
            {(int16_t)(x + t), (int16_t)(y + t + vh), (int16_t)(w - 2 * t), t}, // g middle
        };
        if (d == 10)
            off = C_BG; // a minus sign has no "ghost" segments

        // soft glow: two dim halo layers (outer first) behind the lit segments
        if (CLOCK_GLOW && (parts & SEG_LIT))
        {
            for (uint8_t layer = 2; layer >= 1; layer--)
            {
                uint16_t hc = tint(on, layer == 2 ? 0.08f : 0.18f);
                for (uint8_t i = 0; i < 7; i++)
                    if ((m >> i) & 1)
                        g.fillRoundRect(s[i].x - layer, s[i].y - layer, s[i].w + 2 * layer, s[i].h + 2 * layer,
                                        t / 2 + layer, hc);
            }
        }
        if (parts & SEG_GHOST)
            for (uint8_t i = 0; i < 7; i++)
                if (!((m >> i) & 1))
                    g.fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, t / 2, off);
        if (parts & SEG_LIT)
        {
#if UI_STYLE
            // neon: the colour is the edge and the glow, the inside is nearly white (like the ring of the eyes)
            const uint16_t core = neonCore(on, 0.80f * s_neonFade);
            for (uint8_t i = 0; i < 7; i++)
                if ((m >> i) & 1)
                {
                    if (t >= 3)
                    {
                        g.fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, t / 2, on);
                        g.fillRoundRect(s[i].x + 1, s[i].y + 1, s[i].w - 2, s[i].h - 2, imax(0, t / 2 - 1), core);
                    }
                    else
                        g.fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, t / 2, mixColor(on, core, 0.6f));
                }
#else
            for (uint8_t i = 0; i < 7; i++)
                if ((m >> i) & 1)
                    g.fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, t / 2, on);
#endif
        }
    }

    void drawSegDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int8_t d,
                      uint16_t on, uint16_t off)
    {
        drawSegDigitEx(g, x, y, w, h, d, on, off, SEG_ALL);
    }

    // Colon: two dots with a soft halo. level 0..15 = brightness (it "breathes").
    void drawColonGlow(TFT_eSPI &g, int16_t cx, int16_t y, int16_t h, int16_t t, uint16_t on, uint8_t level)
    {
        float k = level / 15.0f;
        uint16_t c = mixColor(tint(on, 0.15f), on, k);
        for (int i = 1; i <= 2; i++)
        {
            int16_t py = y + i * h / 3;
            if (CLOCK_GLOW)
            {
                for (uint8_t layer = 2; layer >= 1; layer--)
                    g.fillRoundRect(cx - t / 2 - layer, py - t / 2 - layer, t + 2 * layer, t + 2 * layer, t / 2 + layer,
                                    mixColor(C_BG, tint(on, layer == 2 ? 0.08f : 0.18f), k)); // fades into the background, never darker than it
            }
            g.fillRoundRect(cx - t / 2, py - t / 2, t, t, t / 2, c);
#if UI_STYLE
            if (t >= 4) // a bright core: a little blush dot
                g.fillRoundRect(cx - t / 2 + 1, py - t / 2 + 1, t - 2, t - 2, imax(1, t / 2 - 1), neonCore(c, 0.55f * k));
#endif
        }
    }

    // 0..15, follows a smooth sine "breath" (or the old on/off blink if the pulse is disabled)
    uint8_t colonLevelNow(uint32_t now)
    {
        if (!CLOCK_COLON_PULSE)
            return ((now / 500) % 2) == 0 ? 15 : 0;
        float ph = (float)(now % (uint32_t)CLOCK_COLON_PERIOD_MS) / (float)CLOCK_COLON_PERIOD_MS;
        float pulse = 0.5f - 0.5f * cosf(ph * 2.0f * (float)M_PI); // 0 -> 1 -> 0
        return (uint8_t)(pulse * 15.0f + 0.5f);
    }

    // ---- Clock digit animation ----
    // A changing digit is drawn into a small sprite (so it is clipped to its own cell):
    // the unlit segments stay put, the old digit rises and fades out, the new one
    // rises in from below. Needs the info panel to be a real sprite (not the
    // viewport fallback), otherwise digits just change instantly.
    bool digSprOk = false;
    int8_t digCur[4] = {-1, -1, -1, -1};    // digit currently shown (-1 = nothing yet)
    int8_t digPrev[4] = {-1, -1, -1, -1};   // digit it is replacing
    uint32_t digChangeMs[4] = {0, 0, 0, 0}; // when it changed; 0 = not animating

    void clockMetrics(int16_t &dw, int16_t &dh)
    {
        dh = (int16_t)(infoH * 0.42f);
        dw = (int16_t)(dh * 0.56f);
    }

    void drawAnimDigit(int16_t x, int16_t y, int16_t dw, int16_t dh, int8_t oldD, int8_t newD,
                       float p, uint16_t on)
    {
        float inv = 1.0f - p;
        float e = 1.0f - inv * inv * inv; // ease-out: quick start, soft landing
        int16_t travel = dh + 2 * DIG_PAD + imax(2, u(2));
        int16_t off = (int16_t)(e * travel);

        digSpr.fillSprite(C_BG);
        drawSegDigitEx(digSpr, DIG_PAD, DIG_PAD, dw, dh, -1, on, C_GHOST, SEG_GHOST); // unlit backdrop stays
        s_neonFade = 1.0f - e;
        drawSegDigitEx(digSpr, DIG_PAD, DIG_PAD - off, dw, dh, oldD, mixColor(on, C_BG, e), C_GHOST, SEG_LIT);
        s_neonFade = e;
        drawSegDigitEx(digSpr, DIG_PAD, DIG_PAD + travel - off, dw, dh, newD, mixColor(C_BG, on, e), C_GHOST, SEG_LIT);
        s_neonFade = 1.0f;
        digSpr.pushToSprite(&infoCv.spr, x - DIG_PAD, y - DIG_PAD);
    }

    // A digit that rolls from oldD to newD inside its own cell (any size, e.g. the temperature digits).
    // The cell is isolated with a viewport on the info-panel sprite instead of the small digit sprite
    // above, so it works for every digit size. Only for the sprite canvas: in the viewport fallback
    // (no sprite) the caller must not use it.
    void drawRollDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t dw, int16_t dh, int8_t oldD, int8_t newD,
                       float p, int8_t dir, uint16_t on)
    {
        float inv = 1.0f - p;
        float e = 1.0f - inv * inv * inv; // ease-out: quick start, soft landing
        int16_t travel = dh + 2 * DIG_PAD + imax(2, u(2));
        int16_t off = (int16_t)(e * travel);
        int16_t s = dir > 0 ? -1 : 1; // direction the old digit leaves in

        g.setViewport(x - DIG_PAD, y - DIG_PAD, dw + 2 * DIG_PAD, dh + 2 * DIG_PAD, false); // clip to the cell
        g.fillRect(x - DIG_PAD, y - DIG_PAD, dw + 2 * DIG_PAD, dh + 2 * DIG_PAD, C_BG);
        if (newD >= 0) // a digit that is going away leaves no unlit segments behind
            drawSegDigitEx(g, x, y, dw, dh, -1, on, C_GHOST, SEG_GHOST);
        s_neonFade = 1.0f - e;
        drawSegDigitEx(g, x, (int16_t)(y + s * off), dw, dh, oldD, mixColor(on, C_BG, e), C_GHOST, SEG_LIT);
        s_neonFade = e;
        drawSegDigitEx(g, x, (int16_t)(y - s * (travel - off)), dw, dh, newD, mixColor(C_BG, on, e), C_GHOST, SEG_LIT);
        s_neonFade = 1.0f;
        g.resetViewport();
    }

    // Draws digits from a small int array, returns nothing. count digits wide.
    void drawSegNumber(TFT_eSPI &g, int16_t x, int16_t y, int16_t dw, int16_t dh, int16_t gap,
                       const int8_t *digits, uint8_t count, uint16_t on)
    {
        for (uint8_t i = 0; i < count; i++)
            drawSegDigit(g, x + i * (dw + gap), y, dw, dh, digits[i], on, C_GHOST);
    }

} // namespace disp