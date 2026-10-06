#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>
#include <driver/gpio.h>
#include <WiFi.h>
#include "display.h"
#include "state.h"
#include "pins.h"
#include "config.h"
#include "weather.h"
#include "settings.h"
#include "boot_log.h"
#include "sun.h"
#include "pet.h"

static TFT_eSPI tft = TFT_eSPI();

// ---- Palette (RGB565) ----
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// The five colours below follow the time of day (see "Time-of-day theme" below):
// they are variables updated by themeUpdate(). The values here are the night look,
// which is also used until the clock knows the time.
static uint16_t C_BG = rgb(6, 9, 26);           // background
static uint16_t C_ACCENT = rgb(0, 225, 255);    // clock digits, accent lines, bars (was fixed cyan)
static uint16_t C_EYE_TOP = rgb(140, 245, 255); // eye gradient, top
static uint16_t C_EYE_BOT = rgb(0, 140, 235);   // eye gradient, bottom
static uint16_t C_GHOST = rgb(16, 26, 52);      // unlit 7-seg segments, empty bars
static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr uint16_t C_ALARM_TOP = rgb(255, 130, 100);
static constexpr uint16_t C_ALARM_BOT = rgb(210, 15, 25);
static constexpr uint16_t C_RED = rgb(255, 40, 40);
static constexpr uint16_t C_AMBER = rgb(255, 200, 60);
static constexpr uint16_t C_SOFT = rgb(150, 160, 190);
static constexpr uint16_t C_CHEEK = rgb(255, 105, 150);
static constexpr uint16_t C_HEART = rgb(255, 70, 115);
static constexpr uint16_t C_ICE = rgb(170, 200, 255);

// ---- Layout — computed from tft.width()/height() so this works on both
// the real 128x160 ST7735 and the 240x320 ILI9341 used for Wokwi ----
static int16_t W, H, faceH, infoH;
static float sc = 1.0f; // scale relative to the 128 px wide real display

static inline int16_t u(float v) { return (int16_t)(v * sc + 0.5f); }
static inline int16_t imax(int16_t a, int16_t b) { return a > b ? a : b; }
static inline int16_t imin(int16_t a, int16_t b) { return a < b ? a : b; }

// ---- Colour helpers ----
static uint16_t dimColor(uint16_t c, uint8_t v)
{
    uint16_t r = ((c >> 11) & 0x1F) * v / 255;
    uint16_t g = ((c >> 5) & 0x3F) * v / 255;
    uint16_t b = (c & 0x1F) * v / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t mixColor(uint16_t a, uint16_t b, float t)
{
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

// A colour faded toward the (themed) background: k = 0 -> background, 1 -> c.
// Glows and fades use this instead of dimColor() (which fades toward black), so
// they stay a soft glow on a bright day background instead of turning into dark halos.
static inline uint16_t tint(uint16_t c, float k) { return mixColor(C_BG, c, k); }

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
static uint32_t themeStamp = 0;

static void lerp3(uint8_t *out, const uint8_t *a, const uint8_t *b, float t)
{
    for (int i = 0; i < 3; i++)
        out[i] = (uint8_t)(a[i] + ((float)b[i] - (float)a[i]) * t + 0.5f);
}

// Recomputes the themed colours. Once a second is plenty (they drift over hours);
// the preview time-lapse runs every frame.
static void themeUpdate(bool haveTime, const struct tm &ti, uint32_t now)
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

// ---- Off-screen canvases ----
// The face and the info panel are drawn into RAM sprites and pushed in one
// go, so nothing flickers. If a sprite can't be allocated (e.g. the big
// Wokwi screen), drawing falls back to the TFT through a viewport.
struct Canvas
{
    TFT_eSprite spr;
    bool ok = false;
    int16_t y0 = 0, w = 0, h = 0;
    Canvas() : spr(&tft) {}
    bool init(int16_t y, int16_t width, int16_t height)
    {
        y0 = y;
        w = width;
        h = height;
        spr.setColorDepth(16);
        ok = (spr.createSprite(w, h) != nullptr);
        return ok;
    }
    TFT_eSPI &begin()
    {
        if (ok)
            return spr;
        tft.setViewport(0, y0, w, h, true);
        return tft;
    }
    void end() { endAt(0); }
    // Push with a horizontal offset (page slide-in). The sprite is clipped at
    // the right screen edge; the part left of x keeps the old picture. The
    // viewport fallback cannot offset, so it just draws in place.
    void endAt(int16_t x)
    {
        if (ok)
            spr.pushSprite(x, y0);
        else
            tft.resetViewport();
    }
};
static Canvas faceCv, infoCv;

// ---- Idle blink state machine ----
enum class BlinkPhase
{
    OPEN,
    CLOSING,
    CLOSED,
    OPENING
};
static BlinkPhase blinkPhase = BlinkPhase::OPENING; // eyes "wake up" at boot
static uint32_t blinkPhaseStartMs = 0;
static uint32_t nextBlinkAtMs = 0;
static const uint16_t BLINK_TRANSITION_MS = 80;
static const uint16_t BLINK_CLOSED_MS = 100;

// ---- Alarm flash state ----
static bool alarmFlashOn = false;
static uint32_t lastAlarmFlashMs = 0;
static const uint16_t ALARM_FLASH_MS = 200;

// ---- Look-around / glance state ----
static float lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
static uint32_t nextLookAtMs = 0;
static uint32_t glanceUntilMs = 0;

// ---- Petted state: the servo only wiggles for a moment, so the display keeps the
// reaction face on its own for pet_hold_ms(kind). pet.cpp decides WHICH reaction
// (petEv.kind); a new tap (petEv.seq changes) restarts the animation. ----
static PetEvent petEv = {PetKind::HAPPY, 0, 0, 0};
static uint32_t petSeqSeen = 0;
static uint32_t happyStartMs = 0;
static uint32_t happyUntilMs = 0;

// ---- Info panel de-dup ----
static DisplayMode lastMode = static_cast<DisplayMode>(-1);
static uint32_t lastInfoSig = 0xFFFFFFFFu;
static bool transActive = false; // info panel is sliding in
static uint32_t transStartMs = 0;
static bool portalScreenShown = false;
static bool otaScreenShown = false;
static int otaDrawnPct = -1;

// ---- State readers ----
static Mood readMood()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    Mood m = g_state.mood;
    xSemaphoreGive(g_stateMutex);
    return m;
}

static DisplayMode readDisplayMode()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    DisplayMode d = g_state.displayMode;
    xSemaphoreGive(g_stateMutex);
    return d;
}

static bool readTimeSynced()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool t = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    return t;
}

static bool readPortalActive()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool p = g_state.wifiPortalActive;
    xSemaphoreGive(g_stateMutex);
    return p;
}

static int readOtaPercent()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    int p = g_state.otaPercent;
    xSemaphoreGive(g_stateMutex);
    return p;
}

static void readWeather(bool &valid, float &tempC, int &code, uint32_t &updatedMs)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    valid = g_state.weatherValid;
    tempC = g_state.weatherTempC;
    code = g_state.weatherCode;
    updatedMs = g_state.weatherUpdatedMs;
    xSemaphoreGive(g_stateMutex);
}

struct WeatherDetail
{
    bool valid;
    float maxC, minC;
    int humidityPct; // -1 = unknown
    float windKmh;   // < 0 = unknown
    int windDirDeg;  // -1 = unknown
    uint8_t barCount;
    uint8_t hour[AppState::WEATHER_MAX_BARS];
    int8_t tempC[AppState::WEATHER_MAX_BARS];
    int8_t rainPct[AppState::WEATHER_MAX_BARS];
};

static void readWeatherDetail(WeatherDetail &d)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    d.valid = g_state.weatherValid && g_state.detailValid;
    d.maxC = g_state.tMaxC;
    d.minC = g_state.tMinC;
    d.humidityPct = g_state.humidityPct;
    d.windKmh = g_state.windKmh;
    d.windDirDeg = g_state.windDirDeg;
    d.barCount = g_state.barCount;
    for (uint8_t i = 0; i < d.barCount && i < AppState::WEATHER_MAX_BARS; i++)
    {
        d.hour[i] = g_state.barHour[i];
        d.tempC[i] = g_state.barTempC[i];
        d.rainPct[i] = g_state.barRainPct[i];
    }
    xSemaphoreGive(g_stateMutex);
}

// "45s", "12m", "3h", "2d" — compact age for the small screen.
static void fmtAge(char *out, size_t n, uint32_t ageMs)
{
    uint32_t s = ageMs / 1000;
    if (s < 60)
        snprintf(out, n, "%us", (unsigned)s);
    else if (s < 3600)
        snprintf(out, n, "%um", (unsigned)(s / 60));
    else if (s < 48UL * 3600)
        snprintf(out, n, "%uh", (unsigned)(s / 3600));
    else
        snprintf(out, n, "%ud", (unsigned)(s / 86400));
}

// Age of a millis() timestamp. Signed so a timestamp written by another task
// a moment after `now` was sampled reads as 0 instead of wrapping to ~49 days.
static uint32_t ageSince(uint32_t now, uint32_t stamp)
{
    int32_t d = (int32_t)(now - stamp);
    return d < 0 ? 0 : (uint32_t)d;
}

static uint16_t tempColor(float tempC)
{
    if (tempC < 5)
        return rgb(100, 170, 255); // cold
    if (tempC < 18)
        return rgb(110, 225, 220); // cool
    if (tempC < 28)
        return C_AMBER;       // mild
    return rgb(255, 120, 60); // hot
}

// =====================================================================
//  Small drawing primitives
// =====================================================================

static void drawHeart(TFT_eSPI &g, int16_t x, int16_t y, int16_t s, uint16_t c)
{
    int16_t r = imax(1, s / 2);
    g.fillCircle(x - r, y, r, c);
    g.fillCircle(x + r, y, r, c);
    g.fillTriangle(x - 2 * r, y + r / 2, x + 2 * r, y + r / 2, x, y + 3 * r, c);
}

// Eye body: vertical gradient with rounded corners, drawn row by row.
static void drawGradientEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, int16_t h,
                            int16_t corner, uint16_t cTop, uint16_t cBot)
{
    if (h < 2)
        h = 2;
    if (corner > h / 2)
        corner = h / 2;
    for (int16_t r = 0; r < h; r++)
    {
        float t = h > 1 ? (float)r / (float)(h - 1) : 0.0f;
        float inset = 0;
        if (r < corner)
        {
            float d = corner - r - 0.5f;
            inset = corner - sqrtf((float)corner * corner - d * d);
        }
        else if (r >= h - corner)
        {
            float d = r - (h - corner) + 0.5f;
            inset = corner - sqrtf((float)corner * corner - d * d);
        }
        int16_t ins = (int16_t)(inset + 0.5f);
        g.drawFastHLine(cx - w / 2 + ins, cy - h / 2 + r, w - 2 * ins, mixColor(cTop, cBot, t));
    }
}

// One glowing LED-style eye.
//  openness: 0..1 (blink)   lid: 0..1 flat top lid (sleepy)
//  brow: BROW_ANGRY (inner side lowered) / BROW_SAD (outer side lowered, i.e.
//        worried/sad), side = -1 left eye / +1 right eye
enum : uint8_t
{
    BROW_NONE = 0,
    BROW_ANGRY = 1,
    BROW_SAD = 2
};
static void drawEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, int16_t fullH,
                    float openness, float lid, uint8_t brow, int8_t side,
                    uint16_t cTop, uint16_t cBot, bool shine)
{
    int16_t h = (int16_t)(fullH * openness);
    if (h < 3)
        h = 3;
    int16_t corner = (int16_t)(w * 0.36f);
    int16_t top = cy - h / 2;

    // soft glow halo (two dim rounded rects behind the eye)
    if (h > w / 2)
    {
        g.fillRoundRect(cx - w / 2 - u(4), top - u(4), w + u(8), h + u(8), corner + u(4), tint(cTop, 0.10f));
        g.fillRoundRect(cx - w / 2 - u(2), top - u(2), w + u(4), h + u(4), corner + u(2), tint(cTop, 0.22f));
    }

    drawGradientEye(g, cx, cy, w, h, corner, cTop, cBot);

    // heavy lid (sleepy): flat top, covers part of the eye + halo
    if (lid > 0.02f)
    {
        int16_t lh = (int16_t)(h * lid);
        g.fillRect(cx - w / 2 - u(6), top - u(6), w + u(12), lh + u(6), C_BG);
    }

    // angry brow: a slanted cut, lower on the inner side
    if (brow == BROW_ANGRY)
    {
        int16_t outerX = cx + side * (w / 2 + u(5));
        int16_t innerX = cx - side * (w / 2 + u(5));
        int16_t y0 = top - u(6);
        g.fillTriangle(outerX, y0, innerX, y0, innerX, top + (int16_t)(h * 0.55f), C_BG);
    }
    else if (brow == BROW_SAD) // mirror image: lower on the OUTER side
    {
        int16_t outerX = cx + side * (w / 2 + u(5));
        int16_t innerX = cx - side * (w / 2 + u(5));
        int16_t y0 = top - u(6);
        g.fillTriangle(innerX, y0, outerX, y0, outerX, top + (int16_t)(h * 0.50f), C_BG);
    }

    // glossy highlights (kept below a heavy lid)
    int16_t lidPx = lid > 0.02f ? (int16_t)(h * lid) : 0;
    int16_t visH = h - lidPx;
    if (shine && visH > w / 3)
    {
        int16_t sr = imax(2, w / 6);
        g.fillCircle(cx - w / 4, top + lidPx + visH / 4 + sr / 2, sr, C_WHITE);
        g.fillCircle(cx + w / 5, top + h - h / 5, imax(1, sr / 2), mixColor(C_EYE_TOP, C_WHITE, 0.6f));
    }
}

