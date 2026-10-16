// display_menu.cpp - the quick menu page (rotary knob): alarm on/off, alarm time, volumes, pet reactions.
//
// Drawn exactly like the diagnostics page: ONE page in the two existing canvases (no extra RAM), the same content
// function runs once per canvas with a y offset and anything outside the canvas is clipped. Rows sit in the same
// slots as the diagnostics rows, so none of them is ever cut by the join between the cards.
#include "display_internal.h"
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
        uint16_t bg = editing ? tint(C_AMBER, 0.30f) : tint(C_ACCENT, 0.24f);
        g.fillRoundRect(u(3), yc - u(6), W - 2 * u(3), u(12), u(3), bg);
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
            g.fillRect(x1 - w, yc + u(5), w, imax(1, u(1)), C_AMBER);
        }
    }

    static void drawMenuContent(TFT_eSPI &g, int16_t yOff, const MenuView &v)
    {
        auto Yt = [&](float f) -> int16_t
        { return (int16_t)(u(f) - yOff); };
        auto Yb = [&](float f) -> int16_t
        { return (int16_t)(faceH + u(f) - yOff); };

        // accent bar along the top edge of the page (upper card only)
        g.fillRect(0, -yOff, W, imax(2, u(2)), C_ACCENT);
        g.fillRect(0, imax(2, u(2)) - yOff, W, imax(1, u(1)), tint(C_ACCENT, 0.235f));

        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_ACCENT);
        g.drawString("Quick menu", W / 2, Yt(13), 2);
        g.drawFastHLine(u(6), Yt(22), W - 2 * u(6), tint(C_SOFT, 0.31f));

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
        g.drawFastHLine(u(6), Yb(33), W - 2 * u(6), tint(C_SOFT, 0.31f));
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
