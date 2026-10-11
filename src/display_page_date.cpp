// display_page_date.cpp - the Date page (full screen): weekday, big day number, month and year in the upper half,
// a month calendar in the lower half (today highlighted, past days dimmed).
//
// Both halves use canvas coordinates (see display_pages.cpp): the upper half is laid out from its top edge, the
// calendar from the bottom edge of the lower half, so nothing ever crosses the strip between the cards.
#include "display_internal.h"

namespace disp
{

    static int daysInMonth(int year, int mon) // mon = 0..11
    {
        static const uint8_t dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
            return 29;
        return dm[mon];
    }

    static const char WD_INITIAL[7] = {'S', 'M', 'T', 'W', 'T', 'F', 'S'}; // Sunday first, like tm_wday

    static uint8_t calFont() { return sc >= 1.5f ? 2 : 1; } // built-in font for the calendar digits

    // x of the middle of calendar column c (0..6)
    static int16_t calColX(int c)
    {
        int16_t gx0 = u(6), gw = W - 2 * u(6);
        return (int16_t)(gx0 + (c * 2 + 1) * gw / 14);
    }

    void drawDatePageTop(TFT_eSPI &g, bool haveTime, const struct tm &ti)
    {
        const int16_t dh = u(32); // a bit smaller than before: the Date page's upper card is shorter (geometrySet)
        const int16_t dw = (int16_t)(dh * 0.56f);
        const int16_t gap = imax(2, u(3));
        const int16_t dy = u(20);

        if (!haveTime)
        {
            // no time yet: two unlit digits (the eyes are on the clock page, they are not here to "think" about it)
            int16_t x0 = (W - (2 * dw + gap)) / 2;
            for (uint8_t i = 0; i < 2; i++)
                drawSegDigit(g, x0 + i * (dw + gap), dy, dw, dh, -1, C_AMBER, C_GHOST);
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("Syncing...", W / 2, dy + dh + u(7), 2);
            return;
        }

        char buf[16];

        // weekday, centred on top
        strftime(buf, sizeof(buf), "%A", &ti);
        {
            bool fits = smoothWidth(g, ClockSm18, buf) <= W - u(8);
            smoothText(g, fits ? ClockSm18 : ClockSm12, fits ? CLOCKSM18_CAPH : CLOCKSM12_CAPH, buf, W / 2, u(10),
                       C_BASELINE, mixColor(C_SOFT, 0xFFFF, 0.5f), C_BG);
        }

        // day number (big, amber), a thin divider, then month and year
        int8_t d[2] = {(int8_t)(ti.tm_mday / 10), (int8_t)(ti.tm_mday % 10)};
        int16_t dayW = 2 * dw + gap;
        int16_t x0 = u(9);
        drawSegNumber(g, x0, dy, dw, dh, gap, d, 2, C_AMBER);

        int16_t divX = x0 + dayW + u(8);
        g.drawFastVLine(divX, dy + 2, dh - 4, tint(C_AMBER, 0.27f));

        int16_t rx1 = W - u(5);
        int16_t cx = divX + (rx1 - divX) / 2;
        int16_t availW = rx1 - divX - u(4);

        char mon[8], monUp[8];
        strftime(mon, sizeof(mon), "%b", &ti);
        strlcpy(monUp, mon, sizeof(monUp));
        for (char *p = monUp; *p; p++)
            if (*p >= 'a' && *p <= 'z')
                *p -= 32;
        int16_t monY = dy + (int16_t)(dh * 0.28f);
        if (smoothWidth(g, ClockSm26, monUp) <= availW)
            smoothText(g, ClockSm26, CLOCKSM26_CAPH, monUp, cx, monY, C_BASELINE, C_AMBER, C_BG);
        else // a wide month on a narrow screen: the smaller mixed-case font
            smoothText(g, ClockSm18, CLOCKSM18_CAPH, mon, cx, monY, C_BASELINE, C_AMBER, C_BG);

        strftime(buf, sizeof(buf), "%Y", &ti);
        {
            bool big = sc >= 1.5f; // the year is a small caption: 12 px font on the 128 px display, 18 px on bigger ones
            smoothText(g, big ? ClockSm18 : ClockSm12, big ? CLOCKSM18_CAPH : CLOCKSM12_CAPH, buf, cx,
                       dy + (int16_t)(dh * 0.84f), C_BASELINE, C_SOFT, C_BG);
        }

        // calendar header (weekday initials) at the bottom edge of the upper half; today's column is amber
        const uint8_t cf = calFont();
        int16_t hy = faceH - u(6);
        g.setTextDatum(MC_DATUM);
        for (int c = 0; c < 7; c++)
        {
            int wd = (CALENDAR_WEEK_START + c) % 7;
            char t[2] = {WD_INITIAL[wd], 0};
            g.setTextColor(wd == ti.tm_wday ? C_AMBER : tint(C_SOFT, 0.7f));
            g.drawString(t, calColX(c), hy, cf);
        }
    }

    void drawDatePageBottom(TFT_eSPI &g, bool haveTime, const struct tm &ti)
    {
        if (!haveTime)
            return;

        // where the 1st of the month falls in the week, and how many week rows the month needs
        int first = (((ti.tm_wday - (ti.tm_mday - 1)) % 7) + 7) % 7; // weekday of the 1st, 0 = Sunday
        int off = (first - CALENDAR_WEEK_START + 7) % 7;             // empty cells before the 1st
        int dim = daysInMonth(ti.tm_year + 1900, ti.tm_mon);
        int weeks = (off + dim + 6) / 7;

        // Rows get as much height as the card allows: the grid starts right under the top edge and runs down to
        // just above the page dots. The pitch is a float so a 6-week month spreads its rows evenly (an integer
        // pitch lost up to a pixel per row, and 6 rows of 8 px left no gap between the digits).
        int16_t gridTop = u(2);
        int16_t gridBottom = infoH - u(9); // the page dots (radius <= 2.5 u) sit below
        float pitchF = (float)(gridBottom - gridTop) / weeks;
        if (pitchF > u(13))
            pitchF = (float)u(13);
        int16_t pitch = (int16_t)pitchF;

        const uint8_t cf = calFont();
        const int16_t cellW = (int16_t)((W - 2 * u(6)) / 7);
        const uint16_t pastCol = tint(C_SOFT, 0.55f);
        const uint16_t futureCol = mixColor(C_SOFT, 0xFFFF, 0.55f);

        g.setTextDatum(MC_DATUM);
        char buf[4];
        for (int day = 1; day <= dim; day++)
        {
            int idx = off + day - 1;
            int16_t cx = calColX(idx % 7);
            int16_t cy = (int16_t)(gridTop + ((idx / 7) + 0.5f) * pitchF + 0.5f);
            snprintf(buf, sizeof(buf), "%d", day);
            if (day == ti.tm_mday)
            {
                int16_t w = imin((int16_t)(cellW - 1), (int16_t)u(14));
                int16_t h = imax(5, imin((int16_t)(pitch - 2), (int16_t)u(10)));
                g.fillRoundRect(cx - w / 2, cy - h / 2, w, h, imax(1, u(2)), C_AMBER);
                g.setTextColor(C_BG);
            }
            else
                g.setTextColor(day < ti.tm_mday ? pastCol : futureCol);
            g.drawString(buf, cx, cy, cf);
        }
    }

} // namespace disp