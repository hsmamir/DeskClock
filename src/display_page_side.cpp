// display_page_side.cpp - the Date and Weather pages of the landscape UI (160 x 128), drawn in the look of the design
// sheet: navy background, white neon cores with a soft glow, lilac captions, pink accent, the animated weather icons.
//
// They are the two "side pages" of the knob cycle  Face -> Clock -> Date -> Weather -> Face  (touch.cpp). Like the clock
// styles they draw the whole screen into the one big canvas (faceCv); display_tick() pushes it.
//
//   Date     left: weekday, big day number, month, year     right: month calendar (today = pink pill)
//   Weather  icon + big temperature + condition; feels like / humidity / wind; the next hours with their rain chance
//
// Fonts: PageSm12 / PageSm18 / PageTemp36 (display_fonts_pages.h, made by tools/make_vlw_pages.py), plus the big digits
// and the capital month names of display_fonts.h.
#include "display_internal.h"
#include "display_style.h"
#include "display_landscape.h"
#include "display_wxicon.h"
#include "display_fonts_pages.h"
#include "weather_mood.h"
#include <WiFi.h>

namespace disp
{
    namespace
    {
        constexpr uint16_t C_HI = rgb(255, 170, 90); // "high" arrow of the day's range

        // ---- text ----
        // A string with a soft glow: four offset copies in a faded accent first, then the bright core on top. The
        // same recipe as the big clock digits (display_clockstyles.cpp), only with fewer passes.
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

        // The small caption font: 12 px on the real display, 18 px on the bigger simulator screens.
        inline const uint8_t *capFont() { return sc >= 1.5f ? PageSm18 : PageSm12; }
        inline int16_t capFontH() { return sc >= 1.5f ? PAGESM18_CAPH : PAGESM12_CAPH; }

        // ---- the little icons of the Weather page (all about 7 x 10 px, centred on cx / cy) ----
        void iconFeels(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c) // thermometer
        {
            g.fillRoundRect(cx - u(1.5f), cy - u(5), imax(2, u(3)), u(8), imax(1, u(1.5f)), c);
            g.fillCircle(cx, cy + u(4), imax(2, u(3)), c);
        }

        void iconDrop(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c) // humidity
        {
            g.fillCircle(cx, cy + u(2), imax(2, u(3.5f)), c);
            g.fillTriangle(cx, cy - u(5), cx - u(3), cy + u(1), cx + u(3), cy + u(1), c);
        }

        // An arrow that points where the wind blows TO (windDirDeg is where it comes FROM); three short bars if unknown.
        void iconWind(TFT_eSPI &g, int16_t cx, int16_t cy, int windFromDeg, uint16_t c)
        {
            if (windFromDeg < 0)
            {
                for (int i = -1; i <= 1; i++)
                    g.fillRoundRect(cx - u(4), cy + i * u(3.5f) - 1, u(8) - (i == 0 ? 0 : u(2)), imax(2, u(1.6f)), 1, c);
                return;
            }
            const float a = (windFromDeg + 180) * 0.0174533f;
            const float dx = sinf(a), dy = -cosf(a); // unit vector of the arrow
            const float len = 5.0f * sc;
            const float tx = cx + dx * len, ty = cy + dy * len; // tip
            const float bx = cx - dx * len, by = cy - dy * len; // tail
            g.drawLine((int16_t)lroundf(bx), (int16_t)lroundf(by), (int16_t)lroundf(tx), (int16_t)lroundf(ty), c);
            g.drawLine((int16_t)lroundf(bx + 1), (int16_t)lroundf(by), (int16_t)lroundf(tx + 1), (int16_t)lroundf(ty), c);
            const float hx = tx - dx * 3.5f * sc, hy = ty - dy * 3.5f * sc; // base of the head
            const float px = -dy * 3.0f * sc, py = dx * 3.0f * sc;          // across the arrow
            g.fillTriangle((int16_t)lroundf(tx), (int16_t)lroundf(ty), (int16_t)lroundf(hx + px), (int16_t)lroundf(hy + py),
                           (int16_t)lroundf(hx - px), (int16_t)lroundf(hy - py), c);
        }

        enum class Icon : uint8_t
        {
            FEELS,
            DROP,
            WIND
        };