// Happy "^ ^" eye drawn as a thick arch, plus blush.
static void drawHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
{
    float rx = R * 0.95f, ry = R * 0.9f;
    int16_t th = imax(2, (int16_t)(R * 0.28f));
    int16_t baseY = cy + R / 3;
    for (int a = 0; a <= 180; a += 8)
    {
        float t = a * (float)M_PI / 180.0f;
        int16_t x = cx + (int16_t)(rx * cosf(t));
        int16_t y = baseY - (int16_t)(ry * sinf(t));
        g.fillCircle(x, y, th, mixColor(C_EYE_TOP, C_EYE_BOT, 0.35f + 0.4f * (1.0f - sinf(t))));
    }
    g.fillEllipse(cx, cy + R + u(2), R / 2 + u(1), imax(2, R / 5), C_CHEEK);
}

// =====================================================================
//  Face
// =====================================================================

// What the face background should show (filled in by display_tick).
struct SceneInfo
{
    bool timeKnown;    // clock has a usable time (else: plain night sky, as at boot)
    bool day;          // sun is up (real sunrise..sunset, see sun.h)
    bool weatherKnown; // a recent-enough weather reading exists
    int code;          // Open-Meteo WMO weather code (valid if weatherKnown)
    float tempC;       // current temperature (valid if weatherKnown)
    float moonPhase;   // 0 new .. 0.5 full .. 1 new; 0.25 = right half lit (see moon_phase_view)
};

// How the eyes react to the situation (steady while it lasts).
enum class Reaction : uint8_t
{
    NONE,
    WORRIED, // WiFi has been gone for a while: worried brows, darting glances, sweat drop
    STORM,   // thunderstorm: worried brows, startled by every flash
    SAD,     // rain / drizzle: sad brows, looking down, a tear now and then
    HOT,     // heavy lids + sweat drop
    COLD     // shivering
};

static Reaction pickReaction(const SceneInfo &scn, bool offline, bool sleepy)
{
    if (!EXPR_ENABLED || sleepy) // sleepy eyes at night stay sleepy
        return Reaction::NONE;
    if (offline)
        return Reaction::WORRIED;
    if (!scn.weatherKnown)
        return Reaction::NONE;
    if (scn.code >= 95)
        return Reaction::STORM;
    if ((scn.code >= 51 && scn.code <= 67) || (scn.code >= 80 && scn.code <= 82))
        return Reaction::SAD;
    if (scn.tempC >= REACT_HOT_C)
        return Reaction::HOT;
    if (scn.tempC <= REACT_COLD_C)
        return Reaction::COLD;
    return Reaction::NONE;
}

// Short one-off idle expressions, picked at random.
enum class Expr : uint8_t
{
    NONE,
    WINK,
    YAWN,
    SURPRISE
};
static Expr exprCur = Expr::NONE;
static uint32_t exprStartMs = 0, exprDurMs = 0, nextExprAtMs = 0;

// Returns the progress 0..1 of the running expression (0 when none).
static float updateExpression(uint32_t now, bool can, bool sleepy)
{
    if (!EXPR_ENABLED || !can)
    {
        exprCur = Expr::NONE;
        if (nextExprAtMs <= now)
            nextExprAtMs = now + random(4000, 10000); // cool-down after an alarm / pet / worry
        return 0;
    }
    if (exprCur != Expr::NONE)
    {
        uint32_t el = now - exprStartMs;
        if (el >= exprDurMs)
        {
            exprCur = Expr::NONE;
            nextExprAtMs = now + random(EXPR_MIN_GAP_MS, EXPR_MAX_GAP_MS);
            return 0;
        }
        return el / (float)exprDurMs;
    }
    if (nextExprAtMs == 0)
        nextExprAtMs = now + random(8000, 16000);
    if (now >= nextExprAtMs)
    {
        long r = random(0, 100);
        if (sleepy)
            exprCur = r < 80 ? Expr::YAWN : Expr::WINK;
        else
            exprCur = r < 50 ? Expr::WINK : (r < 70 ? Expr::SURPRISE : Expr::YAWN);
        exprStartMs = now;
        exprDurMs = exprCur == Expr::WINK ? 450 : (exprCur == Expr::YAWN ? 2200 : 900);
    }
    return 0;
}

// True around the two lightning flashes of drawLightningBg (same 6.5 s cycle).
static bool lightningActive(uint32_t now) { return (now % 6500) < 260; }

static void drawSceneBackground(TFT_eSPI &g, uint32_t now, const SceneInfo &scn);

// twinkling stars
static void drawStars(TFT_eSPI &g, uint32_t now)
{
    for (uint8_t i = 0; i < 12; i++)
    {
        int16_t sx = (i * 47 + 13) % W;
        int16_t sy = (i * 31 + 5) % faceH;
        int v = 70 + (int)(60.0f * sinf(now / 520.0f + i * 1.7f));
        if (v < 15)
            v = 15;
        uint16_t c = rgb(v, v, imin(255, v + 45));
        g.drawPixel(sx, sy, c);
        if (i % 4 == 0)
        {
            g.drawPixel(sx - 1, sy, dimColor(c, 150));
            g.drawPixel(sx + 1, sy, dimColor(c, 150));
            g.drawPixel(sx, sy - 1, dimColor(c, 150));
            g.drawPixel(sx, sy + 1, dimColor(c, 150));
        }
    }
}

static float updateBlink(uint32_t now)
{
    float openness = 1.0f;
    switch (blinkPhase)
    {
    case BlinkPhase::OPEN:
        if (nextBlinkAtMs == 0)
            nextBlinkAtMs = now + random(3000, 6000);
        if (now >= nextBlinkAtMs)
        {
            blinkPhase = BlinkPhase::CLOSING;
            blinkPhaseStartMs = now;
        }
        break;
    case BlinkPhase::CLOSING:
    {
        float t = (now - blinkPhaseStartMs) / (float)BLINK_TRANSITION_MS;
        openness = 1.0f - (t < 1.0f ? t : 1.0f);
        if (t >= 1.0f)
        {
            blinkPhase = BlinkPhase::CLOSED;
            blinkPhaseStartMs = now;
        }
        break;
    }
    case BlinkPhase::CLOSED:
        openness = 0.05f;
        if (now - blinkPhaseStartMs > BLINK_CLOSED_MS)
        {
            blinkPhase = BlinkPhase::OPENING;
            blinkPhaseStartMs = now;
        }
        break;
    case BlinkPhase::OPENING:
    {
        float t = (now - blinkPhaseStartMs) / (float)BLINK_TRANSITION_MS;
        openness = (t < 1.0f ? t : 1.0f);
        if (t >= 1.0f)
        {
            blinkPhase = BlinkPhase::OPEN;
            nextBlinkAtMs = 0;
        }
        break;
    }
    }
    return openness;
}

static void updateLook(uint32_t now, bool alarm, bool sleepy, Reaction reaction)
{
    if (sleepy)
    {
        lookTX = 0;
        lookTY = 0.6f;
    }
    else if (alarm)
    {
        lookTX = 0;
        lookTY = 0;
    }
    else if (now < glanceUntilMs)
    {
        lookTX = 0;
        lookTY = 1.0f; // glance down at the panel after a page change
    }
    else if (reaction == Reaction::WORRIED)
    {
        lookTX = ((now / 800) & 1) ? 1.0f : -1.0f; // nervously looks left and right
        lookTY = 0;
    }
    else if (reaction == Reaction::SAD || reaction == Reaction::STORM)
    {
        lookTX = 0;
        lookTY = 0.8f;
    }
    else if (now >= nextLookAtMs)
    {
        nextLookAtMs = now + random(1400, 4200);
        long r = random(0, 10);
        lookTX = 0;
        lookTY = 0;
        if (r == 4)
            lookTX = -1;
        else if (r == 5)
            lookTX = 1;
        else if (r == 6)
        {
            lookTX = -1;
            lookTY = -0.8f;
        }
        else if (r == 7)
        {
            lookTX = 1;
            lookTY = -0.8f;
        }
        else if (r == 8)
            lookTY = -1;
    }
    lookX += (lookTX - lookX) * 0.30f;
    lookY += (lookTY - lookY) * 0.30f;
}

static void drawDrop(TFT_eSPI &g, int16_t x, int16_t y, int16_t r, uint16_t c)
{
    g.fillCircle(x, y, r, c);
    g.fillTriangle(x, y - 2 * r - r / 2, x - r, y - r / 2, x + r, y - r / 2, c);
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// "z Z Z" drifting up from the sleepy face (also used by the sleepy pet reaction).
static void drawSleepZ(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t R)
{
    g.setTextDatum(MC_DATUM);
    for (uint8_t k = 0; k < 3; k++)
    {
        float t = (float)((now / 35 + k * 40) % 120) / 120.0f;
        int16_t zx = (int16_t)(W * 0.70f) + (int16_t)(t * u(16)) + k * u(3);
        int16_t zy = (int16_t)(cy - R - t * (cy - R - u(6)));
        g.setTextColor(tint(C_ICE, (230 - t * 200) / 255.0f));
        g.drawString(k == 0 ? "z" : "Z", zx, zy, 2);
    }
}

// =====================================================================
//  Pet reactions (what pet.cpp picked; see PetKind)
// =====================================================================

// Small hearts rising from above the eyes. `age` = ms since they started; heart i starts
// i*stepMs later and lives lifeMs.
static void drawRisingHearts(TFT_eSPI &g, uint32_t age, int16_t cy, int16_t R, uint8_t n,
                             uint16_t stepMs, uint16_t lifeMs, int16_t size)
{
    for (uint8_t i = 0; i < n; i++)
    {
        int32_t a = (int32_t)age - (int32_t)i * stepMs;
        if (a < 0 || a > lifeMs)
            continue;
        float p = a / (float)lifeMs;
        float frac = fmodf(0.30f + i * 0.382f, 1.0f); // spread the hearts without a pattern
        int16_t hx = (int16_t)(W * (0.20f + 0.60f * frac)) + (int16_t)(sinf(p * 6.0f + i) * u(4));
        int16_t hy = (int16_t)(cy - R - u(2) - p * (cy - R - u(8)));
        drawHeart(g, hx, hy, size, tint(C_HEART, (255 - p * 140) / 255.0f));
    }
}

// A heart of total width 2*s centred on (cx, cyc) (drawHeart() takes the top-lobe line).
static void drawHeartCentered(TFT_eSPI &g, int16_t cx, int16_t cyc, int16_t s, uint16_t c)
{
    drawHeart(g, cx, cyc - imax(1, s / 2), s, c);
}

// Closed, contented eye "‿" (sleepy pet, purring): a thin arch curving downwards + blush.
static void drawContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
{
    float rx = R * 0.9f, ry = R * 0.5f;
    int16_t th = imax(2, (int16_t)(R * 0.22f));
    int16_t baseY = cy - R / 6;
    for (int a = 0; a <= 180; a += 10)
    {
        float t = a * (float)M_PI / 180.0f;
        int16_t x = cx + (int16_t)(rx * cosf(t));
        int16_t y = baseY + (int16_t)(ry * sinf(t));
        g.fillCircle(x, y, th, mixColor(C_EYE_TOP, C_EYE_BOT, 0.35f + 0.4f * sinf(t)));
    }
    g.fillEllipse(cx, cy + R + u(2), R / 2 + u(1), imax(2, R / 5), C_CHEEK);
}

// Heart used as an eye: pulsing, with a glow and a highlight.
static void drawHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, uint32_t age)
{
    float pulse = 1.0f + 0.10f * sinf(age / 85.0f);
    int16_t s = (int16_t)(R * 1.55f * pulse);
    drawHeartCentered(g, cx, cy, s + u(5), tint(C_HEART, 0.28f)); // glow
    drawHeartCentered(g, cx, cy, s, C_HEART);
    int16_t r = imax(1, s / 2);
    g.fillCircle(cx - r, cy - r, imax(1, r / 3), C_WHITE); // highlight on the left lobe
}

