// display_eyes.cpp - eye drawing primitives, idle blink / look-around / expression state machines
// and the reaction picker (what the weather / WiFi make the eyes do).
#include "display_internal.h"
#include "display_character.h"
#include "audio.h"
#include "weather_mood.h"

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
    static bool blinkWasDouble = false;
    // The upper lid is a spring (0 = open .. BLINK_MAX = closed). The phase timers below only flip its
    // target; closing is stiff (ease-in: it accelerates from rest), opening is softer (ease-out).
    static const float BLINK_MAX = 0.95f; // a thin sliver stays visible when closed
    static Spring blinkSp;
    static float blinkOmega = 60.0f;
    static bool blinkInit = false;
    static bool blinkHeld = false; // boot: the lids stay shut until blinkOpenSlow()

    // ---- Look-around / glance state ----
    float lookX = 0, lookY = 0;          // current gaze, -1..1 (follows the saccade curve)
    float lookTremX = 0, lookTremY = 0;  // micro-tremor in px, added by the caller
    static float lookTX = 0, lookTY = 0; // what the state machine below wants to look at
    static uint32_t nextLookAtMs = 0;
    uint32_t glanceUntilMs = 0; // display_tick() glances down when the page changes

    // =====================================================================
    //  Small drawing primitives
    // =====================================================================

    void drawHeart(TFT_eSPI &g, int16_t x, int16_t y, int16_t s, uint16_t c)
    {
#if CHARACTER_STYLE
        // the heart spans y - s/2 .. y + 3s/2 (width 2 s): its middle is y + s/2; the sprite is 4 px "s" wide
        drawCharMono(g, CharMono::HEART, x, y + s / 2, s / 4.0f, c);
        return;
#endif
        int16_t r = imax(1, s / 2);
        g.fillCircle(x - r, y, r, c);
        g.fillCircle(x + r, y, r, c);
        g.fillTriangle(x - 2 * r, y + r / 2, x + 2 * r, y + r / 2, x, y + 3 * r, c);
    }

    // =====================================================================
    //  Eye geometry: one function says where the eye is on every row
    // =====================================================================

    // Horizontal shrink of a rounded-rect outline at height y (relative to the eye centre).
    // h / radius are those of the outline (already grown for the halo layers).
    static float cornerInset(float y, float h, float radius)
    {
        if (radius < 0.5f)
            return 0;
        float r = y + h * 0.5f; // distance from the top edge
        float d;
        if (r < radius)
            d = radius - r;
        else if (r > h - radius)
            d = r - (h - radius);
        else
            return 0;
        if (d > radius)
            d = radius;
        return radius - sqrtf(radius * radius - d * d);
    }

    // Where is the eye on the row at height y (px, relative to the eye centre, s.h = height as drawn)?
    //   xa..xb : the visible interval (relative to the centre, fractional px)
    //   hole   : > 0 when the lower lid removes the middle |x| < hole of the row (draw [xa,-hole] and [hole,xb])
    //   grow   : widens the outline by this many px (halo layers); the lids still cut it
    //   side   : -1 left eye / +1 right eye (the inner corner is the one facing the face centre)
    // Returns false when nothing of the eye is on this row.
    bool eyeSpan(const EyeShape &s, int8_t side, float y, float grow, float &xa, float &xb, float &hole)
    {
        hole = 0;
        float hh = s.h * 0.5f + grow;
        if (y < -hh || y > hh)
            return false;
        float rad = fminf(s.radius, fminf(s.w, s.h) * 0.5f) + grow;
        float hw = s.w * 0.5f + grow - cornerInset(y, s.h + 2 * grow, rad);
        xa = -hw;
        xb = hw;

        // upper lid: the eye is only there where y >= y0 + m*x
        float lt = clamp01(s.lidTop), lb = clamp01(s.lidBot);
        float dIn = clamp01(lt + s.slant * 0.5f);  // lid depth at the inner edge (fraction of h)
        float dOut = clamp01(lt - s.slant * 0.5f); // ... at the outer edge
        if (dIn > 0.01f || dOut > 0.01f)
        {
            float yo = -s.h * 0.5f + dOut * s.h; // lid line at the outer edge x = side*w/2
            float yi = -s.h * 0.5f + dIn * s.h;  // ... at the inner edge x = -side*w/2
            float m = (yi - yo) / (-side * s.w);
            float y0 = yo - m * (side * s.w * 0.5f);
            if (fabsf(m) < 1e-3f)
            {
                if (y < y0)
                    return false;
            }
            else if (m > 0)
                xb = fminf(xb, (y - y0) / m);
            else
                xa = fmaxf(xa, (y - y0) / m);
        }

        // lower lid: upper half of an ellipse centred on the bottom edge eats the middle of the row
        // (below the bottom edge it keeps eating, so the halo does not leave a slab under the smile)
        if (lb > 0.01f)
        {
            float ry = lb * s.h;
            float dyb = s.h * 0.5f - y; // height above the bottom edge
            if (dyb < ry)
            {
                float q = (dyb > 0 ? dyb : 0.0f) / ry;
                hole = (s.w * 0.5f + u(3)) * sqrtf(fmaxf(0.0f, 1.0f - q * q));
            }
        }

        if (xb <= xa)
            return false;
        if (hole > 0 && xa >= -hole && xb <= hole)
            return false;
        return true;
    }

    // One horizontal run [xa, xb] (px relative to cx, fractional) on row y.
    // rd != nullptr: the two edge pixels are blended with what is already there by their coverage.
    // edgeCov < 1 (first / last row of the eye): everything is blended, i.e. a fainter row.
    static void drawSpanRow(TFT_eSPI &g, TFT_eSprite *rd, float cx, int16_t y, float xa, float xb,
                            uint16_t col, float edgeCov)
    {
        float A = cx + xa, B = cx + xb;
        if (B - A <= 0.02f)
            return;
        int16_t ia = (int16_t)ceilf(A), ib = (int16_t)floorf(B); // fully covered pixels: [ia, ib)
        if (ib < ia)                                             // both ends inside one pixel
        {
            int16_t px = (int16_t)floorf(A);
            float cov = (B - A) * edgeCov;
            if (rd)
                g.drawPixel(px, y, mixColor(rd->readPixel(px, y), col, cov));
            else if (cov > 0.5f)
                g.drawPixel(px, y, col);
            return;
        }
        if (ib > ia)
        {
            if (edgeCov >= 0.999f)
                g.drawFastHLine(ia, y, ib - ia, col);
            else if (rd)
                for (int16_t x = ia; x < ib; x++)
                    g.drawPixel(x, y, mixColor(rd->readPixel(x, y), col, edgeCov));
            else
                g.drawFastHLine(ia, y, ib - ia, mixColor(C_BG, col, edgeCov));
        }
        float cl = (ia - A) * edgeCov, cr = (B - ib) * edgeCov; // partial pixels left / right
        if (cl > 0.02f)
        {
            if (rd)
                g.drawPixel(ia - 1, y, mixColor(rd->readPixel(ia - 1, y), col, cl));
            else if (cl > 0.5f)
                g.drawPixel(ia - 1, y, col);
        }
        if (cr > 0.02f)
        {
            if (rd)
                g.drawPixel(ib, y, mixColor(rd->readPixel(ib, y), col, cr));
            else if (cr > 0.5f)
                g.drawPixel(ib, y, col);
        }
    }

    // A row of the eye as returned by eyeSpan(): split around the lower-lid hole when there is one.
    static void drawEyeRow(TFT_eSPI &g, TFT_eSprite *rd, float cx, int16_t y, float xa, float xb, float hole,
                           uint16_t col, float edgeCov)
    {
        if (hole <= 0)
        {
            drawSpanRow(g, rd, cx, y, xa, xb, col, edgeCov);
            return;
        }
        if (-hole > xa)
            drawSpanRow(g, rd, cx, y, xa, fminf(xb, -hole), col, edgeCov);
        if (hole < xb)
            drawSpanRow(g, rd, cx, y, fmaxf(xa, hole), xb, col, edgeCov);
    }

    // Eye body: vertical gradient, drawn row by row; the shape of every row comes from eyeSpan().
    // Geometry is fractional: a row that is only partly covered by the eye (fractional height / position)
    // is blended by that coverage, so breathing (+-2 % height) and tiny gaze tremors move smoothly.
    static void drawGradientEye(TFT_eSPI &g, TFT_eSprite *rd, float cxf, float cyf, float topf, const EyeShape &d,
                                int8_t side, uint16_t cTop, uint16_t cBot)
    {
        float botf = topf + d.h;
        int16_t y0 = (int16_t)floorf(topf), y1 = (int16_t)ceilf(botf);
        for (int16_t y = y0; y < y1; y++)
        {
            float lo = fmaxf((float)y, topf), hi = fminf((float)y + 1.0f, botf);
            float ov = hi - lo; // how much of this pixel row the eye covers
            if (ov < 0.04f)
                continue;
            float ym = (lo + hi) * 0.5f;
            float xa, xb, hole;
            if (!eyeSpan(d, side, ym - cyf, 0, xa, xb, hole))
                continue;
            drawEyeRow(g, rd, cxf, y, xa, xb, hole, mixColor(cTop, cBot, clamp01((ym - topf) / d.h)), ov);
        }
    }

    // Is g the face sprite? Then readPixel() works and edges can be anti-aliased against the real background.
    // (In the viewport fallback the pixels live in the display and cannot be read back.)
    static TFT_eSprite *readableSprite(TFT_eSPI &g)
    {
        if (faceCv.ok && &g == static_cast<TFT_eSPI *>(&faceCv.spr))
            return &faceCv.spr;
        return nullptr;
    }

    // =====================================================================
    //  Cheeks and mouth (only in reactions / one-off expressions; idle = eyes only)
    // =====================================================================

    static float affection = 0;                   // 0..1: how fond the pet is of you -> faint permanent cheeks
    static MouthKind mouthKind = MouthKind::NONE; // what is drawn right now
    static MouthKind mouthWant = MouthKind::NONE; // what the situation asks for
    static float mouthA = 0;                      // fade 0..1

    void eyesSetAffection(float a) { affection = clamp01(a); }
    void eyesSetMouth(MouthKind want) { mouthWant = want; }
    static float smooth01(float a)
    {
        a = clamp01(a);
        return a * a * (3.0f - 2.0f * a);
    }
    float eyesMouthAlpha() { return smooth01(mouthA); }
    // cheeks follow a smiling mouth: fade in / out with it
    static float smileCheek()
    {
        return (mouthKind == MouthKind::SMILE || mouthKind == MouthKind::CUTE) ? smooth01(mouthA) : 0.0f;
    }

    // Translucent filled ellipse: every pixel is blended with what is already there.
    static void blendEllipse(TFT_eSPI &g, TFT_eSprite *rd, int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t col,
                             float alpha)
    {
        for (int16_t dy = -ry; dy <= ry; dy++)
        {
            float q = 1.0f - (float)(dy * dy) / (float)(ry * ry);
            if (q < 0)
                continue;
            int16_t hw = (int16_t)(rx * sqrtf(q) + 0.5f);
            for (int16_t dx = -hw; dx <= hw; dx++)
                g.drawPixel(cx + dx, cy + dy, mixColor(rd->readPixel(cx + dx, cy + dy), col, alpha));
        }
    }

    // Cheek = three concentric ellipses (outer -> inner), tint 0.12 / 0.20 / 0.30, times strength 0..1.
    // On the face sprite they are translucent pink over the real background (the layers are chained so the
    // centre ends up at exactly 0.30); in the viewport fallback they are opaque tints of the background colour.
    uint16_t cheekColor = C_CHEEK;

    void drawCheek(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t rx, int16_t ry, float strength)
    {
        if (strength < 0.03f)
            return;
        strength = clamp01(strength);
        static const float kr[3] = {1.0f, 0.72f, 0.45f};
        static const float kt[3] = {0.12f, 0.20f, 0.30f}; // cumulative coverage wanted at each ring
        TFT_eSprite *rd = readableSprite(g);
        float prev = 0;
        for (uint8_t i = 0; i < 3; i++)
        {
            int16_t erx = imax(1, (int16_t)(rx * kr[i])), ery = imax(1, (int16_t)(ry * kr[i]));
            float want = kt[i] * strength;
            if (rd)
            {
                float a = prev < 0.999f ? 1.0f - (1.0f - want) / (1.0f - prev) : 0.0f; // alpha of this layer
                if (a > 0.005f)
                    blendEllipse(g, rd, cx, cy, erx, ery, cheekColor, a);
            }
            else
                g.fillEllipse(cx, cy, erx, ery, tint(cheekColor, want));
            prev = want;
        }
    }

    struct Pt
    {
        float x, y;
    };

    static float distSeg(float px, float py, Pt a, Pt b)
    {
        float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? clamp01(((px - a.x) * dx + (py - a.y) * dy) / l2) : 0.0f;
        float qx = a.x + t * dx - px, qy = a.y + t * dy - py;
        return sqrtf(qx * qx + qy * qy);
    }

    // Anti-aliased stroke of width wd along a polyline; alpha fades the whole stroke into the real background.
    static void strokePath(TFT_eSPI &g, TFT_eSprite *rd, const Pt *p, uint8_t n, bool closed, float wd, uint16_t col,
                           float alpha)
    {
        uint8_t segs = closed ? n : n - 1;
        float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
        for (uint8_t i = 1; i < n; i++)
        {
            minx = fminf(minx, p[i].x);
            maxx = fmaxf(maxx, p[i].x);
            miny = fminf(miny, p[i].y);
            maxy = fmaxf(maxy, p[i].y);
        }
        int16_t x0 = (int16_t)floorf(minx - wd * 0.5f - 1), x1 = (int16_t)ceilf(maxx + wd * 0.5f + 1);
        int16_t y0 = (int16_t)floorf(miny - wd * 0.5f - 1), y1 = (int16_t)ceilf(maxy + wd * 0.5f + 1);
        for (int16_t y = y0; y <= y1; y++)
            for (int16_t x = x0; x <= x1; x++)
            {
                float dm = 1e9f;
                for (uint8_t i = 0; i < segs; i++)
                    dm = fminf(dm, distSeg(x + 0.5f, y + 0.5f, p[i], p[(i + 1) % n]));
                float cov = clamp01(wd * 0.5f + 0.5f - dm) * alpha;
                if (cov < 0.04f)
                    continue;
                if (rd)
                    g.drawPixel(x, y, mixColor(rd->readPixel(x, y), col, cov));
                else if (cov > 0.12f)
                    g.drawPixel(x, y, mixColor(C_BG, col, cov));
            }
    }

