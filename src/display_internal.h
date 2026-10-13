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
#include "occasion.h"
#include "alarm_ctrl.h"
#include "reminders.h"
#include "display_fonts.h" // smooth (VLW) fonts, see tools/make_vlw.py

namespace disp
{

    // ---- Palette (RGB565) ----
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // The five colours below follow the time of day (display_theme.cpp updates them).
    extern uint16_t C_BG;           // background
    extern uint16_t C_ACCENT;       // clock digits, accent lines, bars
    extern uint16_t C_EYE_TOP;      // eye gradient, top    (what is drawn: the time-of-day colour mixed with the feeling)
    extern uint16_t C_EYE_BOT;      // eye gradient, bottom
    extern uint16_t C_EYE_TOP_BASE; // the time-of-day eye colours alone (display_theme.cpp),
    extern uint16_t C_EYE_BOT_BASE; // emoUpdate() mixes them with the emotion into C_EYE_TOP/BOT
    extern uint16_t C_FRAME;        // darker frame around / between the two cards (display_core.cpp)
    extern uint16_t C_GHOST;        // unlit 7-seg segments, empty bars
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
    // W = width of a CARD (all face / info drawing uses it), SW = width of the screen, H = height of the
    // screen. faceH / infoH = card heights; faceY / infoY = screen y of the cards' top edges.
    extern TFT_eSPI tft;
    extern int16_t W, SW, H, faceH, infoH;
    extern int16_t cardX, cardGap, cardGapBase, cardR, faceY, infoY;
    extern float sc; // scale relative to the 128 px wide real display (from SW, not W)

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

    // Which corners of a pushed card get the rounded cut (the full-screen pages keep the middle ones square).
    enum : uint8_t
    {
        CORNERS_TOP = 1,
        CORNERS_BOT = 2,
        CORNERS_ALL = 3
    };

    // ---- Off-screen canvases ----
    struct Canvas
    {
        TFT_eSprite spr;
        bool ok = false;
        int16_t x0 = 0, y0 = 0, w = 0, h = 0; // screen position / size of the card (h and y0 follow geometrySet())
        int16_t sh = 0;                       // real height of the sprite: >= h (the Date page uses a taller / shorter card)
        Canvas() : spr(&tft) {}
        bool init(int16_t x, int16_t y, int16_t width, int16_t height, int16_t spriteH = 0)
        {
            x0 = x;
            y0 = y;
            w = width;
            h = height;
            sh = spriteH > height ? spriteH : height;
            spr.setColorDepth(16);
            ok = (spr.createSprite(w, sh) != nullptr);
            return ok;
        }
        TFT_eSPI &begin()
        {
            if (ok)
                return spr;
            tft.setViewport(x0, y0, w, h, true);
            return tft;
        }
        void end() { endAt(0); }
        // Push with a horizontal offset dx (page slide-in): the sprite is clipped to the card, the part left
        // of dx keeps the old picture. The viewport fallback cannot offset, so it just draws in place.
        // Afterwards the card corners are cut with the frame colour (see display_core.cpp).
        void endAt(int16_t dx, uint8_t corners = CORNERS_ALL);
    };
    extern Canvas faceCv, infoCv;
    extern TFT_eSprite digSpr; // clock-digit animation sprite (display_digits.cpp)
    extern bool alarmFlashOn;

    // ---- Cards / frame (display_core.cpp) ----
    void cardsInit();                                         // computes the layout from the screen size, the corner table, C_FRAME
    void geometrySet(bool dateLayout, bool seamless = false); // card split: the Date page has a shorter upper card; seamless = one page, no strip
    extern int16_t faceHMax, infoHMax;                        // tallest the two cards ever get (= the sprite heights)
    void frameUpdate();                                       // keeps the frame colour in step with the theme; repaints it when needed
    extern bool frameDirty;                                   // a full-screen page (OTA, portal) overwrote the frame: repaint it

