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

        // Where the ink of a string lies in a VLW font, measured from the text cursor: l = left edge of the first glyph,
        // r = right edge of the last one. (textWidth() counts the empty side bearings too, so the layout would be a few px
        // off; this reads the glyph table of the font itself, see make_vlw.py for the format.)
        struct Ink
        {
            int16_t l, r;
        };

        int32_t vlwI32(const uint8_t *p)
        {
            return (int32_t)(((uint32_t)pgm_read_byte(p) << 24) | ((uint32_t)pgm_read_byte(p + 1) << 16) |
                             ((uint32_t)pgm_read_byte(p + 2) << 8) | (uint32_t)pgm_read_byte(p + 3));
        }

        Ink vlwInk(const uint8_t *font, const char *s)
        {
            const int32_t n = vlwI32(font);
            const int32_t space = (vlwI32(font + 16) + vlwI32(font + 20)) * 2 / 7; // what TFT_eSPI uses for a blank
            int32_t x = 0, l = 0, r = 0;
            bool first = true;
            for (const uint8_t *c = (const uint8_t *)s; *c;)
            {
                uint32_t cp = *c++;
                if (cp >= 0xC0 && *c) // a two-byte UTF-8 character (the degree sign)
                    cp = ((cp & 0x1Fu) << 6) | (*c++ & 0x3Fu);
                bool found = false;
                const uint8_t *gp = font + 24;
                for (int32_t i = 0; i < n; i++, gp += 28)
                    if ((uint32_t)vlwI32(gp) == cp)
                    {
                        const int32_t w = vlwI32(gp + 8), adv = vlwI32(gp + 12), dx = vlwI32(gp + 20);
                        if (first)
                            l = x + dx;
                        r = x + dx + w;
                        first = false;
                        x += adv;
                        found = true;
                        break;
                    }
                if (!found && cp == ' ')
                    x += space;
            }
            return {(int16_t)l, (int16_t)r};
        }

        // The advance (the step of the text cursor) of one character in a VLW font.
        int16_t vlwAdv(const uint8_t *font, char ch)
        {
            const int32_t n = vlwI32(font);
            const uint8_t *gp = font + 24;
            for (int32_t i = 0; i < n; i++, gp += 28)
                if ((uint32_t)vlwI32(gp) == (uint8_t)ch)
                    return (int16_t)vlwI32(gp + 12);
            return 0;
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

            const Ink ink = vlwInk(font, s);
            const int16_t x0 = cx - (ink.l + ink.r) / 2, by = cy + caph / 2; // the ink, not the advances, is centred on cx
            g.loadFont(font);
            g.setTextDatum(L_BASELINE);
            const int16_t r2 = imax(2, u(2.2f)), r1 = imax(1, u(0.9f));
            struct Pass
            {
                int16_t dx, dy;
                float glow;
            };
            // far halo (square ring), near halo (plus shape), then the core on top
            const Pass passes[] = {{(int16_t)-r2, 0, 0.16f}, {r2, 0, 0.16f}, {0, (int16_t)-r2, 0.16f}, {0, r2, 0.16f}, {(int16_t)-r1, (int16_t)-r1, 0.16f}, {r1, (int16_t)-r1, 0.16f}, {(int16_t)-r1, r1, 0.16f}, {r1, r1, 0.16f}, {(int16_t)-r1, 0, 0.38f}, {r1, 0, 0.38f}, {0, (int16_t)-r1, 0.38f}, {0, r1, 0.38f}, {0, 0, -1.0f}};
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
                    x += vlwAdv(font, s[i]);
                }
            }
            g.unloadFont();
        }

        void styleSimple(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now, uint16_t accent, uint16_t on)
        {
            const uint8_t level = have ? colonLevelNow(now) : (uint8_t)(7.5f + 7.0f * sinf(now / 450.0f));
            const uint16_t timeCol = have ? on : tint(accent, 0.55f); // dim while the time is unknown
            // time + date are one block, centred on the screen (the same distance to the top and the bottom edge)
            const int16_t digCap = sc >= 1.5f ? CLOCKBIG92_CAPH : CLOCKBIG48_CAPH, dateCap = CLOCKSM18_CAPH;
            const int16_t gap = (int16_t)(H * 0.13f), top = (H - (digCap + gap + dateCap)) / 2 - u(1); // -1: the comma hangs below the baseline
            bigTime(g, W / 2, top + digCap / 2, have, ti, timeCol, level);
            // the date: "Tue, Oct 21" (no leading zero on the day), lavender like the digits
            char cap[24] = "Syncing...";
            if (have)
            {
                char wd[8], mo[8];
                strftime(wd, sizeof(wd), "%a", &ti);
                strftime(mo, sizeof(mo), "%b", &ti);
                snprintf(cap, sizeof(cap), "%s, %s %d", wd, mo, ti.tm_mday);
            }
            const Ink ic = vlwInk(ClockSm18, cap);
            smoothText(g, ClockSm18, CLOCKSM18_CAPH, cap, (int16_t)(W / 2 - (ic.l + ic.r) / 2), (int16_t)(top + digCap + gap + dateCap / 2), L_BASELINE, neonCore(accent, 0.72f), C_BG);
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
            const float cx = W * 0.5f, cy = H * 0.5f;                   // the dial is exactly in the middle of the screen
            const float ringW = 3.0f * sc;
            const float R = H * 0.5f - 5.0f * sc - ringW * 0.5f; // the ring's outer edge is 5 px from the top and bottom edge
            const float k = R / 45.0f;                           // everything else was drawn for R = 45: scale it with the dial

            // the ring
            g.fillCircle(R16(cx), R16(cy), R16(R + ringW * 0.5f), accent);
            g.fillCircle(R16(cx), R16(cy), R16(R - ringW * 0.5f), C_BG);

            // twelve ticks: long and thick at 12 / 3 / 6 / 9, short and thin between
            for (int i = 0; i < 12; i++)
            {
                const float a = i * 0.5235988f, sx = sinf(a), sy = -cosf(a);
                const bool q = (i % 3 == 0);
                const float r0 = (q ? 33.3f : 34.0f) * k, r1 = (q ? 40.5f : 39.0f) * k;
                thickLine(g, cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1, (q ? 1.9f : 1.5f) * k, whiteHand);
            }

            if (!have)
            {
                caption(g, R16(cx), R16(cy + R * 0.5f), "Syncing...", C_SOFT, C_SOFT, false);
                return;
            }

            const float hr = (ti.tm_hour % 12) + ti.tm_min / 60.0f; // seconds are not shown: the hands move once a minute
            const float ah = hr / 12.0f * 6.2831853f, am = ti.tm_min / 60.0f * 6.2831853f;
            const float hl = 24.0f * k, ml = 28.0f * k; // hour hand / minute hand length
            gradLine(g, cx, cy, cx + sinf(ah) * hl, cy - cosf(ah) * hl, 2.2f * k, whiteHand, pinkTip);
            thickLine(g, cx, cy, cx + sinf(am) * ml, cy - cosf(am) * ml, 2.3f * k, whiteHand);
            g.fillCircle(R16(cx), R16(cy), imax(3, R16(4.4f * k)), C_CAP);
        }

        // ------------------------------------------------------------------ Minimal
        // The Minimal face as designed: hour over minute in big ExtraBold white digits with a blue glow (ClockBold45 /
        // ClockBold90) on the left; on the right, top to bottom: the weather (icon + temperature), the weekday and
        // "21 Oct". The weather row is left out until a reading exists. Its icon (display_wxicon.cpp) is animated, so this
        // face redraws about 10 times a second while the weather is shown (see clockScreenSig).

        // One string with its text cursor at x and its digits centred on cy: wide halo, near halo, then the white core.
        void glowString(TFT_eSPI &g, const uint8_t *font, int16_t caph, const char *str, int16_t x, int16_t cy, uint16_t on)
        {
            const uint16_t core = neonCore(on, 0.92f);
            const int16_t r2 = imax(2, u(2.2f)), r1 = imax(1, u(1.0f));
            struct Pass
            {
                int16_t dx, dy;
                float glow;
            };
            const Pass passes[] = {{(int16_t)-r2, 0, 0.22f}, {r2, 0, 0.22f}, {0, (int16_t)-r2, 0.22f}, {0, r2, 0.22f}, {(int16_t)-r1, (int16_t)-r1, 0.22f}, {r1, (int16_t)-r1, 0.22f}, {(int16_t)-r1, r1, 0.22f}, {r1, r1, 0.22f}, {(int16_t)-r1, 0, 0.48f}, {r1, 0, 0.48f}, {0, (int16_t)-r1, 0.48f}, {0, r1, 0.48f}, {0, 0, -1.0f}};
            g.loadFont(font);
            g.setTextDatum(L_BASELINE);
            for (const Pass &p : passes)
            {
                if (p.glow < 0)
                    g.setTextColor(core, tint(on, 0.48f));
                else
                    g.setTextColor(tint(on, p.glow), C_BG);
                g.drawString(str, x + p.dx, cy + caph / 2 + p.dy);
            }
            g.unloadFont();
        }

        // Everything sits in one block that is centred on the screen: the same distance to the left and the right edge, and
        // to the top and the bottom edge. Left: the hour over the minute (the top of the hour and the bottom of the minute
        // are the top and bottom of the block). Right: weather, weekday, date, left-aligned. The widths are measured from
        // the ink of the real text, so the block stays centred whatever the day name, date or temperature is.
        void styleMinimal(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now, uint16_t accent, uint16_t on)
        {
            const bool big = sc >= 1.5f;
            const uint8_t *bold = big ? ClockBold90 : ClockBold45;
            const int16_t boldCap = big ? CLOCKBOLD90_CAPH : CLOCKBOLD45_CAPH;
            char hh[4] = "--", mm[4] = "--";
            char wd[8] = "--", dm[16] = "", tx[12] = "";
            if (have)
            {
                char mo[8];
                snprintf(hh, sizeof(hh), "%02d", ti.tm_hour);
                snprintf(mm, sizeof(mm), "%02d", ti.tm_min);
                strftime(wd, sizeof(wd), "%a", &ti);
                strftime(mo, sizeof(mo), "%b", &ti);
                snprintf(dm, sizeof(dm), "%d %s", ti.tm_mday, mo);
            }
            bool wxOk;
            float tC;
            int code;
            uint32_t upd;
            readWeather(wxOk, tC, code, upd);
            if (wxOk)
                snprintf(tx, sizeof(tx), "%d\xC2\xB0", (int)lroundf(tC));

            // widths of the ink
            const float s = 11.0f * sc; // icon half width: a 22 px wide icon on the 160 x 128 screen (sc = 1 there)
            const int16_t iconGap = u(3), colGap = u(16);
            const Ink ih = vlwInk(bold, hh), im = vlwInk(bold, mm), iw = vlwInk(ClockSm18, wd), id = vlwInk(ClockSm18, dm),
                      it = vlwInk(ClockSm18, tx);
            // the digit column keeps the width of a wide pair ("00" / "88"), so the block does not wobble as "11" or "20" come by
            const Ink i00 = vlwInk(bold, "00"), i88 = vlwInk(bold, "88");
            const int16_t wl = imax(imax(i00.r - i00.l, i88.r - i88.l), imax(ih.r - ih.l, im.r - im.l));
            const int16_t wRow = wxOk ? (int16_t)(R16(2 * s) + iconGap + (it.r - it.l)) : (int16_t)0;
            const int16_t wr = imax(wRow, imax(iw.r - iw.l, id.r - id.l));
            const int16_t xL = (W - (wl + colGap + wr)) / 2; // left edge of the block
            const int16_t xR = xL + wl + colGap;             // left edge of the right column

            // the rows: hour and minute the same distance above and below the middle (the design: 22 px of 128)
            const int16_t yH = (int16_t)(H / 2 - H * 0.1735f), yM = (int16_t)(H / 2 + H * 0.1735f);
            glowString(g, bold, boldCap, hh, (int16_t)(xL + wl / 2 - (ih.l + ih.r) / 2), yH, on);
            glowString(g, bold, boldCap, mm, (int16_t)(xL + wl / 2 - (im.l + im.r) / 2), yM, on);

            const uint16_t lav = neonCore(accent, 0.58f);
            if (wxOk)
            {
                drawWxIcon(g, code, !have || sun_is_day(ti), xR + s, yH, s, now); // animated, day or night sky
                smoothText(g, ClockSm18, CLOCKSM18_CAPH, tx, (int16_t)(xR + R16(2 * s) + iconGap - it.l), yH, L_BASELINE, neonCore(accent, 0.9f), C_BG);
            }
            smoothText(g, ClockSm18, CLOCKSM18_CAPH, wd, (int16_t)(xR - iw.l), (int16_t)(yM - u(9)), L_BASELINE, lav, C_BG);
            if (have)
                smoothText(g, ClockSm18, CLOCKSM18_CAPH, dm, (int16_t)(xR - id.l), (int16_t)(yM + u(9)), L_BASELINE, lav, C_BG);
        }

        // ------------------------------------------------------------------ Retro
        // The Retro face: the classic seven-segment digits (the segments touch, as on every other face) in violet with a
        // proper neon look - violet edge, a lighter tube inside and a soft glow that fades out over three steps - plus
        // round colon dots, a small date line and the wave that keeps moving. No bezel, no scan lines.
        struct Rc
        {
            int16_t x, y, w, h;
        };

        // phase 0 = glow of the lit segments, 1 = the unlit "ghost" segments, 2 = the lit segments. d < 0: nothing lit.
        void retroDigit(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int8_t d, uint8_t phase, uint16_t violet)
        {
            static const uint8_t MASK[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // bit0 = a (top) ... bit6 = g (middle)
            const int16_t t = imax(2, w / 5), vh = (h - 3 * t) / 2;
            const Rc s[7] = {{(int16_t)(x + t), y, (int16_t)(w - 2 * t), t},                      // a top
                             {(int16_t)(x + w - t), (int16_t)(y + t), t, vh},                     // b top right
                             {(int16_t)(x + w - t), (int16_t)(y + 2 * t + vh), t, vh},            // c bottom right
                             {(int16_t)(x + t), (int16_t)(y + h - t), (int16_t)(w - 2 * t), t},   // d bottom
                             {x, (int16_t)(y + 2 * t + vh), t, vh},                               // e bottom left
                             {x, (int16_t)(y + t), t, vh},                                        // f top left
                             {(int16_t)(x + t), (int16_t)(y + t + vh), (int16_t)(w - 2 * t), t}}; // g middle
            const uint8_t m = (d >= 0 && d <= 9) ? MASK[d] : 0;
            const uint16_t tube = mixColor(violet, C_WHITE, 0.55f); // the bright inside of the neon tube
            for (int i = 0; i < 7; i++)
            {
                const bool lit = (m >> i) & 1;
                const Rc &r = s[i];
                if (phase == 0 && lit)
                {
                    g.fillRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, t / 2 + 3, tint(violet, 0.05f));
                    g.fillRoundRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, t / 2 + 2, tint(violet, 0.12f));
                    g.fillRoundRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, t / 2 + 1, tint(violet, 0.28f));
                }
                else if (phase == 1 && !lit)
                    g.fillRoundRect(r.x, r.y, r.w, r.h, t / 2, tint(violet, 0.14f));
                else if (phase == 2 && lit)
                {
                    g.fillRoundRect(r.x, r.y, r.w, r.h, t / 2, violet);
                    if (t >= 3)
                        g.fillRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, imax(0, t / 2 - 1), tube);
                    else
                        g.fillRoundRect(r.x, r.y, r.w, r.h, t / 2, mixColor(violet, tube, 0.6f));
                }
            }
        }

        void styleRetro(TFT_eSPI &g, bool have, const struct tm &ti, uint32_t now)
        {
            const uint16_t violet = C_RETRO;
            const int16_t dh = (int16_t)(H * 0.32f), dw = (int16_t)(dh * 0.56f), gap = imax(2, u(3)), cw = imax(4, u(8));
            const int16_t total = 4 * dw + 4 * gap + cw, x0 = W / 2 - total / 2;
            int16_t dy; // set below: the whole block is centred
            const int16_t xs[4] = {x0, (int16_t)(x0 + dw + gap), (int16_t)(x0 + 2 * dw + 3 * gap + cw), (int16_t)(x0 + 3 * dw + 4 * gap + cw)};
            // digits, date and wave are one block centred on the screen: top of the digits .. bottom of the wave
            const float amp = 3.0f * sc, th = 2.2f * sc;
            const int16_t dateDy = (int16_t)(H * 0.287f), waveDy = (int16_t)(H * 0.425f); // from the middle of the digits
            const int16_t blockH = (int16_t)(dh / 2 + waveDy + amp + th * 0.5f + 1);
            const int16_t yMid = (H - blockH) / 2 + dh / 2; // the vertical middle of the digits
            dy = yMid - dh / 2;
            const int8_t d[4] = {(int8_t)(have ? ti.tm_hour / 10 : -1), (int8_t)(have ? ti.tm_hour % 10 : -1),
                                 (int8_t)(have ? ti.tm_min / 10 : -1), (int8_t)(have ? ti.tm_min % 10 : -1)};
            for (uint8_t phase = 0; phase < 3; phase++) // glows first, then the ghosts, then the lit pills: nothing covers a glow
                for (int i = 0; i < 4; i++)
                    retroDigit(g, xs[i], dy, dw, dh, d[i], phase, violet);

            // the colon: two round dots that blink with the seconds
            const bool colonOn = !have || (ti.tm_sec & 1) == 0;
            const int16_t cx = x0 + 2 * dw + 2 * gap + cw / 2, cr = imax(2, u(1.8f));
            const uint16_t dot = colonOn ? mixColor(violet, C_WHITE, 0.45f) : tint(violet, 0.25f);
            for (int i = 0; i < 2; i++)
            {
                const int16_t cy = dy + (int16_t)(dh * (i ? 0.70f : 0.30f));
                if (colonOn)
                {
                    g.fillCircle(cx, cy, cr + 2, tint(violet, 0.09f));
                    g.fillCircle(cx, cy, cr + 1, tint(violet, 0.22f));
                }
                g.fillCircle(cx, cy, cr, dot);
            }

            // the date, white-lavender (the small font)
            char cap[24] = "Syncing...";
            if (have)
            {
                char wd[8], mo[8];
                strftime(wd, sizeof(wd), "%a", &ti);
                strftime(mo, sizeof(mo), "%b", &ti);
                snprintf(cap, sizeof(cap), "%s, %s %d", wd, mo, ti.tm_mday);
            }
            const Ink ic = vlwInk(ClockSm12, cap);
            smoothText(g, ClockSm12, CLOCKSM12_CAPH, cap, (int16_t)(W / 2 - (ic.l + ic.r) / 2), (int16_t)(yMid + dateDy), L_BASELINE, mixColor(violet, C_WHITE, 0.82f), C_BG); // small, like the design

            // the little wave: one period, drifting, violet -> pink along its length, round and glowing
            const uint16_t pink = rgb(236, 130, 226);
            const int16_t wy = (int16_t)(yMid + waveDy), ww = (int16_t)(34 * sc);
            float px = 0, py = 0;
            for (int16_t i = 0; i <= ww; i++)
            {
                const float k = (float)i / ww;
                const float x = W / 2 - ww / 2 + i, y = wy + amp * sinf(k * 6.2831853f + now / 650.0f);
                const uint16_t c = mixColor(violet, pink, k);
                if (i > 0)
                {
                    thickLine(g, px, py, x, y, th + 2.0f, tint(c, 0.20f)); // glow
                }
                px = x;
                py = y;
            }
            px = W / 2 - ww / 2;
            py = wy + amp * sinf(now / 650.0f);
            for (int16_t i = 1; i <= ww; i++)
            {
                const float k = (float)i / ww;
                const float x = W / 2 - ww / 2 + i, y = wy + amp * sinf(k * 6.2831853f + now / 650.0f);
                thickLine(g, px, py, x, y, th, mixColor(violet, pink, k));
                px = x;
                py = y;
            }
        }

        // ------------------------------------------------------------------ LED (dot matrix)
        const uint8_t FONT5X7[10][7] = {
            {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

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
            return sig | 0x08000000u | (now / 90);        // the syncing animation
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
            sig |= (uint32_t)(ti.tm_hour * 60 + ti.tm_min) & 0x7FFu; // bits 0..10
            sig |= (uint32_t)(ok ? tq : 0) << 11;                    // bits 11..18
            sig |= (uint32_t)(ok ? (now / 100) & 0x1Fu : 0u) << 19;  // bits 19..23: animation frame
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
        alarmChip(g, now);
    }
} // namespace disp