// Spiral for the dizzy eyes.
static void drawSpiral(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase)
{
    const int steps = 36;
    int16_t dot = imax(1, (int16_t)(R * 0.16f));
    for (int i = 0; i <= steps; i++)
    {
        float f = (float)i / steps;
        float ang = f * 2.5f * 2.0f * (float)M_PI + phase;
        float rr = R * 1.05f * f;
        g.fillCircle(cx + (int16_t)(rr * cosf(ang)), cy + (int16_t)(rr * sinf(ang)), dot,
                     mixColor(C_EYE_TOP, C_EYE_BOT, f));
    }
}

// Raised eyebrow: an arch above the eye, `lift` px higher than its resting place.
static void drawRaisedBrow(TFT_eSPI &g, int16_t cx, int16_t eyeTop, int16_t w, int16_t lift)
{
    int16_t th = imax(2, u(3));
    uint16_t col = mixColor(C_EYE_TOP, C_EYE_BOT, 0.15f);
    for (int i = 0; i <= 10; i++)
    {
        float t = i / 10.0f;
        int16_t x = cx - w / 2 + (int16_t)(t * w);
        int16_t y = eyeTop - u(7) - lift - (int16_t)(sinf(t * (float)M_PI) * u(4));
        g.fillCircle(x, y, th / 2 + 1, col);
    }
}

static void drawPetFace(TFT_eSPI &g, uint32_t now, bool purr, bool sleepy, int16_t cy, int16_t lcx,
                        int16_t rcx, int16_t R, int16_t eyeW, int16_t eyeH)
{
    uint32_t age = now - happyStartMs;
    uint16_t dropCol = rgb(120, 200, 255);

    if (purr) // button held: purring
    {
        int16_t j = ((now / 45) & 1) ? 1 : -1; // tiny vibration
        drawContentEye(g, lcx + j, cy, R);
        drawContentEye(g, rcx + j, cy, R);
        drawRisingHearts(g, now % 1800, cy, R, 2, 700, 1100, u(3));
        if (sleepy)
            drawSleepZ(g, now, cy, R);
        return;
    }

    switch (petEv.kind)
    {
    case PetKind::HEART_EYES:
        drawHeartEye(g, lcx, cy, R, age);
        drawHeartEye(g, rcx, cy, R, age);
        drawRisingHearts(g, age, cy, R, 6, 160, 1100, u(4));
        break;

    case PetKind::SLEEPY:
        drawContentEye(g, lcx, cy, R);
        drawContentEye(g, rcx, cy, R);
        drawRisingHearts(g, age, cy, R, 1, 0, 1800, u(3));
        drawSleepZ(g, now, cy, R);
        break;

    case PetKind::RAISED_BROW:
        if (age < 1100) // "rain? really?": one wide eye with a raised brow, the other squints
        {
            float e = clamp01(age / 250.0f);
            int16_t ew = (int16_t)(eyeW * 1.1f), eh = (int16_t)(eyeH * 1.12f);
            drawEye(g, lcx, cy, ew, eh, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawEye(g, rcx, cy + u(1), eyeW, eyeH, 1.0f, 0.38f, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
            drawRaisedBrow(g, lcx, cy - eh / 2, ew, (int16_t)(e * u(4)));
            float pd = (age % 700) / 700.0f; // a raindrop falls past
            drawDrop(g, (int16_t)(W * 0.84f), (int16_t)(u(6) + pd * cy), imax(2, u(2.5f)), tint(dropCol, 0.8f));
        }
        else // ... and then it smiles anyway
        {
            drawHappyEye(g, lcx, cy, R);
            drawHappyEye(g, rcx, cy, R);
            drawRisingHearts(g, age - 1100, cy, R, 1, 0, 1100, u(4));
        }
        break;

    case PetKind::DIZZY:
    {
        float ph = age / 90.0f;
        int16_t sway = (int16_t)(sinf(age / 140.0f) * u(3));
        drawSpiral(g, lcx + sway, cy, R, ph);
        drawSpiral(g, rcx + sway, cy, R, -ph);
        for (uint8_t k = 0; k < 3; k++) // little stars circling the head
        {
            float a = age / 200.0f + k * 2.094f;
            int16_t sx = W / 2 + (int16_t)(cosf(a) * R * 2.4f);
            int16_t sy = cy - R - u(8) + (int16_t)(sinf(a) * u(4));
            g.fillCircle(sx, sy, imax(1, u(2)), C_AMBER);
        }
        break;
    }

    case PetKind::ANNOYED:
    {
        int16_t shake = ((now / 50) & 1) ? u(1) : -u(1);
        uint16_t top = mixColor(C_EYE_TOP, C_ALARM_TOP, 0.5f), bot = mixColor(C_EYE_BOT, C_ALARM_BOT, 0.5f);
        drawEye(g, lcx + shake, cy, eyeW, eyeH, 0.7f, 0, BROW_ANGRY, -1, top, bot, false);
        drawEye(g, rcx + shake, cy, eyeW, eyeH, 0.7f, 0, BROW_ANGRY, 1, top, bot, false);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_RED);
        g.drawString("#", W - u(22), cy - R - u(6), 4); // anger mark
        break;
    }

    case PetKind::MISSED_YOU:
        if (age < 700) // wide eyes: "you're back!"
        {
            float s = 1.0f + 0.3f * clamp01(age / 250.0f);
            int16_t ew = (int16_t)(eyeW * s), eh = (int16_t)(eyeH * s);
            drawEye(g, lcx, cy, ew, eh, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawEye(g, rcx, cy, ew, eh, 1.0f, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
        }
        else // happy hops + heart burst
        {
            float q = clamp01((age - 700) / 1500.0f);
            int16_t dy = -(int16_t)(fabsf(sinf((age - 700) / 130.0f)) * u(5) * (1.0f - q));
            drawHappyEye(g, lcx, cy + dy, R);
            drawHappyEye(g, rcx, cy + dy, R);
            drawRisingHearts(g, age - 700, cy, R, 5, 170, 1100, u(4));
        }
        break;

    case PetKind::MORNING:
        if (age < 1500) // yawn
        {
            float env = sinf(clamp01(age / 1500.0f) * (float)M_PI);
            float open = 1.0f - 0.92f * env;
            drawEye(g, lcx, cy, eyeW, eyeH, open, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawEye(g, rcx, cy, eyeW, eyeH, open, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
            int16_t my = cy + R + u(10);
            int16_t rx = u(2) + (int16_t)(u(7) * env), ry = u(2) + (int16_t)(u(6) * env);
            g.fillEllipse(W / 2, my, rx, ry, dimColor(C_HEART, 200));
            if (rx > 3 && ry > 3)
                g.fillEllipse(W / 2, my, rx - 2, ry - 2, rgb(40, 6, 22));
        }
        else // ... then a happy good morning
        {
            drawHappyEye(g, lcx, cy, R);
            drawHappyEye(g, rcx, cy, R);
            drawRisingHearts(g, age - 1500, cy, R, 2, 260, 1100, u(4));
        }
        break;

    case PetKind::RARE_WINK:
        drawEye(g, lcx, cy, eyeW, eyeH, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
        drawHappyEye(g, rcx, cy, R);                                              // winking eye
        g.fillRoundRect(W / 2 - u(5), cy + R + u(6), u(10), u(9), u(4), C_CHEEK); // tongue
        g.drawFastVLine(W / 2, cy + R + u(6), u(6), dimColor(C_CHEEK, 160));
        drawRisingHearts(g, age, cy, R, 2, 250, 1100, u(4));
        break;

    case PetKind::RARE_SURPRISE:
        if (age < 800)
        {
            float s = 1.0f + 0.3f * clamp01(age / 200.0f);
            int16_t ew = (int16_t)(eyeW * s), eh = (int16_t)(eyeH * s);
            drawEye(g, lcx, cy, ew, eh, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawEye(g, rcx, cy, ew, eh, 1.0f, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_AMBER);
            g.drawString("!", W / 2, cy - u(2), 4);
        }
        else // a big heart pops up between the happy eyes
        {
            drawHappyEye(g, lcx, cy, R);
            drawHappyEye(g, rcx, cy, R);
            float pop = clamp01((age - 800) / 200.0f);
            float pulse = 1.0f + 0.12f * sinf(age / 90.0f);
            drawHeartCentered(g, W / 2, cy - R * 2, (int16_t)(u(8) * pop * pulse) + 1, C_HEART);
        }
        break;

    case PetKind::HOT:
    {
        drawHappyEye(g, lcx, cy, R);
        drawHappyEye(g, rcx, cy, R);
        float pd = (age % 1400) / 1400.0f; // sweat drop, but still happy
        drawDrop(g, lcx - eyeW / 2 - u(9), (int16_t)(cy - R + pd * R * 2.2f), imax(2, u(3)),
                 tint(dropCol, (255 - pd * 140) / 255.0f));
        drawRisingHearts(g, age, cy, R, 1, 0, 1100, u(4));
        break;
    }

    case PetKind::COLD:
    {
        int16_t sh = (((now / 40) & 1) ? 1 : -1) * u(1); // shiver
        drawHappyEye(g, lcx + sh, cy, R);
        drawHappyEye(g, rcx + sh, cy, R);
        for (uint8_t k = 0; k < 4; k++) // snowflakes drifting down
        {
            int16_t sx = (int16_t)(W * (0.15f + 0.2f * k)) + (int16_t)(sinf(age / 300.0f + k) * u(3));
            int16_t sy = (int16_t)((age / 9 + k * 23) % faceH);
            g.fillCircle(sx, sy, imax(1, u(1.5f)), C_ICE);
        }
        drawRisingHearts(g, age, cy, R, 2, 300, 1100, u(4));
        break;
    }

    case PetKind::HAPPY:
    default:
        drawHappyEye(g, lcx, cy, R);
        drawHappyEye(g, rcx, cy, R);
        drawRisingHearts(g, age, cy, R, 3, 220, 1100, u(4));
        break;
    }
}

static void drawFace(TFT_eSPI &g, uint32_t now, bool alarm, bool happy, bool purr, bool sleepy, float blinkOpen,
                     const SceneInfo &scn, Reaction reaction, float exprP)
{
    g.fillRect(0, 0, W, faceH, C_BG);

    // Background: weather/time-of-day scene. The alarm keeps the plain starry
    // look so nothing distracts from the alert.
    if (SCENE_ENABLED && !alarm)
        drawSceneBackground(g, now, scn);
    else
        drawStars(g, now);

    int16_t R = W / 8; // same base size as before
    int16_t eyeW = R * 2;
    int16_t eyeH = (int16_t)(R * 2.25f);
    int16_t cy = (int16_t)(faceH * 0.55f) + (int16_t)(sinf(now / 850.0f) * u(1.5f)); // gentle "breathing"
    int16_t lcx = W / 2 - R * 2;
    int16_t rcx = W / 2 + R * 2;

    if (alarm)
    {
        int16_t shake = alarmFlashOn ? u(2) : -u(2);
        float open = alarmFlashOn ? 1.0f : 0.3f;
        drawEye(g, lcx + shake, cy, eyeW, eyeH, open, 0, BROW_ANGRY, -1, C_ALARM_TOP, C_ALARM_BOT, false);
        drawEye(g, rcx + shake, cy, eyeW, eyeH, open, 0, BROW_ANGRY, 1, C_ALARM_TOP, C_ALARM_BOT, false);
        if (alarmFlashOn)
        {
            g.drawRect(0, 0, W, faceH, C_RED);
            g.drawRect(1, 1, W - 2, faceH - 2, C_RED);
        }
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_RED);
        g.drawString("!", W / 2, cy, 4);
        return;
    }

    if (happy)
    {
        drawPetFace(g, now, purr, sleepy, cy, lcx, rcx, R, eyeW, eyeH);
        return;
    }

    // normal / sleepy eyes
    int16_t ox = (int16_t)(lookX * u(5));
    int16_t oy = (int16_t)(lookY * u(4));
    float open = blinkOpen;
    float lid = 0;
    float eScale = 1.0f; // >1: eyes widen (startled / surprised)
    uint8_t brow = BROW_NONE;
    if (sleepy)
    {
        lid = 0.5f;
        open = blinkOpen; // still blinks, slowly
    }

    // situation reactions (steady while the situation lasts)
    switch (reaction)
    {
    case Reaction::WORRIED:
        brow = BROW_SAD;
        break;
    case Reaction::STORM:
        brow = BROW_SAD;
        if (lightningActive(now))
            eScale = 1.25f; // flinch at every flash
        break;
    case Reaction::SAD:
        brow = BROW_SAD;
        break;
    case Reaction::HOT:
        lid = 0.28f;
        break;
    case Reaction::COLD:
        ox += (((now / 40) & 1) ? 1 : -1) * u(1); // shiver
        lid = 0.15f;
        break;
    default:
        break;
    }

    // one-off idle expressions
    Expr ex = exprCur;
    if (ex == Expr::YAWN)
    {
        float edge = (exprP < 1.0f - exprP ? exprP : 1.0f - exprP) / 0.15f; // ease in/out
        if (edge > 1.0f)
            edge = 1.0f;
        open *= 1.0f - 0.92f * edge; // eyes squeeze shut mid-yawn
    }
    else if (ex == Expr::SURPRISE)
    {
        float env = exprP < 0.15f ? exprP / 0.15f : (exprP > 0.8f ? (1.0f - exprP) / 0.2f : 1.0f);
        eScale = 1.0f + 0.28f * env;
    }

    int16_t ew = (int16_t)(eyeW * eScale), eh = (int16_t)(eyeH * eScale);
    drawEye(g, lcx + ox, cy + oy, ew, eh, open, lid, brow, -1, C_EYE_TOP, C_EYE_BOT, true);
    if (ex == Expr::WINK)
        drawHappyEye(g, rcx + ox, cy + oy, R); // winking eye = a happy arch
    else
        drawEye(g, rcx + ox, cy + oy, ew, eh, open, lid, brow, 1, C_EYE_TOP, C_EYE_BOT, true);

    // extras
    uint16_t dropCol = rgb(120, 200, 255);
    if (reaction == Reaction::WORRIED || reaction == Reaction::HOT)
    {
        // sweat drop sliding down the left side of the face
        float pd = (now % 2600) / 2600.0f;
        int16_t sx = lcx - eyeW / 2 - u(9);
        int16_t sy = (int16_t)(cy - R + pd * R * 2.2f);
        drawDrop(g, sx, sy, imax(2, u(3)), tint(dropCol, (255 - pd * 140) / 255.0f));
    }
    if (reaction == Reaction::SAD)
    {
        // a tear rolls from the lower edge of the left eye now and then
        float pt = (now % 3600) / 3600.0f;
        if (pt < 0.6f)
        {
            float q = pt / 0.6f;
            int16_t tx = lcx - eyeW / 4;
            int16_t ty = (int16_t)(cy + eyeH / 2 + u(2) + q * R * 1.3f);
            drawDrop(g, tx, ty, imax(2, u(2.5f)), tint(dropCol, (255 - q * 150) / 255.0f));
        }
    }
    if (ex == Expr::SURPRISE)
    {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_AMBER);
        g.drawString("!", W / 2, cy - u(2), 4);
    }
    if (ex == Expr::YAWN)
    {
        float s = sinf(exprP * (float)M_PI); // mouth opens, then closes
        int16_t my = cy + R + u(10);
        int16_t rx = u(2) + (int16_t)(u(7) * s), ry = u(2) + (int16_t)(u(6) * s);
        g.fillEllipse(W / 2, my, rx, ry, dimColor(C_HEART, 200));
        if (rx > 3 && ry > 3)
            g.fillEllipse(W / 2, my, rx - 2, ry - 2, rgb(40, 6, 22));
    }

    if (sleepy)
        drawSleepZ(g, now, cy, R);
}

// =====================================================================
//  Info panel (clock / date / weather)
// =====================================================================

// 7-segment digit. d = 0..9, 10 = minus sign, <0 = blank.
static const uint8_t SEG_MASK[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40};
struct Seg
{
    int16_t x, y, w, h;
};

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
        for (uint8_t i = 0; i < 7; i++)
            if ((m >> i) & 1)
                g.fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, t / 2, on);
}

static void drawSegDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int8_t d,
                         uint16_t on, uint16_t off)
{
    drawSegDigitEx(g, x, y, w, h, d, on, off, SEG_ALL);
}

// Colon: two dots with a soft halo. level 0..15 = brightness (it "breathes").
static void drawColonGlow(TFT_eSPI &g, int16_t cx, int16_t y, int16_t h, int16_t t, uint16_t on, uint8_t level)
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
    }
}

