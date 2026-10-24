// display_moments.cpp - special minutes (easter eggs), see EGG_MOMENTS_* in config.h.
//   11:11  WISH    a shooting star crosses the face, the eyes follow it, close and make a wish
//   12:34  DIGITS  the eyes look down and read the digits from left to right, the digits glow once
//   13:37  SHADES  sunglasses drop onto the eyes, a glint runs over the lenses, they lift off again
//   15:14  PI      the eyes look up, "pi = 3.14159" is typed above them
// Each one plays once a day, when the minute arrives while the clock page is up and the eyes are awake and idle.
// momentUpdate() runs right after personaUpdate(): it only borrows the persona's gaze / lid / digit-flash outputs,
// the drawing is drawMomentFront() (called right after drawOccasionFront(), over the eyes).
#include "display_internal.h"
#include <string.h>

namespace disp
{
#if EGG_MOMENTS_ENABLED

    enum class Moment : uint8_t
    {
        NONE,
        WISH,
        DIGITS,
        SHADES,
        PI_DAY,
        COUNT
    };

    static const uint32_t kDurMs[(uint8_t)Moment::COUNT] = {0, 4200, 3300, 6600, 6200};

    static Moment cur = Moment::NONE;
    static uint32_t startMs = 0;
    static int32_t doneKey[(uint8_t)Moment::COUNT] = {0, -1, -1, -1, -1}; // day (year * 400 + yday) each one last played on
    static uint32_t demoNextMs = 8000;

    static Moment momentForTime(const struct tm &ti)
    {
        if (ti.tm_hour == 11 && ti.tm_min == 11)
            return Moment::WISH;
        if (ti.tm_hour == 12 && ti.tm_min == 34)
            return Moment::DIGITS;
        if (ti.tm_hour == 13 && ti.tm_min == 37)
            return Moment::SHADES;
        if (ti.tm_hour == 15 && ti.tm_min == 14)
            return Moment::PI_DAY;
        return Moment::NONE;
    }

    // the eyes as drawFace() places them (display_face.cpp)
    static void eyeGeom(uint32_t now, int16_t &R, int16_t &cy, int16_t &lcx, int16_t &rcx)
    {
        R = W / 8;
        cy = (int16_t)(faceH * 0.55f) + occasionEyeDy(now, false);
        lcx = W / 2 - R * 2;
        rcx = W / 2 + R * 2;
    }

    void momentUpdate(const struct tm *ti, bool clockPage, bool alarm, bool happy, uint32_t now)
    {
        Persona &P = persona;

        // idle = nothing else is going on with the face right now
        const bool quiet = clockPage && !alarm && !happy && P.stage == PStage::RUN && !P.asleep && !P.snoozed &&
                           !P.sulking && !P.frown && !P.flyOn && !P.thinking && P.peek == 0 && !P.shiver &&
                           P.relief == 0 && P.cross == 0 && P.sneezeJerk == 0 && !P.sneezeMouth && P.hiccupText == 0;

        if (cur != Moment::NONE)
        {
            if (!clockPage || alarm || happy || P.asleep) // cut short (it may start again while the minute lasts)
            {
                cur = Moment::NONE;
                return;
            }
            if (now - startMs >= kDurMs[(uint8_t)cur])
            {
                if (ti)
                    doneKey[(uint8_t)cur] = (int32_t)ti->tm_year * 400 + ti->tm_yday;
                cur = Moment::NONE;
                return;
            }
        }
        else if (quiet)
        {
            Moment want = Moment::NONE;
#if EGG_MOMENTS_DEMO >= 1 && EGG_MOMENTS_DEMO <= 4
            if ((int32_t)(now - demoNextMs) >= 0)
            {
                want = (Moment)EGG_MOMENTS_DEMO;
                demoNextMs = now + 25000;
            }
#else
            if (ti)
            {
                want = momentForTime(*ti);
                if (want != Moment::NONE && doneKey[(uint8_t)want] == (int32_t)ti->tm_year * 400 + ti->tm_yday)
                    want = Moment::NONE; // already played today
            }
#endif
            if (want != Moment::NONE)
            {
                cur = want;
                startMs = now;
            }
        }
        if (cur == Moment::NONE)
            return;

        // ---- what the persona does meanwhile ----
        const uint32_t t = now - startMs;
        P.exprOk = false; // no random wink / yawn in the middle of it
        switch (cur)
        {
        case Moment::WISH:
            if (t < 1400) // the eyes follow the star from the right to the left
            {
                float p = t / 1400.0f;
                P.gazeOn = true;
                P.gazeX = 0.9f - 1.8f * p;
                P.gazeY = -0.7f;
            }
            else
            {
                float lid = 0;
                if (t < 1700)
                    lid = clamp01((t - 1400) / 300.0f); // the eyes close
                else if (t < 3600)
                    lid = 1.0f; // the wish
                else
                    lid = clamp01(1.0f - (t - 3600) / 600.0f); // and open again
                P.gazeOn = true;
                P.gazeX = 0;
                P.gazeY = -0.2f;
                P.lidRamp = true;
                P.lidBias = 0.9f * lid;
            }
            break;

        case Moment::DIGITS:
        {
            float p = clamp01((t - 300) / 2400.0f);
            P.gazeOn = true;
            P.gazeX = -0.9f + 1.8f * p; // looks down at the panel, along the digits
            P.gazeY = 1.0f;
            P.digitPulse = fmaxf(P.digitPulse, sinf((float)M_PI * p));
            break;
        }

        case Moment::PI_DAY:
            P.gazeOn = true;
            P.gazeX = 0;
            P.gazeY = -0.9f; // up at the text
            break;

        default: // SHADES: the eyes just carry on behind the glasses
            break;
        }
    }