    // ---- State readers (display_core.cpp) ----
    struct WeatherDetail
    {
        bool valid;
        float maxC, minC;
        int humidityPct; // -1 = unknown
        float windKmh;   // < 0 = unknown
        int windDirDeg;  // -1 = unknown
        float feelsC;    // "feels like"; NAN = unknown
        int pressureHpa; // -1 = unknown
        float uvMax;     // today's highest UV index; < 0 = unknown
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
    bool readTlsActive(); // a TLS connection is open or about to be (weather, update): free the sprites
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

    // ---- Persona: behaviours that span many frames (display_persona.cpp) ----
    // personaUpdate() runs once per tick, after the reaction is known, and fills `persona`; drawFace(),
    // updateLook(), updateBlink(), the info panel and display_tick() read it.
    enum class PStage : uint8_t
    {
        RUN,
        BOOT_BLACK, // wake-up, first 300 ms: the face card is black
        BOOT_LINE   // 300-700 ms: a thin line grows from the middle
    };
    struct Persona
    {
        PStage stage = PStage::BOOT_BLACK;
        float lineP = 0;       // BOOT_LINE: 0..1 how far the line has grown
        float lidBias = 0;     // 0..1: extra upper-lid closing for the eye rig (drowsy, sulk, squint, sigh)
        bool lidRamp = false;  // lids are driven by lidBias, not by the TIRED preset
        bool asleep = false;   // closed contented eyes + Zs (drawSleepFace)
        bool snoozed = false;  // "five more minutes" face
        bool frown = false;    // woken by a touch: small frown first
        bool thinking = false; // time / WiFi not there yet: look up-right, three dots
        bool exprOk = true;    // random idle expressions allowed
        bool sulking = false;
        float scalePulse = 1.0f;  // eye size factor (the chime widens them once)
        float peek = 0;           // 0 normal .. 1 hidden below the card edge
        bool shiver = false;      // small tremble (before the alarm)
        float digitPulse = 0;     // 0..1 the clock digits flash (top of the hour)
        float relief = 0;         // 0..1 progress of the sigh when WiFi came back (0 = none)
        bool flyOn = false;       // the virtual fly is on screen (drawPersonaExtras)
        float flyX = 0, flyY = 0; // where: x -1..1 across the card (beyond = off screen), y -1 (up) .. 1 (down)
        bool celestOk = false;    // the sun / moon is up there: a share of idle looks goes to it
        float celestX = 0, celestY = 0;
        bool gazeOn = false;     // the persona decides where to look
        bool gazeDirect = false; // ... and sets the gaze itself (roll, peek) instead of a saccade target
        float gazeX = 0, gazeY = 0;
    };
    extern Persona persona;
    struct PersonaIn
    {
        uint32_t now;
        bool alarm; // alarm face is up
        bool happy; // a pet reaction is showing
        bool haveTime;
        const struct tm *ti;
        bool night;          // sun_is_night
        bool sleepy;         // night && !alarm (the old "tired eyes" flag)
        bool sunny;          // bright clear day around noon
        bool sunrise;        // the warm sunrise flash is running
        bool offline;        // WiFi gone long enough for the worried eyes
        uint32_t wifiDownMs; // 0 = connected
        Reaction reaction;
        bool weatherKnown;
        int weatherCode;
        AlarmState alarmState;
        int alarmSecsLeft; // seconds until the next alarm ring when <= PRE_ALARM_S, else -1
        bool celestOk;     // the sun / moon is visible: gaze target below
        float celestX, celestY;
    };
    void personaUpdate(const PersonaIn &in);
    struct TouchResult
    {
        PetKind kind;     // the reaction to show (may differ from what pet.cpp picked)
        uint32_t frownMs; // wait this long (a frown) before the reaction starts
    };
    TouchResult personaOnTouch(const PetEvent &pe, uint32_t now);
    void drawBootLine(TFT_eSPI &g, int16_t cy, int16_t R);
    void drawSleepFace(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t lcx, int16_t rcx, int16_t R);
    void drawPersonaExtras(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t R, int16_t eyeH);
    void drawContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R);
    // hooks into the blink / expression machines (display_eyes.cpp)
    void blinkHold(bool on);                       // keep the lids shut (boot)
    void blinkOpenSlow(uint32_t now, uint16_t ms); // release the lids: they open over ms
    void blinkTrigger(uint32_t now);               // one blink right now
    void eyesSetLidBias(float v);
    void exprForce(Expr e, uint32_t now, uint32_t durMs);
    // where the sun / moon is, as a gaze target (display_scene.cpp)
    bool celestialGaze(const SceneInfo &scn, float &gx, float &gy);
    // seconds until the next alarm ring if it is within PRE_ALARM_S, else -1 (display_widgets.cpp)
    int alarmSecondsLeft(const struct tm &ti);