#if CHARACTER_STYLE
    // The resting mouth of every preset: on the design sheet each expression has its own tiny mouth.
    static EyeMood moodNowV = EyeMood::NEUTRAL; // what eyesSetTargets() was last asked for (left eye; right if the left one winks)
    static CharMouth baseMouthFor(EyeMood m)
    {
        switch (m)
        {
        case EyeMood::TIRED:
        case EyeMood::MISTY:
        case EyeMood::SUSPICIOUS:
            return CharMouth::DASH;
        case EyeMood::ANGRY:
        case EyeMood::SAD:
        case EyeMood::ALARMED:
            return CharMouth::FROWN;
        case EyeMood::SURPRISED:
        case EyeMood::HOT:
            return CharMouth::OPEN_O;
        case EyeMood::CHILLED:
        case EyeMood::COLD:
            return CharMouth::WAVE;
        default: // NEUTRAL, HAPPY, CALM, WONDER, WINDY
            return CharMouth::SMILE;
        }
    }
    static CharMouth mouthShapeFor(MouthKind k)
    {
        switch (k)
        {
        case MouthKind::OPEN_O:
            return CharMouth::OPEN_O;
        case MouthKind::CUTE:
            return CharMouth::CUTE;
        default:
            return CharMouth::SMILE_BIG;
        }
    }
