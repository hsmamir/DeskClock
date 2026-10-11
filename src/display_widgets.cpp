// display_widgets.cpp - widgets shared by the pages: alarm badge + next-ring text, the big temperature, wind arrow,
// weather view dots and the hourly-bars timing.
#include "display_internal.h"

namespace disp
{

#if ALARM_ICON_ENABLED
    // ---- Alarm badge (clock page, bottom-left): bell icon + the alarm time ----
    // Shown only while the alarm is switched on in the settings. While it rings the
    // bell turns red, flashes and its clapper swings. The setting is cached and
    // re-read once a second (settings_get() takes a mutex + copies the struct).
    bool alarmCfgOn = false;
    static int alarmCfgHour = 0, alarmCfgMin = 0;
    static int alarmCfgDays = 0x7F; // weekday mask, bit0 = Sunday (same as Settings.alarmDays)
    static uint32_t lastAlarmCfgMs = 0;

    void pollAlarmCfg(uint32_t now)
    {
        if (lastAlarmCfgMs != 0 && now - lastAlarmCfgMs < 1000)
            return;
        lastAlarmCfgMs = now ? now : 1;
        Settings cfg = settings_get();
        alarmCfgOn = cfg.alarmEnabled;
        alarmCfgHour = cfg.alarmHour;
        alarmCfgMin = cfg.alarmMinute;
        alarmCfgDays = cfg.alarmDays;
    }

    // ---- Next scheduled ring ----
    // Minutes from `ti` (local time, seconds ignored) until the next ring of an alarm set
    // to hour:minute on the weekdays in `days` (bit0 = Sunday ... bit6 = Saturday, like
    // tm_wday). Also returns how many calendar days ahead that is (0 = today, 1 = tomorrow ...)
    // and its weekday. -1 = no weekday selected. An alarm time equal to or before now counts
    // as already rung today. Plain calendar arithmetic: across a DST change it can be off by an hour.
    static int nextAlarmIn(const struct tm &ti, int hour, int minute, int days, int &dayOffset, int &wday)
    {
        int nowMin = ti.tm_hour * 60 + ti.tm_min;
        int alarmMin = hour * 60 + minute;
        for (int add = 0; add <= 7; add++)
        {
            int wd = (ti.tm_wday + add) % 7;
            if (!((days >> wd) & 1))
                continue;
            if (add == 0 && alarmMin <= nowMin)
                continue; // today's time has passed: look at the following days
            dayOffset = add;
            wday = wd;
            return add * 1440 + alarmMin - nowMin;
        }
        return -1;
    }

    // Seconds until the next ring when that is within PRE_ALARM_S, else -1 (the clock has no sub-minute
    // alarm: it rings at hh:mm:00, nextAlarmIn() counts whole minutes from the start of this minute).
    int alarmSecondsLeft(const struct tm &ti)
    {
        if (!alarmCfgOn)
            return -1;
        int dayOff = 0, wd = 0;
        int mins = nextAlarmIn(ti, alarmCfgHour, alarmCfgMin, alarmCfgDays, dayOff, wd);
        if (mins != 1)
            return -1;
        int secs = 60 - ti.tm_sec;
        return secs <= PRE_ALARM_S ? secs : -1;
    }

    static const char *const NEXT_ALARM_DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // Small bell: dome + body + rim + clapper. (cx, cy) = middle of the body,
    // r = dome radius, swing = clapper offset in px (0 = hanging straight).
    static void drawBell(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, int16_t swing, uint16_t c)
    {
        g.fillCircle(cx, cy - 1, r, c);                          // dome
        g.fillRect(cx - r, cy - 1, 2 * r + 1, r - 1, c);         // body
        g.fillRect(cx - r - 1, cy + r - 2, 2 * r + 3, 2, c);     // flared rim
        g.fillCircle(cx, cy - r - 1, imax(1, r / 3), c);         // top knob
        g.fillCircle(cx + swing, cy + r + 1, imax(1, r / 3), c); // clapper
    }

