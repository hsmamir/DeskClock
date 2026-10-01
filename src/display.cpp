#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>
#include "display.h"
#include "state.h"
#include "pins.h"
#include "config.h"
#include "weather.h"

static TFT_eSPI tft = TFT_eSPI();

// ---- Palette (RGB565) ----
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static constexpr uint16_t C_BG = rgb(6, 9, 26); // deep night blue
static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr uint16_t C_CYAN = rgb(0, 225, 255);
static constexpr uint16_t C_EYE_TOP = rgb(140, 245, 255);
static constexpr uint16_t C_EYE_BOT = rgb(0, 140, 235);
static constexpr uint16_t C_ALARM_TOP = rgb(255, 130, 100);
static constexpr uint16_t C_ALARM_BOT = rgb(210, 15, 25);
static constexpr uint16_t C_RED = rgb(255, 40, 40);
static constexpr uint16_t C_AMBER = rgb(255, 200, 60);
static constexpr uint16_t C_SOFT = rgb(150, 160, 190);
static constexpr uint16_t C_GHOST = rgb(16, 26, 52); // unlit 7-seg segments
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
    void end()
    {
        if (ok)
            spr.pushSprite(0, y0);
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

// ---- Happy (petted) state: the servo only wiggles for a moment, so the
// display keeps the happy face a bit longer on its own ----
static Mood prevMood = Mood::IDLE_BLINK;
static uint32_t happyStartMs = 0;
static uint32_t happyUntilMs = 0;
static const uint16_t HAPPY_HOLD_MS = 2000;

// ---- Info panel de-dup ----
static DisplayMode lastMode = static_cast<DisplayMode>(-1);
static uint32_t lastInfoSig = 0xFFFFFFFFu;
static bool portalScreenShown = false;

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

static void readWeather(bool &valid, float &tempC, int &code)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    valid = g_state.weatherValid;
    tempC = g_state.weatherTempC;
    code = g_state.weatherCode;
    xSemaphoreGive(g_stateMutex);
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
//  slant: angry brow cut-out, side = -1 left eye / +1 right eye
static void drawEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, int16_t fullH,
                    float openness, float lid, bool angry, int8_t side,
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
        g.fillRoundRect(cx - w / 2 - u(4), top - u(4), w + u(8), h + u(8), corner + u(4), dimColor(cTop, 26));
        g.fillRoundRect(cx - w / 2 - u(2), top - u(2), w + u(4), h + u(4), corner + u(2), dimColor(cTop, 55));
    }

    drawGradientEye(g, cx, cy, w, h, corner, cTop, cBot);

    // heavy lid (sleepy): flat top, covers part of the eye + halo
    if (lid > 0.02f)
    {
        int16_t lh = (int16_t)(h * lid);
        g.fillRect(cx - w / 2 - u(6), top - u(6), w + u(12), lh + u(6), C_BG);
    }

    // angry brow: a slanted cut, lower on the inner side
    if (angry)
    {
        int16_t outerX = cx + side * (w / 2 + u(5));
        int16_t innerX = cx - side * (w / 2 + u(5));
        int16_t y0 = top - u(6);
        g.fillTriangle(outerX, y0, innerX, y0, innerX, top + (int16_t)(h * 0.55f), C_BG);
    }

    // glossy highlights (kept below a heavy lid)
    int16_t lidPx = lid > 0.02f ? (int16_t)(h * lid) : 0;
    int16_t visH = h - lidPx;
    if (shine && visH > w / 3)
    {
        int16_t sr = imax(2, w / 6);
        g.fillCircle(cx - w / 4, top + lidPx + visH / 4 + sr / 2, sr, C_WHITE);
        g.fillCircle(cx + w / 5, top + h - h / 5, imax(1, sr / 2), rgb(200, 245, 255));
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

static void updateLook(uint32_t now, bool alarm, bool sleepy)
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

static void drawFace(TFT_eSPI &g, uint32_t now, bool alarm, bool happy, bool sleepy, float blinkOpen)
{
    g.fillRect(0, 0, W, faceH, C_BG);

    // twinkling stars
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
        drawEye(g, lcx + shake, cy, eyeW, eyeH, open, 0, true, -1, C_ALARM_TOP, C_ALARM_BOT, false);
        drawEye(g, rcx + shake, cy, eyeW, eyeH, open, 0, true, 1, C_ALARM_TOP, C_ALARM_BOT, false);
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
        drawHappyEye(g, lcx, cy, R);
        drawHappyEye(g, rcx, cy, R);
        // little hearts float up
        uint32_t age = now - happyStartMs;
        for (uint8_t i = 0; i < 3; i++)
        {
            int32_t a = (int32_t)age - i * 220;
            if (a < 0 || a > 1100)
                continue;
            float p = a / 1100.0f;
            int16_t hx = (int16_t)(W * (0.30f + 0.20f * i)) + (int16_t)(sinf(p * 6.0f + i) * u(4));
            int16_t hy = (int16_t)(cy - R - u(2) - p * (cy - R - u(8)));
            drawHeart(g, hx, hy, u(4), dimColor(C_HEART, (uint8_t)(255 - p * 140)));
        }
        return;
    }

    // normal / sleepy eyes
    int16_t ox = (int16_t)(lookX * u(5));
    int16_t oy = (int16_t)(lookY * u(4));
    float open = blinkOpen;
    float lid = 0;
    if (sleepy)
    {
        lid = 0.5f;
        open = blinkOpen; // still blinks, slowly
    }
    drawEye(g, lcx + ox, cy + oy, eyeW, eyeH, open, lid, false, -1, C_EYE_TOP, C_EYE_BOT, true);
    drawEye(g, rcx + ox, cy + oy, eyeW, eyeH, open, lid, false, 1, C_EYE_TOP, C_EYE_BOT, true);

    if (sleepy)
    {
        g.setTextDatum(MC_DATUM);
        for (uint8_t k = 0; k < 3; k++)
        {
            float t = (float)((now / 35 + k * 40) % 120) / 120.0f;
            int16_t zx = (int16_t)(W * 0.70f) + (int16_t)(t * u(16)) + k * u(3);
            int16_t zy = (int16_t)(cy - R - t * (cy - R - u(6)));
            g.setTextColor(dimColor(C_ICE, (uint8_t)(230 - t * 200)));
            g.drawString(k == 0 ? "z" : "Z", zx, zy, 2);
        }
    }
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

static void drawSegDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int8_t d,
                         uint16_t on, uint16_t off)
{
    int16_t t = imax(2, w / 5);
    int16_t vh = (h - 3 * t) / 2;
    uint8_t m = (d >= 0 && d <= 10) ? SEG_MASK[d] : 0;
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
    for (uint8_t i = 0; i < 7; i++)
        g.fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, t / 2, ((m >> i) & 1) ? on : off);
}

