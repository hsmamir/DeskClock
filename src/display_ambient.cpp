// display_ambient.cpp - sparse ambient FX for the face card: a few faint motes rising at night and a warm
// flash of the horizon when the sun comes up. Procedural (everything derives from `now`), no per-object state.
#include "display_internal.h"

namespace disp
{

    static bool dayPrev = false, dayPrevValid = false;
    static uint32_t lastSeenMs = 0, flashStartMs = 0;
    static bool flashOn = false;

    // Watches night -> day while the clock is known. No flash at boot, after the time first became known, or
    // when the display was busy elsewhere (OTA / setup screen) for a while and the sunrise is long past.
    void ambientUpdate(const SceneInfo &scn, uint32_t now)
    {
        if (!AMBIENT_FX)
            return;
        if (now - lastSeenMs > 3000)
            dayPrevValid = false;
        lastSeenMs = now;
        if (!scn.timeKnown)
        {
            dayPrevValid = false;
            return;
        }
        if (dayPrevValid && !dayPrev && scn.day)
        {
            flashStartMs = now;
            flashOn = true;
        }
        dayPrev = scn.day;
        dayPrevValid = true;
    }

    // Quick swell (~18 % of the time), then a long soft fade; smoothstepped.
    float ambientSunriseFlash(uint32_t now)
    {
        if (!AMBIENT_FX || !flashOn)
            return 0.0f;
        uint32_t age = now - flashStartMs;
        if (age >= (uint32_t)AMBIENT_SUNRISE_FLASH_MS)
        {
            flashOn = false;
            return 0.0f;
        }
        float p = age / (float)AMBIENT_SUNRISE_FLASH_MS;
        const float rise = 0.18f;
        float e = p < rise ? p / rise : 1.0f - (p - rise) / (1.0f - rise);
        return e * e * (3.0f - 2.0f * e);
    }

    // AMBIENT_MOTES specks, each making a slow trip from the bottom edge to the top (about 12-16 s) along a
    // slightly swaying line, fading in and out. A new trip starts in a new column. The colour is the pixel that
    // is already there pulled AMBIENT_MOTE_TINT toward the accent, so it stays a faint speck on the sky gradient.
    void drawNightMotes(TFT_eSPI &g, uint32_t now)
    {
        if (!AMBIENT_FX)
            return;
        int16_t s = imax(1, u(1.5f));
        for (uint8_t i = 0; i < AMBIENT_MOTES; i++)
        {
            uint32_t period = 11000 + (uint32_t)((i * 2300) % 5000);
            uint32_t t = now + (uint32_t)i * 3700u;
            uint32_t trip = t / period;
            float p = (t % period) / (float)period;
            float col = fmodf(0.21f + i * 0.618f + trip * 0.37f, 1.0f);
            int16_t x = (int16_t)(W * (0.10f + 0.80f * col) + sinf(now / 2300.0f + i * 2.1f) * u(3));
            int16_t y = (int16_t)(faceH - p * (faceH + 2 * s));
            float e = fminf(p / 0.15f, (1.0f - p) / 0.30f);
            if (e <= 0)
                continue;
            if (e > 1.0f)
                e = 1.0f;
            uint16_t bg = (x >= 0 && x < W && y >= 0 && y < faceH && faceCv.ok) ? g.readPixel(x, y) : C_BG;
            g.fillRect(x, y, s, s, mixColor(bg, C_ACCENT, AMBIENT_MOTE_TINT * e));
        }
    }

} // namespace disp