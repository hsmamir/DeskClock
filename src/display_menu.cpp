// display_menu.cpp - the quick menu (rotary knob): alarm on/off, alarm time, volumes, pet reactions.
// Portrait layout: two-card page. Landscape layout (end of the file): the control shade, a panel that slides down.
//
// Drawn exactly like the diagnostics page: ONE page in the two existing canvases (no extra RAM), the same content
// function runs once per canvas with a y offset and anything outside the canvas is clipped. Rows sit in the same
// slots as the diagnostics rows, so none of them is ever cut by the join between the cards.
#include "display_internal.h"
#include "display_style.h"
#include "display_menu.h"
#include "quick_menu.h"

namespace disp
{
    static bool s_drawn = false;
    static uint32_t s_sig = 0;

    void menuScreenLeft()
    {
        s_drawn = false;
    }

    static const uint16_t C_ON = rgb(90, 230, 130);

    // y of every row's centre, in 128 x 160 reference units, measured from the top of the card it lives in.
    // Items 0-3 are in the upper card, 4-5 in the lower one (same slots as display_diag.cpp).
    static int16_t rowY(int item, int16_t yOff)
    {
        static const float kUp[4] = {35.5f, 49.0f, 62.5f, 76.0f};
        static const float kLow[2] = {7.5f, 21.0f};
        if (item < 4)
            return (int16_t)(u(kUp[item]) - yOff);
        return (int16_t)(faceH + u(kLow[item - 4]) - yOff);
    }

    static void drawRowFrame(TFT_eSPI &g, int16_t yc, bool sel, bool editing)
    {
        if (!sel)
            return;
#if UI_STYLE
        // a pill with a neon outline; while editing the outline turns to the blush pink
        uint16_t edge = editing ? C_BLUSH : C_ACCENT;
        g.fillRoundRect(u(3), yc - u(6), W - 2 * u(3), u(12), u(5), tint(edge, 0.20f));
        drawFrame(g, u(3), yc - u(6), W - 2 * u(3), u(12), u(5), tint(edge, 0.85f));
#else
        uint16_t bg = editing ? tint(C_AMBER, 0.30f) : tint(C_ACCENT, 0.24f);
        g.fillRoundRect(u(3), yc - u(6), W - 2 * u(3), u(12), u(3), bg);
#endif
    }

    static void drawLabel(TFT_eSPI &g, int16_t yc, const char *label, bool sel)
    {
        g.setTextDatum(ML_DATUM);
        g.setTextColor(sel ? C_WHITE : C_SOFT);
        g.drawString(label, u(7), yc, 2);
    }

    static void drawValue(TFT_eSPI &g, int16_t yc, const char *value, uint16_t color)
    {
        g.setTextDatum(MR_DATUM);
        g.setTextColor(color);
        g.drawString(value, W - u(7), yc, 2);
    }

    // The alarm time is drawn in two pieces so the field being edited (hour / minute) can be lit on its own.
    static void drawTime(TFT_eSPI &g, int16_t yc, const MenuView &v, bool sel)
    {
        char hh[4], mm[4];
        snprintf(hh, sizeof(hh), "%02u", (unsigned)v.alarmHour);
        snprintf(mm, sizeof(mm), "%02u", (unsigned)v.alarmMinute);
        bool ed = sel && v.editing;
        uint16_t base = sel ? C_WHITE : C_SOFT;
        uint16_t cH = (ed && v.field == 0) ? C_AMBER : base;
        uint16_t cM = (ed && v.field == 1) ? C_AMBER : base;

        int16_t right = W - u(7);
        int16_t wM = g.textWidth(mm, 2);
        int16_t wC = g.textWidth(":", 2);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(cM);
        g.drawString(mm, right, yc, 2);
        g.setTextColor(base);
        g.drawString(":", right - wM, yc, 2);
        g.setTextColor(cH);
        g.drawString(hh, right - wM - wC, yc, 2);
        if (ed)
        {
            // small bar under the field being edited
            int16_t x1 = (v.field == 0) ? right - wM - wC : right;
            int16_t w = (v.field == 0) ? g.textWidth(hh, 2) : wM;
            g.fillRoundRect(x1 - w, yc + u(5), w, imax(1, u(1)), imax(1, u(1)) / 2, UI_STYLE ? C_BLUSH : C_AMBER);
        }
    }

