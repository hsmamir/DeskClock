#pragma once
// Internal header shared by the display_*.cpp files. Not part of the public API (that is display.h).
// Everything here lives in namespace disp so the names (W, H, u, tft ...) cannot collide with other
// code at link time. display.cpp pulls them in with `using namespace disp;`.
#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>
#include "display.h"
#include "state.h"
#include "pins.h"
#include "config.h"
#include "weather.h"
#include "settings.h"
#include "sun.h"
#include "pet.h"
#include "display_fonts.h" // smooth (VLW) fonts, see tools/make_vlw.py

namespace disp
{

    // ---- Palette (RGB565) ----
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // The five colours below follow the time of day (display_theme.cpp updates them).
    extern uint16_t C_BG;      // background
    extern uint16_t C_ACCENT;  // clock digits, accent lines, bars
    extern uint16_t C_EYE_TOP; // eye gradient, top
    extern uint16_t C_EYE_BOT; // eye gradient, bottom
    extern uint16_t C_GHOST;   // unlit 7-seg segments, empty bars
    constexpr uint16_t C_WHITE = 0xFFFF;
    constexpr uint16_t C_ALARM_TOP = rgb(255, 130, 100);
    constexpr uint16_t C_ALARM_BOT = rgb(210, 15, 25);
    constexpr uint16_t C_RED = rgb(255, 40, 40);
    constexpr uint16_t C_AMBER = rgb(255, 200, 60);
    constexpr uint16_t C_SOFT = rgb(150, 160, 190);
    constexpr uint16_t C_CHEEK = rgb(255, 105, 150);
    constexpr uint16_t C_HEART = rgb(255, 70, 115);
    constexpr uint16_t C_ICE = rgb(170, 200, 255);

    // ---- Core objects / layout (defined in display_core.cpp) ----
    extern TFT_eSPI tft;
    extern int16_t W, H, faceH, infoH;
    extern float sc; // scale relative to the 128 px wide real display

    inline int16_t u(float v) { return (int16_t)(v * sc + 0.5f); }
    inline int16_t imax(int16_t a, int16_t b) { return a > b ? a : b; }
    inline int16_t imin(int16_t a, int16_t b) { return a < b ? a : b; }

    // ---- Smooth (VLW) text ----
    // Draws s with one of the ClockSmNN fonts: loadFont -> drawString -> unloadFont, so the font only
    // occupies RAM (a few hundred bytes of glyph metrics) while it is being used and the built-in fonts
    // keep working everywhere else. The text sits on a baseline, with the digits / capitals centred on cy
    // (a plain MC_DATUM would centre the whole line box, descender space included, a few px too high).
    // datum: L_BASELINE / C_BASELINE / R_BASELINE. A smooth font anti-aliases toward bg, so pass the colour
    // the text really sits on (the themed panel colour C_BG). Works on a sprite and on tft.
    inline void smoothText(TFT_eSPI &g, const uint8_t *font, int16_t capH, const char *s,
                           int16_t x, int16_t cy, uint8_t datum, uint16_t fg, uint16_t bg)
    {
        g.loadFont(font);
        g.setTextColor(fg, bg);
        g.setTextDatum(datum);
        g.drawString(s, x, cy + capH / 2);
        g.unloadFont();
    }
    inline int16_t smoothWidth(TFT_eSPI &g, const uint8_t *font, const char *s)
    {
        g.loadFont(font);
        int16_t w = g.textWidth(s, 1); // the font number is ignored while a smooth font is loaded
        g.unloadFont();
        return w;
    }

    // ---- Colour helpers ----
    uint16_t dimColor(uint16_t c, uint8_t v);
    uint16_t mixColor(uint16_t a, uint16_t b, float t);
    // A colour faded toward the (themed) background: k = 0 -> background, 1 -> c.
    // Glows and fades use this instead of dimColor() (which fades toward black), so
    // they stay a soft glow on a bright day background instead of turning into dark halos.
    inline uint16_t tint(uint16_t c, float k) { return mixColor(C_BG, c, k); }

    // ---- Off-screen canvases ----
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
    extern Canvas faceCv, infoCv;
    extern TFT_eSprite digSpr; // clock-digit animation sprite (display_digits.cpp)
    extern bool alarmFlashOn;

    // ---- State readers (display_core.cpp) ----
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

    Mood readMood();
    DisplayMode readDisplayMode();
    bool readTimeSynced();
    bool readPortalActive();
    int readOtaPercent();
    void readWeather(bool &valid, float &tempC, int &code, uint32_t &updatedMs);
    void readWeatherDetail(WeatherDetail &d);
    void fmtAge(char *out, size_t n, uint32_t ageMs);
    uint32_t ageSince(uint32_t now, uint32_t stamp);
    uint16_t tempColor(float tempC);

    // ---- Eyes / face types ----
    enum : uint8_t
    {
        BROW_NONE = 0,
        BROW_ANGRY = 1,
        BROW_SAD = 2
    };