    // ---- Emotional eye colour (display_emotion.cpp) ----
    enum class Emo : uint8_t
    {
        NONE,
        LOVE,    // pink #FF5C93
        ANGER,   // toward red
        SLEEPY,  // washed-out and dark
        COLD,    // icy blue
        FOND,    // bond level: a faint pink, all the time
        DEVOTED, // bond level: a stronger pink
        LONELY   // bond level: a little washed out
    };
    Emo emoPick(bool alarm, bool happy, bool purr, bool sleepy, Reaction reaction, const PetBond &bond);
    // Every frame: moves the emoTint spring toward `want` and writes C_EYE_TOP/BOT (= base colours mixed with the feeling).
    void emoUpdate(uint32_t now, Emo want);

    // ---- Ambient FX (display_ambient.cpp) ----
    void ambientUpdate(const SceneInfo &scn, uint32_t now); // every frame: spots the sunrise
    float ambientSunriseFlash(uint32_t now);                // 0..1 strength of the warm flash (0 = none running)
    void drawNightMotes(TFT_eSPI &g, uint32_t now);         // a few faint specks rising slowly

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

    // ---- Page flow: which layout is up, the slide between pages (display_pages.cpp) ----
    // Clock page = face card above the info card. Date / Weather = ONE full-screen page (no eyes), drawn
    // into the same two canvases: the upper half into faceCv, the lower half into infoCv.
    extern bool fullLayout;    // the full-screen layout is the current one (set by display_tick every frame)
    extern int16_t pageSlideX; // horizontal offset of the page that is sliding in; 0 = settled
    bool isFullPage(DisplayMode m);
    bool pageSliding();                                               // this frame is part of a slide (the landing frame included)
    void pageFlowUpdate(DisplayMode shown, bool alarm, uint32_t now); // once per tick, before anything is drawn
    void pageSlideStart(uint32_t now, uint32_t ms);                   // start a slide of both cards (page / weather view change)
    uint32_t pageSlideSettleMs(uint32_t now);                         // when the page has stopped sliding (now if it is not)
    void gapPaint(int16_t dx);                                        // the strip between the two cards: frame colour or page background
    void gapInvalidate();                                             // the strip was wiped by something else: the next gapPaint() repaints it
    void drawFullPage(DisplayMode shown, bool haveTime, const struct tm &ti, uint32_t now);
    extern bool gapIsFull; // what gapPaint() last painted