// 0..15, follows a smooth sine "breath" (or the old on/off blink if the pulse is disabled)
static uint8_t colonLevelNow(uint32_t now)
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
static TFT_eSprite digSpr(&tft);
static bool digSprOk = false;
static const int16_t DIG_PAD = 1;
static int8_t digCur[4] = {-1, -1, -1, -1};    // digit currently shown (-1 = nothing yet)
static int8_t digPrev[4] = {-1, -1, -1, -1};   // digit it is replacing
static uint32_t digChangeMs[4] = {0, 0, 0, 0}; // when it changed; 0 = not animating

static void clockMetrics(int16_t &dw, int16_t &dh)
{
    dh = (int16_t)(infoH * 0.42f);
    dw = (int16_t)(dh * 0.56f);
}

static void drawAnimDigit(int16_t x, int16_t y, int16_t dw, int16_t dh, int8_t oldD, int8_t newD,
                          float p, uint16_t on)
{
    float inv = 1.0f - p;
    float e = 1.0f - inv * inv * inv; // ease-out: quick start, soft landing
    int16_t travel = dh + 2 * DIG_PAD + imax(2, u(2));
    int16_t off = (int16_t)(e * travel);

    digSpr.fillSprite(C_BG);
    drawSegDigitEx(digSpr, DIG_PAD, DIG_PAD, dw, dh, -1, on, C_GHOST, SEG_GHOST); // unlit backdrop stays
    drawSegDigitEx(digSpr, DIG_PAD, DIG_PAD - off, dw, dh, oldD, mixColor(on, C_BG, e), C_GHOST, SEG_LIT);
    drawSegDigitEx(digSpr, DIG_PAD, DIG_PAD + travel - off, dw, dh, newD, mixColor(C_BG, on, e), C_GHOST, SEG_LIT);
    digSpr.pushToSprite(&infoCv.spr, x - DIG_PAD, y - DIG_PAD);
}

