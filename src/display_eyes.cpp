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
    uint32_t blinkPhaseStartMs = 0;                     // display_init() starts it
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
        if (corner > w / 2)
            corner = w / 2;
        if (corner < 1)
            corner = 1;
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

    // One glowing LED-style eye, described by an EyeShape (see display_internal.h).
    //  openness: 0..1 blink (scales the height around the eye centre)
    //  side:     -1 left eye / +1 right eye (decides which corner is the "inner" one)
    // Upper lid: a straight cut, depth at the inner edge = lidTop + slant/2, at the outer edge = lidTop - slant/2.
    // Lower lid: a rounded cut rising from the bottom edge (happy "^ ^"), with a blush underneath.
    void drawEyeShape(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                      uint16_t cTop, uint16_t cBot, bool shine)
    {
        int16_t w = (int16_t)(s.w + 0.5f);
        int16_t fullH = (int16_t)(s.h + 0.5f);
        if (w < 3 || fullH < 3) // shrunk away (cross-fade to a decoration)
            return;
        cx += (int16_t)lroundf(s.x);
        cy += (int16_t)lroundf(s.y);

        int16_t h = (int16_t)(fullH * openness);
        if (h < 3)
            h = 3;
        int16_t corner = (int16_t)(s.radius + 0.5f);
        int16_t top = cy - h / 2;

        float lt = clamp01(s.lidTop), lb = clamp01(s.lidBot);
        float dIn = clamp01(lt + s.slant * 0.5f);  // lid depth at the inner edge (fraction of h)
        float dOut = clamp01(lt - s.slant * 0.5f); // ... at the outer edge

        // soft glow halo (two dim rounded rects behind the eye); shorter when the lower lid is up
        int16_t haloH = h - (int16_t)(h * lb * 0.6f);
        if (haloH > w / 2)
        {
            g.fillRoundRect(cx - w / 2 - u(4), top - u(4), w + u(8), haloH + u(8), corner + u(4), tint(cTop, 0.10f));
            g.fillRoundRect(cx - w / 2 - u(2), top - u(2), w + u(4), haloH + u(4), corner + u(2), tint(cTop, 0.22f));
        }

        drawGradientEye(g, cx, cy, w, h, corner, cTop, cBot);

        // upper lid: slanted cut painted in the background colour (covers part of the eye + halo)
        if (dIn > 0.01f || dOut > 0.01f)
        {
            float m = (float)u(5); // reach a bit beyond the eye so the halo is covered too
            float xo = cx + side * (w / 2.0f), xi = cx - side * (w / 2.0f);
            float yo = top + dOut * h, yi = top + dIn * h;
            float slope = (yi - yo) / (xi - xo);
            float x1 = xo + side * m, y1 = yo + slope * (x1 - xo);
            float x2 = xi - side * m, y2 = yi + slope * (x2 - xi);
            float y0 = fminf(fminf(y1, y2), (float)top) - u(5); // halo reaches u(4) above the eye
            g.fillTriangle((int16_t)x1, (int16_t)y0, (int16_t)x2, (int16_t)y0, (int16_t)x2, (int16_t)y2, C_BG);
            g.fillTriangle((int16_t)x1, (int16_t)y0, (int16_t)x2, (int16_t)y2, (int16_t)x1, (int16_t)y1, C_BG);
        }

        // lower lid: an upward bulging cut (upper half of an ellipse centred on the bottom edge)
        if (lb > 0.01f)
        {
            int16_t yb = top + h;
            int16_t ry = (int16_t)(lb * h);
            float rx = w / 2.0f + u(3);
            int16_t lim = w / 2 + u(4);
            for (int16_t dy = 1; dy <= ry; dy++)
            {
                float q = (dy - 0.5f) / (float)ry;
                float hw = rx * sqrtf(fmaxf(0.0f, 1.0f - q * q));
                int16_t ihw = (int16_t)(hw + 0.5f);
                if (ihw > lim)
                    ihw = lim;
                if (ihw < 1)
                    continue;
                g.drawFastHLine(cx - ihw, yb - dy, 2 * ihw + 1, C_BG);
            }
            if (lb > 0.2f) // blush, fades in with the smile
                g.fillEllipse(cx, yb + u(3), w / 4 + u(1), imax(2, w / 8), tint(C_CHEEK, clamp01((lb - 0.2f) * 3.3f)));
        }

        // glossy highlights (kept inside the part that is still visible)
        int16_t visTop = top + (int16_t)(h * fmaxf(dIn, dOut));
        int16_t visBot = top + h - (int16_t)(h * lb);
        int16_t visH = visBot - visTop;
        if (shine && visH > w / 3)
        {
            int16_t sr = imax(2, w / 6);
            g.fillCircle(cx - w / 4, visTop + visH / 4 + sr / 2, sr, C_WHITE);
            if (lb < 0.15f)
                g.fillCircle(cx + w / 5, top + h - h / 5, imax(1, sr / 2), mixColor(C_EYE_TOP, C_WHITE, 0.6f));
        }
    }

    // Old-style entry point (alarm face and the pet reactions use it): flat lid + optional brow.
    // lid: 0..1 flat top lid   brow: BROW_ANGRY (inner side lowered) / BROW_SAD (outer side lowered)
    void drawEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, int16_t fullH,
                 float openness, float lid, uint8_t brow, int8_t side,
                 uint16_t cTop, uint16_t cBot, bool shine)
    {
        EyeShape s = {};
        s.w = w;
        s.h = fullH;
        s.radius = w * 0.36f;
        s.lidTop = lid;
        if (brow == BROW_ANGRY || brow == BROW_SAD)
        {
            s.lidTop = fmaxf(lid, 0.30f);
            s.slant = brow == BROW_ANGRY ? 0.6f : -0.6f;
        }
        drawEyeShape(g, cx, cy, s, openness, side, cTop, cBot, shine);
    }

    // =====================================================================
    //  Eye presets + spring rig
    // =====================================================================

    // Sizes are in units of R (R = W/8, so 1 R = 16 px on the 128 px display).
    struct EyePreset
    {
        float w, h, radius, lidTop, slant, lidBot;
    };