        // One info chip: icon + value (+ small unit) on top, a caption under it, everything centred on cx.
        void chip(TFT_eSPI &g, int16_t cx, int16_t valueCy, int16_t capCy, Icon icon, const char *value, const char *unit,
                  const char *caption, int windDeg, uint16_t white, uint16_t lav, uint16_t iconCol)
        {
            const int16_t iw = u(8), gap = u(3);
            const int16_t vw = smoothWidth(g, PageSm18, value);
            const int16_t uw = (unit && unit[0]) ? (int16_t)(u(2) + smoothWidth(g, capFont(), unit)) : 0;
            const int16_t total = iw + gap + vw + uw;
            int16_t x0 = cx - total / 2;
            if (x0 + total > W - u(2)) // never past the screen edge (a 3-digit wind speed is wider than its third)
                x0 = W - u(2) - total;
            if (x0 < u(2))
                x0 = u(2);
            const int16_t ix = x0 + iw / 2;
            switch (icon)
            {
            case Icon::FEELS:
                iconFeels(g, ix, valueCy, iconCol);
                break;
            case Icon::DROP:
                iconDrop(g, ix, valueCy, iconCol);
                break;
            default:
                iconWind(g, ix, valueCy, windDeg, iconCol);
                break;
            }
            smoothText(g, PageSm18, PAGESM18_CAPH, value, x0 + iw + gap, valueCy, L_BASELINE, white, C_BG);
            if (uw)
                smoothText(g, capFont(), capFontH(), unit, x0 + iw + gap + vw + u(2), valueCy, L_BASELINE, lav, C_BG);
            smoothText(g, capFont(), capFontH(), caption, cx, capCy, C_BASELINE, C_SOFT, C_BG);
        }

        // The words under the temperature. (weather_code_to_label() is the short form for the old pages.)
        const char *condLabel(int code, bool day)
        {
            if (code == 0)
                return day ? "Sunny" : "Clear";
            if (code == 1)
                return day ? "Mostly sunny" : "Mostly clear";
            if (code == 2)
                return "Partly cloudy";
            if (code == 3)
                return "Cloudy";
            if (wxIsFog(code))
                return "Foggy";
            if (code >= 51 && code <= 57)
                return "Drizzle";
            if (code >= 61 && code <= 67)
                return "Rainy";
            if (wxIsSnow(code))
                return "Snowy";
            if (code >= 80 && code <= 82)
                return "Showers";
            if (wxIsStorm(code))
                return "Stormy";
            return "Cloudy";
        }