static void drawSpinner(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
{
    uint8_t head = (millis() / 90) % 8;
    for (uint8_t i = 0; i < 8; i++)
    {
        float a = i * ((float)M_PI / 4.0f);
        uint8_t age = (uint8_t)((head - i) & 7);
        g.fillCircle(cx + (int16_t)(r * cosf(a)), cy + (int16_t)(r * sinf(a)),
                     imax(1, u(2)), tint(c, (255 - age * 30) / 255.0f));
    }
}

static void drawCloud(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
{
    g.fillCircle(cx - r * 0.6f, cy + r * 0.2f, r * 0.40f, c);
    g.fillCircle(cx - r * 0.1f, cy - r * 0.15f, r * 0.55f, c);
    g.fillCircle(cx + r * 0.55f, cy + r * 0.15f, r * 0.42f, c);
    g.fillRect(cx - r * 0.6f, cy + r * 0.2f, r * 1.15f, r * 0.4f, c);
}

static void drawSun(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint32_t now, bool withRays)
{
    uint16_t sun = rgb(255, 200, 40);
    g.fillCircle(cx, cy, r * 0.62f, dimColor(sun, 90));
    g.fillCircle(cx, cy, r * 0.5f, sun);
    if (!withRays)
        return;
    float rot = now / 900.0f;
    for (uint8_t i = 0; i < 8; i++)
    {
        float a = rot + i * ((float)M_PI / 4.0f);
        int16_t x0 = cx + (int16_t)(r * 0.72f * cosf(a)), y0 = cy + (int16_t)(r * 0.72f * sinf(a));
        int16_t x1 = cx + (int16_t)(r * 1.0f * cosf(a)), y1 = cy + (int16_t)(r * 1.0f * sinf(a));
        g.drawLine(x0, y0, x1, y1, sun);
        g.drawLine(x0 + 1, y0, x1 + 1, y1, sun);
    }
}

// =====================================================================
//  Face background scene (weather + time of day)
// =====================================================================
// All pieces are dim, procedural (derived from `now`, no per-drop state) and
// drawn BEFORE the eyes, so the eyes always stay on top and in front.

static void drawDimSun(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint32_t now)
{
    uint16_t sun = rgb(255, 200, 40);
    g.fillCircle(cx, cy, r + u(3), tint(sun, 0.14f));
    g.fillCircle(cx, cy, r, dimColor(sun, 200));
    float rot = now / 1400.0f;
    for (uint8_t i = 0; i < 8; i++)
    {
        float a = rot + i * ((float)M_PI / 4.0f);
        g.drawLine(cx + (int16_t)(r * 1.35f * cosf(a)), cy + (int16_t)(r * 1.35f * sinf(a)),
                   cx + (int16_t)(r * 1.8f * cosf(a)), cy + (int16_t)(r * 1.8f * sinf(a)),
                   dimColor(sun, 150));
    }
}

// Moon with its real phase. phase: 0 new, 0.25 first quarter (right half lit),
// 0.5 full, 0.75 last quarter (left half lit). The terminator is an ellipse, drawn
// row by row: lit part of each row runs from the terminator to the limb.
static void drawMoon(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, float phase)
{
    uint16_t lit = dimColor(rgb(225, 230, 255), 235);
#if !MOON_PHASE_ENABLED
    (void)phase;
    g.fillCircle(cx, cy, r, lit);
    g.fillCircle(cx + (int16_t)(r * 0.55f), cy - (int16_t)(r * 0.25f), (int16_t)(r * 0.85f), C_BG);
#else
    // the dark side is still faintly there (earthshine)
    g.fillCircle(cx, cy, r, tint(lit, 0.14f));

    float k = cosf(2.0f * (float)M_PI * phase); // 1 new .. 0 quarter .. -1 full
    bool waxing = phase < 0.5f;
    if ((1.0f - k) * 0.5f < 0.02f)
        return; // new moon: only the faint disc
    for (int16_t dy = -r; dy <= r; dy++)
    {
        float w = sqrtf((float)(r * r - dy * dy) + 0.25f);
        float t = k * w; // terminator x offset on this row
        int16_t x0, x1;
        if (waxing)
        {
            x0 = (int16_t)lroundf(t);
            x1 = (int16_t)lroundf(w);
        }
        else
        {
            x0 = (int16_t)lroundf(-w);
            x1 = (int16_t)lroundf(-t);
        }
        if (x1 >= x0)
            g.drawFastHLine(cx + x0, cy + dy, x1 - x0 + 1, lit);
    }
#endif
}

static void drawShootingStar(TFT_eSPI &g, uint32_t now)
{
    uint32_t ph = now % 11000;
    if (ph >= 700)
        return;
    float p = ph / 700.0f;
    int16_t hx = (int16_t)(W * 0.88f - p * W * 0.45f);
    int16_t hy = (int16_t)(faceH * 0.06f + p * faceH * 0.30f);
    uint8_t v = (uint8_t)(255.0f * (1.0f - p));
    g.drawLine(hx, hy, hx + u(14), hy - u(6), dimColor(rgb(200, 215, 255), v / 3));
    g.drawLine(hx, hy, hx + u(7), hy - u(3), dimColor(rgb(235, 240, 255), v / 2));
    g.drawPixel(hx, hy, dimColor(C_WHITE, v));
}

static void drawDriftClouds(TFT_eSPI &g, uint32_t now, uint8_t count, uint16_t color)
{
    for (uint8_t i = 0; i < count; i++)
    {
        int16_t r = u(9 + (i % 2) * 3);
        int16_t span = W + 4 * r;
        int16_t cx = (int16_t)(((now / (70u + i * 25u)) + (uint32_t)i * (span / count)) % span) - 2 * r;
        drawCloud(g, cx, u(10 + i * 11), r, color);
    }
}

static void drawRainBg(TFT_eSPI &g, uint32_t now, uint8_t n)
{
    uint16_t c = rgb(70, 140, 225);
    for (uint8_t i = 0; i < n; i++)
    {
        int16_t x = (i * 37 + 11) % W;
        int16_t y = (int16_t)(((now / (4u + i % 3)) + i * 53u) % (uint32_t)(faceH + 12)) - 6;
        g.drawLine(x, y, x - 1, y + imax(2, u(5)), c);
    }
}

static void drawSnowBg(TFT_eSPI &g, uint32_t now, uint8_t n)
{
    uint16_t c = dimColor(C_WHITE, 230);
    for (uint8_t i = 0; i < n; i++)
    {
        int16_t x = (i * 41 + 7) % W + (int16_t)(sinf(now / 700.0f + i) * u(3));
        int16_t y = (int16_t)(((now / (24u + (i % 3) * 8u)) + i * 29u) % (uint32_t)faceH);
        if (i % 3 == 0)
            g.fillCircle(x, y, imax(1, u(1.2f)), c);
        else
            g.drawPixel(x, y, c);
    }
}

static void drawFogBg(TFT_eSPI &g, uint32_t now)
{
    uint16_t c = tint(rgb(165, 180, 215), 0.33f);
    int16_t bw = u(70);
    for (uint8_t i = 0; i < 4; i++)
    {
        int16_t x = (int16_t)(((now / 45u) + i * 47u) % (uint32_t)(W + bw)) - bw;
        int16_t y = (int16_t)(faceH * (0.18f + 0.20f * i));
        g.fillRoundRect(x, y, bw, imax(3, u(7)), imax(2, u(3)), c);
    }
}

// Dim flash of the whole sky plus a bolt in the right margin: two quick
// flashes every ~6.5 s (never a rapid strobe).
static void drawLightningBg(TFT_eSPI &g, uint32_t now)
{
    uint32_t ph = now % 6500;
    if (!(ph < 60 || (ph > 150 && ph < 200)))
        return;
    g.fillRect(0, 0, W, faceH, tint(rgb(110, 130, 255), 0.18f));
    uint16_t bolt = dimColor(rgb(255, 230, 120), 200);
    int16_t x = (int16_t)(W * 0.88f);
    int16_t y1 = (int16_t)(faceH * 0.22f), y2 = (int16_t)(faceH * 0.50f);
    g.drawLine(x, 0, x - u(4), y1, bolt);
    g.drawLine(x - u(4), y1, x + u(1), y1, bolt);
    g.drawLine(x + u(1), y1, x - u(5), y2, bolt);
}

static void drawSceneBackground(TFT_eSPI &g, uint32_t now, const SceneInfo &scn)
{
    int code = scn.weatherKnown ? scn.code : -1;
    bool partly = (code == 1 || code == 2);
    bool clearish = !scn.weatherKnown || code == 0 || partly; // sky is (mostly) visible
    bool fog = (code == 45 || code == 48);
    bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool storm = code >= 95;
    bool overcast = scn.weatherKnown && !clearish && !fog && !rain && !snow && !storm; // code 3 etc.

    uint16_t cloudCol = tint(rgb(165, 180, 215), scn.day ? 0.55f : 0.41f);
    uint16_t darkCloud = tint(rgb(120, 130, 170), 0.59f);

    if (storm)
        drawLightningBg(g, now);

    // sky: stars at night when it's clear enough to see them (also: no time yet = boot look)
    if (!scn.day && clearish)
        drawStars(g, now);

    // sun / moon, only once the time is known
    if (scn.timeKnown && clearish)
    {
        if (scn.day)
            drawDimSun(g, u(16), u(16), u(7), now);
        else
        {
            drawMoon(g, u(16), u(16), u(8), scn.moonPhase);
            if (!partly)
                drawShootingStar(g, now);
        }
    }

    // weather layers
    if (clearish)
    {
        if (partly)
            drawDriftClouds(g, now, 2, cloudCol);
        else if (scn.day && scn.timeKnown)
            drawDriftClouds(g, now, 1, cloudCol); // a lone, faint cloud so a clear day isn't empty
    }
    else if (overcast)
        drawDriftClouds(g, now, 3, cloudCol);
    else if (fog)
        drawFogBg(g, now);
    else if (rain)
    {
        drawDriftClouds(g, now, 2, darkCloud);
        drawRainBg(g, now, 16);
    }
    else if (snow)
    {
        drawDriftClouds(g, now, 2, cloudCol);
        drawSnowBg(g, now, 18);
    }
    else if (storm)
    {
        drawDriftClouds(g, now, 3, darkCloud);
        drawRainBg(g, now, 20);
    }
}

static void drawWeatherIcon(TFT_eSPI &g, int code, int16_t cx, int16_t cy, int16_t r, uint32_t now)
{
    uint16_t lightCloud = rgb(215, 225, 245);
    uint16_t greyCloud = rgb(150, 165, 195);
    uint16_t darkCloud = rgb(95, 105, 140);
    uint16_t rain = rgb(80, 170, 255);

    if (code == 0)
    {
        drawSun(g, cx, cy, r, now, true);
    }
    else if (code == 1 || code == 2)
    {
        drawSun(g, cx - r * 0.35f, cy - r * 0.3f, r * 0.75f, now, false);
        drawCloud(g, cx + r * 0.15f, cy + r * 0.25f, r * 0.85f, lightCloud);
    }
    else if (code == 45 || code == 48)
    {
        for (int i = 0; i < 3; i++)
        {
            int16_t sway = (int16_t)(sinf(now / 600.0f + i * 1.3f) * r * 0.15f);
            g.fillRoundRect(cx - r * 0.8f + sway + i * r * 0.1f, cy - r * 0.5f + i * r * 0.5f,
                            r * 1.4f, imax(2, (int16_t)(r * 0.22f)), 2, greyCloud);
        }
    }
    else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
    {
        drawCloud(g, cx, cy - r * 0.3f, r * 0.9f, greyCloud);
        for (int i = 0; i < 3; i++)
        {
            int16_t dy = (int16_t)(((now / 90) + i * 3) % 6) * r * 0.1f;
            int16_t x = cx - r * 0.5f + i * r * 0.5f;
            int16_t y = cy + r * 0.45f + dy;
            g.drawLine(x, y, x - 2, y + r * 0.3f, rain);
        }
    }
    else if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    {
        drawCloud(g, cx, cy - r * 0.3f, r * 0.9f, greyCloud);
        for (int i = 0; i < 3; i++)
        {
            int16_t dy = (int16_t)(((now / 140) + i * 2) % 5) * r * 0.1f;
            int16_t x = cx - r * 0.5f + i * r * 0.5f + (int16_t)(sinf(now / 300.0f + i) * 2);
            g.fillCircle(x, cy + r * 0.5f + dy, imax(1, r / 9), C_WHITE);
        }
    }
    else if (code >= 95)
    {
        drawCloud(g, cx, cy - r * 0.3f, r * 0.9f, darkCloud);
        if ((now / 300) % 3 != 0)
        {
            uint16_t bolt = rgb(255, 220, 50);
            g.fillTriangle(cx + r * 0.2f, cy + r * 0.1f, cx - r * 0.2f, cy + r * 0.55f, cx + r * 0.05f, cy + r * 0.55f, bolt);
            g.fillTriangle(cx - r * 0.05f, cy + r * 0.5f, cx + r * 0.25f, cy + r * 0.5f, cx - r * 0.15f, cy + r * 1.0f, bolt);
        }
    }
    else
    {
        drawCloud(g, cx, cy, r, lightCloud);
    }
}

static const int MODE_COUNT = 3; // CLOCK, DATE, WEATHER (the DIAG page is opened by holding Mode, it has no dot)

static void drawModeDots(TFT_eSPI &g, DisplayMode shown, uint16_t accent, int16_t y)
{
    int idx = (int)shown;
    for (int i = 0; i < MODE_COUNT; i++)
    {
        int16_t x = W / 2 + (int16_t)((i - (MODE_COUNT - 1) / 2.0f) * u(10));
        if (i == idx)
            g.fillCircle(x, y, imax(2, u(2.5f)), accent);
        else
            g.fillCircle(x, y, imax(1, u(1.5f)), tint(accent, 0.27f));
    }
}

// Draws digits from a small int array, returns nothing. count digits wide.
static void drawSegNumber(TFT_eSPI &g, int16_t x, int16_t y, int16_t dw, int16_t dh, int16_t gap,
                          const int8_t *digits, uint8_t count, uint16_t on)
{
    for (uint8_t i = 0; i < count; i++)
        drawSegDigit(g, x + i * (dw + gap), y, dw, dh, digits[i], on, C_GHOST);
}

#if ALARM_ICON_ENABLED
// ---- Alarm badge (clock page, bottom-left): bell icon + the alarm time ----
// Shown only while the alarm is switched on in the settings. While it rings the
// bell turns red, flashes and its clapper swings. The setting is cached and
// re-read once a second (settings_get() takes a mutex + copies the struct).
static bool alarmCfgOn = false;
static int alarmCfgHour = 0, alarmCfgMin = 0;
static int alarmCfgDays = 0x7F; // weekday mask, bit0 = Sunday (same as Settings.alarmDays)
static uint32_t lastAlarmCfgMs = 0;

static void pollAlarmCfg(uint32_t now)
{
    if (lastAlarmCfgMs != 0 && now - lastAlarmCfgMs < 1000)
        return;
    lastAlarmCfgMs = now ? now : 1;
    Settings cfg = settings_get();
    alarmCfgOn = cfg.alarmEnabled;
    alarmCfgHour = cfg.alarmHour;
    alarmCfgMin = cfg.alarmMinute;
    alarmCfgDays = cfg.alarmDays;
}

// ---- Next scheduled ring ----
// Minutes from `ti` (local time, seconds ignored) until the next ring of an alarm set
// to hour:minute on the weekdays in `days` (bit0 = Sunday ... bit6 = Saturday, like
// tm_wday). Also returns how many calendar days ahead that is (0 = today, 1 = tomorrow ...)
// and its weekday. -1 = no weekday selected. An alarm time equal to or before now counts
// as already rung today. Plain calendar arithmetic: across a DST change it can be off by an hour.
static int nextAlarmIn(const struct tm &ti, int hour, int minute, int days, int &dayOffset, int &wday)
{
    int nowMin = ti.tm_hour * 60 + ti.tm_min;
    int alarmMin = hour * 60 + minute;
    for (int add = 0; add <= 7; add++)
    {
        int wd = (ti.tm_wday + add) % 7;
        if (!((days >> wd) & 1))
            continue;
        if (add == 0 && alarmMin <= nowMin)
            continue; // today's time has passed: look at the following days
        dayOffset = add;
        wday = wd;
        return add * 1440 + alarmMin - nowMin;
    }
    return -1;
}

static const char *const NEXT_ALARM_DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Small bell: dome + body + rim + clapper. (cx, cy) = middle of the body,
// r = dome radius, swing = clapper offset in px (0 = hanging straight).
static void drawBell(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, int16_t swing, uint16_t c)
{
    g.fillCircle(cx, cy - 1, r, c);                          // dome
    g.fillRect(cx - r, cy - 1, 2 * r + 1, r - 1, c);         // body
    g.fillRect(cx - r - 1, cy + r - 2, 2 * r + 3, 2, c);     // flared rim
    g.fillCircle(cx, cy - r - 1, imax(1, r / 3), c);         // top knob
    g.fillCircle(cx + swing, cy + r + 1, imax(1, r / 3), c); // clapper
}

static void drawAlarmBadge(TFT_eSPI &g, bool ringing, int16_t cy, const struct tm &ti)
{
    int16_t r = imax(3, u(3.2f));
    int16_t cx = u(9);
    uint16_t bellC = C_AMBER, textC = C_SOFT;
    int16_t swing = 0;
    if (ringing)
    {
        bellC = textC = alarmFlashOn ? C_RED : tint(C_RED, 0.35f);
        swing = alarmFlashOn ? imax(1, r / 2) : -imax(1, r / 2);
    }
    drawBell(g, cx, cy, r, swing, bellC);

    // Left of the page dots: the alarm time, as it always was (short, never reaches the dots).
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmCfgHour, alarmCfgMin);
    uint8_t font = sc >= 1.5f ? 2 : 1;
    g.setTextDatum(ML_DATUM);
    g.setTextColor(textC);
    g.drawString(buf, cx + r + u(4), cy + 1, font);

    // Right of the page dots, right-aligned: when it rings NEXT. Under 24 h it is the time
    // left ("6h12", "42m"), otherwise "Tmrw" or the weekday ("Sat"). Not while it rings.
    // Skipped if it would ever reach the dots (other fonts / screen sizes).
    if (!ringing)
    {
        int dayOff = 0, wd = 0;
        int mins = nextAlarmIn(ti, alarmCfgHour, alarmCfgMin, alarmCfgDays, dayOff, wd);
        char tail[8] = "";
        if (mins >= 0)
        {
            if (mins >= 24 * 60)
                snprintf(tail, sizeof(tail), "%s", dayOff == 1 ? "Tmrw" : NEXT_ALARM_DAY[wd]);
            else if (mins >= 60)
                snprintf(tail, sizeof(tail), "%dh%02d", mins / 60, mins % 60);
            else
                snprintf(tail, sizeof(tail), "%dm", mins);
        }
        int16_t rightX = W - u(5);
        int16_t dotsRight = W / 2 + (int16_t)(((MODE_COUNT - 1) / 2.0f) * u(10)) + imax(2, u(2.5f));
        if (tail[0] && rightX - g.textWidth(tail, font) > dotsRight + u(3))
        {
            g.setTextDatum(MR_DATUM);
            g.setTextColor(tint(C_SOFT, 0.7f)); // dimmer than the alarm time
            g.drawString(tail, rightX, cy + 1, font);
        }
    }
}
#endif

// Big seven-segment temperature with the degree ring and "C", right-hand side of
// the weather / forecast pages.
static void drawTempReadout(TFT_eSPI &g, float tempC, int16_t dw, int16_t dh, int16_t gap, int16_t dy, uint16_t accent)
{
    int t = (int)lroundf(tempC);
    int8_t d[3];
    uint8_t n = 0;
    if (t < 0)
        d[n++] = 10;
    int a = t < 0 ? -t : t;
    if (a >= 10)
        d[n++] = (int8_t)(a / 10);
    d[n++] = (int8_t)(a % 10);

    int16_t dwT = (int16_t)(dw * 0.85f);
    int16_t dhT = (int16_t)(dh * 0.95f);
    int16_t numW = n * dwT + (n - 1) * gap;
    g.setTextDatum(ML_DATUM);
    int16_t wC = g.textWidth("C", 4);
    int16_t ring = imax(2, u(2.5f));
    int16_t total = numW + gap + ring * 2 + gap + wC;
    int16_t x0 = (int16_t)(W * 0.50f) + ((int16_t)(W * 0.48f) - total) / 2;
    if (x0 < (int16_t)(W * 0.40f))
        x0 = (int16_t)(W * 0.40f);
    int16_t y0 = dy + (dh - dhT) / 2;
    drawSegNumber(g, x0, y0, dwT, dhT, gap, d, n, accent);
    int16_t rx = x0 + numW + gap + ring;
    g.drawCircle(rx, y0 + ring, ring, accent);
    g.drawCircle(rx, y0 + ring, imax(1, ring - 1), accent);
    g.setTextColor(accent);
    g.drawString("C", x0 + numW + gap * 2 + ring * 2, y0 + dhT / 2, 4);
}

// ---- Weather "details" view: Hi / Lo, humidity, wind and small hourly bars ----
static const char *windDirName(int deg)
{
    static const char *const N8[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return N8[((deg + 22) / 45) % 8];
}

static void drawWeatherDetails(TFT_eSPI &g, const WeatherDetail &d, uint16_t accent)
{
    const uint8_t fontRow = 2;
    const uint8_t fontLbl = sc >= 1.5f ? 2 : 1;
    const uint16_t rainCol = rgb(120, 200, 255);
    int16_t margin = u(6);
    char buf[32];

    // row 1: today's high / low (left), humidity (right)
    int16_t y1 = u(13);
    snprintf(buf, sizeof(buf), "Hi %d  Lo %d", (int)lroundf(d.maxC), (int)lroundf(d.minC));
    g.setTextDatum(ML_DATUM);
    g.setTextColor(accent);
    g.drawString(buf, margin, y1, fontRow);
    if (d.humidityPct >= 0)
    {
        snprintf(buf, sizeof(buf), "%d%%", d.humidityPct);
        int16_t tw = g.textWidth(buf, fontRow);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(buf, W - margin, y1, fontRow);
        drawDrop(g, W - margin - tw - u(6), y1 + 1, imax(2, u(3)), rainCol); // humidity = a water drop
    }

    // row 2: wind, the longest variant that fits
    int16_t y2 = u(29);
    if (d.windKmh >= 0)
    {
        int spd = (int)lroundf(d.windKmh);
        const char *dir = d.windDirDeg >= 0 ? windDirName(d.windDirDeg) : "";
        char opt[3][32];
        snprintf(opt[0], 32, "Wind %d km/h %s", spd, dir);
        snprintf(opt[1], 32, "%d km/h %s", spd, dir);
        snprintf(opt[2], 32, "%d km/h", spd);
        uint8_t pick = 2;
        for (uint8_t i = 0; i < 3; i++)
            if (g.textWidth(opt[i], fontRow) <= W - 2 * margin)
            {
                pick = i;
                break;
            }
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(opt[pick], margin, y2, fontRow);
    }

    // hourly bars: height = temperature relative to the other shown hours,
    // colour = temperature, hour label below (blue = rain likely)
    int n = d.barCount;
    if (n <= 0)
        return;
    int16_t top = u(39), base = infoH - u(19);
    int16_t area = base - top;
    int16_t x0 = u(8);
    int16_t slot = (W - 2 * x0) / n;
    int16_t barW = imax(3, slot * 6 / 10);
    int tmin = d.tempC[0], tmax = d.tempC[0];
    for (int i = 1; i < n; i++)
    {
        if (d.tempC[i] < tmin)
            tmin = d.tempC[i];
        if (d.tempC[i] > tmax)
            tmax = d.tempC[i];
    }
    int16_t hMin = imax(3, area / 4);
    for (int i = 0; i < n; i++)
    {
        int16_t h = tmax > tmin ? hMin + (int16_t)((long)(area - hMin) * (d.tempC[i] - tmin) / (tmax - tmin)) : (int16_t)(area * 3 / 5);
        int16_t bx = x0 + i * slot + (slot - barW) / 2;
        bool rain = d.rainPct[i] >= WEATHER_BAR_RAIN_PCT;
        uint16_t col = tempColor((float)d.tempC[i]);
        g.fillRect(bx, base - h, barW, h, i == 0 ? col : tint(col, 0.75f));
        if (rain)
            g.fillRect(bx, base + 1, barW, imax(1, u(1.5f)), rainCol);
        if (i == 0)
            strcpy(buf, "now");
        else
            snprintf(buf, sizeof(buf), "%d", d.hour[i]);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(rain ? rainCol : C_SOFT);
        g.drawString(buf, bx + barW / 2, infoH - u(12), fontLbl);
    }
}

static void drawInfoPanel(DisplayMode mode, bool alarm, bool haveTime, const struct tm &ti, uint32_t now)
{
#if ALARM_ICON_ENABLED
    pollAlarmCfg(now);
#endif
    DisplayMode shown = alarm ? DisplayMode::CLOCK : mode; // an alarm always shows the time
    uint8_t colonLevel = colonLevelNow(now);

    // ---- clock digits: spot a changed digit and start its slide animation ----
    int8_t cd[4] = {0, 0, 0, 0};
    bool digAnimating = false;
    if (shown == DisplayMode::CLOCK && haveTime)
    {
        cd[0] = (int8_t)(ti.tm_hour / 10);
        cd[1] = (int8_t)(ti.tm_hour % 10);
        cd[2] = (int8_t)(ti.tm_min / 10);
        cd[3] = (int8_t)(ti.tm_min % 10);
        bool fresh = (lastMode != DisplayMode::CLOCK) || digCur[0] < 0; // came from another page / first time
        for (uint8_t i = 0; i < 4; i++)
        {
            if (fresh)
            {
                digCur[i] = digPrev[i] = cd[i];
                digChangeMs[i] = 0;
            }
            else if (cd[i] != digCur[i])
            {
                digPrev[i] = digCur[i];
                digCur[i] = cd[i];
                digChangeMs[i] = (CLOCK_DIGIT_ANIM && digSprOk) ? now : 0;
            }
            if (digChangeMs[i] != 0)
            {
                if (now - digChangeMs[i] < (uint32_t)CLOCK_DIGIT_ANIM_MS)
                    digAnimating = true;
                else
                    digChangeMs[i] = 0; // finished: from now on a plain static digit
            }
        }
    }
    else
        digCur[0] = -1; // no time shown: the next time we show one it appears without animating

    bool wValid = false;
    float tempC = 0;
    int wCode = 0;
    uint32_t wUpdatedMs = 0;
    if (shown == DisplayMode::WEATHER)
        readWeather(wValid, tempC, wCode, wUpdatedMs);
    uint32_t wAgeMs = wValid ? ageSince(millis(), wUpdatedMs) : 0;
    bool wStale = wValid && wAgeMs > WEATHER_STALE_MS; // offline / fetches failing: reading is old

    // Weather page: opens on the big temperature, then alternates with the details view.
    static uint32_t weatherEnterMs = 0;
    if (shown == DisplayMode::WEATHER && lastMode != DisplayMode::WEATHER)
        weatherEnterMs = now;
    WeatherDetail wd = {};
    bool detailView = false;
    if (WEATHER_DETAILS_ENABLED && shown == DisplayMode::WEATHER)
    {
        readWeatherDetail(wd);
        detailView = wd.valid && wValid && (((now - weatherEnterMs) / WEATHER_PAGE_SWITCH_MS) & 1);
    }

    // ---- signature: only redraw when something visible changed ----
    uint32_t sig = 0;
    switch (shown)
    {
    case DisplayMode::CLOCK:
        if (haveTime)
            sig = (1u << 28) | (uint32_t)(ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec) | // bits 0-16
                  (alarmFlashOn ? (1u << 17) : 0u) | ((uint32_t)colonLevel << 18) |         // colon "breath"
                  (digAnimating ? ((((now / 33) & 0x1Fu) + 1u) << 22) : 0u);                // redraw every frame while a digit slides
        else
            sig = (2u << 28) | (now / 90);
        break;
    case DisplayMode::DATE:
        sig = (3u << 28) | (haveTime ? (uint32_t)(ti.tm_mday | (ti.tm_mon << 5) | ((ti.tm_year & 0x7F) << 9)) : 0u) |
              ((themeStamp & 0xFFFu) << 16); // theme moved -> redraw
        break;
    default:
        sig = (detailView ? (6u << 28) | ((now / 1000) & 0xFF) : (4u << 28) | ((now / 120) & 0xFF)) | // details don't animate
              (((uint32_t)((int)lroundf(tempC) + 128) & 0xFF) << 8) |
              (((uint32_t)wCode & 0x7F) << 16) | (wValid ? (1u << 23) : 0u);
        break;
    }
    bool modeChanged = (shown != lastMode);
    // start a slide-in between two normal pages (not from boot / the diagnostics page / an alarm)
    if (TRANSITION_ENABLED && modeChanged && !alarm && infoCv.ok &&
        (int)lastMode >= 0 && lastMode != DisplayMode::DIAG)
    {
        transActive = true;
        transStartMs = now;
    }
    if (!transActive && !modeChanged && sig == lastInfoSig)
        return; // nothing visible changed (while sliding, every frame is redrawn)
    lastMode = shown;
    lastInfoSig = sig;

    uint16_t accent = shown == DisplayMode::CLOCK ? C_ACCENT : (shown == DisplayMode::DATE ? C_AMBER : C_SOFT);
    if (shown == DisplayMode::WEATHER && wValid)
        accent = wStale ? tint(tempColor(tempC), 0.43f) : tempColor(tempC); // stale = dimmed
    if (alarm)
        accent = alarmFlashOn ? C_RED : tint(C_RED, 0.35f);

    TFT_eSPI &g = infoCv.begin();
    g.fillRect(0, 0, W, infoH, C_BG);

    // accent line with a soft glow underneath
    g.fillRect(0, 0, W, imax(2, u(2)), accent);
    g.fillRect(0, imax(2, u(2)), W, imax(1, u(1)), tint(accent, 0.235f));

    int16_t dh = (int16_t)(infoH * 0.42f);
    int16_t dw = (int16_t)(dh * 0.56f);
    int16_t gap = imax(2, u(3));
    int16_t dy = (int16_t)(infoH * 0.10f);
    int16_t capY = (int16_t)(infoH * 0.68f);
    char cap[24] = "";

    if (shown == DisplayMode::CLOCK)
    {
        if (haveTime)
        {
            int16_t cw = imax(4, u(8));
            int16_t total = 4 * dw + 2 * gap + 2 * gap + cw;
            int16_t x0 = (W - total) / 2;
            uint16_t on = alarm ? (alarmFlashOn ? C_RED : tint(C_RED, 0.35f)) : C_ACCENT;
            int16_t xs[4] = {x0, (int16_t)(x0 + dw + gap),
                             (int16_t)(x0 + 2 * dw + gap + 2 * gap + cw),
                             (int16_t)(x0 + 2 * dw + gap + 2 * gap + cw + dw + gap)};
            for (uint8_t i = 0; i < 4; i++)
            {
                if (digChangeMs[i] != 0 && digSprOk)
                {
                    float pr = (now - digChangeMs[i]) / (float)CLOCK_DIGIT_ANIM_MS;
                    drawAnimDigit(xs[i], dy, dw, dh, digPrev[i], digCur[i], pr > 1.0f ? 1.0f : pr, on);
                }
                else
                    drawSegDigit(g, xs[i], dy, dw, dh, digCur[i], on, C_GHOST);
            }
            drawColonGlow(g, x0 + 2 * dw + gap + gap + cw / 2, dy, dh, imax(2, dw / 4), on, colonLevel);
            strftime(cap, sizeof(cap), "%a %d %b", &ti);

            // seconds progress bar
            int16_t bx = u(12), bw = W - 2 * u(12), bh = imax(2, u(2.5f));
            int16_t by = infoH - u(15);
            g.fillRoundRect(bx, by, bw, bh, bh / 2, C_GHOST);
            int16_t fw = (int16_t)((int32_t)bw * (ti.tm_sec + 1) / 60);
            if (fw > 0)
                g.fillRoundRect(bx, by, fw, bh, bh / 2, accent);

#if ALARM_ICON_ENABLED
            if (alarmCfgOn || alarm)
                drawAlarmBadge(g, alarm, infoH - u(7), ti);
#endif
        }
        else
        {
            drawSpinner(g, W / 2, (int16_t)(infoH * 0.38f), u(10), accent);
            strcpy(cap, "Syncing...");
        }
    }
    else if (shown == DisplayMode::DATE)
    {
        if (haveTime)
        {
            int8_t d[2] = {(int8_t)(ti.tm_mday / 10), (int8_t)(ti.tm_mday % 10)};
            int16_t dayW = 2 * dw + gap;
            int16_t x0 = (int16_t)(W * 0.10f);
            drawSegNumber(g, x0, dy, dw, dh, gap, d, 2, C_AMBER);
            // thin divider
            int16_t divX = x0 + dayW + u(9);
            g.drawFastVLine(divX, dy + 2, dh - 4, tint(C_AMBER, 0.27f));
            char mon[8];
            strftime(mon, sizeof(mon), "%b", &ti);
            for (char *p = mon; *p; p++)
                if (*p >= 'a' && *p <= 'z')
                    *p -= 32;
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_AMBER);
            g.drawString(mon, divX + (W - divX) / 2, dy + dh / 2, 4);
            strftime(cap, sizeof(cap), "%A", &ti);
        }
        else
        {
            drawSpinner(g, W / 2, (int16_t)(infoH * 0.38f), u(10), accent);
            strcpy(cap, "Syncing...");
        }
    }
    else // WEATHER
    {
        if (wValid && detailView)
        {
            drawWeatherDetails(g, wd, accent);
        }
        else if (wValid)
        {
            int16_t r = u(14);
            drawWeatherIcon(g, wCode, (int16_t)(W * 0.22f), dy + dh / 2, r, now);

            drawTempReadout(g, tempC, dw, dh, gap, dy, accent);

            strncpy(cap, weather_code_to_label(wCode), sizeof(cap) - 1);
            cap[sizeof(cap) - 1] = '\0';

            if (wStale)
            {
                // "Clear 3h ago" (drops "ago" if it would not fit), plus a small clock badge
                char age[8];
                fmtAge(age, sizeof(age), wAgeMs);
                char full[24];
                snprintf(full, sizeof(full), "%s %s ago", cap, age);
                g.setTextDatum(MC_DATUM);
                if (g.textWidth(full, 2) <= W - u(4))
                    strcpy(cap, full);
                else
                    snprintf(cap, sizeof(cap), "%s %s", weather_code_to_label(wCode), age);

                int16_t bx = W - u(10), by = infoH - u(7), br = imax(3, u(4));
                g.drawCircle(bx, by, br, C_AMBER);
                g.drawLine(bx, by, bx, by - br + 1, C_AMBER);
                g.drawLine(bx, by, bx + br - 1, by, C_AMBER);
            }
        }
        else
        {
            drawCloud(g, W / 2, dy + dh / 2, u(14), tint(C_SOFT, 0.47f));
            strcpy(cap, "No weather yet");
        }
    }

    if (cap[0])
    {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(cap, W / 2, capY, 2);
    }
    drawModeDots(g, shown, accent, infoH - u(5));

    int16_t slideX = 0;
    if (transActive)
    {
        uint32_t el = now - transStartMs;
        if (el >= (uint32_t)TRANSITION_MS)
            transActive = false; // last frame lands exactly at x = 0
        else
        {
            float inv = 1.0f - el / (float)TRANSITION_MS; // ease-out: fast start, soft landing
            slideX = (int16_t)(W * inv * inv * inv);
        }
    }
    infoCv.endAt(slideX);
}

// =====================================================================
//  Setup screen (WiFiManager portal)
// =====================================================================
static void drawWifiIcon(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
{
    g.fillCircle(cx, cy, imax(2, r / 7), c);
    for (int k = 1; k <= 3; k++)
    {
        float rad = r * k / 3.0f;
        for (int a = -45; a <= 45; a += 3)
        {
            float t = a * (float)M_PI / 180.0f;
            g.fillCircle(cx + (int16_t)(rad * sinf(t)), cy - (int16_t)(rad * cosf(t)), imax(1, r / 14), c);
        }
    }
}

static void drawPortalScreen()
{
    tft.fillScreen(C_BG);
    drawWifiIcon(tft, W / 2, (int16_t)(H * 0.34f), u(26), C_ACCENT);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_WHITE);
    tft.drawString("WiFi setup", W / 2, (int16_t)(H * 0.50f), 2);
    tft.setTextColor(C_SOFT);
    tft.drawString("Join network:", W / 2, (int16_t)(H * 0.60f), 2);
    tft.setTextColor(C_AMBER);
    tft.drawString(WIFI_PORTAL_AP_NAME, W / 2, (int16_t)(H * 0.72f), 4);
    tft.setTextColor(C_SOFT);
    tft.drawString("192.168.4.1", W / 2, (int16_t)(H * 0.86f), 2);
}

// OTA progress screen: static part once per update, then only the bar and the
// percentage when the value changes (nothing else may draw during a flash write).
static void drawOtaScreen(int pct)
{
    if (otaDrawnPct < 0)
    {
        tft.fillScreen(C_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(C_WHITE);
        tft.drawString("Updating", W / 2, (int16_t)(H * 0.30f), 4);
        tft.setTextColor(C_SOFT);
        tft.drawString("Do not unplug", W / 2, (int16_t)(H * 0.82f), 2);
    }
    if (pct == otaDrawnPct)
        return;
    otaDrawnPct = pct;

    int16_t bx = u(14), bw = W - 2 * bx, bh = imax(6, u(8));
    int16_t by = (int16_t)(H * 0.46f);
    tft.fillRoundRect(bx, by, bw, bh, bh / 2, C_GHOST);
    int16_t fw = (int16_t)((int32_t)bw * pct / 100);
    if (fw > 0)
        tft.fillRoundRect(bx, by, fw, bh, imin(bh / 2, fw / 2), C_ACCENT);

    char txt[8];
    snprintf(txt, sizeof(txt), "%d%%", pct);
    int16_t py = (int16_t)(H * 0.62f);
    tft.fillRect(0, py - u(10), W, u(20), C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT);
    tft.drawString(txt, W / 2, py, 2);
}

// =====================================================================
//  Status icon (WiFi lost) — drawn over the face, top-right corner
// =====================================================================
// Only visible while the clock is offline: a dimmed WiFi symbol with a red
// slash. Polled twice a second (WiFi.status() is cheap, but there is no need
// to ask 30 times a second).
static bool wifiOkNow = false;       // cached WiFi.status() == WL_CONNECTED
static uint32_t wifiDownSinceMs = 0; // 0 = connected; else millis() when it was first seen down

static void pollWifi(uint32_t now)
{
    static uint32_t lastPollMs = 0;
    if (lastPollMs != 0 && now - lastPollMs < 500)
        return;
    lastPollMs = now ? now : 1;
    wifiOkNow = (WiFi.status() == WL_CONNECTED);
    if (wifiOkNow)
        wifiDownSinceMs = 0;
    else if (wifiDownSinceMs == 0)
        wifiDownSinceMs = now ? now : 1;
}

static void drawStatusIcons(TFT_eSPI &g, uint32_t now)
{
    (void)now;
    if (wifiOkNow)
        return;

    int16_t r = u(7);
    int16_t cx = W - u(13), cy = u(15);
    drawWifiIcon(g, cx, cy, r, dimColor(C_SOFT, 170));
    // slash: bg-coloured shadow first so it stays readable over the arcs
    g.drawLine(cx - r, cy - r - 1, cx + r, cy + 3, C_BG);
    g.drawLine(cx - r, cy - r, cx + r, cy + 4, C_RED);
    g.drawLine(cx - r + 1, cy - r, cx + r + 1, cy + 4, C_RED);
}

// Ring that fills while the Mode button is held toward the diagnostics page
// (g_state.modeHoldPct, set by touch.cpp). Drawn over the middle of the face.
static void drawHoldRing(TFT_eSPI &g)
{
    if (!MODE_HOLD_RING)
        return;
    uint8_t pct = g_state.modeHoldPct;
    if (pct == 0)
        return;

    int16_t cx = W / 2, cy = faceH / 2;
    int16_t r = u(17);
    int16_t dot = imax(1, u(1.8f));
    g.fillCircle(cx, cy, r + dot + u(3), C_BG);
    g.drawCircle(cx, cy, r + dot + u(3), C_GHOST);

    const int N = 40; // dots around the ring, starting at 12 o'clock, clockwise
    int filled = (pct * N + 50) / 100;
    for (int i = 0; i < N; i++)
    {
        float a = -(float)M_PI / 2.0f + i * 2.0f * (float)M_PI / N;
        int16_t x = cx + (int16_t)lroundf(r * cosf(a)), y = cy + (int16_t)lroundf(r * sinf(a));
        g.fillCircle(x, y, dot, i < filled ? C_ACCENT : C_GHOST);
    }
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_SOFT);
    g.drawString("DIAG", cx, cy, sc >= 1.5f ? 2 : 1);
}

// =====================================================================
//  Diagnostics page (full screen) — IP, signal, free heap, last sync
// =====================================================================
// Drawn into the two existing canvases (face area on top, info area below), so
// it needs no extra RAM and does not flicker. The same content function runs
// once per canvas with a y offset; anything outside a canvas is clipped.
struct DiagData
{
    bool wifi;
    char ip[20];
    int rssi;
    uint32_t heapKb;
    char sync[24]; // last NTP sync: "14:05 (12m)" or "none yet"
    bool syncOk;
    char weather[16]; // age of the last weather reading
    bool weatherOk;
    bool weatherStale;
    char uptime[16];
    char reset[12]; // why this boot happened: "Brownout", "Power on", ...
    bool resetBad;  // brownout / panic / watchdog
    char boots[16]; // "42" or "42 | BO 3" (BO = brownouts so far)
    bool bootsWarn; // at least one brownout on record
};
static uint32_t lastDiagDrawMs = 0;
static bool diagShown = false;

static void collectDiag(DiagData &d, uint32_t now)
{
    d.wifi = (WiFi.status() == WL_CONNECTED);
    d.rssi = 0;
    if (d.wifi)
    {
        IPAddress ip = WiFi.localIP();
        snprintf(d.ip, sizeof(d.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        d.rssi = WiFi.RSSI();
    }
    else
    {
        d.ip[0] = '\0';
    }
    d.heapKb = ESP.getFreeHeap() / 1024;

    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    uint32_t ntpEpoch = g_state.ntpSyncEpoch;
    uint32_t ntpMs = g_state.ntpSyncMs;
    bool wxValid = g_state.weatherValid;
    uint32_t wxMs = g_state.weatherUpdatedMs;
    xSemaphoreGive(g_stateMutex);

    char age[8];
    d.syncOk = ntpEpoch != 0;
    if (d.syncOk)
    {
        time_t t = (time_t)ntpEpoch;
        struct tm lt;
        localtime_r(&t, &lt);
        char hm[8];
        strftime(hm, sizeof(hm), "%H:%M", &lt);
        fmtAge(age, sizeof(age), ageSince(now, ntpMs));
        snprintf(d.sync, sizeof(d.sync), "%s (%s)", hm, age);
    }
    else
    {
        strcpy(d.sync, "none yet");
    }

    d.weatherOk = wxValid;
    d.weatherStale = false;
    if (wxValid)
    {
        uint32_t a = ageSince(now, wxMs);
        d.weatherStale = a > WEATHER_STALE_MS;
        fmtAge(age, sizeof(age), a);
        snprintf(d.weather, sizeof(d.weather), "%s ago", age);
    }
    else
    {
        strcpy(d.weather, "none yet");
    }

    uint32_t s = now / 1000;
    uint32_t days = s / 86400, hours = (s / 3600) % 24, mins = (s / 60) % 60;
    if (days > 0)
        snprintf(d.uptime, sizeof(d.uptime), "%ud %02uh", (unsigned)days, (unsigned)hours);
    else if (hours > 0)
        snprintf(d.uptime, sizeof(d.uptime), "%uh %02um", (unsigned)hours, (unsigned)mins);
    else
        snprintf(d.uptime, sizeof(d.uptime), "%um", (unsigned)mins);

    BootRecord cur;
    if (boot_log_history(&cur, 1) == 1)
    {
        strlcpy(d.reset, boot_log_reason_name(cur.reason), sizeof(d.reset));
        d.resetBad = boot_log_reason_abnormal(cur.reason);
    }
    else
    {
        strcpy(d.reset, "--");
        d.resetBad = false;
    }
    uint32_t bo = boot_log_brownouts();
    d.bootsWarn = bo > 0;
    if (bo > 0)
        snprintf(d.boots, sizeof(d.boots), "%u | BO %u", (unsigned)boot_log_count(), (unsigned)bo);
    else
        snprintf(d.boots, sizeof(d.boots), "%u", (unsigned)boot_log_count());
}

static void drawDiagRow(TFT_eSPI &g, int16_t y, const char *label, const char *value, uint16_t valueColor)
{
    g.setTextDatum(ML_DATUM);
    g.setTextColor(C_SOFT);
    g.drawString(label, u(6), y, 2);
    g.setTextDatum(MR_DATUM);
    g.setTextColor(valueColor);
    g.drawString(value, W - u(6), y, 2);
}

// yOff = screen y of this canvas' top edge; all coordinates below are screen
// coordinates, shifted into the canvas.
static void drawDiagContent(TFT_eSPI &g, int16_t yOff, const DiagData &d)
{
    auto Y = [&](float v) -> int16_t
    { return (int16_t)(u(v) - yOff); };

    uint16_t accent = d.wifi ? C_ICE : C_RED;
    g.fillRect(0, -yOff, W, imax(2, u(2)), accent);
    g.fillRect(0, imax(2, u(2)) - yOff, W, imax(1, u(1)), tint(accent, 0.235f));

    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_WHITE);
    g.drawString("Diagnostics", W / 2, Y(11), 2);
    g.drawFastHLine(u(6), Y(20), W - 2 * u(6), tint(C_SOFT, 0.31f));

    // IP, centred on its own line (a full 192.168.xxx.xxx would not fit next to a label)
    g.setTextDatum(MC_DATUM);
    g.setTextColor(d.wifi ? C_ACCENT : C_RED);
    g.drawString(d.wifi ? d.ip : "Offline", W / 2, Y(30), 2);

    char buf[24];
    uint16_t c;

    // signal strength
    if (d.wifi)
    {
        snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
        c = d.rssi >= -60 ? rgb(90, 230, 130) : (d.rssi >= -75 ? C_AMBER : C_RED);
    }
    else
    {
        strcpy(buf, "--");
        c = C_RED;
    }
    drawDiagRow(g, Y(46), "Signal", buf, c);

    // free heap
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)d.heapKb);
    c = d.heapKb < 30 ? C_RED : (d.heapKb < 60 ? C_AMBER : C_WHITE);
    drawDiagRow(g, Y(61), "Heap", buf, c);

    // last NTP sync (clock time + age)
    drawDiagRow(g, Y(76), "Sync", d.sync, d.syncOk ? C_WHITE : C_AMBER);

    // weather reading age
    drawDiagRow(g, Y(91), "Weather", d.weather, d.weatherOk ? (d.weatherStale ? C_AMBER : C_WHITE) : C_AMBER);

    // uptime
    drawDiagRow(g, Y(106), "Uptime", d.uptime, C_WHITE);

    // why this boot happened (kept in flash; red = brownout / panic / watchdog)
    drawDiagRow(g, Y(121), "Reset", d.reset, d.resetBad ? C_RED : C_WHITE);

    // boot counter; "BO n" = brownouts so far (amber once there is one)
    drawDiagRow(g, Y(136), "Boots", d.boots, d.bootsWarn ? C_AMBER : C_WHITE);
}