    static void drawMenuContent(TFT_eSPI &g, int16_t yOff, const MenuView &v)
    {
        auto Yt = [&](float f) -> int16_t
        { return (int16_t)(u(f) - yOff); };
        auto Yb = [&](float f) -> int16_t
        { return (int16_t)(faceH + u(f) - yOff); };

        // accent bar along the top edge of the page (upper card only)
        drawTopBar(g, yOff, C_ACCENT);

        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_ACCENT);
        g.drawString("Quick menu", W / 2, Yt(13), 2);
        drawDivider(g, u(6), W - u(6), Yt(22));

        char buf[16];
        for (int i = 0; i < (int)MenuItem::COUNT; i++)
        {
            int16_t yc = rowY(i, yOff);
            bool sel = (v.cursor == i);
            bool ed = sel && v.editing;
            drawRowFrame(g, yc, sel, ed);

            switch ((MenuItem)i)
            {
            case MenuItem::ALARM_ON:
                drawLabel(g, yc, "Alarm", sel);
                drawValue(g, yc, v.alarmOn ? "ON" : "OFF", v.alarmOn ? C_ON : C_SOFT);
                break;
            case MenuItem::ALARM_TIME:
                drawLabel(g, yc, "Time", sel);
                drawTime(g, yc, v, sel);
                break;
            case MenuItem::VOLUME:
                drawLabel(g, yc, "Volume", sel);
                snprintf(buf, sizeof(buf), "%u", (unsigned)v.volume);
                drawValue(g, yc, v.volume == 0 ? "mute" : buf, ed ? C_AMBER : (sel ? C_WHITE : C_SOFT));
                break;
            case MenuItem::ALARM_VOLUME:
                drawLabel(g, yc, "Alarm vol", sel);
                snprintf(buf, sizeof(buf), "%u", (unsigned)v.alarmVolume);
                drawValue(g, yc, buf, ed ? C_AMBER : (sel ? C_WHITE : C_SOFT));
                break;
            case MenuItem::PET_REACTIONS:
                drawLabel(g, yc, "Reactions", sel);
                drawValue(g, yc, v.petReactions ? "ON" : "OFF", v.petReactions ? C_ON : C_SOFT);
                break;
            case MenuItem::CLOSE:
                drawLabel(g, yc, "Close", sel);
                break;
            default:
                break;
            }
        }

        // hints (lower card)
        drawDivider(g, u(6), W - u(6), Yb(33));
        const char *l1, *l2, *l3;
        if (v.editing)
        {
            l1 = "Turn: change";
            l2 = (v.cursor == (uint8_t)MenuItem::ALARM_TIME && v.field == 0) ? "Push: next" : "Push: save";
            l3 = "Hold: cancel";
        }
        else
        {
            l1 = "Turn: move";
            l2 = "Push: select";
            l3 = "Hold: exit";
        }
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(l1, W / 2, Yb(43), 1);
        g.drawString(l2, W / 2, Yb(53), 1);
        g.drawString(l3, W / 2, Yb(63), 1);
    }

    void drawMenuScreen(uint32_t now)
    {
        (void)now;
        MenuView v;
        if (!quickmenu_get(v))
            return; // closing: the next tick shows the clock again

        // Redraw only when something visible changed (a turn / push, or the theme colours moved), or when
        // something repainted the strip between the cards.
        uint32_t sig = v.seq * 2654435761u ^ ((uint32_t)C_BG << 16) ^ (uint32_t)C_ACCENT;
        if (s_drawn && gapIsFull && sig == s_sig)
            return;
        s_drawn = true;
        s_sig = sig;

        TFT_eSPI &gt = faceCv.begin();
        gt.fillRect(0, 0, W, faceH, C_BG);
        drawMenuContent(gt, 0, v);
        faceCv.endAt(0, CORNERS_TOP); // outer corners only: one page, not two cards

        TFT_eSPI &gb = infoCv.begin();
        gb.fillRect(0, 0, W, infoH, C_BG);
        drawMenuContent(gb, faceH, v);
        infoCv.endAt(0, CORNERS_BOT);

        gapInvalidate();
        gapIsFull = true; // one-piece page: the clock page paints the strip between its cards again when it returns
    }

} // namespace disp