#define SYM(w, h, r, lt, sl, lb)                       \
    {                                                  \
        {w, h, r, lt, sl, lb}, { w, h, r, lt, sl, lb } \
    } // same for both eyes

    // One row per expression: {left eye}, {right eye}. Add a row here (and in EyeMood) for a new expression.
    static const EyePreset PRESETS[(uint8_t)EyeMood::COUNT][2] = {
        /* NEUTRAL    32x36 */ SYM(2.0000f, 2.2500f, 0.72f, 0.00f, 0.0f, 0.0f),
        /* HAPPY      32x34 */ SYM(2.0000f, 2.1250f, 0.72f, 0.00f, 0.0f, 0.5f),
        /* ANGRY      32x34 */ SYM(2.0000f, 2.1250f, 0.72f, 0.35f, +0.6f, 0.0f),
        /* SAD        32x36 */ SYM(2.0000f, 2.2500f, 0.72f, 0.30f, -0.6f, 0.0f),
        /* TIRED      32x34 */ SYM(2.0000f, 2.1250f, 0.72f, 0.50f, 0.0f, 0.0f),
        /* SURPRISED  37x42 */ SYM(2.3125f, 2.6250f, 0.90f, 0.00f, 0.0f, 0.0f),
        /* SUSPICIOUS       */ {{2.0f, 2.25f, 0.72f, 0.50f, 0.0f, 0.0f}, {2.0f, 2.25f, 0.72f, 0.00f, 0.0f, 0.0f}},
        /* HOT              */ SYM(2.0000f, 2.2500f, 0.72f, 0.28f, 0.0f, 0.0f),
        /* COLD             */ SYM(2.0000f, 2.2500f, 0.72f, 0.15f, 0.0f, 0.0f),
    };

    enum EyeParam : uint8_t
    {
        P_W,
        P_H,
        P_RADIUS,
        P_LID_TOP,
        P_SLANT,
        P_LID_BOT,
        P_X,
        P_Y,
        P_COUNT
    };

    static Spring eyeSp[2][P_COUNT];
    static Spring decorSp; // 0 = eyes, 1 = a decorative pet face (heart / spiral ...) fully grown
    static bool rigInit = false;
    static uint32_t rigLastMs = 0;

    void eyesSetTargets(EyeMood left, EyeMood right, float scale, float lookPxX, float lookPxY, int16_t R)
    {
        const EyeMood mood[2] = {left, right};
        for (uint8_t i = 0; i < 2; i++)
        {
            const EyePreset &p = PRESETS[(uint8_t)mood[i]][i];
            Spring *sp = eyeSp[i];
            sp[P_W].target = p.w * R * scale;
            sp[P_H].target = p.h * R * scale;
            sp[P_RADIUS].target = p.radius * R * scale;
            sp[P_LID_TOP].target = p.lidTop;
            sp[P_SLANT].target = p.slant;
            sp[P_LID_BOT].target = p.lidBot;
            sp[P_X].target = lookPxX;
            sp[P_Y].target = lookPxY;
            if (!rigInit) // first frame: start in the pose, no grow-in from zero
                for (uint8_t k = 0; k < P_COUNT; k++)
                {
                    sp[k].x = sp[k].target;
                    sp[k].v = 0;
                }
        }
        rigInit = true;
    }

    void eyesSetDecor(bool on) { decorSp.target = on ? 1.0f : 0.0f; }

    float eyesDecorPop() { return decorSp.x < 0 ? 0 : decorSp.x; }

    void eyesStep(uint32_t now)
    {
        float dt = rigLastMs ? (now - rigLastMs) / 1000.0f : 0.033f;
        rigLastMs = now;
        if (dt > 0.05f)
            dt = 0.05f;
        // Sub-steps of <= 8 ms: the overshoot (~9 %) then does not depend on the frame rate
        // (one big Euler step per 33 ms frame would damp it down to ~3 %).
        uint8_t n = (uint8_t)(dt / 0.008f) + 1;
        float h = dt / n;
        for (uint8_t j = 0; j < n; j++)
        {
            for (uint8_t i = 0; i < 2; i++)
                for (uint8_t k = 0; k < P_COUNT; k++)
                    springStep(eyeSp[i][k], h);
            springStep(decorSp, h);
        }
    }

    EyeShape eyeShapeNow(uint8_t eye)
    {
        const Spring *sp = eyeSp[eye & 1];
        EyeShape s;
        s.w = fmaxf(0.0f, sp[P_W].x); // springs overshoot: keep sizes non-negative
        s.h = fmaxf(0.0f, sp[P_H].x);
        s.radius = fmaxf(0.0f, sp[P_RADIUS].x);
        s.lidTop = clamp01(sp[P_LID_TOP].x);
        s.slant = sp[P_SLANT].x < -1 ? -1 : (sp[P_SLANT].x > 1 ? 1 : sp[P_SLANT].x);
        s.lidBot = clamp01(sp[P_LID_BOT].x);
        s.x = sp[P_X].x;
        s.y = sp[P_Y].x;
        return s;
    }

    // Which preset the situation asks for. (A wink is handled by the caller: right eye = HAPPY.)
    EyeMood pickEyeMood(bool sleepy, Reaction reaction, Expr ex)
    {
        if (ex == Expr::SURPRISE)
            return EyeMood::SURPRISED;
        switch (reaction)
        {
        case Reaction::WORRIED:
        case Reaction::STORM:
        case Reaction::SAD:
            return EyeMood::SAD;
        case Reaction::HOT:
            return EyeMood::HOT;
        case Reaction::COLD:
            return EyeMood::COLD;
        default:
            break;
        }
        return sleepy ? EyeMood::TIRED : EyeMood::NEUTRAL;
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
        // no smoothing here any more: the x/y springs of the eyes do it (and add a little overshoot)
        lookX = lookTX;
        lookY = lookTY;
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