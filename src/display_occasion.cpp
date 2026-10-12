// display_occasion.cpp - yearly occasions with their own face animations:
//   Halloween     bats flying past, a glowing jack-o'-lantern, orange eyes
//   Easter        bunny ears (one twitches now and then), pastel eggs that hop
//   Christmas     falling snow, Santa hat, a string of blinking lights
//   New Year's Eve fireworks, confetti, party hat, a 10..1 countdown and the new year at midnight
// Everything is procedural (derived from `now`), no per-object state. Two layers: drawOccasionBack()
// runs right after the background (behind the eyes), drawOccasionFront() after the eyes.
// The dates are in occasionUpdate(); occasion_preview() (web page) forces one for a short while.
#include "display_internal.h"
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

    OccState occ = {Occasion::NONE, 0, false, 0};

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

    void occasionUpdate(const struct tm *ti, uint32_t now)
    {
        occ = {Occasion::NONE, 0, false, 0};

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
                return;
            }
        }

        if (!ti || !settings_get().occasions)
            return;

        int y = ti->tm_year + 1900, mon = ti->tm_mon + 1, day = ti->tm_mday;

        // Halloween: the last days of October
        if (mon == 10 && day >= 31 - OCC_HALLOWEEN_DAYS_BEFORE)
        {
            occ.kind = Occasion::HALLOWEEN;
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
    static void bgFireworks(TFT_eSPI &g, uint32_t now, bool heavy)
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
            uint16_t col = partyColor(hash32(cyc * 7 + b));
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