// =====================================================================
//  Landscape UI: the control shade (160 x 128)
// =====================================================================
// A panel that slides down from the top edge over the page that was up (the face or the clock). It speaks the same
// language as the pages: deep navy, white "neon" cores with a soft glow, lavender captions, the pink blush for what is
// switched on, rounded pills and small sparkles. Its lower edge is the page's top bar turned upside down: a glow line
// with a bright notch in the middle, which is also the handle of the shade.
//
//   y   0..21   status strip: time (glow) on the left, next alarm (pink pill) and WiFi bars on the right
//               (while a value is edited: a short hint instead)
//   y  22       divider with the sparkle
//   y  26..98   2 rows x 4 tiles:  Alarm | Time | Volume | Alarm vol  /  Reactions | Style | Diag | Close
//   y 106       caption of the tile under the cursor, between two sparkles
//   y 116..120  the neon edge with the notch
//
// The panel only ever grows downwards while it opens, so it is simply drawn over the last frame of the page that is
// already in the canvas (no second buffer, no redrawing the page). When it is open nothing is redrawn unless a value
// changed. Closing is instant: the page below is drawn again by the next tick.
//
// The model (cursor, editing, saving) is quick_menu.cpp; this file only draws it.
#if LANDSCAPE_UI
#include "ui_state.h"
#include "display_landscape.h"
#include "display_fonts_pages.h"
#include <WiFi.h>
#include <time.h>
#include <string.h>

#ifndef SHADE_OPEN_MS
#define SHADE_OPEN_MS 240 // slide-down time (0 = appears at once)
#endif

namespace disp
{
    static uint32_t s_openMs = 0;  // when the shade started to slide down
    static bool s_settled = false; // the fully open frame has been drawn

    namespace
    {
        const int kCols = 4;
        inline int16_t panelH() { return u(120); }

        inline const uint8_t *capFont() { return sc >= 1.5f ? PageSm18 : PageSm12; }
        inline int16_t capFontH() { return sc >= 1.5f ? PAGESM18_CAPH : PAGESM12_CAPH; }

        // the three text colours of the pages: white core, lavender captions, and a dim lavender for what is not selected
        inline uint16_t colWhite() { return neonCore(C_ACCENT, 0.9f); }
        inline uint16_t colLav() { return neonCore(C_ACCENT, 0.6f); }
        inline uint16_t colDim() { return tint(neonCore(C_ACCENT, 0.6f), 0.62f); }

        // A string with a soft glow: four offset copies in a faded accent first, then the bright core on top
        // (the same recipe as the big digits of the Date and Weather pages).
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

        // The page top bar turned upside down: glow line, faint line above it, a bright notch in the middle.
        void drawBottomBar(TFT_eSPI &g, int16_t yBottom, uint16_t accent)
        {
            const int16_t t = imax(2, u(2));
            const int16_t y1 = yBottom - t;
            g.fillRect(0, y1, W, t, tint(accent, 0.55f));
            g.fillRect(0, y1 - imax(1, u(1)), W, imax(1, u(1)), tint(accent, 0.16f));
            const int16_t sw = (int16_t)(W * 0.30f), sx = (W - sw) / 2, m = u(3);
            g.fillRect(sx - m, y1 - imax(1, u(1)), sw + 2 * m, imax(1, u(1)), tint(accent, 0.38f));
            g.fillRoundRect(sx, y1 - t, sw, 2 * t + 1, t, accent);
            g.fillRoundRect(sx + 1, y1, sw - 2, t + 1, imax(1, t / 2), neonCore(accent));
        }