    void drawAlarmBadge(TFT_eSPI &g, bool ringing, int16_t cy, const struct tm &ti)
    {
        int16_t r = imax(3, u(3.2f));
        int16_t cx = u(9);
        uint16_t bellC = C_AMBER, textC = C_SOFT;
        int16_t swing = 0;
        if (ringing)
        {
            bellC = textC = alarmFlashOn ? C_RED : tint(C_RED, 0.35f);
            swing = alarmFlashOn ? imax(1, r / 2) : -imax(1, r / 2);
        }
        drawBell(g, cx, cy, r, swing, bellC);

        // Left of the page dots: the alarm time, as it always was (short, never reaches the dots).
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", alarmCfgHour, alarmCfgMin);
        const bool big = sc >= 1.5f; // 12 px smooth font on the 128 px display, 18 px on bigger ones
        const uint8_t *sf = big ? ClockSm18 : ClockSm12;
        const int16_t sfCap = big ? CLOCKSM18_CAPH : CLOCKSM12_CAPH;
        smoothText(g, sf, sfCap, buf, cx + r + u(4), cy + 1, L_BASELINE, textC, C_BG);

        // Right of the page dots, right-aligned: when it rings NEXT. Under 24 h it is the time
        // left ("6h12", "42m"), otherwise "Tmrw" or the weekday ("Sat"). Not while it rings.
        // Skipped if it would ever reach the dots (other fonts / screen sizes).
        if (!ringing)
        {
            int dayOff = 0, wd = 0;
            int mins = nextAlarmIn(ti, alarmCfgHour, alarmCfgMin, alarmCfgDays, dayOff, wd);
            char tail[8] = "";
            if (mins >= 0)
            {
                if (mins >= 24 * 60)
                    snprintf(tail, sizeof(tail), "%s", dayOff == 1 ? "Tmrw" : NEXT_ALARM_DAY[wd]);
                else if (mins >= 60)
                    snprintf(tail, sizeof(tail), "%dh%02d", mins / 60, mins % 60);
                else
                    snprintf(tail, sizeof(tail), "%dm", mins);
            }
            int16_t rightX = W - u(5);
            int16_t dotsRight = W / 2 + (int16_t)(((MODE_COUNT - 1) / 2.0f) * u(10)) + imax(2, u(2.5f));
            if (tail[0] && rightX - smoothWidth(g, sf, tail) > dotsRight + u(3))
                smoothText(g, sf, sfCap, tail, rightX, cy + 1, R_BASELINE, tint(C_SOFT, 0.7f), C_BG); // dimmer than the alarm time
        }
    }
#endif

#if !ALARM_ICON_ENABLED
    int alarmSecondsLeft(const struct tm &) { return -1; }
#endif

    // ---- Rolling temperature ----
    // What was on screen the last time the big temperature was drawn. If the reading now differs, the
    // digits that changed roll (see WEATHER_TEMP_ROLL in config.h).
    static bool tempSeen = false;
    static bool tempRollOn = false;
    static int tempTarget = 0, tempFrom = 0;
    static uint32_t tempRollStart = 0; // may lie in the future: the roll waits until the panel has slid in

    // Digits of t right-aligned into slots: slot 0 = units, 1 = tens ...; 10 = minus sign, -1 = empty.
    static void tempSlots(int t, int8_t out[4])
    {
        for (uint8_t i = 0; i < 4; i++)
            out[i] = -1;
        int a = t < 0 ? -t : t;
        if (a > 999)
            a = 999;
        uint8_t k = 0;
        do
        {
            out[k++] = (int8_t)(a % 10);
            a /= 10;
        } while (a > 0 && k < 4);
        if (t < 0 && k < 4)
            out[k++] = 10;
    }

    bool tempRollActive(float tempC, uint32_t now)
    {
#if WEATHER_TEMP_ROLL
        if (!faceCv.ok) // the temperature is drawn on the upper card of the weather page
            return false;
        if (tempSeen && (int)lroundf(tempC) != tempTarget)
            return true; // a roll is about to start
        return tempRollOn && (int32_t)(now - tempRollStart) < (int32_t)WEATHER_TEMP_ROLL_MS;
#else
        (void)tempC;
        (void)now;
        return false;
#endif
    }

