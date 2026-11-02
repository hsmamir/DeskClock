// display_setup.cpp - the pages of the first-boot setup (landscape UI, 160 x 128). The model and the input are in
// setup_wizard.cpp; this file only draws them, in the same language as the other pages: deep navy, white neon cores
// with a soft glow, lavender captions, rounded pills, sparkles.
//
//   Welcome   the REAL face of the clock (display.cpp draws it as usual, blinking and looking around) with a greeting
//             above the eyes and "push to start / hold to skip" below them
//   Place     a list of cities; the one in the middle is the one picked (weather location + time zone)
//   Volume    a big glowing number and a neon bar
//   Style     the REAL clock page in the style that is picked, with a small selector pill over its lower edge
//   Alarm     on / off, then the hour and the minute
//   All set   what was chosen, and where to find the rest (the web page of the clock)
//
// Every page draws the whole screen; it is redrawn only when something changed.
#include "display_internal.h"
#include "display_style.h"
#include "display_landscape.h"
#include "display_fonts_pages.h"
#include "setup_wizard.h"
#include "ui_state.h"
#include "config.h"
#include <time.h>
#include <string.h>

#if LANDSCAPE_UI
namespace disp
{
    static bool s_drawn = false;
    static uint32_t s_sig = 0;

    void setupScreenLeft() { s_drawn = false; }

    bool setupShowsFace()
    {
        SetupView v;
        return setup_get(v) && v.step == (uint8_t)SetupStep::WELCOME;
    }

    namespace
    {
        inline const uint8_t *capFont() { return sc >= 1.5f ? PageSm18 : PageSm12; }
        inline int16_t capFontH() { return sc >= 1.5f ? PAGESM18_CAPH : PAGESM12_CAPH; }
        inline uint16_t colWhite() { return neonCore(C_ACCENT, 0.9f); }
        inline uint16_t colLav() { return neonCore(C_ACCENT, 0.6f); }
        inline uint16_t colDim() { return tint(neonCore(C_ACCENT, 0.6f), 0.62f); }

        // A string with a soft glow (the recipe of the big digits on the Date and Weather pages).
        void glowText(TFT_eSPI &g, const uint8_t *font, int16_t capH, const char *s, int16_t x, int16_t cy, uint8_t datum,
                      uint16_t accent, uint16_t core)
        {
            const int16_t r = imax(1, u(1.2f));
            const int16_t off[4][2] = {{(int16_t)-r, 0}, {r, 0}, {0, (int16_t)-r}, {0, r}};
            g.loadFont(font);
            g.setTextDatum(datum);
            g.setTextColor(tint(accent, 0.26f), C_BG);
            for (const auto &o : off)
                g.drawString(s, x + o[0], cy + capH / 2 + o[1]);
            g.setTextColor(core, tint(accent, 0.26f));
            g.drawString(s, x, cy + capH / 2);
            g.unloadFont();
        }

        // Title and divider of a page.
        void header(TFT_eSPI &g, const char *title)
        {
            glowText(g, PageSm18, PAGESM18_CAPH, title, W / 2, u(14), C_BASELINE, C_ACCENT, colWhite());
            drawDivider(g, u(6), W - u(6), u(26));
        }

        // Progress dots (one per page) and the hint line at the foot of a page.
        void footer(TFT_eSPI &g, uint8_t step, const char *hint)
        {
            const int n = (int)SetupStep::COUNT;
            const int16_t y = H - u(15), gapX = u(9);
            for (int i = 0; i < n; i++)
            {
                const int16_t x = W / 2 + (int16_t)((i - (n - 1) / 2.0f) * gapX);
                if (i == step)
                {
                    g.fillCircle(x, y, imax(2, u(3)), tint(C_ACCENT, 0.35f));
                    g.fillCircle(x, y, imax(1, u(2)), neonCore(C_ACCENT));
                }
                else
                    g.fillCircle(x, y, imax(1, u(1.5f)), i < step ? tint(C_ACCENT, 0.7f) : tint(C_ACCENT, 0.27f));
            }
            smoothText(g, capFont(), capFontH(), hint, W / 2, H - u(6), C_BASELINE, colDim(), C_BG);
        }