        // ---- icons (about 14 x 14 px, centred on cx / cy) ----
        void iconBell(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c, int16_t r = 0)
        {
            if (r <= 0)
                r = imax(3, u(4.5f));
            g.fillCircle(cx, cy - 2, r, c);
            g.fillRect(cx - r, cy - 2, 2 * r + 1, r, c);
            g.fillRect(cx - r - 1, cy + r - 2, 2 * r + 3, 2, c);
            g.fillCircle(cx, cy + r + 1, imax(1, r / 3), c);
        }

        void iconClock(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c)
        {
            const int16_t r = imax(4, u(6.5f));
            g.drawCircle(cx, cy, r, c);
            g.drawCircle(cx, cy, r - 1, c);
            g.drawLine(cx, cy, cx, cy - r + u(3), c);
            g.drawLine(cx + 1, cy, cx + 1, cy - r + u(3), c);
            g.drawLine(cx, cy, cx + u(3), cy + u(1), c);
        }

        void iconSpeaker(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c, int level) // level 0 = muted
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
            if (level >= 12)
                g.drawCircleHelper(cx + u(1), cy, u(7), 0x6, c);
        }

        void iconHeart(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c)
        {
            g.fillCircle(cx - u(3), cy - u(2), u(3.5f), c);
            g.fillCircle(cx + u(3), cy - u(2), u(3.5f), c);
            g.fillTriangle(cx - u(6.5f), cy - u(1), cx + u(6.5f), cy - u(1), cx, cy + u(6), c);
        }

        void iconDigits(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c) // clock style
        {
            g.drawRoundRect(cx - u(7), cy - u(5), u(15), u(11), u(2), c);
            g.fillRect(cx - u(4), cy - u(2), u(3), u(5), c);
            g.fillRect(cx + u(1), cy - u(2), u(3), u(5), c);
        }

        void iconPulse(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c) // diagnostics
        {
            const int16_t px[6] = {(int16_t)(cx - u(7)), (int16_t)(cx - u(3)), (int16_t)(cx - u(1)), (int16_t)(cx + u(2)), (int16_t)(cx + u(4)), (int16_t)(cx + u(7))};
            const int16_t py[6] = {(int16_t)(cy + u(1)), (int16_t)(cy + u(1)), (int16_t)(cy - u(5)), (int16_t)(cy + u(5)), (int16_t)(cy + u(1)), (int16_t)(cy + u(1))};
            for (int i = 0; i < 5; i++)
            {
                g.drawLine(px[i], py[i], px[i + 1], py[i + 1], c);
                g.drawLine(px[i], py[i] + 1, px[i + 1], py[i + 1] + 1, c);
            }
        }

        void iconClose(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c)
        {
            const int16_t d = u(5);
            for (int k = 0; k < 2; k++)
            {
                g.drawLine(cx - d + k, cy - d, cx + d + k, cy + d, c);
                g.drawLine(cx - d + k, cy + d, cx + d + k, cy - d, c);
            }
        }

        // The alarm time of its tile: HH:MM, the field being edited lit, a small bar under it.
        void tileTime(TFT_eSPI &g, int16_t cx, int16_t cy, const MenuView &v, bool sel, uint16_t bg)
        {
            char hh[4], mm[4];
            snprintf(hh, sizeof(hh), "%02u", (unsigned)v.alarmHour);
            snprintf(mm, sizeof(mm), "%02u", (unsigned)v.alarmMinute);
            const uint8_t *f = capFont();
            const int16_t ch = capFontH();
            const int16_t wh = smoothWidth(g, f, hh), wc = smoothWidth(g, f, ":"), wm = smoothWidth(g, f, mm);
            const int16_t x = cx - (wh + wc + wm) / 2;
            const bool ed = sel && v.editing;
            const uint16_t base = sel ? colWhite() : colDim();
            smoothText(g, f, ch, hh, x, cy, L_BASELINE, (ed && v.field == 0) ? C_AMBER : base, bg);
            smoothText(g, f, ch, ":", x + wh, cy, L_BASELINE, base, bg);
            smoothText(g, f, ch, mm, x + wh + wc, cy, L_BASELINE, (ed && v.field == 1) ? C_AMBER : base, bg);
            if (ed)
                g.fillRoundRect(v.field == 0 ? x : x + wh + wc, cy + ch / 2 + u(2), v.field == 0 ? wh : wm, imax(1, u(1)), 1, C_AMBER);
        }