static void drawColon(TFT_eSPI &g, int16_t cx, int16_t y, int16_t h, int16_t t, uint16_t c)
{
    g.fillRoundRect(cx - t / 2, y + h / 3 - t / 2, t, t, t / 2, c);
    g.fillRoundRect(cx - t / 2, y + 2 * h / 3 - t / 2, t, t, t / 2, c);
}

static void drawSpinner(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
{
    uint8_t head = (millis() / 90) % 8;
    for (uint8_t i = 0; i < 8; i++)
    {
        float a = i * ((float)M_PI / 4.0f);
        uint8_t age = (uint8_t)((head - i) & 7);
        g.fillCircle(cx + (int16_t)(r * cosf(a)), cy + (int16_t)(r * sinf(a)),
                     imax(1, u(2)), dimColor(c, (uint8_t)(255 - age * 30)));
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

static void drawModeDots(TFT_eSPI &g, DisplayMode shown, uint16_t accent)
{
    int idx = (int)shown;
    int16_t y = infoH - u(5);
    for (int i = 0; i < 3; i++)
    {
        int16_t x = W / 2 + (i - 1) * u(10);
        if (i == idx)
            g.fillCircle(x, y, imax(2, u(2.5f)), accent);
        else
            g.fillCircle(x, y, imax(1, u(1.5f)), dimColor(accent, 70));
    }
}

// Draws digits from a small int array, returns nothing. count digits wide.
static void drawSegNumber(TFT_eSPI &g, int16_t x, int16_t y, int16_t dw, int16_t dh, int16_t gap,
                          const int8_t *digits, uint8_t count, uint16_t on)
{
    for (uint8_t i = 0; i < count; i++)
        drawSegDigit(g, x + i * (dw + gap), y, dw, dh, digits[i], on, C_GHOST);
}

static void drawInfoPanel(DisplayMode mode, bool alarm, bool haveTime, const struct tm &ti, uint32_t now)
{
    DisplayMode shown = alarm ? DisplayMode::CLOCK : mode; // an alarm always shows the time
    bool colonOn = ((now / 500) % 2) == 0;

    bool wValid = false;
    float tempC = 0;
    int wCode = 0;
    if (shown == DisplayMode::WEATHER)
        readWeather(wValid, tempC, wCode);

    // ---- signature: only redraw when something visible changed ----
    uint32_t sig = 0;
    switch (shown)
    {
    case DisplayMode::CLOCK:
        if (haveTime)
            sig = (1u << 28) | ((uint32_t)(ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec) << 2) |
                  (colonOn ? 2u : 0u) | (alarmFlashOn ? 1u : 0u);
        else
            sig = (2u << 28) | (now / 90);
        break;
    case DisplayMode::DATE:
        sig = (3u << 28) | (haveTime ? (uint32_t)(ti.tm_mday | (ti.tm_mon << 5) | ((ti.tm_year & 0x7F) << 9)) : 0u);
        break;
    default:
        sig = (4u << 28) | ((now / 120) & 0xFF) | (((uint32_t)((int)lroundf(tempC) + 128) & 0xFF) << 8) |
              (((uint32_t)wCode & 0x7F) << 16) | (wValid ? (1u << 23) : 0u);
        break;
    }
    if (shown == lastMode && sig == lastInfoSig)
        return;
    lastMode = shown;
    lastInfoSig = sig;

    uint16_t accent = shown == DisplayMode::CLOCK ? C_CYAN : (shown == DisplayMode::DATE ? C_AMBER : C_SOFT);
    if (shown == DisplayMode::WEATHER && wValid)
        accent = tempColor(tempC);
    if (alarm)
        accent = alarmFlashOn ? C_RED : dimColor(C_RED, 90);

    TFT_eSPI &g = infoCv.begin();
    g.fillRect(0, 0, W, infoH, C_BG);

    // accent line with a soft glow underneath
    g.fillRect(0, 0, W, imax(2, u(2)), accent);
    g.fillRect(0, imax(2, u(2)), W, imax(1, u(1)), dimColor(accent, 60));

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
            int8_t d[4] = {(int8_t)(ti.tm_hour / 10), (int8_t)(ti.tm_hour % 10),
                           (int8_t)(ti.tm_min / 10), (int8_t)(ti.tm_min % 10)};
            int16_t cw = imax(4, u(8));
            int16_t total = 4 * dw + 2 * gap + 2 * gap + cw;
            int16_t x0 = (W - total) / 2;
            uint16_t on = alarm ? (alarmFlashOn ? C_RED : dimColor(C_RED, 90)) : C_CYAN;
            drawSegNumber(g, x0, dy, dw, dh, gap, d, 2, on);
            drawColon(g, x0 + 2 * dw + gap + gap + cw / 2, dy, dh, imax(2, dw / 4),
                      colonOn ? on : dimColor(on, 40));
            drawSegNumber(g, x0 + 2 * dw + gap + 2 * gap + cw, dy, dw, dh, gap, d + 2, 2, on);
            strftime(cap, sizeof(cap), "%a %d %b", &ti);

            // seconds progress bar
            int16_t bx = u(12), bw = W - 2 * u(12), bh = imax(2, u(2.5f));
            int16_t by = infoH - u(15);
            g.fillRoundRect(bx, by, bw, bh, bh / 2, C_GHOST);
            int16_t fw = (int16_t)((int32_t)bw * (ti.tm_sec + 1) / 60);
            if (fw > 0)
                g.fillRoundRect(bx, by, fw, bh, bh / 2, accent);
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
            g.drawFastVLine(divX, dy + 2, dh - 4, dimColor(C_AMBER, 70));
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
        if (wValid)
        {
            int16_t r = u(14);
            drawWeatherIcon(g, wCode, (int16_t)(W * 0.22f), dy + dh / 2, r, now);

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

            strncpy(cap, weather_code_to_label(wCode), sizeof(cap) - 1);
            cap[sizeof(cap) - 1] = '\0';
        }
        else
        {
            drawCloud(g, W / 2, dy + dh / 2, u(14), dimColor(C_SOFT, 120));
            strcpy(cap, "No weather yet");
        }
    }

    if (cap[0])
    {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(cap, W / 2, capY, 2);
    }
    drawModeDots(g, shown, accent);
    infoCv.end();
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
    drawWifiIcon(tft, W / 2, (int16_t)(H * 0.34f), u(26), C_CYAN);
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

// =====================================================================
//  Public API
// =====================================================================

void display_init()
{
    // NOTE: PWM dimming via LEDC removed for now — the ledc API differs
    // between arduino-esp32 core versions (ledcAttach vs ledcSetup+
    // ledcAttachPin) and isn't worth pinning down until the core version
    // is fixed. Backlight is just full-on here.
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

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
    Serial.printf("display sprites: face=%s info=%s (free heap %u)\n",
                  f ? "ok" : "fallback", i ? "ok" : "fallback", (unsigned)ESP.getFreeHeap());

    blinkPhaseStartMs = millis();
}

void display_tick()
{
    uint32_t now = millis();
    Mood mood = readMood();
    DisplayMode mode = readDisplayMode();
    bool timeSynced = readTimeSynced();

    // While the WiFi setup portal is open, show how to join it instead of the face.
    if (readPortalActive())
    {
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

    bool alarm = (mood == Mood::ALARM_ALERT);
    bool sleepy = haveTime && ti.tm_hour < 6 && !alarm;

    // petted: hold the happy face for a while even after the servo finishes
    if (mood == Mood::PET_HAPPY && prevMood != Mood::PET_HAPPY)
    {
        happyStartMs = now;
        happyUntilMs = now + HAPPY_HOLD_MS;
    }
    prevMood = mood;
    bool happy = !alarm && now < happyUntilMs;

    if (alarm && now - lastAlarmFlashMs > ALARM_FLASH_MS)
    {
        alarmFlashOn = !alarmFlashOn;
        lastAlarmFlashMs = now;
    }

    // glance down at the panel whenever the page changes
    if (mode != lastMode && !alarm)
        glanceUntilMs = now + 900;

    updateLook(now, alarm, sleepy);
    float blinkOpen = updateBlink(now);

    TFT_eSPI &g = faceCv.begin();
    drawFace(g, now, alarm, happy, sleepy, blinkOpen);
    faceCv.end();

    drawInfoPanel(mode, alarm, haveTime, ti, now);
}