        // The selection look of the pages: halo, fill, 1 px neon outline.
        void pill(TFT_eSPI &g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t edge, bool selected)
        {
            if (selected)
            {
                g.fillRoundRect(x - 1, y - 1, w + 2, h + 2, r + 1, tint(edge, 0.22f));
                g.fillRoundRect(x, y, w, h, r, tint(edge, 0.24f));
                drawFrame(g, x, y, w, h, r, tint(edge, 0.9f));
            }
            else
                g.fillRoundRect(x, y, w, h, r, tint(C_ACCENT, 0.10f));
        }

        void iconBell(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c)
        {
            const int16_t r = imax(3, u(4.5f));
            g.fillCircle(cx, cy - 2, r, c);
            g.fillRect(cx - r, cy - 2, 2 * r + 1, r, c);
            g.fillRect(cx - r - 1, cy + r - 2, 2 * r + 3, 2, c);
            g.fillCircle(cx, cy + r + 1, imax(1, r / 3), c);
        }

        void iconSpeaker(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c, int level)
        {
            g.fillRect(cx - u(7), cy - u(2), u(3), u(5), c);
            g.fillTriangle(cx - u(4), cy - u(3), cx + u(1), cy - u(7), cx + u(1), cy + u(7), c);
            g.fillTriangle(cx - u(4), cy - u(3), cx - u(4), cy + u(3), cx + u(1), cy + u(7), c);
            if (level <= 0)
            {
                g.drawLine(cx + u(3), cy - u(3), cx + u(8), cy + u(3), c);
                g.drawLine(cx + u(3), cy + u(3), cx + u(8), cy - u(3), c);
                return;
            }
            g.drawCircleHelper(cx + u(1), cy, u(4), 0x6, c);
            if (level >= 10)
                g.drawCircleHelper(cx + u(1), cy, u(7), 0x6, c);
            if (level >= 20)
                g.drawCircleHelper(cx + u(1), cy, u(10), 0x6, c);
        }

        // ------------------------------------------------------------------ the pages
        void pagePlace(TFT_eSPI &g, const SetupView &v)
        {
            header(g, "Where are you?");
            const int16_t y0 = u(31), rowH = u(13), selH = u(20), side = u(6);
            for (int o = -2; o <= 2; o++)
            {
                const int idx = (int)v.place + o;
                const char *name = setup_place_name(idx), *utc = setup_place_utc(idx);
                if (o == 0)
                {
                    const int16_t y = y0 + 2 * rowH;
                    pill(g, side, y, W - 2 * side, selH, u(8), C_ACCENT, true);
                    smoothText(g, PageSm18, PAGESM18_CAPH, name, u(14), y + selH / 2, L_BASELINE, colWhite(), tint(C_ACCENT, 0.24f));
                    smoothText(g, capFont(), capFontH(), utc, W - u(14), y + selH / 2, R_BASELINE, colLav(), tint(C_ACCENT, 0.24f));
                    continue;
                }
                const int16_t top = o < 0 ? y0 + (o + 2) * rowH : y0 + 2 * rowH + selH + (o - 1) * rowH;
                const uint16_t col = (o == -1 || o == 1) ? colDim() : tint(colDim(), 0.55f);
                smoothText(g, capFont(), capFontH(), name, u(16), top + rowH / 2, L_BASELINE, col, C_BG);
                smoothText(g, capFont(), capFontH(), utc, W - u(16), top + rowH / 2, R_BASELINE, col, C_BG);
            }
            footer(g, v.step, "Turn: city   Push: next");
        }

        void pageVolume(TFT_eSPI &g, const SetupView &v)
        {
            header(g, "Sound volume");
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", (unsigned)v.volume);
            iconSpeaker(g, W / 2 - u(40), u(56), v.volume == 0 ? colDim() : colLav(), v.volume);
            glowText(g, PageTemp36, PAGETEMP36_CAPH, buf, W / 2 + u(8), u(56), C_BASELINE, C_ACCENT, colWhite());
            drawNeonBar(g, u(16), u(78), W - 2 * u(16), imax(4, u(8)), (float)v.volume / 30.0f, C_ACCENT, true);
            smoothText(g, capFont(), capFontH(), v.volume == 0 ? "Muted" : "Turn the knob to hear it", W / 2, u(96), C_BASELINE,
                       colDim(), C_BG);
            footer(g, v.step, "Turn: volume   Push: next");
        }