        // ------------------------------------------------------------------ Weather
        void drawWeatherPage(TFT_eSPI &g, bool day, uint32_t now)
        {
            g.fillRect(0, 0, W, H, C_BG);
            const uint16_t accent = C_ACCENT;
            const uint16_t white = neonCore(accent, 0.9f);
            const uint16_t lav = neonCore(accent, 0.6f);

            bool ok = false;
            float tC = 0;
            int code = 0;
            uint32_t upd = 0;
            readWeather(ok, tC, code, upd);

            if (!ok) // no reading yet (boot, no WiFi, new city)
            {
                drawWxIcon(g, 3, day, W / 2, u(42), u(20), now); // a plain cloud
                smoothText(g, PageSm18, PAGESM18_CAPH, "No weather yet", W / 2, u(84), C_BASELINE, lav, C_BG);
                const char *sub = WiFi.status() == WL_CONNECTED ? "Updating..." : "Waiting for WiFi";
                smoothText(g, capFont(), capFontH(), sub, W / 2, u(102), C_BASELINE, C_SOFT, C_BG);
                return;
            }

            const uint32_t age = ageSince(now, upd);
            const bool stale = age > (uint32_t)WEATHER_STALE_MS;
            WeatherDetail d;
            readWeatherDetail(d);
            const bool full = d.valid;
            const int16_t dy = full ? 0 : u(12); // without the details the main row sits lower

            // ---- main row: icon, big temperature, condition ----
            drawWxIcon(g, code, day, u(33), u(34) + dy, u(19), now);

            char tb[12];
            snprintf(tb, sizeof(tb), "%d\xC2\xB0", (int)lroundf(tC));
            glowText(g, PageTemp36, PAGETEMP36_CAPH, tb, u(64), u(30) + dy, L_BASELINE, accent, stale ? tint(white, 0.6f) : white);
            {
                const char *cl = condLabel(code, day);
                const bool fits = smoothWidth(g, PageSm18, cl) <= W - u(64) - u(4); // "Mostly sunny" / "Partly cloudy" do not
                smoothText(g, fits ? PageSm18 : capFont(), fits ? PAGESM18_CAPH : capFontH(), cl, u(64), u(55) + dy, L_BASELINE,
                           stale ? tint(lav, 0.6f) : lav, C_BG);
            }

            // top left: how old the reading is, once it is stale
            if (stale)
            {
                char ab[16], ag[8];
                fmtAge(ag, sizeof(ag), age);
                snprintf(ab, sizeof(ab), "%s ago", ag);
                smoothText(g, capFont(), capFontH(), ab, u(6), u(9), L_BASELINE, C_AMBER, C_BG);
            }

            if (!full)
                return;

            // top right: today's range  ^24  v15
            {
                char hi[8], lo[8];
                snprintf(hi, sizeof(hi), "%d\xC2\xB0", (int)lroundf(d.maxC));
                snprintf(lo, sizeof(lo), "%d\xC2\xB0", (int)lroundf(d.minC));
                const int16_t cy = u(9);
                int16_t xr = W - u(6);
                smoothText(g, capFont(), capFontH(), lo, xr, cy, R_BASELINE, lav, C_BG);
                int16_t x = xr - smoothWidth(g, capFont(), lo) - u(5);
                g.fillTriangle(x, cy + u(3), x - u(3), cy - u(2), x + u(3), cy - u(2), C_ICE);
                x -= u(8);
                smoothText(g, capFont(), capFontH(), hi, x, cy, R_BASELINE, lav, C_BG);
                x -= smoothWidth(g, capFont(), hi) + u(5);
                g.fillTriangle(x, cy - u(3), x - u(3), cy + u(2), x + u(3), cy + u(2), C_HI);
            }

            // ---- chips: feels like, humidity, wind ----
            {
                const int16_t vCy = u(76), cCy = u(90);
                char v[12];
                if (!isnan(d.feelsC))
                {
                    snprintf(v, sizeof(v), "%d\xC2\xB0", (int)lroundf(d.feelsC));
                    chip(g, W / 6, vCy, cCy, Icon::FEELS, v, nullptr, "Feels like", -1, white, lav, C_HI);
                }
                if (d.humidityPct >= 0)
                {
                    snprintf(v, sizeof(v), "%d%%", d.humidityPct);
                    chip(g, W / 2, vCy, cCy, Icon::DROP, v, nullptr, "Humidity", -1, white, lav, C_ICE);
                }
                if (d.windKmh >= 0)
                {
                    snprintf(v, sizeof(v), "%d", (int)lroundf(d.windKmh));
                    chip(g, W * 5 / 6, vCy, cCy, Icon::WIND, v, "km/h", "Wind", d.windDirDeg, white, lav, lav);
                }
            }

            // ---- the next hours: hour, temperature, rain chance ----
            if (d.barCount > 0)
            {
                drawDivider(g, u(8), W - u(8), u(98));
                const int n = d.barCount < 5 ? d.barCount : 5;
                for (int i = 0; i < n; i++)
                {
                    const int16_t cx = (int16_t)((i * 2 + 1) * W / (2 * 5));
                    char hb[6], tb2[8];
                    if (i == 0)
                        strlcpy(hb, "Now", sizeof(hb));
                    else
                        snprintf(hb, sizeof(hb), "%d", d.hour[i]);
                    snprintf(tb2, sizeof(tb2), "%d\xC2\xB0", (int)d.tempC[i]);
                    smoothText(g, capFont(), capFontH(), hb, cx, u(107), C_BASELINE, C_SOFT, C_BG);
                    smoothText(g, capFont(), capFontH(), tb2, cx, u(116), C_BASELINE, mixColor(tempColor((float)d.tempC[i]), C_WHITE, 0.45f), C_BG);
                    if (d.rainPct[i] >= 0)
                    {
                        const int16_t bw = u(20), bh = imax(2, u(3));
                        drawNeonBar(g, cx - bw / 2, u(122), bw, bh, d.rainPct[i] / 100.0f, C_ICE, false);
                    }
                }
            }
        }

        // ------------------------------------------------------------------ Date
        int daysInMonth(int year, int mon) // mon = 0..11
        {
            static const uint8_t dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
                return 29;
            return dm[mon];
        }