    // How many cells the number needs: digits plus the minus sign.
    static uint8_t tempCells(int t)
    {
        int a = t < 0 ? -t : t;
        if (a > 999)
            a = 999;
        uint8_t n = a >= 100 ? 3 : (a >= 10 ? 2 : 1);
        return (uint8_t)(n + (t < 0 ? 1 : 0));
    }

    // Largest dh (at most maxDh) for which the whole readout (digits, ring, "C") still fits into zoneW
    // (a fraction of the page width), so "-12" and "104" shrink instead of running off the card.
    int16_t tempReadoutDh(TFT_eSPI &g, float tempC, float zoneW, int16_t maxDh)
    {
        uint8_t n = tempCells((int)lroundf(tempC));
        int16_t gap = imax(2, u(3));
        int16_t ring = imax(2, u(2.5f));
        int16_t wC = smoothWidth(g, ClockSm26, "C");
        int16_t limit = (int16_t)(W * zoneW);
        int16_t lo = imax(8, u(16));
        for (int16_t dh = maxDh; dh > lo; dh--)
        {
            int16_t dw = (int16_t)(dh * 0.56f);
            int16_t dwT = (int16_t)(dw * 0.85f);
            int16_t total = n * dwT + (n - 1) * gap + gap + ring * 2 + gap + wC;
            if (total <= limit)
                return dh;
        }
        return lo;
    }

    // Big seven-segment temperature with the degree ring and "C", centred in the part of the width
    // between zoneL and zoneR (fractions of W).
    void drawTempReadout(TFT_eSPI &g, float tempC, int16_t dw, int16_t dh, int16_t gap, int16_t dy, uint16_t accent,
                         uint32_t now, uint32_t settleMs, float zoneL, float zoneR)
    {
        int t = (int)lroundf(tempC);
        int from = t; // value the digits roll away from
        float p = 1.0f;
#if WEATHER_TEMP_ROLL
        if (!tempSeen)
        {
            tempSeen = true;
            tempTarget = t;
        }
        else if (t != tempTarget)
        {
            tempFrom = tempTarget;
            tempTarget = t;
            tempRollStart = ((int32_t)(settleMs - now) > 0) ? settleMs : now;
            tempRollOn = faceCv.ok; // rolling needs the sprite
        }
        if (tempRollOn)
        {
            int32_t el = (int32_t)(now - tempRollStart);
            if (el >= (int32_t)WEATHER_TEMP_ROLL_MS)
                tempRollOn = false;
            else
            {
                from = tempFrom;
                p = el > 0 ? el / (float)WEATHER_TEMP_ROLL_MS : 0.0f;
            }
        }
#else
        (void)now;
        (void)settleMs;
#endif
        int8_t cur[4], old[4];
        tempSlots(t, cur);
        tempSlots(from, old);
        uint8_t n = 0; // digits (and minus sign) of the new value; the layout follows it
        for (uint8_t i = 0; i < 4; i++)
            if (cur[i] >= 0)
                n++;

        int16_t dwT = (int16_t)(dw * 0.85f);
        int16_t dhT = (int16_t)(dh * 0.95f);
        int16_t numW = n * dwT + (n - 1) * gap;
        int16_t wC = smoothWidth(g, ClockSm26, "C");
        int16_t ring = imax(2, u(2.5f));
        int16_t total = numW + gap + ring * 2 + gap + wC;
        int16_t zl = (int16_t)(W * zoneL), zw = (int16_t)(W * (zoneR - zoneL));
        int16_t x0 = zl + (zw - total) / 2;
        if (x0 < zl - (int16_t)(W * 0.10f))
            x0 = zl - (int16_t)(W * 0.10f);
        int16_t y0 = dy + (dh - dhT) / 2;

        // right to left, so the units always roll against the units. A slot that only the OLD value
        // has (21 -> 9) lies left of the new number and is rolled out there.
        int8_t dir = t >= from ? 1 : -1;
        for (uint8_t k = 0; k < 4; k++)
        {
            int16_t x = x0 + (int16_t)(n - 1 - k) * (dwT + gap);
            if (cur[k] == old[k])
            {
                if (cur[k] >= 0)
                    drawSegDigit(g, x, y0, dwT, dhT, cur[k], accent, C_GHOST);
            }
            else
                drawRollDigit(g, x, y0, dwT, dhT, old[k], cur[k], p, dir, accent);
        }

        int16_t rx = x0 + numW + gap + ring;
        g.drawCircle(rx, y0 + ring, ring, accent);
        g.drawCircle(rx, y0 + ring, imax(1, ring - 1), accent);
        smoothText(g, ClockSm26, CLOCKSM26_CAPH, "C", x0 + numW + gap * 2 + ring * 2, y0 + dhT / 2, L_BASELINE, accent, C_BG);
    }

