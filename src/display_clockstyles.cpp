// display_clockstyles.cpp - the five clock faces of the landscape UI: Simple, Analog, Minimal, Retro, LED.
// All of them are drawn into the full-screen canvas (160 x 128 on the real display, scaled by sc on bigger ones) with the
// look of the clock character: navy, white neon cores with a blue glow, pink blush accents.
#include "display_internal.h"
#include "display_style.h"
#include "display_landscape.h"
#include "ui_state.h"
#include "display_wxicon.h"
#include "sun.h"

namespace disp
{
    namespace
    {
        constexpr uint16_t C_RETRO = rgb(176, 140, 255); // the violet of the retro face

        inline int16_t R16(float v) { return (int16_t)lroundf(v); }

        // A line with round ends, w px thick.
        void thickLine(TFT_eSPI &g, float x0, float y0, float x1, float y1, float w, uint16_t c)
        {
            float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
            if (len < 0.5f)
            {
                g.fillCircle(R16(x0), R16(y0), imax(1, R16(w * 0.5f)), c);
                return;
            }
            float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
            g.fillTriangle(R16(x0 + nx), R16(y0 + ny), R16(x1 + nx), R16(y1 + ny), R16(x1 - nx), R16(y1 - ny), c);
            g.fillTriangle(R16(x0 + nx), R16(y0 + ny), R16(x1 - nx), R16(y1 - ny), R16(x0 - nx), R16(y0 - ny), c);
            if (w >= 2.0f)
            {
                g.fillCircle(R16(x0), R16(y0), R16(w * 0.5f), c);
                g.fillCircle(R16(x1), R16(y1), R16(w * 0.5f), c);
            }
        }

        // The same line as a neon stroke: glow, colour, white core.
        void neonLine(TFT_eSPI &g, float x0, float y0, float x1, float y1, float w, uint16_t accent)
        {
            thickLine(g, x0, y0, x1, y1, w + 4.0f * sc, tint(accent, 0.16f));
            thickLine(g, x0, y0, x1, y1, w + 2.0f * sc, accent);
            thickLine(g, x0, y0, x1, y1, w, neonCore(accent, 0.85f));
        }