static void drawDiagScreen(uint32_t now)
{
    DiagData d;
    collectDiag(d, now);

    TFT_eSPI &gt = faceCv.begin();
    gt.fillRect(0, 0, W, faceH, C_BG);
    drawDiagContent(gt, 0, d);
    faceCv.end();

    TFT_eSPI &gb = infoCv.begin();
    gb.fillRect(0, 0, W, infoH, C_BG);
    drawDiagContent(gb, faceH, d);
    infoCv.end();
}

// =====================================================================
//  Backlight — dimmed at night, full during the day / alarm / setup
// =====================================================================
// Two ways to dim (BACKLIGHT_NIGHT_PWM in config.h):
//   1 = PWM on the LEDC peripheral, smooth fade. Works on any module, but the
//       fast current pulses can couple into the SPI lines as random pixels.
//   0 = NO PWM at all: the pin stays steady HIGH and only its drive strength
//       is lowered at night. No switching edges -> no switching noise. This
//       only dims a module whose LED pin is fed straight from the GPIO.
// In both modes the pin drive strength is BACKLIGHT_DAY_DRIVE_CAP by day and
// BACKLIGHT_NIGHT_DRIVE_CAP at night (weaker = slower edges, less current).
static bool blNight = false; // true once the night drive strength is applied

