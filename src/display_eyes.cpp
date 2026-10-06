// display_eyes.cpp - eye drawing primitives, idle blink / look-around / expression state machines
// and the reaction picker (what the weather / WiFi make the eyes do).
#include "display_internal.h"

namespace disp
{

// ---- Idle blink state machine ----
enum class BlinkPhase
{
    OPEN,
    CLOSING,
    CLOSED,
    OPENING
};
static BlinkPhase blinkPhase = BlinkPhase::OPENING; // eyes "wake up" at boot
uint32_t blinkPhaseStartMs = 0; // display_init() starts it
static uint32_t nextBlinkAtMs = 0;
static const uint16_t BLINK_TRANSITION_MS = 80;
static const uint16_t BLINK_CLOSED_MS = 100;

// ---- Look-around / glance state ----
float lookX = 0, lookY = 0;
static float lookTX = 0, lookTY = 0;
static uint32_t nextLookAtMs = 0;
uint32_t glanceUntilMs = 0; // display_tick() glances down when the page changes


// =====================================================================
//  Small drawing primitives
// =====================================================================

void drawHeart(TFT_eSPI &g, int16_t x, int16_t y, int16_t s, uint16_t c)
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
void drawEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, int16_t fullH,
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
void drawHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
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

Reaction pickReaction(const SceneInfo &scn, bool offline, bool sleepy)
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

Expr exprCur = Expr::NONE;
static uint32_t exprStartMs = 0, exprDurMs = 0, nextExprAtMs = 0;

// Returns the progress 0..1 of the running expression (0 when none).
float updateExpression(uint32_t now, bool can, bool sleepy)
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


// twinkling stars
void drawStars(TFT_eSPI &g, uint32_t now)
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

float updateBlink(uint32_t now)
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

void updateLook(uint32_t now, bool alarm, bool sleepy, Reaction reaction)
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

void drawDrop(TFT_eSPI &g, int16_t x, int16_t y, int16_t r, uint16_t c)
{
    g.fillCircle(x, y, r, c);
    g.fillTriangle(x, y - 2 * r - r / 2, x - r, y - r / 2, x + r, y - r / 2, c);
}


// "z Z Z" drifting up from the sleepy face (also used by the sleepy pet reaction).
void drawSleepZ(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t R)
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

} // namespace disp
