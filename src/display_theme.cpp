// display_theme.cpp - time-of-day theme: moves the background / accent colours with the sun.
#include "display_internal.h"

namespace disp
{

// =====================================================================
//  Time-of-day theme
// =====================================================================
struct ThemeColors
{
    uint8_t bg[3], ghost[3], accent[3], eyeTop[3], eyeBot[3];
};
//                                   background      unlit segments    accent           eye top          eye bottom
static const ThemeColors TH_NIGHT = {{6, 9, 26}, {16, 26, 52}, {0, 225, 255}, {140, 245, 255}, {0, 140, 235}};
static const ThemeColors TH_DAWN = {{34, 14, 26}, {64, 28, 44}, {255, 170, 105}, {255, 222, 178}, {240, 105, 85}};
static const ThemeColors TH_DAY = {{22, 62, 112}, {36, 84, 138}, {255, 238, 140}, {255, 250, 208}, {255, 185, 55}};
static const ThemeColors TH_DUSK = {{40, 16, 14}, {74, 32, 22}, {255, 125, 40}, {255, 196, 122}, {235, 80, 30}};
// Twilight sits between night <-> dawn and dusk <-> night. Blending cyan straight into
// orange passes through a muddy grey; going through lilac/pink follows the real sky.
static const ThemeColors TH_TWILIGHT = {{24, 12, 40}, {46, 26, 74}, {205, 145, 255}, {228, 195, 255}, {150, 100, 230}};

struct ThemeKey
{
    float hour;
    const ThemeColors *look;
};
// Ascending hours; before the first and after the last key it is simply night.
// The two twilight keys sit halfway between their neighbours.
static const int THEME_KEY_COUNT = 8;
static ThemeKey THEME_KEYS[THEME_KEY_COUNT];

// Fills THEME_KEYS for today. With the real sun the hours are offsets from sunrise /
// sunset; they are kept non-decreasing so a very short winter day can't scramble them.
// Returns false when there is no usable sunrise/sunset (polar day/night): the caller
// then picks a fixed look.
static bool themeBuildKeys(bool haveTime, const struct tm &ti, bool &polarNight, bool &midnightSun)
{
    polarNight = midnightSun = false;
    float h[6] = {THEME_NIGHT_END_H, THEME_DAWN_H, THEME_DAY_FROM_H, THEME_DAY_TO_H, THEME_DUSK_H, THEME_NIGHT_FROM_H};
    if (SUN_ENABLED && haveTime)
    {
        SunTimes s = sun_today(ti);
        if (s.valid)
        {
            if (s.polarNight || s.midnightSun)
            {
                polarNight = s.polarNight;
                midnightSun = s.midnightSun;
                return false;
            }
            h[0] = s.riseH + THEME_NIGHT_END_FROM_SUNRISE_H;
            h[1] = s.riseH + THEME_DAWN_FROM_SUNRISE_H;
            h[2] = s.riseH + THEME_DAY_FROM_SUNRISE_H;
            h[3] = s.setH + THEME_DAY_TO_FROM_SUNSET_H;
            h[4] = s.setH + THEME_DUSK_FROM_SUNSET_H;
            h[5] = s.setH + THEME_NIGHT_FROM_SUNSET_H;
            if (h[3] < h[2]) // short day: bright part collapses to one point
                h[2] = h[3] = (h[2] + h[3]) / 2.0f;
            for (int i = 1; i < 6; i++)
                if (h[i] < h[i - 1])
                    h[i] = h[i - 1];
        }
    }
    THEME_KEYS[0] = {h[0], &TH_NIGHT};
    THEME_KEYS[1] = {(h[0] + h[1]) / 2.0f, &TH_TWILIGHT};
    THEME_KEYS[2] = {h[1], &TH_DAWN};
    THEME_KEYS[3] = {h[2], &TH_DAY};
    THEME_KEYS[4] = {h[3], &TH_DAY};
    THEME_KEYS[5] = {h[4], &TH_DUSK};
    THEME_KEYS[6] = {(h[4] + h[5]) / 2.0f, &TH_TWILIGHT};
    THEME_KEYS[7] = {h[5], &TH_NIGHT};
    return true;
}

// Changes whenever a themed colour changes: pages that are normally redrawn only
// when their content changes (the date page) include it in their redraw signature.
uint32_t themeStamp = 0;

static void lerp3(uint8_t *out, const uint8_t *a, const uint8_t *b, float t)
{
    for (int i = 0; i < 3; i++)
        out[i] = (uint8_t)(a[i] + ((float)b[i] - (float)a[i]) * t + 0.5f);
}

// Recomputes the themed colours. Once a second is plenty (they drift over hours);
// the preview time-lapse runs every frame.
void themeUpdate(bool haveTime, const struct tm &ti, uint32_t now)
{
    if (!THEME_ENABLED)
        return;
    static uint32_t lastSec = 0xFFFFFFFFu;
    uint32_t sec = now / 1000;
    if (!THEME_PREVIEW && sec == lastSec)
        return;
    lastSec = sec;

    float hour = 0; // no time yet -> night look (the boot look)
    if (THEME_PREVIEW)
        hour = (float)(now % THEME_PREVIEW_DAY_MS) * 24.0f / (float)THEME_PREVIEW_DAY_MS;
    else if (haveTime)
        hour = ti.tm_hour + ti.tm_min / 60.0f + ti.tm_sec / 3600.0f;

    ThemeColors c = TH_NIGHT;
    bool polarNight, midnightSun;
    if (!themeBuildKeys(haveTime && !THEME_PREVIEW, ti, polarNight, midnightSun))
    {
        if (midnightSun)
            c = TH_DAY; // sun never sets today
    }
    else
    {
        // a sunset after midnight: the small hours still belong to yesterday's evening
        if (hour < THEME_KEYS[0].hour && hour + 24.0f < THEME_KEYS[THEME_KEY_COUNT - 1].hour)
            hour += 24.0f;
        if (hour > THEME_KEYS[0].hour && hour < THEME_KEYS[THEME_KEY_COUNT - 1].hour)
        {
            int i = 0;
            while (i < THEME_KEY_COUNT - 2 && hour >= THEME_KEYS[i + 1].hour)
                i++;
            const ThemeKey &k0 = THEME_KEYS[i], &k1 = THEME_KEYS[i + 1];
            float span = k1.hour - k0.hour;
            float t = span > 0.01f ? (hour - k0.hour) / span : 1.0f;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            t = t * t * (3.0f - 2.0f * t); // smoothstep: lingers at each look, moves in between
            lerp3(c.bg, k0.look->bg, k1.look->bg, t);
            lerp3(c.ghost, k0.look->ghost, k1.look->ghost, t);
            lerp3(c.accent, k0.look->accent, k1.look->accent, t);
            lerp3(c.eyeTop, k0.look->eyeTop, k1.look->eyeTop, t);
            lerp3(c.eyeBot, k0.look->eyeBot, k1.look->eyeBot, t);
        }
    }

    uint16_t bg = rgb(c.bg[0], c.bg[1], c.bg[2]);
    uint16_t ghost = rgb(c.ghost[0], c.ghost[1], c.ghost[2]);
    uint16_t accent = rgb(c.accent[0], c.accent[1], c.accent[2]);
    if (bg != C_BG || ghost != C_GHOST || accent != C_ACCENT)
        themeStamp++;
    C_BG = bg;
    C_GHOST = ghost;
    C_ACCENT = accent;
    if (THEME_EYES)
    {
        C_EYE_TOP = rgb(c.eyeTop[0], c.eyeTop[1], c.eyeTop[2]);
        C_EYE_BOT = rgb(c.eyeBot[0], c.eyeBot[1], c.eyeBot[2]);
    }
}

} // namespace disp