    // ---- Info panel (clock page) ----
    const int MODE_COUNT = 3; // CLOCK, DATE, WEATHER (the DIAG page is opened by holding Mode, it has no dot)
    extern DisplayMode lastMode;
    extern uint32_t lastInfoSig;
    void drawInfoPanel(bool alarm, bool haveTime, const struct tm &ti, uint32_t now);
    void drawModeDots(TFT_eSPI &g, DisplayMode shown, uint16_t accent, int16_t y);

// ---- Info-panel widgets ----
#if ALARM_ICON_ENABLED
    extern bool alarmCfgOn;
    void pollAlarmCfg(uint32_t now);
    void drawAlarmBadge(TFT_eSPI &g, bool ringing, int16_t cy, const struct tm &ti);
#endif
    // settleMs: when the page has finished sliding in (a roll starts then); pass `now` if it is not sliding.
    // zoneL / zoneR: the part of the width (0..1) the number is centred in.
    void drawTempReadout(TFT_eSPI &g, float tempC, int16_t dw, int16_t dh, int16_t gap, int16_t dy, uint16_t accent,
                         uint32_t now, uint32_t settleMs, float zoneL = 0.50f, float zoneR = 0.98f);
    int16_t tempReadoutDh(TFT_eSPI &g, float tempC, float zoneW, int16_t maxDh); // largest dh (<= maxDh) whose number fits zoneW (fraction of W)
    bool tempRollActive(float tempC, uint32_t now);                              // a temperature roll is running or about to start
    uint32_t barsGrowTotalMs(uint8_t barCount);                                  // how long the hourly bars take to grow in
    void drawViewDots(TFT_eSPI &g, uint8_t active, uint16_t accent, int16_t y);
    void drawWindArrow(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, int deg, uint16_t col);

    // ---- Full-screen pages: Date and Weather (display_page_date.cpp, display_page_weather.cpp) ----
    // Each page is drawn in two halves with canvas coordinates: Top = the upper card (faceCv), Bottom = the lower card (infoCv).
    // Nothing may cross the gap between them, so a page lays its content out from the top edge of the upper half and
    // from the bottom edge of the lower half.
    void drawPageAccent(TFT_eSPI &g, uint16_t accent); // coloured line + soft glow along the top edge
    void drawDatePageTop(TFT_eSPI &g, bool haveTime, const struct tm &ti);
    void drawDatePageBottom(TFT_eSPI &g, bool haveTime, const struct tm &ti);
    struct WeatherPage
    {
        bool valid; // a weather reading exists
        float tempC;
        int code;
        bool stale; // the reading is old (offline / fetches failing)
        uint32_t ageMs;
        WeatherDetail wd;  // wd.valid = the extra details exist
        bool forecastView; // second view: hourly forecast + sun / UV / pressure (first view: now)
        bool twoViews;     // both views available -> view indicator
        uint16_t accent;
        uint32_t now, settleMs;
        int32_t growMs; // ms since the forecast view landed (the bars grow)
        bool haveTime;
        const struct tm *ti;
    };
    void drawWeatherPageTop(TFT_eSPI &g, const WeatherPage &p);
    void drawWeatherPageBottom(TFT_eSPI &g, const WeatherPage &p);

    // ---- Full-screen pages, status icons, backlight ----
    extern int otaDrawnPct;
    extern uint32_t wifiDownSinceMs;
    extern bool diagShown;
    extern uint32_t lastDiagDrawMs;
    void drawPortalScreen();
    void drawOtaScreen(int pct);
    void pollWifi(uint32_t now);
    // ---- Yearly occasions: Halloween, Easter, Christmas, New Year's Eve (display_occasion.cpp) ----
    struct OccState
    {
        Occasion kind;
        int8_t countdown; // New Year's Eve: 10..1 in the last ten seconds, else 0
        bool midnight;    // New Year's Eve: the first seconds of the new year
        uint16_t year;    // the new year to show while `midnight`
    };
    extern OccState occ;
    void occasionUpdate(const struct tm *ti, uint32_t now); // once per tick (ti = nullptr: time not known)
    void occasionTintEyes();                                // call right before drawFace(); the caller restores C_EYE_*
    void drawOccasionBack(TFT_eSPI &g, uint32_t now);       // behind the eyes
    void drawOccasionFront(TFT_eSPI &g, uint32_t now);      // over the eyes (hats, ears, lights ...)

    void drawStatusIcons(TFT_eSPI &g, uint32_t now);
    void drawHoldRing(TFT_eSPI &g);
    void drawDiagScreen(uint32_t now);
    // The full-screen reminder (display_reminder.cpp): drawn into the two cards like the diagnostics page.
    void drawReminderScreen(const ReminderView &rv, uint32_t now);
    void backlightInit();
    void backlightUpdate(bool night, bool jump);

} // namespace disp