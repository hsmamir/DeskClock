// display_menu.cpp - the quick menu page (rotary knob): alarm on/off, alarm time, volumes, pet reactions.
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
//  Landscape UI: the quick menu as one full-screen page (160 x 128)
// =====================================================================
#if LANDSCAPE_UI
#include "ui_state.h"
#include "display_landscape.h"

namespace disp
{
    // 7 rows do not all fit under the title: a window of kRows rows follows the cursor.
    static const int kRows = 6;

    void drawMenuScreenL(uint32_t now)
    {
        (void)now;
        MenuView v;
        if (!quickmenu_get(v))
            return; // closing: the next tick shows the home page again
        uint32_t sig = v.seq * 2654435761u ^ ((uint32_t)C_BG << 16) ^ (uint32_t)C_ACCENT ^ ((uint32_t)v.clockStyle << 8);
        if (s_drawn && sig == s_sig)
            return;
        s_drawn = true;
        s_sig = sig;

        TFT_eSPI &g = faceCv.begin();
        g.fillRect(0, 0, W, H, C_BG);
        drawTopBar(g, 0, C_ACCENT);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_ACCENT);
        g.drawString("Quick menu", W / 2, u(12), 2);

        const int16_t pitch = (H - u(24) - u(14)) / kRows; // rows between the title and the hint line
        const int first = v.cursor < kRows ? 0 : v.cursor - kRows + 1;
        char buf[16];
        for (int n = 0; n < kRows && first + n < (int)MenuItem::COUNT; n++)
        {
            const int i = first + n;
            const int16_t yc = u(24) + n * pitch + pitch / 2;
            const bool sel = (v.cursor == i), ed = sel && v.editing;
            if (sel)
            {
                uint16_t edge = ed ? C_BLUSH : C_ACCENT;
                g.fillRoundRect(u(3), yc - pitch / 2 + 1, W - 2 * u(3), pitch - 1, pitch / 2, tint(edge, 0.20f));
                drawFrame(g, u(3), yc - pitch / 2 + 1, W - 2 * u(3), pitch - 1, pitch / 2, tint(edge, 0.85f));
            }
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
            case MenuItem::CLOCK_STYLE:
                drawLabel(g, yc, "Clock", sel);
                drawValue(g, yc, ui_clock_style_name((ClockStyle)v.clockStyle), sel ? C_WHITE : C_SOFT);
                break;
            case MenuItem::CLOSE:
                drawLabel(g, yc, "Close", sel);
                break;
            default:
                break;
            }
        }
        // more rows above / below the window
        if (first > 0)
            g.fillTriangle(W - u(5), u(20), W - u(8), u(23), W - u(2), u(23), C_SOFT);
        if (first + kRows < (int)MenuItem::COUNT)
            g.fillTriangle(W - u(5), H - u(16), W - u(8), H - u(19), W - u(2), H - u(19), C_SOFT);

        const char *hint = v.editing ? ((v.cursor == (uint8_t)MenuItem::ALARM_TIME && v.field == 0) ? "Turn change  Push next" : "Turn change  Push save")
                                     : "Turn move  Push ok  Hold exit";
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(hint, W / 2, H - u(7), 1);
        faceCv.endAt(0);
    }
} // namespace disp
#endif