    // ---- drawing ----

    // a small four-pointed sparkle
    static void drawSparkle(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t s, uint16_t c)
    {
        g.drawFastHLine(cx - s, cy, 2 * s + 1, c);
        g.drawFastVLine(cx, cy - s, 2 * s + 1, c);
        int16_t d = s / 2;
        uint16_t c2 = mixColor(C_BG, c, 0.55f);
        g.drawLine(cx - d, cy - d, cx + d, cy + d, c2);
        g.drawLine(cx - d, cy + d, cx + d, cy - d, c2);
    }

    static void drawWish(TFT_eSPI &g, uint32_t t)
    {
        uint16_t bright = mixColor(C_EYE_TOP, C_WHITE, 0.6f);
        if (t < 1400)
        {
            float p = t / 1400.0f;
            float sx0 = (float)(W - u(6)), sy0 = (float)u(5);
            float sx1 = (float)u(10), sy1 = (float)u(18);
            float dx = sx1 - sx0, dy = sy1 - sy0;
            float len = sqrtf(dx * dx + dy * dy);
            dx /= len;
            dy /= len;
            float hx = sx0 + (sx1 - sx0) * p, hy = sy0 + (sy1 - sy0) * p;
            float fade = p > 0.85f ? (1.0f - p) / 0.15f : 1.0f;
            for (int j = 7; j >= 0; j--) // tail first, head last
            {
                float k = (1.0f - j / 8.0f) * fade;
                int16_t px = (int16_t)lroundf(hx - dx * j * u(3));
                int16_t py = (int16_t)lroundf(hy - dy * j * u(3));
                g.fillCircle(px, py, imax(1, (int16_t)(u(2) - j / 3)), tint(bright, k));
            }
        }
        else if (t >= 1700 && t < 3900) // the wish: a sparkle above the closed eyes
        {
            float k = 1.0f;
            if (t < 2000)
                k = (t - 1700) / 300.0f;
            else if (t > 3600)
                k = (3900 - t) / 300.0f;
            int16_t s = (int16_t)(u(3) + u(2) * (0.5f + 0.5f * sinf(t * 0.012f)));
            drawSparkle(g, W / 2, u(11), imax(2, s), tint(C_WHITE, clamp01(k)));
        }
    }

    static void drawPi(TFT_eSPI &g, uint32_t t)
    {
        static const char kPi[] = "pi = 3.14159";
        size_t len = strlen(kPi);
        size_t n = t < 400 ? 0 : (size_t)((t - 400) / 170 + 1);
        if (n > len)
            n = len;
        if (n == 0)
            return;
        float k = 1.0f;
        if (t < 700)
            k = clamp01((t - 400) / 300.0f);
        if (t > kDurMs[(uint8_t)Moment::PI_DAY] - 700)
            k = clamp01((kDurMs[(uint8_t)Moment::PI_DAY] - t) / 700.0f);
        char buf[16];
        memcpy(buf, kPi, n);
        buf[n] = '\0';
        int16_t x = (W - g.textWidth(kPi, 2)) / 2; // the text grows to the right from a fixed start
        g.setTextDatum(ML_DATUM);
        g.setTextColor(tint(C_ACCENT, k));
        g.drawString(buf, x, u(12), 2);
    }