        void miniBell(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
        {
            g.fillCircle(cx, cy - 1, r, c);
            g.fillRect(cx - r, cy - 1, 2 * r + 1, r - 1, c);
            g.fillRect(cx - r - 1, cy + r - 2, 2 * r + 3, 2, c);
            g.fillCircle(cx, cy + r + 1, imax(1, r / 3), c);
        }

        // The little alarm chip (bell + time) in the top left corner, only when an alarm is set.
        void alarmChip(TFT_eSPI &g, uint32_t now)
        {
            (void)now;
            static uint32_t lastReadMs = 0;
            static bool on = false;
            static int hh = 0, mm = 0;
            if (lastReadMs == 0 || (uint32_t)(millis() - lastReadMs) > 2000) // settings change rarely
            {
                Settings st = settings_get();
                on = st.alarmEnabled;
                hh = st.alarmHour;
                mm = st.alarmMinute;
                lastReadMs = millis() ? millis() : 1;
            }
            if (!on)
                return;
            const bool big = sc >= 1.5f;
            const uint8_t *sf = big ? ClockSm18 : ClockSm12;
            const int16_t cap = big ? CLOCKSM18_CAPH : CLOCKSM12_CAPH;
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
            const int16_t r = imax(3, u(3.2f)), cx = u(10), cy = u(10);
            miniBell(g, cx, cy, r, C_BLUSH);
            smoothText(g, sf, cap, buf, cx + r + u(4), cy + 1, L_BASELINE, C_SOFT, C_BG);
        }

        void caption(TFT_eSPI &g, int16_t cx, int16_t cy, const char *txt, uint16_t col, uint16_t spark, bool sparkles)
        {
            const bool big = sc >= 1.5f;
            const uint8_t *sf = big ? ClockSm18 : ClockSm12;
            const int16_t cap = big ? CLOCKSM18_CAPH : CLOCKSM12_CAPH;
            smoothText(g, sf, cap, txt, cx, cy, C_BASELINE, col, C_BG);
            if (sparkles)
            {
                int16_t half = smoothWidth(g, sf, txt) / 2 + u(8);
                drawSparkle(g, cx - half, cy - u(3), 0.8f, spark);
                drawSparkle(g, cx + half, cy - u(3), 0.8f, spark);
            }
        }

        // HH:MM as seven-segment digits (unlit when the time is not known yet), centred on cx.
        void timeDigits(TFT_eSPI &g, int16_t cx, int16_t y, int16_t dh, bool have, const struct tm &ti, uint16_t on,
                        uint16_t colonCol, uint8_t level, uint16_t ghost)
        {
            const int16_t dw = (int16_t)(dh * 0.56f);
            const int16_t gap = imax(2, u(3));
            const int16_t cw = imax(4, u(8));
            const int16_t total = 4 * dw + 4 * gap + cw;
            const int16_t x0 = cx - total / 2;
            const int8_t d[4] = {(int8_t)(have ? ti.tm_hour / 10 : -1), (int8_t)(have ? ti.tm_hour % 10 : -1),
                                 (int8_t)(have ? ti.tm_min / 10 : -1), (int8_t)(have ? ti.tm_min % 10 : -1)};
            const int16_t xs[4] = {x0, (int16_t)(x0 + dw + gap), (int16_t)(x0 + 2 * dw + 3 * gap + cw),
                                   (int16_t)(x0 + 3 * dw + 4 * gap + cw)};
            for (int i = 0; i < 4; i++)
                drawSegDigit(g, xs[i], y, dw, dh, d[i], on, ghost);
            drawColonGlow(g, x0 + 2 * dw + 2 * gap + cw / 2, y, dh, imax(2, dw / 4), colonCol, level);
        }

        // ------------------------------------------------------------------ Simple
        // The Simple face as designed: the time in big, round, medium-bold digits (white-lavender core with a soft
        // blue-violet glow around them, ClockBig48 / ClockBig92) and "Tue, Oct 21" in the same lavender under it.
        // Nothing else: no seconds bar, no sparkles. The colon keeps its slow "breath".

        // "HH:MM" (or "--:--" while the time is unknown) centred on cx, digits centred on cy, drawn in three passes:
        // the wide glow, the near glow, the bright core. Every glyph is placed one by one so the colon can breathe.
        void bigTime(TFT_eSPI &g, int16_t cx, int16_t cy, bool have, const struct tm &ti, uint16_t on, uint8_t colonLevel)
        {
            const bool big = sc >= 1.5f;
            const uint8_t *font = big ? ClockBig92 : ClockBig48;
            const int16_t caph = big ? CLOCKBIG92_CAPH : CLOCKBIG48_CAPH;
            char s[6];
            if (have)
                snprintf(s, sizeof(s), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else
                strcpy(s, "--:--");
            const uint16_t core = neonCore(on, 0.82f);
            const float ck = 0.35f + 0.65f * (colonLevel / 15.0f); // the colon never goes out completely

            g.loadFont(font);
            g.setTextDatum(L_BASELINE);
            int16_t cw[5], total = 0;
            for (int i = 0; i < 5; i++)
            {
                const char one[2] = {s[i], 0};
                cw[i] = g.textWidth(one, 1);
                total += cw[i];
            }
            const int16_t x0 = cx - total / 2, by = cy + caph / 2;
            const int16_t r2 = imax(2, u(2.2f)), r1 = imax(1, u(0.9f));
            struct Pass
            {
                int16_t dx, dy;
                float glow;
            };
            // far halo (square ring), near halo (plus shape), then the core on top
            const Pass passes[] = {{(int16_t)-r2, 0, 0.16f}, {r2, 0, 0.16f},  {0, (int16_t)-r2, 0.16f}, {0, r2, 0.16f},
                                   {(int16_t)-r1, (int16_t)-r1, 0.16f}, {r1, (int16_t)-r1, 0.16f},
                                   {(int16_t)-r1, r1, 0.16f}, {r1, r1, 0.16f},
                                   {(int16_t)-r1, 0, 0.38f}, {r1, 0, 0.38f}, {0, (int16_t)-r1, 0.38f}, {0, r1, 0.38f},
                                   {0, 0, -1.0f}};
            for (const Pass &p : passes)
            {
                const bool isCore = p.glow < 0;
                int16_t x = x0;
                for (int i = 0; i < 5; i++)
                {
                    const char one[2] = {s[i], 0};
                    const float k = (i == 2) ? ck : 1.0f; // index 2 is the colon
                    uint16_t fg, bg;
                    if (isCore)
                    {
                        fg = mixColor(tint(on, 0.38f * k), core, k);
                        bg = tint(on, 0.38f * k);
                    }
                    else
                    {
                        fg = tint(on, p.glow * k);
                        bg = C_BG;
                    }
                    g.setTextColor(fg, bg);
                    g.drawString(one, x + p.dx, by + p.dy);
                    x += cw[i];
                }
            }
            g.unloadFont();
        }

        void styleSimple(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now, uint16_t accent, uint16_t on)
        {
            const uint8_t level = have ? colonLevelNow(now) : (uint8_t)(7.5f + 7.0f * sinf(now / 450.0f));
            const uint16_t timeCol = have ? on : tint(accent, 0.55f); // dim while the time is unknown
            bigTime(g, W / 2, (int16_t)(H * 0.42f), have, ti, timeCol, level);
            // the date: "Tue, Oct 21" (no leading zero on the day), lavender like the digits
            char cap[24] = "Syncing...";
            if (have)
            {
                char wd[8], mo[8];
                strftime(wd, sizeof(wd), "%a", &ti);
                strftime(mo, sizeof(mo), "%b", &ti);
                snprintf(cap, sizeof(cap), "%s, %s %d", wd, mo, ti.tm_mday);
            }
            smoothText(g, ClockSm18, CLOCKSM18_CAPH, cap, W / 2, (int16_t)(H * 0.75f), C_BASELINE, neonCore(accent, 0.72f), C_BG);
        }

        // ------------------------------------------------------------------ Analog
        // The Analog face as designed: a thin violet ring, twelve short white ticks (longer at 12 / 3 / 6 / 9), a white
        // minute hand, an hour hand that fades from white to pink at its tip and a peach cap in the middle.
        // No glow, no date, no second hand. All sizes are in px of the 160 x 128 screen and scale with sc.

        // A line that fades from colour c0 at (x0, y0) to c1 at (x1, y1).
        void gradLine(TFT_eSPI &g, float x0, float y0, float x1, float y1, float w, uint16_t c0, uint16_t c1)
        {
            const int n = 8;
            for (int i = 0; i < n; i++)
            {
                const float a = (float)i / n, b = (float)(i + 1) / n;
                thickLine(g, x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b, y0 + (y1 - y0) * b, w,
                          mixColor(c0, c1, (a + b) * 0.5f));
            }
        }

        void styleAnalog(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now, uint16_t accent)
        {
            (void)now;
            const uint16_t C_CAP = rgb(247, 186, 112);                  // the peach cap in the middle
            const uint16_t whiteHand = neonCore(accent, 0.90f);         // ticks and minute hand
            const uint16_t pinkTip = mixColor(C_BLUSH, C_WHITE, 0.35f); // the tip of the hour hand
            const float cx = W * 0.5f, cy = H * 0.512f;                 // the dial sits a touch below the middle
            const float R = 45.0f * sc, ringW = 2.4f * sc;

            // the ring
            g.fillCircle(R16(cx), R16(cy), R16(R + ringW * 0.5f), accent);
            g.fillCircle(R16(cx), R16(cy), R16(R - ringW * 0.5f), C_BG);

            // twelve ticks: long and thick at 12 / 3 / 6 / 9, short and thin between
            for (int i = 0; i < 12; i++)
            {
                const float a = i * 0.5235988f, sx = sinf(a), sy = -cosf(a);
                const bool q = (i % 3 == 0);
                const float r0 = (q ? 33.3f : 34.0f) * sc, r1 = (q ? 40.5f : 39.0f) * sc;
                thickLine(g, cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1, (q ? 1.9f : 1.5f) * sc, whiteHand);
            }

            if (!have)
            {
                caption(g, R16(cx), R16(cy + R * 0.5f), "Syncing...", C_SOFT, C_SOFT, false);
                return;
            }

            const float hr = (ti.tm_hour % 12) + ti.tm_min / 60.0f; // seconds are not shown: the hands move once a minute
            const float ah = hr / 12.0f * 6.2831853f, am = ti.tm_min / 60.0f * 6.2831853f;
            const float hl = 24.0f * sc, ml = 27.0f * sc; // hour hand / minute hand length
            gradLine(g, cx, cy, cx + sinf(ah) * hl, cy - cosf(ah) * hl, 2.2f * sc, whiteHand, pinkTip);
            thickLine(g, cx, cy, cx + sinf(am) * ml, cy - cosf(am) * ml, 2.3f * sc, whiteHand);
            g.fillCircle(R16(cx), R16(cy), imax(3, R16(4.4f * sc)), C_CAP);
        }

        // ------------------------------------------------------------------ Minimal
        // The Minimal face as designed: hour over minute in big ExtraBold white digits with a blue glow (ClockBold45 /
        // ClockBold90) on the left; on the right, top to bottom: the weather (icon + temperature), the weekday and
        // "21 Oct". The weather row is left out until a reading exists. Its icon (display_wxicon.cpp) is animated, so this
        // face redraws about 10 times a second while the weather is shown (see clockScreenSig).

        // One string centred on cx, its digits centred on cy: wide halo, near halo, then the white core.
        void glowString(TFT_eSPI &g, const uint8_t *font, int16_t caph, const char *str, int16_t cx, int16_t cy, uint16_t on)
        {
            const uint16_t core = neonCore(on, 0.92f);
            const int16_t r2 = imax(2, u(2.2f)), r1 = imax(1, u(1.0f));
            struct Pass
            {
                int16_t dx, dy;
                float glow;
            };
            const Pass passes[] = {{(int16_t)-r2, 0, 0.22f}, {r2, 0, 0.22f}, {0, (int16_t)-r2, 0.22f}, {0, r2, 0.22f},
                                   {(int16_t)-r1, (int16_t)-r1, 0.22f}, {r1, (int16_t)-r1, 0.22f},
                                   {(int16_t)-r1, r1, 0.22f}, {r1, r1, 0.22f},
                                   {(int16_t)-r1, 0, 0.48f}, {r1, 0, 0.48f}, {0, (int16_t)-r1, 0.48f}, {0, r1, 0.48f},
                                   {0, 0, -1.0f}};
            g.loadFont(font);
            g.setTextDatum(C_BASELINE);
            for (const Pass &p : passes)
            {
                if (p.glow < 0)
                    g.setTextColor(core, tint(on, 0.48f));
                else
                    g.setTextColor(tint(on, p.glow), C_BG);
                g.drawString(str, cx + p.dx, cy + caph / 2 + p.dy);
            }
            g.unloadFont();
        }

        void styleMinimal(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now, uint16_t accent, uint16_t on)
        {
            const bool big = sc >= 1.5f;
            const uint8_t *bold = big ? ClockBold90 : ClockBold45;
            const int16_t boldCap = big ? CLOCKBOLD90_CAPH : CLOCKBOLD45_CAPH;
            char hh[4] = "--", mm[4] = "--";
            if (have)
            {
                snprintf(hh, sizeof(hh), "%02d", ti.tm_hour);
                snprintf(mm, sizeof(mm), "%02d", ti.tm_min);
            }
            const int16_t xc = (int16_t)(W * 0.368f);
            glowString(g, bold, boldCap, hh, xc, (int16_t)(H * 0.33f), on);
            glowString(g, bold, boldCap, mm, xc, (int16_t)(H * 0.677f), on);

            // the right column
            const int16_t xr = (int16_t)(W * 0.633f);
            const uint16_t lav = neonCore(accent, 0.58f);
            bool wxOk;
            float tC;
            int code;
            uint32_t upd;
            readWeather(wxOk, tC, code, upd);
            if (wxOk)
            {
                const float s = 6.5f * sc;
                const int16_t cy = (int16_t)(H * 0.34f);
                drawWxIcon(g, code, !have || sun_is_day(ti), xr + s, cy, s, now); // animated, day or night sky
                char t[10];
                snprintf(t, sizeof(t), "%d\xC2\xB0", (int)lroundf(tC));
                smoothText(g, ClockSm18, CLOCKSM18_CAPH, t, (int16_t)(xr + 2 * s + u(4)), cy, L_BASELINE, neonCore(accent, 0.9f), C_BG);
            }
            char wd[8] = "--", dm[16] = "";
            if (have)
            {
                char mo[8];
                strftime(wd, sizeof(wd), "%a", &ti);
                strftime(mo, sizeof(mo), "%b", &ti);
                snprintf(dm, sizeof(dm), "%d %s", ti.tm_mday, mo);
            }
            smoothText(g, ClockSm18, CLOCKSM18_CAPH, wd, xr, (int16_t)(H * 0.608f), L_BASELINE, lav, C_BG);
            if (have)
                smoothText(g, ClockSm18, CLOCKSM18_CAPH, dm, xr, (int16_t)(H * 0.748f), L_BASELINE, lav, C_BG);
        }

        // ------------------------------------------------------------------ Retro
        void styleRetro(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now)
        {
            const uint16_t violet = C_RETRO;
            // a CRT bezel
            g.drawRoundRect(u(3), u(3), W - 2 * u(3), H - 2 * u(3), u(9), tint(violet, 0.35f));
            g.drawRoundRect(u(4), u(4), W - 2 * u(4), H - 2 * u(4), u(8), tint(violet, 0.14f));
            const int16_t dh = (int16_t)(H * 0.36f), dy = (int16_t)(H * 0.20f);
            const bool colonOn = !have || (ti.tm_sec & 1) == 0; // the colon blinks with the seconds
            timeDigits(g, W / 2, dy, dh, have, ti, violet, colonOn ? violet : tint(violet, 0.25f), 15, tint(violet, 0.14f));
            // scan lines over the digits
            for (int16_t y = dy; y < dy + dh; y += imax(3, u(3)))
                g.drawFastHLine(u(12), y, W - 2 * u(12), C_BG);
            char cap[24] = "Syncing...";
            if (have)
                strftime(cap, sizeof(cap), "%a, %b %d", &ti);
            caption(g, W / 2, (int16_t)(H * 0.70f), cap, mixColor(violet, C_WHITE, 0.35f), violet, false);
            // the little wave under it
            const int16_t wy = (int16_t)(H * 0.82f), ww = (int16_t)(W * 0.30f);
            for (int16_t x = -ww / 2; x <= ww / 2; x++)
            {
                float ph = (x + now / 90.0f) * 0.55f;
                int16_t y = wy + R16(sinf(ph) * u(2.2f));
                g.drawPixel(W / 2 + x, y, violet);
                g.drawPixel(W / 2 + x, y + 1, tint(violet, 0.4f));
            }
        }

        // ------------------------------------------------------------------ LED (dot matrix)
        const uint8_t FONT5X7[10][7] = {
            {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
            {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
            {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
            {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
            {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

        void ledDot(TFT_eSPI &g, int16_t x, int16_t y, int16_t p, bool lit, uint16_t accent)
        {
            const int16_t r = imax(1, p / 2 - 1);
            if (lit)
            {
                g.fillCircle(x, y, r + 1, tint(accent, 0.22f));
                g.fillCircle(x, y, r, accent);
                g.drawPixel(x, y, neonCore(accent, 0.95f));
            }
            else
                g.fillCircle(x, y, imax(1, r / 2), tint(C_SOFT, 0.16f));
        }

        void ledDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t p, int8_t d, uint16_t accent)
        {
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                {
                    const bool lit = d >= 0 && ((FONT5X7[d][row] >> (4 - col)) & 1);
                    ledDot(g, x + col * p + p / 2, y + row * p + p / 2, p, lit, accent);
                }
        }

        void styleLed(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now, uint16_t accent)
        {
            const int16_t p = imax(3, imin(6 * sc, (W - 2 * u(5)) / 25));
            const int16_t total = 25 * p, x0 = (W - total) / 2, y0 = (int16_t)(H * 0.20f);
            const int8_t d[4] = {(int8_t)(have ? ti.tm_hour / 10 : -1), (int8_t)(have ? ti.tm_hour % 10 : -1),
                                 (int8_t)(have ? ti.tm_min / 10 : -1), (int8_t)(have ? ti.tm_min % 10 : -1)};
            ledDigit(g, x0, y0, p, d[0], accent);
            ledDigit(g, x0 + 6 * p, y0, p, d[1], accent);
            const bool colonOn = !have || (ti.tm_sec & 1) == 0 || true; // steady: the seconds row below keeps time
            ledDot(g, x0 + 12 * p + p / 2, y0 + 2 * p + p / 2, p, colonOn && have, accent);
            ledDot(g, x0 + 12 * p + p / 2, y0 + 4 * p + p / 2, p, colonOn && have, accent);
            ledDigit(g, x0 + 14 * p, y0, p, d[2], accent);
            ledDigit(g, x0 + 20 * p, y0, p, d[3], accent);
            char cap[24] = "Syncing...";
            if (have)
                strftime(cap, sizeof(cap), "%a, %b %d", &ti);
            caption(g, W / 2, y0 + 7 * p + u(18), cap, C_SOFT, tint(accent, 0.8f), have);
            // the seconds: a row of 60 tiny dots
            if (have)
            {
                const int16_t sy = H - u(12), step = imax(2, (W - 2 * u(14)) / 60), sx = (W - step * 59) / 2;
                for (int i = 0; i < 60; i++)
                    g.drawPixel(sx + i * step, sy, i <= ti.tm_sec ? accent : tint(C_SOFT, 0.2f));
                (void)now;
            }
        }
    } // namespace

    uint32_t clockScreenSig(bool haveTime, const struct tm &ti, uint32_t now)
    {
        uint32_t sig = (uint32_t)ui_clock_style() << 28;
        if (!haveTime)
            return sig | 0x08000000u | (now / 90); // the syncing animation
        if (ui_clock_style() == ClockStyle::ANALOG_CLOCK) // no seconds, no colon: the dial changes once a minute
            return sig | ((uint32_t)(ti.tm_hour * 3600 + ti.tm_min * 60) & 0x1FFFF);
        if (ui_clock_style() == ClockStyle::MINIMAL) // once a minute, when the temperature changes, and per frame of the icon
        {
            bool ok;
            float tC;
            int code;
            uint32_t upd;
            readWeather(ok, tC, code, upd);
            int tq = (int)lroundf(tC) + 100; // 0 = no reading
            tq = tq < 1 ? 1 : (tq > 255 ? 255 : tq);
            sig |= (uint32_t)(ti.tm_hour * 60 + ti.tm_min) & 0x7FFu;               // bits 0..10
            sig |= (uint32_t)(ok ? tq : 0) << 11;                                   // bits 11..18
            sig |= (uint32_t)(ok ? (now / 100) & 0x1Fu : 0u) << 19;                 // bits 19..23: animation frame
            if (persona.digitPulse > 0)
                sig |= 1u << 27;
            return sig;
        }
        sig |= (uint32_t)(ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec) & 0x1FFFF;
        sig |= (uint32_t)colonLevelNow(now) << 17;
        if (ui_clock_style() == ClockStyle::RETRO)
            sig |= ((now / 90) & 0x7Fu) << 21; // the wave moves
        if (persona.digitPulse > 0)
            sig |= 1u << 27;
        return sig;
    }

    void drawAlarmOverlay(TFT_eSPI &g, bool haveTime, const struct tm &ti, bool flashOn)
    {
        const uint16_t c = flashOn ? C_RED : tint(C_RED, 0.35f);
        const int16_t dh = (int16_t)(H * 0.22f);
        timeDigits(g, W / 2, H - dh - u(5), dh, haveTime, ti, c, c, 15, tint(C_RED, 0.12f));
    }

    void drawClockScreen(TFT_eSPI &g, bool haveTime, const struct tm &ti, uint32_t now)
    {
        g.fillRect(0, 0, W, H, C_BG);
        const uint16_t accent = C_ACCENT;
        const uint16_t on = mixColor(C_ACCENT, 0xFFFF, 0.6f * persona.digitPulse); // top of the hour: flash
        switch (ui_clock_style())
        {
        case ClockStyle::ANALOG_CLOCK:
            styleAnalog(g, haveTime, ti, now, accent);
            break;
        case ClockStyle::MINIMAL:
            styleMinimal(g, haveTime, ti, now, accent, on);
            break;
        case ClockStyle::RETRO:
            styleRetro(g, haveTime, ti, now);
            break;
        case ClockStyle::LED:
            styleLed(g, haveTime, ti, now, accent);
            break;
        default:
            styleSimple(g, haveTime, ti, now, accent, on);
            break;
        }
        if (ui_clock_style() != ClockStyle::RETRO) // the retro face has its own bezel in that corner
            alarmChip(g, now);
    }
} // namespace disp
