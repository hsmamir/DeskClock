// display_occasion.cpp - yearly occasions with their own face animations:
//   Halloween     bats flying past, a glowing jack-o'-lantern, orange eyes
//   Easter        bunny ears (one twitches now and then), pastel eggs that hop
//   Christmas     falling snow, Santa hat, a string of blinking lights
//   Valentine     hearts drifting up, blushing cheeks, a beating heart, pink eyes
//   New Year's Eve fireworks, confetti, party hat, a 10..1 countdown and the new year at midnight
//   Chaharshanbe Suri  (Iranian, Tuesday evening before the last Wednesday of the Persian year) three bonfires with
//                 sparks, warm fireworks, fiery eyes that hop, the call "zardi-ye man az to, sorkhi-ye to az man"
//   Yalda         (Iranian, 30 Azar sunset -> 1 Dey sunrise) a candle, a watermelon slice, a pomegranate, falling ruby seeds
//   Nowruz        (Iranian, 29 Esfand .. 13 Farvardin) blossom petals, sabzeh, hyacinth, goldfish bowl, green eyes; a
//                 "Nowruz mubarak <year>" greeting with fireworks at midnight of 1 Farvardin; kite + butterfly on Sizdah Bedar
// Everything is procedural (derived from `now`), no per-object state. Two layers: drawOccasionBack()
// runs right after the background (behind the eyes), drawOccasionFront() after the eyes.
// The dates are in occasionUpdate(); occasion_preview() (web page) forces one for a short while.
#include "display_internal.h"
#include "display_vazir.h" // Persian smooth fonts (plaques of the Iranian occasions)
#include "persian_text.h"  // shaping + right-to-left order for them
#include "jalali.h"        // Persian calendar
#include "occasion.h"

// ---- preview, written by the web task, read by the display task ----
static volatile uint8_t s_prevId = 0;
static volatile uint32_t s_prevStartMs = 0;
static volatile uint32_t s_prevUntilMs = 0;

void occasion_preview(uint8_t id, uint32_t durationMs)
{
    if (id == 0 || id >= (uint8_t)Occasion::COUNT)
    {
        s_prevId = 0;
        return;
    }
    uint32_t now = millis();
    s_prevStartMs = now;
    s_prevUntilMs = now + durationMs;
    s_prevId = id;
}

namespace disp
{

    OccState occ = {Occasion::NONE, 0, false, 0, false};