    // Sunglasses: two see-through tinted lenses (darker at the top, lighter at the bottom, so the eyes glow through),
    // a thin rim, a bar across the top and over the nose. No arms. Falls in from above, bounces, a glint runs over
    // the lenses, lifts off again.
    static void drawShades(TFT_eSPI &g, uint32_t t, uint32_t now)
    {
        const uint32_t DROP = 450, LIFT_AT = 6100, LIFT = 500;
        int16_t R, cy, lcx, rcx;
        eyeGeom(now, R, cy, lcx, rcx);
        const int16_t lensH = (int16_t)(R * 1.75f);
        const int16_t lensW = 2 * R + u(6);
        const int16_t top = cy - R;
        const float away = (float)(top + lensH + u(4)); // how far above the card they wait

        float off = 0;
        if (t < DROP)
        {
            float p = t / (float)DROP;
            off = -(1.0f - p) * (1.0f - p) * away; // falls in, fast at the end
        }
        else if (t < DROP + 150)
            off = -sinf((t - DROP) * (float)M_PI / 150.0f) * u(3); // a small bounce on landing
        else if (t >= LIFT_AT)
        {
            float p = clamp01((t - LIFT_AT) / (float)LIFT);
            off = -p * p * away;
        }
        const int16_t y0 = top + (int16_t)lroundf(off);

        const uint16_t glass = mixColor(rgb(8, 10, 22), C_EYE_BOT, 0.12f); // the tint of the lenses
        const uint16_t rim = mixColor(rgb(95, 100, 125), C_EYE_TOP, 0.25f);
        const uint16_t barCol = rgb(10, 11, 20);
        const int16_t rt = imax(2, u(3));                          // top corners: nearly square
        const int16_t rb = imin(u(10), (int16_t)(lensH - rt - 1)); // bottom corners: round
        const int16_t barH = imax(2, u(3));
        const int16_t xl = lcx - lensW / 2, xr = rcx - lensW / 2;
        const bool canRead = faceCv.ok && (&g == static_cast<TFT_eSPI *>(&faceCv.spr)); // blending needs the sprite

        // where the lens is on row dy: [xa, xb] (x0 = its left edge)
        auto span = [&](int16_t x0, int16_t dy, int16_t &xa, int16_t &xb)
        {
            float inset = 0;
            if (dy < rt)
            {
                float yy = rt - dy - 0.5f;
                inset = rt - sqrtf(fmaxf(0.0f, (float)rt * rt - yy * yy));
            }
            else if (dy >= lensH - rb)
            {
                float yy = dy + 0.5f - (lensH - rb);
                inset = rb - sqrtf(fmaxf(0.0f, (float)rb * rb - yy * yy));
            }
            int16_t in = (int16_t)lroundf(inset);
            xa = x0 + in;
            xb = x0 + lensW - 1 - in;
        };

        for (int8_t side = 0; side < 2; side++)
        {
            const int16_t x0 = side ? xr : xl;
            for (int16_t dy = 0; dy < lensH; dy++)
            {
                const int16_t y = y0 + dy;
                if (y < 0 || y >= faceH)
                    continue;
                int16_t xa, xb, nxa, nxb;
                span(x0, dy, xa, xb);
                span(x0, (int16_t)imin((int16_t)(dy + 1), (int16_t)(lensH - 1)), nxa, nxb);

                // tint: strong at the top, lighter towards the bottom
                float a = 0.78f - 0.38f * (dy / (float)lensH);
                for (int16_t x = xa; x <= xb; x++)
                {
                    if (x < 0 || x >= W)
                        continue;
                    if (canRead)
                        g.drawPixel(x, y, mixColor(g.readPixel(x, y), glass, a));
                    else
                        g.drawPixel(x, y, mixColor(C_BG, glass, 0.85f));
                }

                // rim: the two ends of the row, joined up where the outline steps sideways; the whole first and last row
                int16_t lo = xa, hi = xa;
                if (nxa > xa)
                    hi = nxa - 1;
                else if (nxa < xa)
                    lo = nxa + 1;
                g.drawFastHLine(lo, y, hi - lo + 1, rim);
                lo = hi = xb;
                if (nxb < xb)
                    lo = nxb + 1;
                else if (nxb > xb)
                    hi = nxb - 1;
                g.drawFastHLine(lo, y, hi - lo + 1, rim);
                if (dy == 0 || dy == lensH - 1)
                    g.drawFastHLine(xa, y, xb - xa + 1, rim);
            }
        }

        // the bar across the top, over the nose
        g.fillRect(xl, y0 - 1, (xr + lensW) - xl, barH, barCol);
        g.drawFastHLine(xl, y0 - 1, (xr + lensW) - xl, rim);

        // a glint runs over both lenses once the glasses have settled
        if (t >= 1100 && t < 1700)
        {
            float p = (t - 1100) / 600.0f;
            uint16_t gl = mixColor(C_WHITE, C_EYE_TOP, 0.2f);
            int16_t sl = lensH / 3;
            for (int8_t side = 0; side < 2; side++)
            {
                int16_t x = side ? xr : xl;
                int16_t gx = x + (int16_t)(p * (lensW + sl)) - sl;
                for (int8_t i = 0; i < 2; i++)
                {
                    int16_t aa = gx + i * u(3);
                    if (aa >= x + 3 && aa + sl <= x + lensW - 4)
                        g.drawLine(aa, y0 + lensH - rb / 2, aa + sl, y0 + 3, gl);
                }
            }
        }
    }

    bool momentActive() { return cur != Moment::NONE; }

    void drawMomentFront(TFT_eSPI &g, uint32_t now)
    {
        if (cur == Moment::NONE)
            return;
        uint32_t t = now - startMs;
        switch (cur)
        {
        case Moment::WISH:
            drawWish(g, t);
            break;
        case Moment::PI_DAY:
            drawPi(g, t);
            break;
        case Moment::SHADES:
            drawShades(g, t, now);
            break;
        default:
            break;
        }
    }

#else

    void momentUpdate(const struct tm *, bool, bool, bool, uint32_t) {}
    bool momentActive() { return false; }
    void drawMomentFront(TFT_eSPI &, uint32_t) {}

#endif

} // namespace disp