    // ---- Parametric eye (display_eyes.cpp) ----
    // An eye is a set of numbers, every expression is just a preset of them. Each number
    // is driven by its own spring, so changing the expression morphs smoothly.
    struct EyeShape
    {
        float w, h;   // size in px (base: w = 2R, h = 2.25R)
        float radius; // corner radius in px
        float lidTop; // 0..1  how far the upper lid has come down
        float slant;  // -1..+1  positive = inner corner low (angry), negative = outer corner low (sad)
        float lidBot; // 0..1  how far the lower lid has come up (happy "^ ^")
        float x, y;   // gaze offset in px
    };

    struct Spring
    {
        float x = 0, v = 0, target = 0;
    };
    // k = stiffness, c = damping. k=220, c=18 -> damping ratio ~0.6, ~10 % overshoot (feels alive).
    inline void springStep(Spring &s, float dt, float k = 220.f, float c = 18.f)
    {
        s.v += (k * (s.target - s.x) - c * s.v) * dt;
        s.x += s.v * dt;
    }

    // Presets (rows of the table in display_eyes.cpp). A new expression = a new row here + a new row there.
    enum class EyeMood : uint8_t
    {
        NEUTRAL,
        HAPPY,
        ANGRY,
        SAD, // also: worried
        TIRED,
        SURPRISED,
        SUSPICIOUS, // left eye half shut, right eye open
        HOT,        // heavy lids
        COLD,       // slightly lowered lids
        COUNT
    };

    // What the face background should show (filled in by display_tick).
    struct SceneInfo
    {
        bool timeKnown;    // clock has a usable time (else: plain night sky, as at boot)
        bool day;          // sun is up (real sunrise..sunset, see sun.h)
        bool weatherKnown; // a recent-enough weather reading exists
        int code;          // Open-Meteo WMO weather code (valid if weatherKnown)
        float tempC;       // current temperature (valid if weatherKnown)
        float windKmh;     // wind speed; < 0 = unknown (valid only if weatherKnown)
        int windDirDeg;    // direction the wind comes FROM, 0-359; -1 = unknown
        float moonPhase;   // 0 new .. 0.5 full .. 1 new; 0.25 = right half lit (see moon_phase_view)
        float arc;         // progress 0..1 of the sun (day) / moon (night) along its path across the sky; <0 = unknown
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

    // Short one-off idle expressions, picked at random.
    enum class Expr : uint8_t
    {
        NONE,
        WINK,
        YAWN,
        SURPRISE
    };
    extern Expr exprCur;
    extern float lookX, lookY;
    extern float lookTremX, lookTremY; // px, micro-tremor while the gaze holds still
    extern uint32_t glanceUntilMs;
    extern uint32_t blinkPhaseStartMs;
    extern PetEvent petEv;
    extern uint32_t happyStartMs;

    inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    inline bool lightningActive(uint32_t now) { return (now % 6500) < 260; }