    // ---- Wind arrow, view dots, bar timing (used by the weather page) ----
    // Wind direction as a small arrow (up = north, clockwise). windDirDeg is the direction the wind
    // comes FROM; WEATHER_WIND_ARROW_DOWNWIND decides whether the arrow shows where it blows TO
    // (weather-map style, default) or where it comes from.
    void drawWindArrow(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, int deg, uint16_t col)
    {
#if WEATHER_WIND_ARROW_DOWNWIND
        deg += 180;
#endif
        float a = (float)(deg % 360) * 0.01745329f;
        float dx = sinf(a), dy = -cosf(a); // unit vector the arrow points along
        float px = -dy, py = dx;           // perpendicular to it
        float rf = (float)r;
        float hl = rf * 0.9f, hw = rf * 0.55f; // arrow-head length / half width
        int16_t tipX = (int16_t)lroundf(cx + dx * rf), tipY = (int16_t)lroundf(cy + dy * rf);
        int16_t tailX = (int16_t)lroundf(cx - dx * rf), tailY = (int16_t)lroundf(cy - dy * rf);
        float bx = cx + dx * (rf - hl), by = cy + dy * (rf - hl); // where the head starts
        int16_t baseX = (int16_t)lroundf(bx), baseY = (int16_t)lroundf(by);
        g.drawLine(tailX, tailY, baseX, baseY, col);
        if (sc >= 1.4f) // thicker shaft on bigger screens
            g.drawLine(tailX + (int16_t)lroundf(px), tailY + (int16_t)lroundf(py),
                       baseX + (int16_t)lroundf(px), baseY + (int16_t)lroundf(py), col);
        g.fillTriangle(tipX, tipY,
                       (int16_t)lroundf(bx + px * hw), (int16_t)lroundf(by + py * hw),
                       (int16_t)lroundf(bx - px * hw), (int16_t)lroundf(by - py * hw), col);
    }

    // Two small rings right of the page dots: which of the weather page's two views is showing
    // (filled = current). Rings, so they don't read as two more pages.
    void drawViewDots(TFT_eSPI &g, uint8_t active, uint16_t accent, int16_t y)
    {
        int16_t dotsRight = W / 2 + (int16_t)(((MODE_COUNT - 1) / 2.0f) * u(10)) + imax(2, u(2.5f));
        int16_t r = imax(2, u(2));
        int16_t x0 = dotsRight + u(16);
        for (uint8_t i = 0; i < 2; i++)
        {
            int16_t x = x0 + (int16_t)i * u(8);
            if (i == active)
                g.fillCircle(x, y, r, accent);
            else
                g.drawCircle(x, y, r, tint(accent, 0.5f));
        }
    }

    // How long the hourly bars take to grow in: the last bar starts (n-1) staggers late.
    uint32_t barsGrowTotalMs(uint8_t barCount)
    {
#if WEATHER_BARS_GROW
        return (uint32_t)WEATHER_BARS_GROW_MS + (barCount > 1 ? (uint32_t)(barCount - 1) * WEATHER_BARS_STAGGER_MS : 0u);
#else
        (void)barCount;
        return 0;
#endif
    }

} // namespace disp