        const char *styleShort(uint8_t s)
        {
            switch ((ClockStyle)s)
            {
            case ClockStyle::SIMPLE:
                return "Simp";
            case ClockStyle::ANALOG_CLOCK:
                return "Anlg";
            case ClockStyle::MINIMAL:
                return "Mini";
            case ClockStyle::RETRO:
                return "Retro";
            case ClockStyle::LED:
                return "LED";
            default:
                return "?";
            }
        }

        // Four little bars, the lit ones follow the signal; red when there is no connection.
        void wifiBars(TFT_eSPI &g, int16_t x, int16_t yBottom, bool ok, int bars)
        {
            for (int i = 0; i < 4; i++)
            {
                const int16_t h = u(2.0f + 2.0f * i);
                uint16_t c = ok ? (i < bars ? neonCore(C_ACCENT, 0.5f) : tint(C_SOFT, 0.35f)) : tint(C_RED, 0.75f);
                g.fillRoundRect(x + i * u(3), yBottom - h, imax(2, u(2)), h, 1, c);
            }
        }

        void drawShade(TFT_eSPI &g, const MenuView &v, int16_t yOff)
        {
            const int16_t PH = panelH();
            const uint16_t accent = C_ACCENT, white = colWhite(), lav = colLav(), dim = colDim();
            const int16_t side = u(6), gap = u(4);
            const int16_t tw = (int16_t)((W - 2 * side - (kCols - 1) * gap) / kCols), th = u(34);
            const int16_t tilesTop = yOff + u(26);

            // ---- the panel itself: the page colour, with the neon edge below ----
            g.fillRect(0, yOff, W, PH, C_BG);
            drawBottomBar(g, yOff + PH, accent);

            // ---- status strip (a hint while a value is being edited) ----
            const int16_t sy = yOff + u(11);
            if (v.editing)
            {
                const char *hint = (v.cursor == (uint8_t)MenuItem::ALARM_TIME && v.field == 0) ? "Turn: change   Push: next" : "Turn: change   Push: save";
                smoothText(g, capFont(), capFontH(), hint, W / 2, sy, C_BASELINE, C_AMBER, C_BG);
            }
            else
            {
                struct tm ti;
                const bool have = getLocalTime(&ti, 0) && ti.tm_year >= 120;
                char tb[8];
                if (have)
                    snprintf(tb, sizeof(tb), "%02d:%02d", ti.tm_hour, ti.tm_min);
                else
                    strcpy(tb, "--:--");
                glowText(g, PageSm18, PAGESM18_CAPH, tb, side + u(1), sy, L_BASELINE, accent, white);

                // right side, from the edge inwards: WiFi bars, then the alarm pill
                const bool wifiOk = (WiFi.status() == WL_CONNECTED);
                int bars = 0;
                if (wifiOk)
                {
                    const int rssi = WiFi.RSSI();
                    bars = rssi > -55 ? 4 : (rssi > -65 ? 3 : (rssi > -75 ? 2 : 1));
                }
                const int16_t wx = W - side - u(11);
                wifiBars(g, wx, sy + u(4), wifiOk, bars);

                if (v.alarmOn)
                {
                    char ab[8];
                    snprintf(ab, sizeof(ab), "%02u:%02u", (unsigned)v.alarmHour, (unsigned)v.alarmMinute);
                    const int16_t aw = smoothWidth(g, capFont(), ab);
                    const int16_t ph = u(15), pw = u(5) + u(8) + u(3) + aw + u(5);
                    const int16_t px = wx - u(6) - pw, py = sy - ph / 2;
                    g.fillRoundRect(px - 1, py - 1, pw + 2, ph + 2, ph / 2 + 1, tint(C_BLUSH, 0.28f)); // soft glow
                    g.fillRoundRect(px, py, pw, ph, ph / 2, tint(C_BLUSH, 0.18f));
                    iconBell(g, px + u(5) + u(4), sy - u(1), C_BLUSH, imax(2, u(3.2f)));
                    smoothText(g, capFont(), capFontH(), ab, px + u(5) + u(8) + u(3), sy, L_BASELINE, lav, tint(C_BLUSH, 0.18f));
                }
            }
            drawDivider(g, side, W - side, yOff + u(22));

            // ---- tiles ----
            char buf[12];
            for (int i = 0; i < (int)MenuItem::COUNT; i++)
            {
                const int16_t x = side + (i % kCols) * (tw + gap), y = tilesTop + (i / kCols) * (th + gap);
                const int16_t cx = x + tw / 2;
                const bool sel = (v.cursor == i), ed = sel && v.editing;
                const uint16_t edge = ed ? C_BLUSH : accent;
                const uint16_t fill = sel ? tint(edge, 0.24f) : tint(accent, 0.10f);
                const int16_t r = u(8);
                if (sel) // the same selection as the pages: a soft halo, the fill, a 1 px neon outline and a sparkle
                {
                    g.fillRoundRect(x - 1, y - 1, tw + 2, th + 2, r + 1, tint(edge, 0.22f));
                    g.fillRoundRect(x, y, tw, th, r, fill);
                    drawFrame(g, x, y, tw, th, r, tint(edge, 0.9f));
                    drawSparkle(g, x + tw - u(5), y + u(5), 0.55f, neonCore(edge, 0.85f));
                }
                else
                    g.fillRoundRect(x, y, tw, th, r, fill);

                const int16_t iy = y + u(12), ty = y + u(26);
                const uint16_t ic = sel ? white : dim;                                // icons
                const uint16_t txt = ed ? C_AMBER : (sel ? white : dim);              // values
                switch ((MenuItem)i)
                {
                case MenuItem::ALARM_ON:
                    iconBell(g, cx, iy, v.alarmOn ? C_BLUSH : ic);
                    if (!v.alarmOn) // switched off: a slash through the bell
                    {
                        g.drawLine(cx - u(6), iy + u(7), cx + u(6), iy - u(7), fill);
                        g.drawLine(cx - u(6) + 1, iy + u(7), cx + u(6) + 1, iy - u(7), ic);
                    }
                    smoothText(g, capFont(), capFontH(), v.alarmOn ? "ON" : "OFF", cx, ty, C_BASELINE, v.alarmOn ? C_BLUSH : txt, fill);
                    break;
                case MenuItem::ALARM_TIME:
                    iconClock(g, cx, iy, ed ? C_AMBER : ic);
                    tileTime(g, cx, ty, v, sel, fill);
                    break;
                case MenuItem::VOLUME:
                    iconSpeaker(g, cx, iy, ed ? C_AMBER : ic, v.volume);
                    if (v.volume == 0)
                        strcpy(buf, "mute");
                    else
                        snprintf(buf, sizeof(buf), "%u", (unsigned)v.volume);
                    smoothText(g, capFont(), capFontH(), buf, cx, ty, C_BASELINE, txt, fill);
                    break;
                case MenuItem::ALARM_VOLUME:
                    iconBell(g, cx - u(2), iy, ed ? C_AMBER : ic);
                    g.drawCircleHelper(cx + u(1), iy - u(1), u(5), 0x6, ed ? C_AMBER : ic);
                    g.drawCircleHelper(cx + u(1), iy - u(1), u(8), 0x6, ed ? C_AMBER : ic);
                    snprintf(buf, sizeof(buf), "%u", (unsigned)v.alarmVolume);
                    smoothText(g, capFont(), capFontH(), buf, cx, ty, C_BASELINE, txt, fill);
                    break;
                case MenuItem::PET_REACTIONS:
                    iconHeart(g, cx, iy, v.petReactions ? C_BLUSH : ic);
                    smoothText(g, capFont(), capFontH(), v.petReactions ? "ON" : "OFF", cx, ty, C_BASELINE, v.petReactions ? C_BLUSH : txt, fill);
                    break;
                case MenuItem::CLOCK_STYLE:
                    iconDigits(g, cx, iy, ic);
                    smoothText(g, capFont(), capFontH(), styleShort(v.clockStyle), cx, ty, C_BASELINE, txt, fill);
                    break;
                case MenuItem::DIAGNOSTICS:
                    iconPulse(g, cx, iy, ic);
                    smoothText(g, capFont(), capFontH(), "Diag", cx, ty, C_BASELINE, txt, fill);
                    break;
                case MenuItem::CLOSE:
                default:
                    iconClose(g, cx, iy, ic);
                    smoothText(g, capFont(), capFontH(), "Close", cx, ty, C_BASELINE, txt, fill);
                    break;
                }
            }

            // ---- caption: the full name of the tile under the cursor, between two sparkles ----
            char cap[28];
            switch ((MenuItem)v.cursor)
            {
            case MenuItem::ALARM_ON:
                snprintf(cap, sizeof(cap), "Alarm %s", v.alarmOn ? "on" : "off");
                break;
            case MenuItem::ALARM_TIME:
                strcpy(cap, "Alarm time");
                break;
            case MenuItem::VOLUME:
                strcpy(cap, "Volume");
                break;
            case MenuItem::ALARM_VOLUME:
                strcpy(cap, "Alarm volume");
                break;
            case MenuItem::PET_REACTIONS:
                strcpy(cap, "Pet reactions");
                break;
            case MenuItem::CLOCK_STYLE:
                snprintf(cap, sizeof(cap), "Clock style: %s", ui_clock_style_name((ClockStyle)v.clockStyle));
                break;
            case MenuItem::DIAGNOSTICS:
                strcpy(cap, "Diagnostics");
                break;
            default:
                strcpy(cap, "Close");
                break;
            }
            const int16_t capY = tilesTop + 2 * th + gap + u(9);
            smoothText(g, capFont(), capFontH(), cap, W / 2, capY, C_BASELINE, lav, C_BG);
            const int16_t half = smoothWidth(g, capFont(), cap) / 2 + u(8);
            drawSparkle(g, W / 2 - half, capY - u(3), 0.8f, tint(accent, 0.7f));
            drawSparkle(g, W / 2 + half, capY - u(3), 0.8f, tint(accent, 0.7f));
        }
    } // namespace

    void drawMenuScreenL(uint32_t now)
    {
        MenuView v;
        if (!quickmenu_get(v))
            return; // closing: the next tick shows the home page again
        if (!s_drawn) // the shade has just been opened
        {
            s_openMs = now ? now : 1;
            s_settled = false;
        }

        float p = 1.0f;
        if (SHADE_OPEN_MS > 0)
        {
            const uint32_t t = now - s_openMs;
            p = t >= (uint32_t)SHADE_OPEN_MS ? 1.0f : (float)t / (float)SHADE_OPEN_MS;
        }
        const bool animating = p < 1.0f;

        struct tm ti;
        const bool have = getLocalTime(&ti, 0) && ti.tm_year >= 120;
        const uint32_t minuteKey = have ? (uint32_t)(ti.tm_hour * 60 + ti.tm_min + 1) : 0u;
        const uint32_t sig = v.seq * 2654435761u ^ ((uint32_t)C_BG << 16) ^ (uint32_t)C_ACCENT ^ ((uint32_t)v.clockStyle << 8) ^
                             (minuteKey * 7919u) ^ (WiFi.status() == WL_CONNECTED ? 0x5A5A0000u : 0u);
        if (s_drawn && s_settled && !animating && sig == s_sig)
            return;
        s_drawn = true;
        s_sig = sig;
        s_settled = !animating;

        const float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p); // fast start, soft landing
        const int16_t yOff = (int16_t)(-(1.0f - e) * (float)panelH());

        // Drawn over the last frame of the page: nothing is cleared (see the top of this block).
        TFT_eSPI &g = faceCv.begin();
        drawShade(g, v, yOff);
        faceCv.endAt(0);
    }
} // namespace disp
#endif