    void drawHeart(TFT_eSPI &g, int16_t x, int16_t y, int16_t s, uint16_t c);
    void drawEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, int16_t fullH,
                 float openness, float lid, uint8_t brow, int8_t side,
                 uint16_t cTop, uint16_t cBot, bool shine);
    void drawHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R);
    // Where the eye is on the row at height y (px from the eye centre): visible interval [xa, xb], plus the
    // middle |x| < hole that the lower lid removes. grow widens the outline (halo layers).
    bool eyeSpan(const EyeShape &s, int8_t side, float y, float grow, float &xa, float &xb, float &hole);
    // Parametric eye: openness 0..1 is the blink (scales the height), side = -1 left eye / +1 right eye.
    void drawEyeShape(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                      uint16_t cTop, uint16_t cBot, bool shine);
    EyeMood pickEyeMood(bool sleepy, Reaction reaction, Expr ex);
    // Preset -> spring targets. scale multiplies w/h/radius (1.25 = flinch, 0 = eyes shrink away for a decoration).
    // gazeN = -1..1 gaze direction (for the parallax); lookPx = gaze offset in px (not sprung).
    void eyesSetTargets(EyeMood left, EyeMood right, float scale, float lookPxX, float lookPxY, float gazeN, int16_t R);
    void eyesSetFast(bool on);              // anticipation: much stiffer springs for a moment
    void eyesSetBreath(float f);            // eye height factor (breathing), ~0.98..1.02
    void eyeCloseLid(EyeShape &s, float k); // bring the upper lid down by k (0..1)
    bool exprAnticipating(uint32_t now);

    // Mouth: hidden while idle (eyes only); reactions / one-off expressions fade it in.
    enum class MouthKind : uint8_t
    {
        NONE,
        SMILE,  // arc
        OPEN_O, // surprise "o"
        CUTE    // "w" of two small bowls
    };
    void eyesSetMouth(MouthKind want); // fades the old one out, then the new one in
    void drawMouth(TFT_eSPI &g, int16_t cx, int16_t my);
    float eyesMouthAlpha();         // current fade 0..1
    void eyesSetAffection(float a); // 0..1: faint permanent cheeks (hook, not wired to anything yet)
    // Three concentric ellipses (tint 0.12 / 0.20 / 0.30 x strength).
    void drawCheek(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t rx, int16_t ry, float strength);
    constexpr uint16_t ANTICIPATE_MS = 80;
    void eyesSetDecor(bool on);        // a decorative pet face (heart, spiral ...) wants to take over from the eyes
    float eyesDecorPop();              // 0..1 (slightly more while overshooting): how far that decoration has grown
    void eyesStep(uint32_t now);       // advance all springs by the real frame time (max 0.05 s)
    EyeShape eyeShapeNow(uint8_t eye); // 0 = left, 1 = right
    Reaction pickReaction(const SceneInfo &scn, bool offline, bool sleepy);
    float updateExpression(uint32_t now, bool can, bool sleepy);
    void drawStars(TFT_eSPI &g, uint32_t now);
    void updateBlink(uint32_t now, bool sleepy);
    void updateLook(uint32_t now, bool alarm, bool sleepy, Reaction reaction);
    void drawDrop(TFT_eSPI &g, int16_t x, int16_t y, int16_t r, uint16_t c);
    void drawSleepZ(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t R);
    void drawFace(TFT_eSPI &g, uint32_t now, bool alarm, bool happy, bool purr, bool sleepy,
                  const SceneInfo &scn, Reaction reaction, float exprP);

    // ---- Time-of-day theme ----
    extern uint32_t themeStamp;
    extern float themeGlow; // horizon glow strength of the current time-of-day look (0..1)
    void themeUpdate(bool haveTime, const struct tm &ti, uint32_t now);

    // ---- Digits ----
    constexpr int16_t DIG_PAD = 1;
    extern bool digSprOk;
    extern int8_t digCur[4];
    extern int8_t digPrev[4];
    extern uint32_t digChangeMs[4];
    uint8_t colonLevelNow(uint32_t now);
    void clockMetrics(int16_t &dw, int16_t &dh);
    void drawAnimDigit(int16_t x, int16_t y, int16_t dw, int16_t dh, int8_t oldD, int8_t newD,
                       float p, uint16_t on);
    void drawColonGlow(TFT_eSPI &g, int16_t cx, int16_t y, int16_t h, int16_t t, uint16_t on, uint8_t level);
    void drawSegDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int8_t d,
                      uint16_t on, uint16_t off);
    void drawSegNumber(TFT_eSPI &g, int16_t x, int16_t y, int16_t dw, int16_t dh, int16_t gap,
                       const int8_t *digits, uint8_t count, uint16_t on);
    // One digit rolling from oldD to newD (p 0..1), clipped to its own cell. dir > 0: the digits move up
    // (new one comes from below), dir < 0: down. Draws into the info-panel SPRITE only (uses a viewport).
    void drawRollDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t dw, int16_t dh, int8_t oldD, int8_t newD,
                       float p, int8_t dir, uint16_t on);

    // ---- Scene / weather glyphs ----
    void drawSpinner(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c);
    void drawCloud(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c);
    void drawSceneBackground(TFT_eSPI &g, uint32_t now, const SceneInfo &scn);
    void drawWeatherIcon(TFT_eSPI &g, int code, int16_t cx, int16_t cy, int16_t r, uint32_t now);

    // ---- Info panel ----
    const int MODE_COUNT = 3; // CLOCK, DATE, WEATHER (the DIAG page is opened by holding Mode, it has no dot)
    extern DisplayMode lastMode;
    extern uint32_t lastInfoSig;
    void drawInfoPanel(DisplayMode mode, bool alarm, bool haveTime, const struct tm &ti, uint32_t now);

// ---- Info-panel widgets ----
#if ALARM_ICON_ENABLED
    extern bool alarmCfgOn;
    void pollAlarmCfg(uint32_t now);
    void drawAlarmBadge(TFT_eSPI &g, bool ringing, int16_t cy, const struct tm &ti);
#endif
    // settleMs: when the panel has finished sliding in (a roll starts then); pass `now` if it is not sliding.
    void drawTempReadout(TFT_eSPI &g, float tempC, int16_t dw, int16_t dh, int16_t gap, int16_t dy, uint16_t accent,
                         uint32_t now, uint32_t settleMs);
    bool tempRollActive(float tempC, uint32_t now); // a temperature roll is running or about to start
    uint32_t barsGrowTotalMs(uint8_t barCount);     // how long the hourly bars take to grow in
    void drawViewDots(TFT_eSPI &g, uint8_t active, uint16_t accent, int16_t y);
    // growMs: time since the bars started growing (negative = not yet, large = fully grown).
    void drawWeatherDetails(TFT_eSPI &g, const WeatherDetail &d, uint16_t accent, int32_t growMs);

    // ---- Full-screen pages, status icons, backlight ----
    extern int otaDrawnPct;
    extern uint32_t wifiDownSinceMs;
    extern bool diagShown;
    extern uint32_t lastDiagDrawMs;
    void drawPortalScreen();
    void drawOtaScreen(int pct);
    void pollWifi(uint32_t now);
    void drawStatusIcons(TFT_eSPI &g, uint32_t now);
    void drawHoldRing(TFT_eSPI &g);
    void drawDiagScreen(uint32_t now);
    void backlightInit();
    void backlightUpdate(bool night, bool jump);

} // namespace disp