        // The real clock page in the picked style, with a selector pill over its lower edge.
        void pageStyle(TFT_eSPI &g, const SetupView &v, bool haveTime, const struct tm &ti, uint32_t now)
        {
            drawClockScreen(g, haveTime, ti, now);
            const int16_t pw = W - 2 * u(14), ph = u(22), px = u(14), py = H - u(28);
            g.fillRoundRect(px - 1, py - 1, pw + 2, ph + 2, ph / 2 + 1, tint(C_ACCENT, 0.30f));
            g.fillRoundRect(px, py, pw, ph, ph / 2, mixColor(C_BG, C_ACCENT, 0.16f));
            drawFrame(g, px, py, pw, ph, ph / 2, tint(C_ACCENT, 0.9f));
            const int16_t cy = py + ph / 2;
            g.fillTriangle(px + u(14), cy, px + u(20), cy - u(5), px + u(20), cy + u(5), colLav());                       // <
            g.fillTriangle(px + pw - u(14), cy, px + pw - u(20), cy - u(5), px + pw - u(20), cy + u(5), colLav());       // >
            smoothText(g, PageSm18, PAGESM18_CAPH, ui_clock_style_name((ClockStyle)v.style), W / 2, cy, C_BASELINE, colWhite(),
                       mixColor(C_BG, C_ACCENT, 0.16f));
        }

        void pageAlarm(TFT_eSPI &g, const SetupView &v)
        {
            header(g, "Wake-up alarm");
            const int16_t side = u(14), ph = u(22), py = u(33);
            const bool onSel = (v.field == 0);
            pill(g, side, py, W - 2 * side, ph, ph / 2, v.alarmOn ? C_BLUSH : C_ACCENT, onSel);
            iconBell(g, side + u(14), py + ph / 2 - u(1), v.alarmOn ? C_BLUSH : colDim());
            const uint16_t fill = onSel ? tint(v.alarmOn ? C_BLUSH : C_ACCENT, 0.24f) : tint(C_ACCENT, 0.10f);
            smoothText(g, PageSm18, PAGESM18_CAPH, "Alarm", side + u(30), py + ph / 2, L_BASELINE, onSel ? colWhite() : colLav(), fill);
            smoothText(g, PageSm18, PAGESM18_CAPH, v.alarmOn ? "ON" : "OFF", W - side - u(12), py + ph / 2, R_BASELINE,
                       v.alarmOn ? C_BLUSH : colDim(), fill);

            if (v.alarmOn)
            {
                char hh[4], mm[4];
                snprintf(hh, sizeof(hh), "%02u", (unsigned)v.alarmHour);
                snprintf(mm, sizeof(mm), "%02u", (unsigned)v.alarmMinute);
                const int16_t cy = u(78), gapW = u(14);
                const int16_t wh = smoothWidth(g, PageTemp36, hh), wm = smoothWidth(g, PageTemp36, mm);
                const int16_t x0 = W / 2 - (wh + gapW + wm) / 2;
                const uint16_t cH = (v.field == 1) ? C_AMBER : colWhite(), cM = (v.field == 2) ? C_AMBER : colWhite();
                glowText(g, PageTemp36, PAGETEMP36_CAPH, hh, x0, cy, L_BASELINE, v.field == 1 ? C_AMBER : C_ACCENT, cH);
                glowText(g, PageTemp36, PAGETEMP36_CAPH, mm, x0 + wh + gapW, cy, L_BASELINE, v.field == 2 ? C_AMBER : C_ACCENT, cM);
                g.fillCircle(x0 + wh + gapW / 2, cy - u(6), imax(1, u(1.8f)), colLav()); // the colon
                g.fillCircle(x0 + wh + gapW / 2, cy + u(6), imax(1, u(1.8f)), colLav());
                if (v.field >= 1)
                {
                    const int16_t ux = v.field == 1 ? x0 : x0 + wh + gapW, uw = v.field == 1 ? wh : wm;
                    g.fillRoundRect(ux, cy + PAGETEMP36_CAPH / 2 + u(4), uw, imax(2, u(2)), 1, C_AMBER);
                }
            }
            else
                smoothText(g, capFont(), capFontH(), "No alarm - you can add one later", W / 2, u(78), C_BASELINE, colDim(), C_BG);

            const char *hint = v.field == 0 ? "Turn: on/off   Push: next" : (v.field == 1 ? "Turn: hour   Push: minute" : "Turn: minute   Push: done");
            footer(g, v.step, hint);
        }