        void drawDatePage(TFT_eSPI &g, bool haveTime, const struct tm &ti)
        {
            g.fillRect(0, 0, W, H, C_BG);
            const uint16_t accent = C_ACCENT;
            const uint16_t white = neonCore(accent, 0.9f);
            const uint16_t lav = neonCore(accent, 0.6f);

            if (!haveTime)
            {
                smoothText(g, PageSm18, PAGESM18_CAPH, "Syncing...", W / 2, H / 2, C_BASELINE, tint(accent, 0.7f), C_BG);
                return;
            }

            const bool big = sc >= 1.5f;
            const int16_t colX = u(31); // centre of the left column
            char buf[16];

            // weekday
            strftime(buf, sizeof(buf), "%A", &ti);
            {
                const bool fits = smoothWidth(g, PageSm18, buf) <= u(58);
                smoothText(g, fits ? PageSm18 : capFont(), fits ? PAGESM18_CAPH : capFontH(), buf, colX, u(20), C_BASELINE, lav, C_BG);
            }

            // the day number, big, with a glow
            snprintf(buf, sizeof(buf), "%d", ti.tm_mday);
            glowText(g, big ? ClockBig92 : ClockBig48, big ? CLOCKBIG92_CAPH : CLOCKBIG48_CAPH, buf, colX, u(56), C_BASELINE,
                     accent, white);

            // month in capitals (ClockSm26 only has capitals), year under it
            {
                char mon[8];
                strftime(mon, sizeof(mon), "%b", &ti);
                for (char *p = mon; *p; p++)
                    if (*p >= 'a' && *p <= 'z')
                        *p -= 32;
                smoothText(g, ClockSm26, CLOCKSM26_CAPH, mon, colX, u(92), C_BASELINE, white, C_BG);
                strftime(buf, sizeof(buf), "%Y", &ti);
                smoothText(g, capFont(), capFontH(), buf, colX, u(110), C_BASELINE, C_SOFT, C_BG);
            }

            // thin rule between the two halves
            g.drawFastVLine(u(62), u(14), H - u(28), tint(C_SOFT, 0.25f));

            // ---- month calendar ----
            static const char WD_INITIAL[7] = {'S', 'M', 'T', 'W', 'T', 'F', 'S'}; // Sunday first, like tm_wday
            const int16_t gx0 = u(66), gw = W - u(4) - gx0;
            const float colW = gw / 7.0f;
            const int first = (((ti.tm_wday - (ti.tm_mday - 1)) % 7) + 7) % 7; // weekday of the 1st, 0 = Sunday
            const int off = (first - CALENDAR_WEEK_START + 7) % 7;             // empty cells before the 1st
            const int dim = daysInMonth(ti.tm_year + 1900, ti.tm_mon);
            const int weeks = (off + dim + 6) / 7;
            const int16_t pitch = u(14), hdrH = u(13);
            const int16_t top = (H - (hdrH + weeks * pitch)) / 2;
            auto colCx = [&](int c) { return (int16_t)(gx0 + (c + 0.5f) * colW); };

            for (int c = 0; c < 7; c++)
            {
                const int wd = (CALENDAR_WEEK_START + c) % 7;
                const char t[2] = {WD_INITIAL[wd], 0};
                smoothText(g, capFont(), capFontH(), t, colCx(c), top + hdrH / 2, C_BASELINE,
                           wd == ti.tm_wday ? C_BLUSH : tint(C_SOFT, 0.8f), C_BG);
            }

            const uint16_t pastCol = tint(C_SOFT, 0.55f);
            const uint16_t futureCol = mixColor(C_SOFT, C_WHITE, 0.55f);
            for (int day = 1; day <= dim; day++)
            {
                const int idx = off + day - 1;
                const int16_t cx = colCx(idx % 7);
                const int16_t cy = (int16_t)(top + hdrH + (idx / 7) * pitch + pitch / 2);
                snprintf(buf, sizeof(buf), "%d", day);
                if (day == ti.tm_mday) // today: a pink pill
                {
                    const int16_t pw = imax(u(11), (int16_t)(smoothWidth(g, capFont(), buf) + u(4)));
                    const int16_t ph = u(12);
                    g.fillRoundRect(cx - pw / 2, cy - ph / 2, pw, ph, ph / 2, C_BLUSH);
                    smoothText(g, capFont(), capFontH(), buf, cx, cy, C_BASELINE, C_WHITE, C_BLUSH);
                }
                else
                    smoothText(g, capFont(), capFontH(), buf, cx, cy, C_BASELINE, day < ti.tm_mday ? pastCol : futureCol, C_BG);
            }
        }
    } // namespace

    uint32_t sidePageSig(DisplayMode mode, bool haveTime, const struct tm &ti, uint32_t now)
    {
        if (mode == DisplayMode::WEATHER) // the icon moves: ten frames a second
            return 0xE0000000u | ((now / 100u) & 0x0FFFFFFFu);
        uint32_t s = 0xD0000000u; // Date: changes once a day
        if (haveTime)
            s |= 0x08000000u | ((uint32_t)(ti.tm_year & 0xFF) << 9) | (uint32_t)(ti.tm_yday & 0x1FF);
        return s;
    }

    void drawSidePage(TFT_eSPI &g, DisplayMode mode, bool haveTime, const struct tm &ti, bool day, uint32_t now)
    {
        if (mode == DisplayMode::WEATHER)
            drawWeatherPage(g, day, now);
        else
            drawDatePage(g, haveTime, ti);
    }
} // namespace disp