static void backlightSetDrive(bool night)
{
    gpio_set_drive_capability((gpio_num_t)TFT_BL_PIN,
                              (gpio_drive_cap_t)(night ? BACKLIGHT_NIGHT_DRIVE_CAP
                                                       : BACKLIGHT_DAY_DRIVE_CAP));
    blNight = night;
}

#if BACKLIGHT_NIGHT_PWM
static uint8_t blDuty = BACKLIGHT_DAY;

static void backlightWrite(uint8_t duty)
{
    blDuty = duty;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(TFT_BL_PIN, duty); // core 3.x: addressed by pin
#else
    ledcWrite(BACKLIGHT_PWM_CHANNEL, duty); // core 2.x: addressed by channel
#endif
}
#endif

static void backlightInit()
{
#if BACKLIGHT_NIGHT_PWM
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(TFT_BL_PIN, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
#else
    ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
    ledcAttachPin(TFT_BL_PIN, BACKLIGHT_PWM_CHANNEL);
#endif
    backlightWrite(BACKLIGHT_DAY); // clock unknown at boot -> start fully lit
#else
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH); // plain DC: no PWM, no edges
#endif
    backlightSetDrive(false);
}

// night = true dims, false is full brightness.
// jump = true goes there at once (alarm: must be bright immediately); only
// matters in PWM mode, where the change is otherwise a smooth fade.
static void backlightUpdate(bool night, bool jump)
{
#if BACKLIGHT_NIGHT_PWM
    uint8_t target = night ? BACKLIGHT_NIGHT : BACKLIGHT_DAY;

    if (!night && blNight)
        backlightSetDrive(false); // leaving night: full drive before brightening

    if (blDuty != target)
    {
        if (jump)
        {
            backlightWrite(target);
        }
        else
        {
            int next = (int)blDuty + (target > blDuty ? BACKLIGHT_FADE_STEP : -BACKLIGHT_FADE_STEP);
            if ((target > blDuty && next > target) || (target < blDuty && next < target))
                next = target;
            backlightWrite((uint8_t)next);
        }
    }

    if (night && !blNight && blDuty == target)
        backlightSetDrive(true); // fade finished: now weaken the drive
#else
    (void)jump;
    if (night != blNight)
        backlightSetDrive(night);
#endif
}