        void pageDone(TFT_eSPI &g, const SetupView &v)
        {
            header(g, "All set!");
            smoothText(g, PageSm18, PAGESM18_CAPH, setup_place_name(v.place), W / 2, u(42), C_BASELINE, colWhite(), C_BG);
            char line[40];
            if (v.alarmOn)
                snprintf(line, sizeof(line), "Alarm at %02u:%02u   Volume %u", (unsigned)v.alarmHour, (unsigned)v.alarmMinute, (unsigned)v.volume);
            else
                snprintf(line, sizeof(line), "No alarm   Volume %u", (unsigned)v.volume);
            smoothText(g, capFont(), capFontH(), line, W / 2, u(58), C_BASELINE, colLav(), C_BG);
            drawSparkle(g, W / 2 - u(30), u(69), 0.6f, tint(C_ACCENT, 0.6f));
            drawSparkle(g, W / 2 + u(30), u(69), 0.6f, tint(C_ACCENT, 0.6f));
            smoothText(g, capFont(), capFontH(), "More settings on your phone:", W / 2, u(80), C_BASELINE, colDim(), C_BG);
            glowText(g, PageSm18, PAGESM18_CAPH, WEB_HOSTNAME ".local", W / 2, u(96), C_BASELINE, C_ACCENT, colWhite());
            footer(g, v.step, "Push to finish");
        }
    } // namespace

    // The greeting of the Welcome page, laid over the face (the eyes sit between about y 40 and y 92 of the 128, so the
    // text goes above and below them). Drawn by display.cpp right after the face, every frame.
    void drawSetupWelcomeOverlay(TFT_eSPI &g)
    {
        SetupView v;
        if (!setup_get(v))
            return;
        glowText(g, PageSm18, PAGESM18_CAPH, "Hello!", W / 2, u(17), C_BASELINE, C_ACCENT, colWhite());
        drawSparkle(g, W / 2 - u(40), u(15), 0.8f, tint(C_ACCENT, 0.8f));
        drawSparkle(g, W / 2 + u(40), u(15), 0.8f, tint(C_ACCENT, 0.8f));
        smoothText(g, capFont(), capFontH(), "Let's set up your clock", W / 2, u(100), C_BASELINE, colLav(), C_BG);
        footer(g, v.step, "Push: start   Hold: skip");
    }

    void drawSetupScreenL(uint32_t now)
    {
        SetupView v;
        if (!setup_get(v))
            return;

        struct tm ti = {};
        const bool haveTime = getLocalTime(&ti, 0) && ti.tm_year >= 120;
        const bool isStyle = (v.step == (uint8_t)SetupStep::STYLE);
        // the clock page moves on by itself (its own signature); the other pages change only with the model
        const uint32_t sig = v.seq * 2654435761u ^ ((uint32_t)C_BG << 16) ^ (uint32_t)C_ACCENT ^ (isStyle ? clockScreenSig(haveTime, ti, now) : 0u);
        if (s_drawn && sig == s_sig)
            return;
        s_drawn = true;
        s_sig = sig;

        TFT_eSPI &g = faceCv.begin();
        if (isStyle)
            pageStyle(g, v, haveTime, ti, now);
        else
        {
            g.fillRect(0, 0, W, H, C_BG);
            switch ((SetupStep)v.step)
            {
            case SetupStep::WELCOME: // normally drawn over the real face by display.cpp; this is only the fallback
                drawSetupWelcomeOverlay(g);
                break;
            case SetupStep::PLACE:
                pagePlace(g, v);
                break;
            case SetupStep::VOLUME:
                pageVolume(g, v);
                break;
            case SetupStep::ALARM:
                pageAlarm(g, v);
                break;
            default:
                pageDone(g, v);
                break;
            }
        }
        faceCv.endAt(0);
    }
} // namespace disp
#endif