#endif

    // The mouth, centred at (cx, my), faded by the current fade value. Nothing when there is no mouth.
    void drawMouth(TFT_eSPI &g, int16_t cx, int16_t my)
    {
        float a = smooth01(mouthA);
#if CHARACTER_STYLE
        // The character always has a tiny mouth that belongs to the mood (smile, dash, frown, "o", wave ...); it
        // changes with a short cross-fade. A reaction's own mouth (big smile, "o", "w") fades in over it.
        // Not while a yawn draws its own big mouth.
        {
            static CharMouth cur = CharMouth::SMILE, prev = CharMouth::SMILE;
            static uint32_t changedMs = 0;
            const uint32_t nowMs = millis();
            const CharMouth want = persona.thinking ? CharMouth::THINK : baseMouthFor(moodNowV);
            if (want != cur)
            {
                prev = cur;
                cur = want;
                changedMs = nowMs;
            }
            const float t = clamp01((nowMs - changedMs) / 160.0f);
            const bool bigYawn = exprCur == Expr::YAWN || (petEv.kind == PetKind::MORNING && millis() - happyStartMs < 1500);
            if (!bigYawn)
            {
                const float base = mouthKind == MouthKind::NONE ? 1.0f : 1.0f - a;
                if (t < 1.0f)
                    drawCharMouthShape(g, cx, my, prev, base * (1.0f - t));
                drawCharMouthShape(g, cx, my, cur, base * (t < 1.0f ? t : 1.0f));
            }
            if (mouthKind != MouthKind::NONE && a >= 0.04f)
                drawCharMouthShape(g, cx, my, mouthShapeFor(mouthKind), a);
            return;
        }
#endif
        if (mouthKind == MouthKind::NONE || a < 0.04f)
            return;
        TFT_eSprite *rd = readableSprite(g);
        uint16_t col = mixColor(C_EYE_TOP, C_EYE_BOT, 0.25f);
        float wd = fmaxf(1.6f, (float)u(1.8f));
        Pt p[13];
        switch (mouthKind)
        {
        case MouthKind::SMILE: // an arc, ~11 px wide
        {
            float hw = u(5.5f), d = u(3.5f);
            for (uint8_t i = 0; i < 9; i++)
            {
                float sx = (i / 4.0f) - 1.0f;
                p[i] = {cx + hw * sx, my - d * 0.5f + d * (1.0f - sx * sx)};
            }
            strokePath(g, rd, p, 9, false, wd, col, a);
            break;
        }
        case MouthKind::OPEN_O: // small "o"
        {
            float rx = u(2.6f), ry = u(3.4f);
            for (uint8_t i = 0; i < 12; i++)
            {
                float t = i * 6.2831853f / 12.0f;
                p[i] = {cx + rx * cosf(t), my + 1.0f + ry * sinf(t)};
            }
            strokePath(g, rd, p, 12, true, wd, col, a);
            break;
        }
        case MouthKind::CUTE: // "w" turned: two small bowls meeting in the middle
        {
            float hw = u(5.5f), d = u(2.8f);
            for (uint8_t side = 0; side < 2; side++)
            {
                for (uint8_t i = 0; i < 7; i++)
                {
                    float t = i / 6.0f;
                    float x = side == 0 ? cx - hw + hw * t : cx + hw * t;
                    p[i] = {x, my - d * 0.5f + d * sinf(t * 3.14159265f)};
                }
                strokePath(g, rd, p, 7, false, wd, col, a);
            }
            break;
        }
        default:
            break;
        }
    }

    // One glowing LED-style eye, described by an EyeShape (see display_internal.h).
    //  openness: 0..1 scales the height (used by the old-style drawEye callers; the rig blinks with the lid)
    //  side:     -1 left eye / +1 right eye (decides which corner is the "inner" one)
    // Lids are NOT painted over the eye: eyeSpan() shortens every row instead, so the
    // real background (sky, clouds) stays untouched.
    void drawEyeShape(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                      uint16_t cTop, uint16_t cBot, bool shine)
    {
#if CHARACTER_STYLE
        drawCharacterEye(g, cx, cy, s, openness, side, cTop, cBot, shine);
        return;
#endif
        if (s.w < 3 || s.h < 3) // shrunk away (cross-fade to a decoration)
            return;
        float w = s.w;
        float h = fmaxf(3.0f, s.h * openness);
        float cxf = cx + s.x; // fractional centre
        float cyf = cy + s.y;
        float topf = cyf - h * 0.5f;

        EyeShape d = s; // the shape as it is drawn now
        d.w = w;
        d.h = h;
        d.radius = fminf(s.radius, fminf(w, h) * 0.5f);
        TFT_eSprite *rd = readableSprite(g);

        // soft glow: 4 growing layers (outer -> inner), cut by the same lids as the body
        if (h > w * 0.5f)
        {
            static const float growPx[4] = {6, 4, 2, 1};
            static const float tintK[4] = {0.05f, 0.09f, 0.15f, 0.25f};
            for (uint8_t l = 0; l < 4; l++)
            {
                float gr = (float)u(growPx[l]);
                if (gr < 1)
                    gr = 1;
                uint16_t col = tint(cTop, tintK[l]);
                int16_t y0 = (int16_t)floorf(topf - gr), y1 = (int16_t)ceilf(topf + h + gr);
                for (int16_t y = y0; y < y1; y++)
                {
                    float xa, xb, hole;
                    if (eyeSpan(d, side, y + 0.5f - cyf, gr, xa, xb, hole))
                        drawEyeRow(g, nullptr, cxf, y, xa, xb, hole, col, 1.0f);
                }
            }
        }

        drawGradientEye(g, rd, cxf, cyf, topf, d, side, cTop, cBot);

        float lt = clamp01(s.lidTop), lb = clamp01(s.lidBot);
        float dIn = clamp01(lt + s.slant * 0.5f), dOut = clamp01(lt - s.slant * 0.5f);

        // cheeks under the eye: a faint permanent hint (affection), more with a smile / happy lids
        float cheekS = fmaxf(affection * 0.45f, fmaxf(clamp01((lb - 0.2f) * 3.3f), smileCheek()));
        drawCheek(g, (int16_t)lroundf(cxf), (int16_t)lroundf(topf + h) + u(3), (int16_t)(w / 4) + u(1),
                  imax(2, (int16_t)(w / 8)), cheekS);

        // glossy highlights, inside the part that is still visible; they sit 1..2 px AGAINST the gaze
        // (the eye "body" moves with the gaze, the reflection stays behind: a bit of depth)
        int16_t hx = (int16_t)lroundf(-fmaxf(-2.0f, fminf(2.0f, s.x * 0.4f)));
        int16_t hy = (int16_t)lroundf(-fmaxf(-2.0f, fminf(2.0f, s.y * 0.5f)));
        float visTop = topf + h * fmaxf(dIn, dOut);
        float visBot = topf + h - h * lb;
        float visH = visBot - visTop;
        if (shine && visH > w / 3)
        {
            int16_t sr = imax(2, (int16_t)(w / 6));
            g.fillCircle((int16_t)lroundf(cxf - w / 4) + hx, (int16_t)lroundf(visTop + visH / 4) + sr / 2 + hy, sr, C_WHITE);
            if (lb < 0.15f)
                g.fillCircle((int16_t)lroundf(cxf + w / 5) + hx, (int16_t)lroundf(topf + h - h / 5) + hy,
                             imax(1, sr / 2), mixColor(C_EYE_TOP, C_WHITE, 0.6f));
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
        float w, h, radius, lidTop, slant, lidBot, brow; // brow: 0 none, + raised, - lowered flat (display_character.cpp)
    };
#define SYM(w, h, r, lt, sl, lb)                             \
    {                                                        \
        {w, h, r, lt, sl, lb, 0.0f}, { w, h, r, lt, sl, lb, 0.0f } \
    } // same for both eyes

    // One row per expression: {left eye}, {right eye}. Add a row here (and in EyeMood) for a new expression.
    static const EyePreset PRESETS[(uint8_t)EyeMood::COUNT][2] = {
        /* NEUTRAL    32x36 */ SYM(2.0000f, 2.2500f, 0.72f, 0.00f, 0.0f, 0.0f),
        /* HAPPY      32x34 */ SYM(2.0000f, 2.1250f, 0.72f, 0.00f, 0.0f, 0.5f),
        /* ANGRY      32x34 */ SYM(2.0000f, 2.1250f, 0.72f, 0.35f, +0.6f, 0.0f),
        /* SAD        32x36 */ SYM(2.0000f, 2.2500f, 0.72f, 0.30f, -0.6f, 0.0f),
        /* TIRED      32x34 */ SYM(2.0000f, 2.1250f, 0.72f, 0.50f, 0.0f, 0.0f),
        /* SURPRISED  37x42 */ SYM(2.3125f, 2.6250f, 0.90f, 0.00f, 0.0f, 0.0f),
        /* SUSPICIOUS       */ {{2.0f, 2.25f, 0.72f, 0.50f, 0.0f, 0.0f, -1.0f}, {2.0f, 2.25f, 0.72f, 0.00f, 0.0f, 0.0f, 1.0f}}, // half-lidded eye with a flat low brow, the other open with a raised one
        /* HOT              */ SYM(2.0000f, 2.2000f, 0.72f, 0.34f, -0.20f, 0.0f),  // lids droop, the outer corners a little more
        /* COLD             */ SYM(2.0000f, 2.2500f, 0.72f, 0.15f, 0.0f, 0.0f),    // (the squash before a big pet reaction)
        /* CALM       32x35 */ SYM(2.0000f, 2.2000f, 0.72f, 0.12f, -0.10f, 0.06f), // relaxed, no sad brows
        /* ALARMED    36x41 */ SYM(2.2500f, 2.5500f, 0.84f, 0.08f, -0.35f, 0.0f),  // wide and worried
        /* WONDER     34x39 */ SYM(2.1500f, 2.4500f, 0.80f, 0.00f, 0.0f, 0.12f),   // big, bright, a smile underneath
        /* MISTY      32x33 */ SYM(2.0000f, 2.0800f, 0.72f, 0.32f, 0.0f, 0.10f),   // half-lidded
        /* CHILLED    31x34 */ SYM(1.9000f, 2.1000f, 0.68f, 0.28f, 0.0f, 0.24f),   // squeezed shut a little from both sides
        /* WINDY      30x32 */ SYM(1.9000f, 2.0000f, 0.66f, 0.36f, -0.12f, 0.20f), // narrowed into the gust, brows a touch lowered outside
    };

    enum EyeParam : uint8_t
    {
        P_W,
        P_H,
        P_RADIUS,
        P_LID_TOP,
        P_SLANT,
        P_LID_BOT,
        P_BROW,
        P_X,
        P_Y,
        P_COUNT
    };

    static Spring eyeSp[2][P_COUNT];
    static Spring decorSp; // 0 = eyes, 1 = a decorative pet face (heart / spiral ...) fully grown
    static bool rigInit = false;
    static uint32_t rigLastMs = 0;
    static bool rigFast = false; // anticipation: the eyes squash in ~80 ms instead of ~250 ms
    static float gazeNorm = 0;   // -1 left .. +1 right, for the parallax
    static float breathF = 1.0f; // height factor of the breathing
    static float lidBiasV = 0;   // extra upper-lid closing on top of the preset (persona: drowsy, sulk, squint ...)

    void eyesSetLidBias(float v) { lidBiasV = clamp01(v); }

    static float crossPxV = 0; // cross-eyed: the left eye moves this far right, the right eye the same far left
    void eyesSetCross(float px) { crossPxV = px; }

    static float moodKV = 1.0f; // how far the mood presets are applied (hot / cold are graded)
    void eyesSetMoodK(float k) { moodKV = clamp01(k); }

    // The preset p, blended from the NEUTRAL eyes by moodKV.
    static EyePreset gradedPreset(const EyePreset &p, const EyePreset &n)
    {
        const float k = moodKV;
        if (k >= 0.999f)
            return p;
        return {n.w + (p.w - n.w) * k, n.h + (p.h - n.h) * k, n.radius + (p.radius - n.radius) * k,
                n.lidTop + (p.lidTop - n.lidTop) * k, n.slant + (p.slant - n.slant) * k, n.lidBot + (p.lidBot - n.lidBot) * k,
                n.brow + (p.brow - n.brow) * k};
    }

    void eyesSetTargets(EyeMood left, EyeMood right, float scale, float lookPxX, float lookPxY, float gazeN, int16_t R)
    {
        const EyeMood mood[2] = {left, right};
#if CHARACTER_STYLE
        moodNowV = (left == EyeMood::HAPPY && right != EyeMood::HAPPY) ? right : left; // (a wink: the open eye's mood)
#endif
        gazeNorm = gazeN < -1 ? -1 : (gazeN > 1 ? 1 : gazeN);
        for (uint8_t i = 0; i < 2; i++)
        {
            const EyePreset p = gradedPreset(PRESETS[(uint8_t)mood[i]][i], PRESETS[(uint8_t)EyeMood::NEUTRAL][i]);
            Spring *sp = eyeSp[i];
            sp[P_W].target = p.w * R * scale;
            sp[P_H].target = p.h * R * scale;
            sp[P_RADIUS].target = p.radius * R * scale;
            sp[P_LID_TOP].target = p.lidTop + lidBiasV * (1.0f - p.lidTop);
            sp[P_SLANT].target = p.slant;
            sp[P_LID_BOT].target = p.lidBot;
            sp[P_BROW].target = p.brow;
            // the gaze is not sprung: the saccade curve in updateLook() already is the motion
            const float cx = i == 0 ? crossPxV : -crossPxV; // i = 0 is the left eye
            sp[P_X].x = sp[P_X].target = lookPxX + cx;
            sp[P_Y].x = sp[P_Y].target = lookPxY;
            sp[P_X].v = sp[P_Y].v = 0;
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
    void eyesSetFast(bool on) { rigFast = on; }
    void eyesSetBreath(float f) { breathF = f; }

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
        float k = rigFast ? 1000.0f : 220.0f, c = rigFast ? 64.0f : 18.0f;
        for (uint8_t j = 0; j < n; j++)
        {
            for (uint8_t i = 0; i < 2; i++)
                for (uint8_t q = 0; q < P_COUNT; q++)
                    springStep(eyeSp[i][q], h, k, c);
            springStep(decorSp, h);
        }
        // mouth fade: out 0.12 s, then (if something new is wanted) in 0.18 s
        if (mouthWant != mouthKind)
        {
            mouthA -= dt / 0.12f;
            if (mouthA <= 0)
            {
                mouthA = 0;
                mouthKind = mouthWant;
            }
        }
        else if (mouthKind != MouthKind::NONE)
            mouthA = fminf(1.0f, mouthA + dt / 0.18f);
        // the blink lid is much stiffer: sub-steps of <= 2 ms
        uint8_t nb = (uint8_t)(dt / 0.002f) + 1;
        float hb = dt / nb;
        for (uint8_t j = 0; j < nb; j++)
            springStep(blinkSp, hb, blinkOmega * blinkOmega, 2.0f * blinkOmega);
    }

    // Brings the upper lid down by k (0..1): lidTop -> 1 and the slant fades out (a closed eye is flat).
    void eyeCloseLid(EyeShape &s, float k)
    {
        k = clamp01(k);
        float lt = clamp01(s.lidTop);
        s.lidTop = lt + k * (1.0f - lt);
        s.slant *= (1.0f - k);
    }

    EyeShape eyeShapeNow(uint8_t eye)
    {
        const Spring *sp = eyeSp[eye & 1];
        EyeShape s;
        s.w = fmaxf(0.0f, sp[P_W].x); // springs overshoot: keep sizes non-negative
        s.h = fmaxf(0.0f, sp[P_H].x) * breathF;
        s.radius = fmaxf(0.0f, sp[P_RADIUS].x);
        s.lidTop = clamp01(sp[P_LID_TOP].x);
        s.slant = sp[P_SLANT].x < -1 ? -1 : (sp[P_SLANT].x > 1 ? 1 : sp[P_SLANT].x);
        s.lidBot = clamp01(sp[P_LID_BOT].x);
        s.brow = sp[P_BROW].x < -1 ? -1 : (sp[P_BROW].x > 1 ? 1 : sp[P_BROW].x);
        s.x = sp[P_X].x;
        s.y = sp[P_Y].x;
        // parallax: looking right -> the right (far) eye 8 % narrower, the left (near) eye 4 % wider; mirrored for left
        float g = gazeNorm, f;
        if (g >= 0)
            f = (eye & 1) ? 1.0f - 0.08f * g : 1.0f + 0.04f * g;
        else
            f = (eye & 1) ? 1.0f - 0.04f * g : 1.0f + 0.08f * g;
        s.w *= f;
        eyeCloseLid(s, blinkSp.x);
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
        case Reaction::SAD: // heavy rain
            return EyeMood::SAD;
        case Reaction::STORM:
            return EyeMood::ALARMED;
        case Reaction::RAIN:
            return EyeMood::CALM;
        case Reaction::SNOW:
            return EyeMood::WONDER;
        case Reaction::FOG:
            return EyeMood::MISTY;
        case Reaction::HOT:
            return EyeMood::HOT;
        case Reaction::COLD:
            return EyeMood::CHILLED;
        case Reaction::WIND:
            return EyeMood::WINDY;
        default:
            break;
        }
        return sleepy ? EyeMood::TIRED : EyeMood::NEUTRAL;
    }

    // Happy "^ ^" eye drawn as a thick arch, plus blush.
    void drawHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
#if CHARACTER_STYLE
        charHappyEye(g, cx, cy, R);
        return;
#endif
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
        drawCheek(g, cx, cy + R + u(2), R / 2 + u(1), imax(2, R / 5), 1.0f);
    }

    // =====================================================================
    //  Face
    // =====================================================================

    // What the weather makes the eyes do, as a steady situation (no timing).
    static Reaction weatherSituation(const SceneInfo &scn)
    {
        switch (weatherMood(scn.weatherKnown, scn.code, scn.tempC, scn.windKmh)) // the one shared weather decision (weather_mood.h)
        {
        case WxMood::STORM:
            return Reaction::STORM;
        case WxMood::RAIN: // only heavy rain is sad; drizzle and ordinary rain are calm
            return wxIsHeavyRain(scn.code) ? Reaction::SAD : Reaction::RAIN;
        case WxMood::HOT:
            return Reaction::HOT;
        case WxMood::COLD:
            return Reaction::COLD;
        case WxMood::SNOW:
            return Reaction::SNOW;
        case WxMood::FOG:
            return Reaction::FOG;
        case WxMood::WIND:
            return Reaction::WIND;
        default:
            return Reaction::NONE;
        }
    }

    // Weather moments (WX_MOMENT_*): the situation only shows while a "moment" runs.
    static Reaction momKind = Reaction::NONE; // the situation the schedule below was made for
    static bool momOn = false;
    static uint32_t momEndMs = 0, momNextMs = 0;

    static uint32_t momDuration(Reaction r)
    {
        return r == Reaction::STORM ? random(WX_MOMENT_STORM_DUR_MIN_MS, WX_MOMENT_STORM_DUR_MAX_MS)
                                    : random(WX_MOMENT_DUR_MIN_MS, WX_MOMENT_DUR_MAX_MS);
    }
    static uint32_t momGap(Reaction r)
    {
        return r == Reaction::STORM ? random(WX_MOMENT_STORM_GAP_MIN_MS, WX_MOMENT_STORM_GAP_MAX_MS)
                                    : random(WX_MOMENT_GAP_MIN_MS, WX_MOMENT_GAP_MAX_MS);
    }

    Reaction pickReaction(const SceneInfo &scn, bool offline, bool sleepy, uint32_t now)
    {
        if (!EXPR_ENABLED || sleepy) // sleepy eyes at night stay sleepy
            return Reaction::NONE;
        if (offline)
            return Reaction::WORRIED; // a warning, not a mood: always on
        const Reaction sit = weatherSituation(scn);
        if (!WX_MOMENT_ENABLED)
            return sit;
        if (sit == Reaction::NONE)
        {
            momKind = Reaction::NONE;
            momOn = false;
            return Reaction::NONE;
        }
        const bool flash = sit == Reaction::STORM && lightningActive(now);
        if (sit != momKind) // new kind of weather = news: show it at once
        {
            momKind = sit;
            momOn = true;
            momEndMs = now + momDuration(sit);
        }
        else if (momOn)
        {
            if (flash && (int32_t)(momEndMs - now) < 1500)
                momEndMs = now + 1500; // every flash while it runs keeps the flinch going
            if ((int32_t)(now - momEndMs) >= 0)
            {
                momOn = false;
                momNextMs = now + momGap(sit);
            }
        }
        else if ((int32_t)(now - momNextMs) >= 0 && (sit != Reaction::STORM || flash))
        {
            momOn = true; // a storm waits for its next flash
            momEndMs = now + momDuration(sit);
        }
        return momOn ? sit : Reaction::NONE;
    }

    Expr exprCur = Expr::NONE;
    static uint32_t exprStartMs = 0, exprDurMs = 0, nextExprAtMs = 0;
    // Variants, so the same expression does not look the same every time:
    bool exprWinkLeft = false; // which eye winks (display_face.cpp); tends to alternate
    float exprYawnSize = 1.0f; // how wide this yawn opens: 0.55 (small) .. 1 (big)
    static Expr lastExpr = Expr::NONE;
    static bool lastWinkLeft = false;

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
            // The weights are as before (sleepy: yawn 80 / wink 20, else wink 50 / surprise 20 / yawn 30), but the one
            // shown last time counts a quarter: the same face rarely comes twice in a row.
            float wW = sleepy ? 20.0f : 50.0f, wS = sleepy ? 0.0f : 20.0f, wY = sleepy ? 80.0f : 30.0f;
            if (lastExpr == Expr::WINK)
                wW *= 0.25f;
            else if (lastExpr == Expr::SURPRISE)
                wS *= 0.25f;
            else if (lastExpr == Expr::YAWN)
                wY *= 0.25f;
            float r = random(0, 1000) / 1000.0f * (wW + wS + wY);
            exprCur = r < wW ? Expr::WINK : (r < wW + wS ? Expr::SURPRISE : Expr::YAWN);
            lastExpr = exprCur;
            exprWinkLeft = random(0, 100) < 70 ? !lastWinkLeft : lastWinkLeft;
            lastWinkLeft = exprWinkLeft;
            exprYawnSize = 0.55f + 0.45f * (random(0, 100) / 100.0f);
            exprStartMs = now;
            exprDurMs = exprCur == Expr::WINK ? 450 : (exprCur == Expr::YAWN ? (uint32_t)(1700.0f + 500.0f * exprYawnSize) : 900);
            if (exprCur == Expr::YAWN)
                audio_play_async(SOUND_INDEX_YAWN);
        }
        return 0;
    }

    // The persona starts an expression itself (the two yawns before sleep, "oh!" at the first rain).
    void exprForce(Expr e, uint32_t now, uint32_t durMs)
    {
        exprCur = e;
        lastExpr = e; // a random expression does not repeat this one right away
        exprStartMs = now;
        exprDurMs = durMs;
        if (e == Expr::YAWN)
        {
            exprYawnSize = 0.7f + 0.3f * (random(0, 100) / 100.0f);
            audio_play_async(SOUND_INDEX_YAWN);
        }
    }

    // The first ANTICIPATE_MS of a surprise: the eyes squash a little before they pop (see drawFace).
    bool exprAnticipating(uint32_t now)
    {
        return exprCur == Expr::SURPRISE && now - exprStartMs < ANTICIPATE_MS;
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

    // Blink: closing 70 ms (ease-in), closed 40 ms, opening 130 ms (ease-out); sleepy eyes 160 / 250 ms.
    // Next blink after a random 2.5..7 s, 15 % of them are followed by a second one right away.
    // The timers only flip the lid spring's target; the spring (eyesStep) makes the motion.
    void updateBlink(uint32_t now, bool sleepy)
    {
        if (!blinkInit)
        {
            blinkInit = true;
            blinkSp.x = BLINK_MAX; // eyes wake up: start closed
            blinkSp.target = 0;
        }
        if (blinkHeld)
        {
            blinkSp.x = BLINK_MAX;
            blinkSp.v = 0;
            blinkSp.target = BLINK_MAX;
            blinkPhase = BlinkPhase::CLOSED;
            blinkPhaseStartMs = now;
            return;
        }
        const uint16_t closeMs = sleepy ? 160 : 70, closedMs = 40, openMs = sleepy ? 250 : 130;
        uint32_t el = now - blinkPhaseStartMs;
        switch (blinkPhase)
        {
        case BlinkPhase::OPEN:
            if (nextBlinkAtMs == 0)
                nextBlinkAtMs = now + random(2500, 7000);
            if (now >= nextBlinkAtMs)
            {
                blinkPhase = BlinkPhase::CLOSING;
                blinkPhaseStartMs = now;
                blinkSp.target = BLINK_MAX;
                blinkOmega = 4.9f / (closeMs / 1000.0f); // critically damped: ~95 % closed after closeMs
            }
            break;
        case BlinkPhase::CLOSING:
            if (el >= closeMs)
            {
                blinkPhase = BlinkPhase::CLOSED;
                blinkPhaseStartMs = now;
            }
            break;
        case BlinkPhase::CLOSED:
            if (el >= closedMs)
            {
                blinkPhase = BlinkPhase::OPENING;
                blinkPhaseStartMs = now;
                blinkSp.target = 0;
                blinkOmega = 4.55f / (openMs / 1000.0f); // slower: opening feels more natural
            }
            break;
        case BlinkPhase::OPENING:
            if (el >= openMs)
            {
                blinkPhase = BlinkPhase::OPEN;
                if (!blinkWasDouble && random(0, 100) < 15)
                {
                    blinkWasDouble = true;
                    nextBlinkAtMs = now + 110; // second blink right behind the first
                }
                else
                {
                    blinkWasDouble = false;
                    nextBlinkAtMs = now + random(2500, 7000);
                }
            }
            break;
        }
    }

    void blinkHold(bool on) { blinkHeld = on; }

    // Releases the held lids: the spring opens them over about `ms`; the next idle blink waits a while.
    void blinkOpenSlow(uint32_t now, uint16_t ms)
    {
        blinkHeld = false;
        blinkPhase = BlinkPhase::OPENING;
        blinkPhaseStartMs = now;
        blinkSp.target = 0;
        blinkOmega = 4.55f / (ms / 1000.0f);
        nextBlinkAtMs = now + 1500;
    }

    void blinkTrigger(uint32_t now)
    {
        if (blinkPhase != BlinkPhase::OPEN)
            return;
        blinkPhase = BlinkPhase::CLOSING;
        blinkPhaseStartMs = now;
        blinkSp.target = BLINK_MAX;
        blinkOmega = 4.9f / 0.07f;
    }

    // ---- Saccades: the gaze JUMPS to a new point (90 ms, ease-out), then holds still with a tiny tremor ----
    static const uint16_t SACCADE_MS = 90;
    static float sacFromX = 0, sacFromY = 0, sacToX = 0, sacToY = 0;
    static uint32_t sacStartMs = 0;
    static bool sacActive = false;
    static float tgtX = 0, tgtY = 0; // where the gaze is heading
    static bool pend = false;        // a big idle look waits for the next blink
    static float pendX = 0, pendY = 0;
    static uint32_t pendSince = 0;
    static uint32_t nextTremorAtMs = 0;

    static void startSaccade(uint32_t now, float tx, float ty)
    {
        sacFromX = lookX;
        sacFromY = lookY;
        sacToX = tgtX = tx;
        sacToY = tgtY = ty;
        sacStartMs = now;
        sacActive = true;
        pend = false;
        lookTremX = lookTremY = 0;
        nextTremorAtMs = now + SACCADE_MS + random(300, 900);
    }

    static float windLookX = 0; // where the wind blows to: +1 right .. -1 left, 0 = unknown
    void eyesSetWindDir(float vx) { windLookX = vx; }

    void updateLook(uint32_t now, bool alarm, bool sleepy, Reaction reaction)
    {
        bool idlePick = false; // a random idle look was chosen this frame

        // The persona (roll, peek-a-boo) sets the gaze itself; every other persona look is only a new target.
        static bool wasPersona = false;
        if (persona.gazeOn && persona.gazeDirect)
        {
            lookX = tgtX = lookTX = persona.gazeX;
            lookY = tgtY = lookTY = persona.gazeY;
            sacActive = false;
            pend = false;
            lookTremX = lookTremY = 0;
            wasPersona = true;
            return;
        }
        if (wasPersona)
        {
            wasPersona = false;
            nextLookAtMs = now + 300; // back to the usual idle looks soon
        }
        if (persona.gazeOn)
        {
            lookTX = persona.gazeX;
            lookTY = persona.gazeY;
            wasPersona = true;
        }
        else if (sleepy)
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
        else if (reaction == Reaction::SAD)
        {
            lookTX = 0;
            lookTY = 0.8f;
        }
        else if (reaction == Reaction::STORM)
        {
            lookTX = 0; // tense, eyes on the sky (the flashes come from there)
            lookTY = -0.6f;
        }
        else if (reaction == Reaction::WIND)
        {
            // squinting, looking away from the gust (the way it blows to); every few seconds a quick look back at it
            float side = windLookX > 0.1f ? 1.0f : (windLookX < -0.1f ? -1.0f : (((now / 2600) & 1) ? 1.0f : -1.0f));
            lookTX = ((now / 1900) % 4 == 3) ? -0.5f * side : side;
            lookTY = 0.15f;
        }
        else if (now >= nextLookAtMs)
        {
            nextLookAtMs = now + random(1400, 4200);
            idlePick = true;
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
            if (reaction == Reaction::RAIN) // the usual wandering look, but now and then up at the sky / the drops
                lookTY = fminf(lookTY, -0.4f);
            else if (reaction == Reaction::SNOW) // ... and for the flakes
                lookTY = fminf(lookTY, -0.3f);
            if (persona.celestOk && random(0, 100) < SKY_LOOK_PERCENT) // now and then: a look at the sun / moon
            {
                lookTX = persona.celestX;
                lookTY = persona.celestY;
            }
        }

        // new target? idle looks that are big wait for a blink (people move their eyes while the lids are down);
        // everything else (alarm, page glance, worry ...) reacts at once
        float refX = pend ? pendX : tgtX, refY = pend ? pendY : tgtY;
        if (lookTX != refX || lookTY != refY)
        {
            bool big = fabsf(lookTX - refX) + fabsf(lookTY - refY) > 0.5f;
            if (idlePick && big)
            {
                pend = true;
                pendX = lookTX;
                pendY = lookTY;
                pendSince = now;
            }
            else
                startSaccade(now, lookTX, lookTY);
        }
        if (pend && (blinkPhase == BlinkPhase::CLOSING || blinkPhase == BlinkPhase::CLOSED || now - pendSince > 1200))
            startSaccade(now, pendX, pendY); // mid-blink (or the blink took too long)

        if (sacActive)
        {
            float t = (now - sacStartMs) / (float)SACCADE_MS;
            if (t >= 1.0f)
            {
                t = 1.0f;
                sacActive = false;
            }
            float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // ease-out
            lookX = sacFromX + (sacToX - sacFromX) * e;
            lookY = sacFromY + (sacToY - sacFromY) * e;
        }
        else if (now >= nextTremorAtMs) // holding still: a +-0.4 px twitch every 300..900 ms
        {
            lookTremX = random(-4, 5) * 0.1f;
            lookTremY = random(-4, 5) * 0.1f;
            nextTremorAtMs = now + random(300, 900);
        }
    }

    void drawDrop(TFT_eSPI &g, int16_t x, int16_t y, int16_t r, uint16_t c)
    {
#if CHARACTER_STYLE
        drawCharMono(g, CharMono::DROP, x, y, r / 3.0f, c); // the sprite's round part has radius 3
        return;
#endif
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