    // ---- date helpers ----
    // Days since 1970-01-01 for a civil date (so two dates can be subtracted).
    static int32_t daysFromCivil(int y, int m, int d)
    {
        y -= m <= 2;
        int era = (y >= 0 ? y : y - 399) / 400;
        int yoe = y - era * 400;
        int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    // Western (Gregorian) Easter Sunday of a year.
    static void easterSunday(int y, int &month, int &day)
    {
        int a = y % 19, b = y / 100, c = y % 100;
        int d = b / 4, e = b % 4, f = (b + 8) / 25;
        int g = (b - f + 1) / 3;
        int h = (19 * a + b - d - g + 15) % 30;
        int i = c / 4, k = c % 4;
        int l = (32 + 2 * e + 2 * i - h - k) % 7;
        int m = (a + 11 * h + 22 * l) / 451;
        month = (h + l - 7 * m + 114) / 31;
        day = ((h + l - 7 * m + 114) % 31) + 1;
    }

    static float rnd01(uint32_t i, uint32_t salt); // defined with the drawing helpers below

    // The Persian year that starts at the coming Nowruz (for the preview of the greeting).
    static uint16_t nextPersianYear(const struct tm *ti)
    {
        if (!ti)
            return 1406;
        int jy, jm, jd;
        jalali::fromGregorian(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday, jy, jm, jd);
        return (uint16_t)(jy + 1);
    }

    // The Iranian occasions, from the Persian calendar. They are checked before every other occasion
    // (Yalda can fall into the Christmas days, Nowruz into Easter): sets occ and returns true when one is on.
    static bool persianOccasion(const struct tm *ti)
    {
        int jy, jm, jd;
        jalali::fromGregorian(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday, jy, jm, jd);

        // Chaharshanbe Suri: the evening of the Tuesday before the last Wednesday of Esfand, until the small hours
        if (jm == 12)
        {
            int lastWed = jalali::lastWednesdayOfEsfand(jy, jd, ti->tm_wday);
            bool eve = (jd == lastWed - 1 && ti->tm_hour >= OCC_CHAHARSHANBE_FROM_HOUR);
            bool after = (jd == lastWed && ti->tm_hour < OCC_CHAHARSHANBE_UNTIL_HOUR);
            if (eve || after)
            {
                occ.kind = Occasion::CHAHARSHANBE_SURI;
                return true;
            }
        }

        // Yalda: the night of 30 Azar -> 1 Dey, from the real sunset to the real sunrise (fixed hours without a location)
        if ((jm == 9 && jd == 30) || (jm == 10 && jd == 1))
        {
            SunTimes st = sun_today(*ti);
            bool sunOk = st.valid && !st.polarNight && !st.midnightSun;
            float nowH = ti->tm_hour + ti->tm_min / 60.0f;
            bool eve = (jm == 9) && nowH >= (sunOk ? st.setH : (float)OCC_YALDA_FROM_HOUR);
            bool after = (jm == 10) && nowH < (sunOk ? st.riseH : (float)OCC_YALDA_UNTIL_HOUR);
            if (eve || after)
            {
                occ.kind = Occasion::YALDA;
                return true;
            }
        }

        // Nowruz: 29 Esfand .. 13 Farvardin
        if ((jm == 12 && jd >= OCC_NOWRUZ_FROM_DAY) || (jm == 1 && jd <= OCC_NOWRUZ_UNTIL_DAY))
        {
            occ.kind = Occasion::NOWRUZ;
            occ.sizdah = (jm == 1 && jd == 13);
            if (jm == 1 && jd == 1 && ti->tm_hour == 0 && ti->tm_min < OCC_NOWRUZ_GREETING_MIN)
            {
                occ.midnight = true;
                occ.year = (uint16_t)jy;
            }
            return true;
        }
        return false;
    }

    void occasionUpdate(const struct tm *ti, uint32_t now)
    {
        occ = {Occasion::NONE, 0, false, 0, false};

        // Preview from the web page
        uint8_t pid = s_prevId;
        if (pid != 0)
        {
            if ((int32_t)(s_prevUntilMs - now) <= 0)
            {
                s_prevId = 0;
            }
            else
            {
                occ.kind = (Occasion)pid;
                if (occ.kind == Occasion::NEW_YEAR_EVE)
                {
                    // 20 s story: 0-7 fireworks, 8-17 countdown 10..1 (1 s each), then the new year
                    uint32_t t = ((now - s_prevStartMs) / 1000) % 28;
                    if (t >= 8 && t < 18)
                        occ.countdown = (int8_t)(10 - (t - 8));
                    else if (t >= 18)
                    {
                        occ.midnight = true;
                        occ.year = ti ? (uint16_t)(ti->tm_year + 1901) : 2027;
                    }
                }
                else if (occ.kind == Occasion::NOWRUZ)
                {
                    // 24 s story: 0-11 the spring look, 12-17 "Nowruz mubarak <year>", 18-23 Sizdah Bedar
                    uint32_t t = ((now - s_prevStartMs) / 1000) % 24;
                    if (t >= 12 && t < 18)
                    {
                        occ.midnight = true;
                        occ.year = nextPersianYear(ti);
                    }
                    else if (t >= 18)
                        occ.sizdah = true;
                }
                return;
            }
        }

        if (!ti || !settings_get().occasions)
            return;

        int y = ti->tm_year + 1900, mon = ti->tm_mon + 1, day = ti->tm_mday;

        // The Iranian occasions go first
        if (persianOccasion(ti))
            return;

        // Halloween: the last days of October
        if (mon == 10 && day >= 31 - OCC_HALLOWEEN_DAYS_BEFORE)
        {
            occ.kind = Occasion::HALLOWEEN;
            return;
        }
        // Valentine's Day: Feb 14 (and the days before it, OCC_VALENTINE_DAYS_BEFORE)
        if (mon == 2 && day >= 14 - OCC_VALENTINE_DAYS_BEFORE && day <= 14)
        {
            occ.kind = Occasion::VALENTINE;
            return;
        }
        // Christmas: from OCC_CHRISTMAS_FROM_DAY to the 26th
        if (mon == 12 && day >= OCC_CHRISTMAS_FROM_DAY && day <= 26)
        {
            occ.kind = Occasion::CHRISTMAS;
            return;
        }
        // Easter: Good Friday .. Easter Monday
        {
            int em, ed;
            easterSunday(y, em, ed);
            int32_t diff = daysFromCivil(y, mon, day) - daysFromCivil(y, em, ed);
            if (diff >= -2 && diff <= 1)
            {
                occ.kind = Occasion::EASTER;
                return;
            }
        }
        // New Year's Eve: from the evening of Dec 31 until the small hours of Jan 1
        bool eve = (mon == 12 && day == 31 && ti->tm_hour >= OCC_NYE_FROM_HOUR);
        bool after = (mon == 1 && day == 1 && ti->tm_hour < OCC_NYE_UNTIL_HOUR);
        if (eve || after)
        {
            occ.kind = Occasion::NEW_YEAR_EVE;
            if (eve && ti->tm_hour == 23 && ti->tm_min == 59 && ti->tm_sec >= 50)
                occ.countdown = (int8_t)(60 - ti->tm_sec); // 10 .. 1
            else if (after && ti->tm_hour == 0 && ti->tm_min == 0 && ti->tm_sec < 30)
            {
                occ.midnight = true;
                occ.year = (uint16_t)y;
            }
        }
    }

    void occasionTintEyes()
    {
        if (occ.kind == Occasion::HALLOWEEN)
        {
            C_EYE_TOP = mixColor(C_EYE_TOP, rgb(255, 150, 25), 0.75f);
            C_EYE_BOT = mixColor(C_EYE_BOT, rgb(225, 70, 0), 0.75f);
        }
        else if (occ.kind == Occasion::VALENTINE)
        {
            C_EYE_TOP = mixColor(C_EYE_TOP, rgb(255, 125, 175), 0.6f);
            C_EYE_BOT = mixColor(C_EYE_BOT, rgb(225, 45, 105), 0.6f);
        }
        else if (occ.kind == Occasion::CHAHARSHANBE_SURI)
        {
            // the eyes catch the fire light: the tint flickers a little
            float k = 0.55f + 0.2f * rnd01(millis() / 110, 61);
            C_EYE_TOP = mixColor(C_EYE_TOP, rgb(255, 175, 45), k);
            C_EYE_BOT = mixColor(C_EYE_BOT, rgb(235, 55, 10), k);
        }
        else if (occ.kind == Occasion::YALDA)
        {
            C_EYE_TOP = mixColor(C_EYE_TOP, rgb(245, 80, 110), 0.6f); // pomegranate red
            C_EYE_BOT = mixColor(C_EYE_BOT, rgb(165, 10, 50), 0.6f);
        }
        else if (occ.kind == Occasion::NOWRUZ)
        {
            C_EYE_TOP = mixColor(C_EYE_TOP, rgb(175, 240, 140), 0.6f); // fresh spring green
            C_EYE_BOT = mixColor(C_EYE_BOT, rgb(35, 165, 105), 0.6f);
        }
    }

    // ---- small helpers ----
    static uint32_t hash32(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }
    // stable pseudo-random 0..1 for (index, salt)
    static float rnd01(uint32_t i, uint32_t salt) { return (hash32(i * 2654435761u + salt * 40503u) & 0xFFFF) / 65535.0f; }

    static uint16_t partyColor(uint32_t i)
    {
        static const uint16_t pal[] = {rgb(255, 80, 90), rgb(255, 210, 60), rgb(90, 220, 140), rgb(90, 160, 255), rgb(220, 120, 255), rgb(255, 150, 60)};
        return pal[i % 6];
    }

    // =====================================================================
    //  Halloween
    // =====================================================================
    // A bat of half-span ~1.7*s; flap -1..1 moves the wing tips up / down.
    static void drawBat(TFT_eSPI &g, int16_t x, int16_t y, int16_t s, float flap, uint16_t c)
    {
        for (int side = -1; side <= 1; side += 2)
        {
            int16_t tipX = x + side * (int16_t)(s * 1.7f);
            int16_t tipY = y - (int16_t)(flap * s * 1.0f) - s / 4;
            g.fillTriangle(x, y - s / 3, tipX, tipY, x + side * (int16_t)(s * 1.0f), y + s / 3, c);
            g.fillTriangle(x, y + s / 4, tipX, tipY, x + side * (int16_t)(s * 0.55f), y + (int16_t)(s * 0.75f), c);
        }
        g.fillEllipse(x, y, imax(2, s / 3), imax(2, s / 2), c);                  // body
        g.fillTriangle(x - s / 3, y - s / 3, x - s / 6, y - s, x, y - s / 3, c); // ears
        g.fillTriangle(x + s / 3, y - s / 3, x + s / 6, y - s, x, y - s / 3, c);
    }

    static void drawJackOLantern(TFT_eSPI &g, uint32_t now, int16_t cx, int16_t cy, int16_t r)
    {
        const uint16_t orange = rgb(245, 120, 20), orangeDk = rgb(205, 85, 10);
        g.fillEllipse(cx - r * 45 / 100, cy, r * 62 / 100, r * 80 / 100, orangeDk);
        g.fillEllipse(cx + r * 45 / 100, cy, r * 62 / 100, r * 80 / 100, orangeDk);
        g.fillEllipse(cx, cy, r * 75 / 100, r * 90 / 100, orange);
        // stem
        g.fillRect(cx - u(1.5f), cy - r * 90 / 100 - u(4), u(3), u(5), rgb(70, 130, 40));
        // the candle flickers: the face colour wobbles
        uint8_t fl = 175 + (uint8_t)(80 * rnd01(now / 110, 7));
        uint16_t glow = dimColor(rgb(255, 225, 70), fl);
        // eyes (triangles) and a jagged mouth
        g.fillTriangle(cx - r * 55 / 100, cy - r * 5 / 100, cx - r * 15 / 100, cy - r * 5 / 100, cx - r * 35 / 100, cy - r * 45 / 100, glow);
        g.fillTriangle(cx + r * 15 / 100, cy - r * 5 / 100, cx + r * 55 / 100, cy - r * 5 / 100, cx + r * 35 / 100, cy - r * 45 / 100, glow);
        g.fillRect(cx - r * 55 / 100, cy + r * 20 / 100, r * 110 / 100, r * 35 / 100, glow);
        g.fillRect(cx - r * 30 / 100, cy + r * 20 / 100, imax(2, r * 18 / 100), r * 16 / 100, orange); // teeth
        g.fillRect(cx + r * 15 / 100, cy + r * 39 / 100, imax(2, r * 18 / 100), r * 16 / 100, orange);
    }

    static void bgHalloween(TFT_eSPI &g, uint32_t now)
    {
        const uint16_t bat = rgb(40, 25, 55);
        for (uint8_t i = 0; i < 3; i++)
        {
            uint32_t period = 9000 + i * 2300;
            float p = fmodf((now + i * 3100u) / (float)period, 1.0f);
            if (i & 1)
                p = 1.0f - p; // every other bat flies the other way
            int16_t x = (int16_t)(-u(12) + p * (W + u(24)));
            int16_t y = u(11) + i * u(13) + (int16_t)(sinf(now / 700.0f + i * 2.0f) * u(4));
            drawBat(g, x, y, u(4.5f), sinf(now / 75.0f + i), bat);
        }
    }

    static void fgHalloween(TFT_eSPI &g, uint32_t now)
    {
        drawJackOLantern(g, now, W - u(15), faceH - u(12), u(10));
    }

    // =====================================================================
    //  Easter
    // =====================================================================
    static void drawEgg(TFT_eSPI &g, int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t base, uint16_t stripe)
    {
        g.fillEllipse(x, y, rx, ry, base);
        g.fillRect(x - rx * 85 / 100, y - 1, rx * 170 / 100, imax(2, ry / 4), stripe);
        g.fillCircle(x - rx / 3, y - ry / 2, imax(1, rx / 5), stripe);
        g.fillCircle(x + rx / 3, y + ry / 2, imax(1, rx / 5), stripe);
    }

    static void fgEaster(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t eyeW, int16_t eyeH, int16_t lcx, int16_t rcx)
    {
        // bunny ears above the eyes; the right one twitches for half a second every 7 s
        int16_t eyeTop = cy - eyeH / 2;
        for (int side = -1; side <= 1; side += 2)
        {
            int16_t ex = (side < 0 ? lcx : rcx) + side * u(3);
            int16_t rx = u(5), ry = u(13);
            if (side > 0 && (now % 7000) < 500)
            {
                float tw = sinf((now % 7000) / 500.0f * (float)M_PI);
                ex += (int16_t)(tw * u(4));
                ry -= (int16_t)(tw * u(3));
            }
            int16_t ecy = eyeTop - u(1) - ry;
            if (ecy - ry < 0) // never above the card
                ry = (eyeTop - u(1)) / 2, ecy = eyeTop - u(1) - ry;
            g.fillEllipse(ex, ecy, rx, ry, rgb(246, 244, 250));
            g.fillEllipse(ex, ecy + u(1), imax(1, rx - u(2)), imax(2, ry - u(3)), rgb(255, 170, 195));
        }
        // eggs in the bottom corners, one hops now and then
        struct Egg
        {
            int x, y;
            uint16_t base, stripe;
            uint16_t phase;
        };
        const Egg eggs[3] = {
            {u(10), faceH - u(9), rgb(255, 150, 190), rgb(255, 255, 255), 0},
            {u(23), faceH - u(7), rgb(120, 190, 255), rgb(255, 225, 100), 1300},
            {W - u(11), faceH - u(8), rgb(255, 225, 100), rgb(190, 120, 255), 2600}};
        for (const Egg &e : eggs)
        {
            uint32_t t = (now + e.phase) % 4000;
            int16_t hop = t < 500 ? (int16_t)(sinf(t / 500.0f * (float)M_PI) * u(5)) : 0;
            drawEgg(g, e.x, e.y - hop, u(4.5f), u(6), e.base, e.stripe);
        }
    }

    // =====================================================================
    //  Christmas
    // =====================================================================
    static void bgSnow(TFT_eSPI &g, uint32_t now)
    {
        for (uint8_t i = 0; i < 20; i++)
        {
            float speed = 0.012f + 0.018f * rnd01(i, 3); // px per ms (at scale 1)
            float yy = fmodf(now * speed * sc + rnd01(i, 4) * (faceH + 8), (float)(faceH + 8)) - 4;
            float xx = rnd01(i, 5) * W + sinf(now / 900.0f + i) * u(3);
            int16_t r = rnd01(i, 6) > 0.6f ? 2 : 1;
            g.fillCircle((int16_t)xx, (int16_t)yy, r, tint(C_WHITE, 0.9f));
        }
    }

    static void fgChristmas(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t eyeW, int16_t eyeH, int16_t lcx)
    {
        // a string of lights along the top edge
        const uint8_t n = 9;
        int16_t px = 0, py = 0;
        for (uint8_t i = 0; i < n; i++)
        {
            int16_t x = (int16_t)((i + 0.5f) * W / n);
            int16_t y = u(3) + ((i & 1) ? u(4) : 0);
            if (i > 0)
                g.drawLine(px, py, x, y, rgb(30, 90, 45));
            px = x;
            py = y;
        }
        for (uint8_t i = 0; i < n; i++)
        {
            int16_t x = (int16_t)((i + 0.5f) * W / n);
            int16_t y = u(3) + ((i & 1) ? u(4) : 0);
            static const uint16_t cols[4] = {rgb(255, 60, 60), rgb(70, 230, 100), rgb(255, 200, 60), rgb(90, 160, 255)};
            bool on = ((now / 450) + i) % 3 != 0;
            uint16_t c = on ? cols[i & 3] : dimColor(cols[i & 3], 90);
            g.fillCircle(x, y + u(2), imax(2, (int16_t)u(2)), c);
        }
        // Santa hat on the left eye
        int16_t eyeTop = cy - eyeH / 2;
        int16_t brimY = eyeTop + u(1);
        int16_t sway = (int16_t)(sinf(now / 900.0f) * u(1.5f));
        int16_t tipX = lcx + eyeW / 2 + u(6) + sway, tipY = brimY - u(15);
        g.fillTriangle(lcx - eyeW / 2 - u(1), brimY, lcx + eyeW / 2 + u(1), brimY, tipX, tipY, rgb(215, 25, 40));
        g.fillTriangle(lcx, brimY, lcx + eyeW / 2 + u(1), brimY, tipX, tipY, rgb(175, 15, 30)); // shade
        g.fillRoundRect(lcx - eyeW / 2 - u(4), brimY - u(2.5f), eyeW + u(8), u(5), u(2.5f), rgb(250, 250, 252));
        g.fillCircle(tipX, tipY, u(3), rgb(250, 250, 252));
    }

    // =====================================================================
    //  New Year's Eve
    // =====================================================================
    // pal: 0 = party colours (New Year's Eve), 1 = fire colours (Chaharshanbe Suri), 2 = spring colours (Nowruz)
    static uint16_t fireworkColor(uint8_t pal, uint32_t i)
    {
        static const uint16_t warm[6] = {rgb(255, 90, 30), rgb(255, 200, 50), rgb(255, 140, 20), rgb(255, 235, 150), rgb(235, 40, 20), rgb(255, 170, 60)};
        static const uint16_t spring[6] = {rgb(255, 150, 190), rgb(120, 230, 140), rgb(255, 215, 80), rgb(255, 255, 255), rgb(120, 220, 230), rgb(190, 150, 255)};
        if (pal == 1)
            return warm[i % 6];
        if (pal == 2)
            return spring[i % 6];
        return partyColor(i);
    }

    static void bgFireworks(TFT_eSPI &g, uint32_t now, bool heavy, uint8_t pal = 0)
    {
        const uint8_t bursts = heavy ? 5 : 3;
        const uint32_t period = heavy ? 1500 : 3100, life = 1300;
        for (uint8_t b = 0; b < bursts; b++)
        {
            uint32_t t = now + b * (period / bursts);
            uint32_t cyc = t / period, age = t % period;
            if (age >= life)
                continue;
            float p = age / (float)life;
            int16_t x = (int16_t)(W * (0.12f + 0.76f * rnd01(cyc * 7 + b, 1)));
            int16_t y = (int16_t)(faceH * (0.14f + 0.36f * rnd01(cyc * 7 + b, 2)));
            uint16_t col = fireworkColor(pal, hash32(cyc * 7 + b));
            if (age < 120)
                g.fillCircle(x, y, u(3), C_WHITE); // the flash
            float r = u(4) + (1.0f - (1.0f - p) * (1.0f - p)) * u(24);
            float grav = p * p * u(7);
            uint16_t c = tint(col, 1.0f - p * 0.85f);
            for (uint8_t k = 0; k < 12; k++)
            {
                float a = k * (2.0f * (float)M_PI / 12.0f) + rnd01(cyc + b, 9) * 0.5f;
                int16_t sx = x + (int16_t)(cosf(a) * r), sy = y + (int16_t)(sinf(a) * r + grav);
                if (p < 0.7f)
                    g.fillRect(sx, sy, 2, 2, c);
                else
                    g.drawPixel(sx, sy, c);
            }
        }
    }

    static void fgNewYear(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t eyeW, int16_t eyeH, int16_t rcx)
    {
        // confetti
        for (uint8_t i = 0; i < 16; i++)
        {
            float speed = 0.02f + 0.02f * rnd01(i, 11);
            float yy = fmodf(now * speed * sc + rnd01(i, 12) * (faceH + 8), (float)(faceH + 8)) - 4;
            float xx = rnd01(i, 13) * W + sinf(now / 500.0f + i * 1.7f) * u(4);
            bool flip = ((now / 160) + i) & 1;
            g.fillRect((int16_t)xx, (int16_t)yy, flip ? 2 : 4, flip ? 4 : 2, partyColor(i));
        }
        // party hat on the right eye
        int16_t eyeTop = cy - eyeH / 2;
        int16_t brimY = eyeTop + u(1);
        int16_t tipX = rcx + u(5), tipY = brimY - u(16);
        g.fillTriangle(rcx - eyeW / 3, brimY, rcx + eyeW / 3, brimY, tipX, tipY, rgb(80, 130, 255));
        g.fillTriangle(rcx - eyeW / 6, brimY - u(5), rcx + eyeW / 6, brimY - u(5), tipX, tipY, rgb(255, 205, 60)); // stripe
        g.fillCircle(tipX, tipY, u(2.5f), rgb(255, 80, 110));
    }

    // 10..1 before midnight, the new year right after: a dark plaque with gold text at the top.
    static void drawBigText(TFT_eSPI &g, uint32_t now, const char *s, uint16_t col)
    {
        int16_t tw = smoothWidth(g, ClockSm18, s);
        int16_t cx = W / 2, cyT = u(14);
        int16_t pw = tw + u(12), ph = u(22);
        const uint16_t plaque = rgb(10, 10, 35);
        g.fillRoundRect(cx - pw / 2, cyT - ph / 2, pw, ph, u(6), plaque);
        g.drawRoundRect(cx - pw / 2, cyT - ph / 2, pw, ph, u(6), (now / 250) & 1 ? C_AMBER : dimColor(C_AMBER, 120));
        smoothText(g, ClockSm18, CLOCKSM18_CAPH, s, cx, cyT, C_BASELINE, col, plaque);
    }

    // =====================================================================
    //  Valentine's Day
    // =====================================================================
    static void bgValentine(TFT_eSPI &g, uint32_t now)
    {
        // small hearts drifting up from the bottom edge, each at its own speed
        for (uint8_t i = 0; i < 9; i++)
        {
            float speed = 0.010f + 0.014f * rnd01(i, 21); // px per ms (at scale 1)
            float span = (float)(faceH + 16);
            float p = fmodf(now * speed * sc + rnd01(i, 22) * span, span) / span; // 0 bottom .. 1 top
            int16_t x = (int16_t)(rnd01(i, 23) * W + sinf(now / 700.0f + i * 1.9f) * u(4));
            int16_t y = (int16_t)(faceH + u(6) - p * span);
            int16_t s = u(2.5f) + (int16_t)(rnd01(i, 24) * u(2.5f));
            uint16_t c = tint(C_HEART, 0.45f + 0.45f * (1.0f - p)); // fades toward the top
            drawHeart(g, x, y - imax(1, s / 2), s, c);
        }
    }

    static void fgValentine(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t eyeW, int16_t eyeH, int16_t lcx, int16_t rcx)
    {
        // blushing cheeks under the eyes
        int16_t chY = cy + eyeH / 2 - u(1);
        drawCheek(g, lcx - eyeW / 2 + u(1), chY, u(6), u(3.5f), 1.0f);
        drawCheek(g, rcx + eyeW / 2 - u(1), chY, u(6), u(3.5f), 1.0f);
        // a big heart in the bottom-right corner that beats twice, then rests
        uint32_t t = now % 1400;
        float beat = 0;
        if (t < 160)
            beat = sinf(t / 160.0f * (float)M_PI);
        else if (t >= 240 && t < 400)
            beat = 0.7f * sinf((t - 240) / 160.0f * (float)M_PI);
        int16_t s = u(7) + (int16_t)(beat * u(2.5f));
        int16_t hx = W - u(14), hy = faceH - u(13);
        drawHeart(g, hx, hy - imax(1, s / 2), s, C_HEART);
        drawHeart(g, hx - s / 3, hy - s / 3 - imax(1, s / 5), imax(1, s / 4), rgb(255, 200, 215)); // shine
    }

    // =====================================================================
    //  Iranian occasions - shared helpers
    // =====================================================================
    // Persian digits (U+06F0..U+06F9, UTF-8 0xDB 0xB0..0xB9) of v.
    static void persianNumber(unsigned v, char *out)
    {
        char d[8];
        int n = 0;
        do
        {
            d[n++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && n < 7);
        int o = 0;
        while (n > 0)
        {
            out[o++] = (char)0xDB;
            out[o++] = (char)(0xB0 + (d[--n] - '0'));
        }
        out[o] = '\0';
    }

    // A dark plaque with Persian text at the top of the card (the counterpart of drawBigText). The text is shaped and put
    // into visual order by fa::visual(), then drawn with the Vazir font: the bigger one when it fits.
    static void drawPersianPlaque(TFT_eSPI &g, uint32_t now, const char *utf8, uint16_t textCol, uint16_t borderCol)
    {
        char vis[112];
        fa::visual(utf8, strlen(utf8), vis, sizeof(vis), fa::DIR_RTL);
        const uint8_t *font = Vazir18;
        int16_t tw = smoothWidth(g, font, vis);
        int16_t ph = u(25);
        if (tw > W - u(14))
        {
            font = Vazir14;
            tw = smoothWidth(g, font, vis);
            ph = u(22);
        }
        int16_t cx = W / 2, cyT = u(14);
        int16_t pw = imin(tw + u(12), (int16_t)(W - u(2)));
        const uint16_t plaque = rgb(10, 10, 35);
        g.fillRoundRect(cx - pw / 2, cyT - ph / 2, pw, ph, u(6), plaque);
        g.drawRoundRect(cx - pw / 2, cyT - ph / 2, pw, ph, u(6), (now / 250) & 1 ? borderCol : dimColor(borderCol, 120));
        g.loadFont(font);
        g.setTextColor(textCol, plaque);
        g.setTextDatum(MC_DATUM);
        g.drawString(vis, cx, cyT);
        g.unloadFont();
    }

    // A flame: a teardrop of height hh with its tip swayed sideways, base centred on x at by.
    static void flameShape(TFT_eSPI &g, int16_t x, int16_t by, int16_t w, int16_t hh, int16_t sway, uint16_t c)
    {
        if (w < 1 || hh < 2)
            return;
        g.fillEllipse(x, by - hh * 28 / 100, w, hh * 30 / 100 + 1, c);
        g.fillTriangle(x - w, by - hh * 28 / 100, x + w, by - hh * 28 / 100, x + sway, by - hh, c);
    }

    // The part of a disc below the line y0 (flat side up), by scanlines.
    static void fillHalfDisc(TFT_eSPI &g, int16_t cx, int16_t y0, int16_t r, uint16_t c)
    {
        for (int16_t dy = 0; dy <= r; dy++)
        {
            int16_t hw = (int16_t)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
            g.drawFastHLine(cx - hw, y0 + dy, hw * 2 + 1, c);
        }
    }

    // =====================================================================
    //  Chaharshanbe Suri
    // =====================================================================
    // A bonfire: crossed logs, three layers of flame that flicker and sway, sparks rising from it.
    static void drawBonfire(TFT_eSPI &g, uint32_t now, int16_t x, int16_t baseY, int16_t w, int16_t h, uint32_t seed)
    {
        const uint16_t red = rgb(225, 45, 10), orange = rgb(255, 135, 15), yellow = rgb(255, 225, 90);
        int16_t hh = (int16_t)(h * (0.80f + 0.30f * rnd01(now / 70 + seed * 13, 31)));
        int16_t sway = (int16_t)(sinf(now / 130.0f + seed * 1.7f) * w * 0.35f);
        int16_t by = baseY - u(3);
        g.fillCircle(x, by - hh / 3, w + u(3), tint(orange, 0.20f)); // the light of the fire
        flameShape(g, x, by, w, hh, sway, red);
        flameShape(g, x - w * 6 / 10, by, w * 55 / 100, hh * 65 / 100, -sway / 2, red); // side tongues
        flameShape(g, x + w * 6 / 10, by, w * 55 / 100, hh * 55 / 100, sway / 2, red);
        flameShape(g, x, by, w * 7 / 10, hh * 72 / 100, sway * 7 / 10, orange);
        flameShape(g, x, by, w * 4 / 10, hh * 42 / 100, sway * 4 / 10, yellow);
        // logs
        g.fillRoundRect(x - w, baseY - u(3), w * 2, u(3), 1, rgb(95, 55, 25));
        g.fillRect(x - w * 6 / 10, baseY - u(4), w * 12 / 10, u(1.5f), rgb(135, 85, 40));
        // sparks
        for (uint8_t k = 0; k < 5; k++)
        {
            uint32_t life = 900 + k * 170;
            uint32_t age = (now + k * 337 + seed * 211) % life;
            float p = age / (float)life;
            int16_t sx = x + (int16_t)(sinf(now / 260.0f + k * 2.1f + seed) * u(3) * (0.4f + p)) + (int16_t)((rnd01(k + seed * 5, 41) - 0.5f) * w);
            int16_t sy = by - hh * 60 / 100 - (int16_t)(p * u(26));
            if (p < 0.55f)
                g.fillRect(sx, sy, 2, 2, yellow);
            else
                g.drawPixel(sx, sy, tint(orange, 1.0f - (p - 0.55f) * 2.0f));
        }
    }

    // One 12 s round: the eyes hop, "zardi-ye man az to" (my yellow is yours), the eyes hop again, "sorkhi-ye to az man" (your red is mine).
    static const uint32_t CHANT_CYCLE_MS = 12000;

    int16_t occasionEyeDy(uint32_t now, bool alarm)
    {
        if (alarm || occ.kind != Occasion::CHAHARSHANBE_SURI)
            return 0;
        uint32_t t = now % CHANT_CYCLE_MS;
        uint32_t h = t < 500 ? t : (t >= 3500 && t < 4000 ? t - 3500 : 500);
        if (h >= 500)
            return 0;
        return (int16_t)(-sinf(h / 500.0f * (float)M_PI) * u(6));
    }

    static void fgChaharshanbe(TFT_eSPI &g, uint32_t now)
    {
        drawBonfire(g, now, u(12), faceH - u(1), u(4.5f), u(17), 1);
        drawBonfire(g, now, u(28), faceH - u(1), u(3.2f), u(11), 2);
        drawBonfire(g, now, W - u(13), faceH - u(1), u(4.5f), u(15), 3);
        uint32_t t = now % CHANT_CYCLE_MS;
        const uint16_t gold = rgb(255, 220, 110), fire = rgb(255, 120, 30);
        if (t >= 600 && t < 3400)
            drawPersianPlaque(g, now, "زردی من از تو", gold, fire);
        else if (t >= 4100 && t < 6900)
            drawPersianPlaque(g, now, "سرخی تو از من", gold, fire);
    }

    // =====================================================================
    //  Yalda
    // =====================================================================
    // Pomegranate seeds drifting down slowly behind the eyes.
    static void bgYalda(TFT_eSPI &g, uint32_t now)
    {
        for (uint8_t i = 0; i < 9; i++)
        {
            float speed = 0.006f + 0.010f * rnd01(i, 71); // px per ms (at scale 1)
            float yy = fmodf(now * speed * sc + rnd01(i, 72) * (faceH + 8), (float)(faceH + 8)) - 4;
            float xx = rnd01(i, 73) * W + sinf(now / 1100.0f + i * 1.3f) * u(3);
            bool flat = ((now / 380) + i) & 1; // the seed tumbles
            uint16_t c = tint(rgb(215, 25, 60), 0.85f);
            g.fillEllipse((int16_t)xx, (int16_t)yy, flat ? 2 : 1, flat ? 1 : 2, c);
            g.drawPixel((int16_t)xx, (int16_t)yy - 1, tint(rgb(255, 160, 180), 0.8f));
        }
    }

    static void fgYalda(TFT_eSPI &g, uint32_t now)
    {
        // One still life in the bottom-left corner: a candle, then the watermelon slice, and the cut pomegranate
        // leaning on the watermelon (drawn last, so it overlaps the slice's right edge).

        // candle with a small dish, on the far left
        int16_t cx = u(7);
        int16_t bodyBottom = faceH - u(2);
        int16_t bodyTop = bodyBottom - u(9);
        int16_t fh = (int16_t)(u(7) * (0.85f + 0.3f * rnd01(now / 90, 81)));
        int16_t sway = (int16_t)(sinf(now / 170.0f) * u(1));
        g.fillCircle(cx, bodyTop - fh / 3, u(7), tint(C_AMBER, 0.18f));      // glow
        g.fillRect(cx - u(3.5f), bodyBottom, u(7), u(2), rgb(200, 160, 60)); // dish
        g.fillRoundRect(cx - u(2.5f), bodyTop, u(5), u(9), 1, rgb(250, 240, 215));
        g.fillRect(cx - u(2.5f), bodyTop, u(2), u(9), rgb(255, 252, 235)); // light side
        g.drawFastVLine(cx, bodyTop - u(2), u(2), rgb(60, 40, 30));        // wick
        flameShape(g, cx, bodyTop - u(1), u(2.2f), fh, sway, rgb(255, 150, 20));
        flameShape(g, cx, bodyTop - u(1), u(1.2f), fh * 55 / 100, sway / 2, rgb(255, 235, 140));

        // watermelon slice: rind, flesh, seeds
        int16_t wr = u(10), wcx = u(22), wy0 = faceH - u(2) - wr;
        fillHalfDisc(g, wcx, wy0, wr, rgb(40, 140, 60));
        fillHalfDisc(g, wcx, wy0, wr - u(1), rgb(225, 240, 190));
        fillHalfDisc(g, wcx, wy0, wr - u(2.5f), rgb(235, 50, 70));
        static const uint8_t sa[5] = {35, 65, 90, 115, 145}; // seed angles; the middle ones sit closer to the rind
        static const uint8_t sd[5] = {60, 30, 62, 30, 60};   // distance from the centre in % of the radius
        for (uint8_t k = 0; k < 5; k++)
        {
            float a = sa[k] * (float)M_PI / 180.0f;
            int16_t sx = wcx + (int16_t)(cosf(a) * wr * sd[k] / 100.0f);
            int16_t sy = wy0 + (int16_t)(sinf(a) * wr * sd[k] / 100.0f);
            g.fillEllipse(sx, sy, 1, 2, rgb(35, 20, 25));
        }

        // pomegranate cut open, overlapping the right edge of the watermelon: crown, rind, pith, ruby seeds that twinkle
        int16_t pr = u(7), pcx = u(32), pcy = faceH - u(8);
        const uint16_t rindC = rgb(190, 25, 45), rindDk = rgb(140, 15, 35);
        g.fillTriangle(pcx - u(3), pcy - pr + 2, pcx - u(1.5f), pcy - pr - u(2.5f), pcx - u(0.5f), pcy - pr + 2, rindDk);
        g.fillTriangle(pcx - u(1.5f), pcy - pr + 2, pcx, pcy - pr - u(3.5f), pcx + u(1.5f), pcy - pr + 2, rindDk);
        g.fillTriangle(pcx + u(0.5f), pcy - pr + 2, pcx + u(1.5f), pcy - pr - u(2.5f), pcx + u(3), pcy - pr + 2, rindDk);
        g.fillCircle(pcx, pcy, pr, rindC);
        g.fillCircle(pcx, pcy, pr - u(1.5f), rgb(255, 232, 190)); // pith
        int16_t sr = imax(2, (int16_t)((pr + 2) / 4));
        int16_t ring = pr * 55 / 100;
        uint8_t glint = (now / 250) % 14; // 0..6 = one seed flashes, 7..13 = rest
        for (uint8_t k = 0; k < 7; k++)
        {
            int16_t sx = pcx, sy = pcy;
            if (k > 0)
            {
                float a = (k - 1) * (float)M_PI / 3.0f + 0.5f;
                sx += (int16_t)(cosf(a) * ring);
                sy += (int16_t)(sinf(a) * ring);
            }
            g.fillCircle(sx, sy, sr, glint == k ? rgb(255, 170, 190) : rgb(215, 25, 60));
            g.drawPixel(sx - 1, sy - 1, rgb(255, 150, 170));
        }
    }

    // =====================================================================
    //  Nowruz
    // =====================================================================
    // Blossom petals drifting down and sideways.
    static void bgNowruz(TFT_eSPI &g, uint32_t now)
    {
        for (uint8_t i = 0; i < 14; i++)
        {
            float speed = 0.008f + 0.012f * rnd01(i, 91); // px per ms (at scale 1)
            float yy = fmodf(now * speed * sc + rnd01(i, 92) * (faceH + 8), (float)(faceH + 8)) - 4;
            float xx = rnd01(i, 93) * W + sinf(now / 800.0f + i * 1.6f) * u(5) + yy * 0.15f;
            bool flat = ((now / 260) + i) & 1; // the petal flutters
            uint16_t c = (i % 3 == 0) ? rgb(255, 245, 248) : rgb(255, 185, 210);
            g.fillEllipse((int16_t)xx, (int16_t)yy, flat ? 2 : 1, flat ? 1 : 2, tint(c, 0.9f));
        }
    }

    // Sabzeh (sprouted wheat) in an earthen bowl; the blades sway.
    static void drawSabzeh(TFT_eSPI &g, uint32_t now, int16_t x, int16_t baseY)
    {
        int16_t top = baseY - u(5);
        for (uint8_t i = 0; i < 9; i++)
        {
            int16_t bx = x - u(8) + i * u(2);
            int16_t h = u(6) + (int16_t)(rnd01(i, 51) * u(6));
            int16_t tipX = bx + (int16_t)(sinf(now / 700.0f + i * 0.9f) * u(1.5f)) + (i - 4) * u(0.4f);
            uint16_t c = (i & 1) ? rgb(60, 185, 70) : rgb(115, 220, 90);
            g.drawLine(bx, top, tipX, top - h, c);
            g.drawLine(bx + 1, top, tipX + 1, top - h, c);
        }
        g.fillRoundRect(x - u(10), top, u(20), u(5), u(2), rgb(150, 95, 55));
        g.fillRect(x - u(10), top, u(20), u(1.5f), rgb(190, 130, 80));
    }

    // Hyacinth (sonbol): a spike of purple florets on a short stem with two leaves.
    static void drawHyacinth(TFT_eSPI &g, int16_t x, int16_t baseY)
    {
        const uint16_t leaf = rgb(60, 170, 70);
        g.fillTriangle(x, baseY - u(2), x - u(4), baseY - u(9), x - u(1), baseY - u(2), leaf);
        g.fillTriangle(x, baseY - u(2), x + u(4), baseY - u(8), x + u(1), baseY - u(2), leaf);
        int16_t topY = baseY - u(15);
        g.drawFastVLine(x, topY + u(6), u(9), leaf);
        for (int j = 0; j < 6; j++)
        {
            int16_t yy = topY + j * u(1.6f);
            int n = 1 + j / 2;
            for (int k = 0; k < n; k++)
            {
                int16_t xx = (int16_t)(x + (k - (n - 1) * 0.5f) * u(2.6f));
                g.fillCircle(xx, yy, imax(1, (int16_t)u(1.5f)), ((j + k) & 1) ? rgb(150, 95, 225) : rgb(205, 165, 255));
            }
        }
        g.fillRect(x - u(3), baseY - u(2), u(6), u(2), rgb(150, 95, 55)); // little pot
    }

    // The round goldfish bowl (tang) with a swimming goldfish and rising bubbles.
    static void drawGoldfishBowl(TFT_eSPI &g, uint32_t now, int16_t bx, int16_t by, int16_t r)
    {
        const uint16_t glass = rgb(185, 225, 245), orange = rgb(255, 110, 15);
        g.fillRect(bx - r / 3, by - r - u(2), r * 2 / 3 + 1, u(3), glass); // the neck
        g.fillCircle(bx, by, r, rgb(70, 155, 205));                        // the water
        g.drawCircle(bx, by, r, glass);
        g.drawLine(bx - r * 60 / 100, by - r * 30 / 100, bx - r * 35 / 100, by - r * 60 / 100, rgb(225, 247, 255)); // glint
        float ph = now / 1300.0f;
        int dir = cosf(ph) >= 0 ? 1 : -1;
        int16_t fx = bx + (int16_t)(sinf(ph) * r * 0.35f);
        int16_t fy = by + (int16_t)(sinf(now / 900.0f) * r * 0.15f);
        int16_t rx = u(3.2f), ry = u(2.2f);
        g.fillTriangle(fx - dir * (rx - 1), fy, fx - dir * (rx + u(3)), fy - u(2.3f), fx - dir * (rx + u(3)), fy + u(2.3f), rgb(255, 150, 40)); // tail
        g.fillEllipse(fx, fy, rx, ry, orange);
        g.drawPixel(fx + dir * (rx - u(1.2f)), fy - 1, rgb(20, 20, 30)); // eye
        for (uint8_t k = 0; k < 2; k++)
        {
            float p = fmodf(now / 2400.0f + k * 0.5f, 1.0f);
            g.drawCircle(bx + (k ? u(2) : -u(2)), by + r / 2 - (int16_t)(p * r * 1.2f), 1, rgb(210, 240, 255));
        }
    }

    // Sizdah Bedar (13 Farvardin): a kite on the right ...
    static void drawKite(TFT_eSPI &g, uint32_t now, int16_t kx, int16_t ky)
    {
        kx += (int16_t)(sinf(now / 900.0f) * u(2));
        ky += (int16_t)(cosf(now / 1100.0f) * u(1.5f));
        int16_t hw = u(6), ht = u(7), hb = u(8);
        g.fillTriangle(kx, ky - ht, kx + hw, ky, kx, ky, rgb(235, 50, 60));
        g.fillTriangle(kx + hw, ky, kx, ky + hb, kx, ky, rgb(255, 210, 60));
        g.fillTriangle(kx, ky + hb, kx - hw, ky, kx, ky, rgb(70, 140, 255));
        g.fillTriangle(kx - hw, ky, kx, ky - ht, kx, ky, rgb(70, 200, 100));
        static const uint16_t bows[3] = {rgb(255, 255, 255), rgb(235, 50, 60), rgb(255, 210, 60)};
        int16_t px = kx, py = ky + hb;
        for (uint8_t k = 0; k < 3; k++)
        {
            int16_t tx = kx + (int16_t)(sinf(now / 300.0f + k * 1.4f) * u(2));
            int16_t ty = ky + hb + (k + 1) * u(2.5f);
            g.drawLine(px, py, tx, ty, C_WHITE);
            g.fillRect(tx - 1, ty, 3, 2, bows[k]);
            px = tx;
            py = ty;
        }
    }

    // ... and a butterfly flying about on the left.
    static void drawButterfly(TFT_eSPI &g, uint32_t now, int16_t bx, int16_t by)
    {
        bx += (int16_t)(sinf(now / 1500.0f) * u(5));
        by += (int16_t)(cosf(now / 1000.0f) * u(3));
        float flap = fabsf(sinf(now / 110.0f));
        int16_t rx = imax(1, (int16_t)(u(4.5f) * flap)), ry = u(3.5f);
        g.fillEllipse(bx - rx, by - 1, rx, ry, rgb(255, 150, 40));
        g.fillEllipse(bx + rx, by - 1, rx, ry, rgb(255, 150, 40));
        g.fillEllipse(bx - rx * 7 / 10, by + u(2.5f), imax(1, rx * 7 / 10), imax(1, ry * 6 / 10), rgb(170, 110, 240));
        g.fillEllipse(bx + rx * 7 / 10, by + u(2.5f), imax(1, rx * 7 / 10), imax(1, ry * 6 / 10), rgb(170, 110, 240));
        g.fillRect(bx, by - u(3), 2, u(6), rgb(40, 30, 40)); // body
        g.drawLine(bx, by - u(3), bx - u(2), by - u(5), rgb(40, 30, 40));
        g.drawLine(bx + 1, by - u(3), bx + u(3), by - u(5), rgb(40, 30, 40));
    }

    static void fgNowruz(TFT_eSPI &g, uint32_t now)
    {
        drawSabzeh(g, now, u(14), faceH - u(1));
        if (occ.sizdah)
        {
            drawButterfly(g, now, u(34), faceH - u(12));
            drawKite(g, now, W - u(14), faceH - u(15));
        }
        else
        {
            drawHyacinth(g, u(33), faceH - u(1));
            drawGoldfishBowl(g, now, W - u(13), faceH - u(10), u(8));
        }
        if (occ.midnight)
        {
            char buf[64] = "نوروزت مبارک ";
            persianNumber(occ.year, buf + strlen(buf));
            drawPersianPlaque(g, now, buf, rgb(255, 235, 150), rgb(110, 225, 130));
        }
        else if (occ.sizdah && (now % 16000) < 5000)
            drawPersianPlaque(g, now, "سیزده"
                                      "\xE2\x80\x8C"
                                      "بدر",
                              rgb(190, 255, 170), rgb(110, 225, 130));
    }

    // =====================================================================
    //  Entry points
    // =====================================================================
    void drawOccasionBack(TFT_eSPI &g, uint32_t now)
    {
        switch (occ.kind)
        {
        case Occasion::HALLOWEEN:
            bgHalloween(g, now);
            break;
        case Occasion::CHRISTMAS:
            bgSnow(g, now);
            break;
        case Occasion::NEW_YEAR_EVE:
            bgFireworks(g, now, occ.midnight || occ.countdown > 0);
            break;
        case Occasion::VALENTINE:
            bgValentine(g, now);
            break;
        case Occasion::CHAHARSHANBE_SURI:
            bgFireworks(g, now, ((now / 8000) & 1) != 0, 1); // calm and lively rounds alternate
            break;
        case Occasion::YALDA:
            bgYalda(g, now);
            break;
        case Occasion::NOWRUZ:
            if (occ.midnight)
                bgFireworks(g, now, true, 2);
            bgNowruz(g, now);
            break;
        default:
            break;
        }
    }

    void drawOccasionFront(TFT_eSPI &g, uint32_t now)
    {
        if (occ.kind == Occasion::NONE)
            return;
        int16_t R = W / 8;
        int16_t eyeW = R * 2, eyeH = (int16_t)(R * 2.25f);
        int16_t cy = (int16_t)(faceH * 0.55f);
        int16_t lcx = W / 2 - R * 2, rcx = W / 2 + R * 2;
        switch (occ.kind)
        {
        case Occasion::HALLOWEEN:
            fgHalloween(g, now);
            break;
        case Occasion::EASTER:
            fgEaster(g, now, cy, eyeW, eyeH, lcx, rcx);
            break;
        case Occasion::CHRISTMAS:
            fgChristmas(g, now, cy, eyeW, eyeH, lcx);
            break;
        case Occasion::VALENTINE:
            fgValentine(g, now, cy, eyeW, eyeH, lcx, rcx);
            break;
        case Occasion::CHAHARSHANBE_SURI:
            fgChaharshanbe(g, now);
            break;
        case Occasion::YALDA:
            fgYalda(g, now);
            break;
        case Occasion::NOWRUZ:
            fgNowruz(g, now);
            break;
        case Occasion::NEW_YEAR_EVE:
        {
            fgNewYear(g, now, cy, eyeW, eyeH, rcx);
            char buf[8];
            if (occ.countdown > 0)
            {
                snprintf(buf, sizeof(buf), "%d", occ.countdown);
                drawBigText(g, now, buf, C_AMBER);
            }
            else if (occ.midnight)
            {
                snprintf(buf, sizeof(buf), "%u", (unsigned)occ.year);
                drawBigText(g, now, buf, C_WHITE);
            }
            break;
        }
        default:
            break;
        }
    }

} // namespace disp