// =====================================================================
//  Public API
// =====================================================================

void display_init()
{
    // Backlight (see the Backlight section): dims at night, see display_tick.
    backlightInit();

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(C_BG);

    W = tft.width();
    H = tft.height();
    sc = W / 128.0f;
    faceH = (int16_t)(H * 0.55f);
    infoH = H - faceH;

    bool f = faceCv.init(0, W, faceH);
    bool i = infoCv.init(faceH, W, infoH);

    // small sprite for the clock-digit slide animation (needs the info panel to be a sprite too)
    if (CLOCK_DIGIT_ANIM && i)
    {
        int16_t dw, dh;
        clockMetrics(dw, dh);
        digSpr.setColorDepth(16);
        digSprOk = (digSpr.createSprite(dw + 2 * DIG_PAD, dh + 2 * DIG_PAD) != nullptr);
    }
    Serial.printf("display sprites: face=%s info=%s digit=%s (free heap %u)\n",
                  f ? "ok" : "fallback", i ? "ok" : "fallback", digSprOk ? "ok" : "off",
                  (unsigned)ESP.getFreeHeap());

    blinkPhaseStartMs = millis();
}

void display_tick()
{
    uint32_t now = millis();
    Mood mood = readMood();
    DisplayMode mode = readDisplayMode();
    bool timeSynced = readTimeSynced();

    // A firmware update owns the screen: full brightness, progress only.
    int otaPct = readOtaPercent();
    if (otaPct >= 0)
    {
        backlightUpdate(false, true);
        drawOtaScreen(otaPct);
        otaScreenShown = true;
        return;
    }
    if (otaScreenShown)
    {
        // update failed (on success the clock reboots): back to normal
        tft.fillScreen(C_BG);
        otaScreenShown = false;
        otaDrawnPct = -1;
        lastInfoSig = 0xFFFFFFFFu;
    }

    // While the WiFi setup portal is open, show how to join it instead of the face.
    if (readPortalActive())
    {
        backlightUpdate(false, true); // setup screen must always be readable
        if (!portalScreenShown)
        {
            drawPortalScreen();
            portalScreenShown = true;
        }
        return;
    }
    if (portalScreenShown)
    {
        tft.fillScreen(C_BG);
        portalScreenShown = false;
        lastInfoSig = 0xFFFFFFFFu;
    }

    struct tm ti;
    bool haveTime = timeSynced && getLocalTime(&ti, 0);
    themeUpdate(haveTime, ti, now); // background / accent follow the time of day

    bool alarm = (mood == Mood::ALARM_ALERT);
    bool night = haveTime && sun_is_night(ti);
    bool sleepy = night && !alarm;

    // Dim at night; an alarm always gets full brightness, immediately.
    backlightUpdate(night && !alarm, alarm);

    // petted: hold the reaction face for a while even after the servo finishes.
    // A new tap (new seq) restarts the animation, also while the old one is still running.
    PetEvent pe = pet_get_event();
    if (pe.seq != petSeqSeen)
    {
        petSeqSeen = pe.seq;
        petEv = pe;
        happyStartMs = now;
        happyUntilMs = now + pet_hold_ms(pe.kind);
    }
    bool purr = !alarm && mood == Mood::PET_PURR; // button held: purring takes over from the tap reaction
    bool happy = !alarm && (purr || now < happyUntilMs);

    if (alarm && now - lastAlarmFlashMs > ALARM_FLASH_MS)
    {
        alarmFlashOn = !alarmFlashOn;
        lastAlarmFlashMs = now;
    }

    // glance down at the panel whenever the page changes
    if (mode != lastMode && !alarm)
        glanceUntilMs = now + 900;

    pollWifi(now);

    // what the face background / reactions need to know
    SceneInfo scn;
    scn.timeKnown = haveTime;
    scn.day = haveTime && sun_is_day(ti);
    scn.moonPhase = haveTime ? moon_phase_view() : 0.5f;
    {
        bool wv = false;
        float wt = 0;
        int wc = 0;
        uint32_t wMs = 0;
        readWeather(wv, wt, wc, wMs);
        scn.weatherKnown = wv && ageSince(now, wMs) <= SCENE_WEATHER_MAX_AGE_MS;
        scn.code = wc;
        scn.tempC = wt;
    }
    bool offline = wifiDownSinceMs != 0 && (now - wifiDownSinceMs) >= REACT_OFFLINE_DELAY_MS;
    Reaction reaction = alarm ? Reaction::NONE : pickReaction(scn, offline, sleepy);

    updateLook(now, alarm, sleepy, reaction);
    float blinkOpen = updateBlink(now);
    float exprP = updateExpression(now, !alarm && !happy && reaction != Reaction::WORRIED, sleepy);

    // Diagnostics page: full screen, replaces face + info panel. An alarm
    // always wins (it falls through to the normal clock/face drawing below).
    if (mode == DisplayMode::DIAG && !alarm)
    {
        lastMode = DisplayMode::DIAG; // so the page change is noticed on the way out
        if (!diagShown || now - lastDiagDrawMs >= 500)
        {
            lastDiagDrawMs = now;
            diagShown = true;
            drawDiagScreen(now);
        }
        return;
    }
    diagShown = false;

    TFT_eSPI &g = faceCv.begin();
    drawFace(g, now, alarm, happy, purr, sleepy, blinkOpen, scn, reaction, exprP);
    drawStatusIcons(g, now);
    drawHoldRing(g);
    faceCv.end();

    drawInfoPanel(mode, alarm, haveTime, ti, now);
}