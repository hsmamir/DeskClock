// display_page_side.cpp - the Date and Weather pages of the landscape UI (160 x 128), drawn in the look of the design
// sheet: navy background, white neon cores with a soft glow, lilac captions, pink accent, the animated weather icons.
//
// They are the two "side pages" of the knob cycle  Face -> Clock -> Date -> Weather -> Face  (touch.cpp). Like the clock
// styles they draw the whole screen into the one big canvas (faceCv); display_tick() pushes it.
//
//   Date     left: weekday, big day number, month, year     right: month calendar (today = pink pill)
//   Weather  icon + big temperature + condition; feels like / humidity / wind; the next hours with their rain chance
//            The Weather page has up to three VIEWS, the knob steps through them (touch.cpp, g_state.weatherScrollStep):
//            0 = Now (above), 1 = Next days (7-day forecast), 2 = Air quality (AQI, PM2.5, PM10, pollen, UV, pressure).
//            A view without data is skipped; small dots at the right edge show which one is up.
//
// Fonts: PageSm12 / PageSm18 / PageTemp36 (display_fonts_pages.h, made by tools/make_vlw_pages.py), plus the big digits
// and the capital month names of display_fonts.h.
#include "display_internal.h"
#include "display_style.h"
#include "display_landscape.h"
#include "display_wxicon.h"
#include "display_fonts_pages.h"
#include "weather_mood.h"
#include "weather_extras.h"
#include <WiFi.h>
#include <string.h>

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
        // Layout (160 x 128), every block centred on the screen:
        //   y  9   top strip:  condition (left)                       today's range ^hi  vlo (right)
        //   y 40   main row:   big animated icon | big temperature (glow)
        //   y 73   chips:      feels like | humidity | wind   (value + caption), thin dividers between them
        //   y 95   divider
        //   y 99.. next five hours: hour, temperature, rain bar ("Now" sits on a faint pill)
        // Without the details (no daily range / hourly data yet) the main row moves to the middle of the screen and the
        // condition goes under the temperature in the bigger font.
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
                drawWxIcon(g, 3, day, W / 2, u(44), u(24), now); // a plain cloud
                smoothText(g, PageSm18, PAGESM18_CAPH, "No weather yet", W / 2, u(86), C_BASELINE, lav, C_BG);
                const char *sub = WiFi.status() == WL_CONNECTED ? "Updating..." : "Waiting for WiFi";
                smoothText(g, capFont(), capFontH(), sub, W / 2, u(104), C_BASELINE, C_SOFT, C_BG);
                return;
            }

            const uint32_t age = ageSince(now, upd);
            const bool stale = age > (uint32_t)WEATHER_STALE_MS;
            WeatherDetail d;
            readWeatherDetail(d);
            const bool full = d.valid;
            const uint16_t coreCol = stale ? tint(white, 0.6f) : white;
            const uint16_t condCol = stale ? tint(lav, 0.6f) : lav;
            const char *cl = condLabel(code, day);

            char tb[12];
            snprintf(tb, sizeof(tb), "%d\xC2\xB0", (int)lroundf(tC));

            // ---- main row: icon + glowing temperature ----
            // The block (icon | gap | temperature) is centred horizontally on the screen.
            {
                const int16_t is = u(20);                          // icon half width
                const int16_t tw = smoothWidth(g, PageTemp36, tb); // temperature width
                const int16_t gap = u(9);
                const int16_t total = 2 * is + gap + tw;
                const int16_t x0 = (W - total) / 2;
                const int16_t rowCy = full ? u(40) : u(50);
                drawWxIcon(g, code, day, x0 + is, rowCy + u(1), is, now);
                const int16_t tx = x0 + 2 * is + gap;
                const int16_t tCy = full ? rowCy - u(1) : rowCy - u(8);
                glowText(g, PageTemp36, PAGETEMP36_CAPH, tb, tx, tCy, L_BASELINE, accent, coreCol);
                if (!full) // the condition under the temperature
                {
                    const int16_t room = W - tx - u(4);
                    const bool fits = smoothWidth(g, PageSm18, cl) <= room;
                    smoothText(g, fits ? PageSm18 : capFont(), fits ? PAGESM18_CAPH : capFontH(), cl, tx, tCy + u(24), L_BASELINE, condCol, C_BG);
                }
            }

            // ---- top strip: condition (or how old the reading is, once it is stale) | range ----
            if (full)
            {
                const int16_t cy = u(9), mx = u(6);
                char hi[8], lo[8];
                snprintf(hi, sizeof(hi), "%d\xC2\xB0", (int)lroundf(d.maxC));
                snprintf(lo, sizeof(lo), "%d\xC2\xB0", (int)lroundf(d.minC));
                int16_t xr = W - mx;
                smoothText(g, capFont(), capFontH(), lo, xr, cy, R_BASELINE, lav, C_BG);
                int16_t x = xr - smoothWidth(g, capFont(), lo) - u(5);
                g.fillTriangle(x, cy + u(3), x - u(3), cy - u(2), x + u(3), cy - u(2), C_ICE);
                x -= u(8);
                smoothText(g, capFont(), capFontH(), hi, x, cy, R_BASELINE, lav, C_BG);
                x -= smoothWidth(g, capFont(), hi) + u(5);
                g.fillTriangle(x, cy - u(3), x - u(3), cy + u(2), x + u(3), cy + u(2), C_HI);
                const int16_t rangeL = x - u(4); // left edge of the range block

                char ab[24];
                const char *left = cl;
                if (stale)
                {
                    char ag[8];
                    fmtAge(ag, sizeof(ag), age);
                    snprintf(ab, sizeof(ab), "%s ago", ag);
                    left = ab;
                }
                const bool fits = mx + smoothWidth(g, capFont(), left) <= rangeL;
                if (fits)
                    smoothText(g, capFont(), capFontH(), left, mx, cy, L_BASELINE, stale ? C_AMBER : condCol, C_BG);
            }
            else if (stale)
            {
                char ab[16], ag[8];
                fmtAge(ag, sizeof(ag), age);
                snprintf(ab, sizeof(ab), "%s ago", ag);
                smoothText(g, capFont(), capFontH(), ab, W / 2, u(9), C_BASELINE, C_AMBER, C_BG);
            }

            if (!full)
                return;

            // ---- chips: feels like, humidity, wind ----
            {
                const int16_t vCy = u(72), cCy = u(85);
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
                    chip(g, W * 5 / 6, vCy, cCy, Icon::WIND, v, nullptr, "km/h", d.windDirDeg, white, lav, lav);
                }
            }

            // ---- the next hours: hour, temperature, rain chance ----
            if (d.barCount > 0)
            {
                drawDivider(g, u(8), W - u(8), u(96));
                const int n = d.barCount < 5 ? d.barCount : 5;
                const int16_t colW = W / 5;
                { // "Now" sits on a faint pill
                    g.fillRoundRect(u(3), u(99), colW - u(4), u(27), u(6), tint(accent, 0.22f));
                }
                for (int i = 0; i < n; i++)
                {
                    const int16_t cx = (int16_t)((i * 2 + 1) * W / (2 * 5));
                    char hb[6], tb2[8];
                    if (i == 0)
                        strlcpy(hb, "Now", sizeof(hb));
                    else
                        snprintf(hb, sizeof(hb), "%d", d.hour[i]);
                    snprintf(tb2, sizeof(tb2), "%d\xC2\xB0", (int)d.tempC[i]);
                    smoothText(g, capFont(), capFontH(), hb, cx, u(106), C_BASELINE, i == 0 ? C_WHITE : C_SOFT, i == 0 ? tint(accent, 0.22f) : C_BG);
                    smoothText(g, capFont(), capFontH(), tb2, cx, u(114), C_BASELINE, mixColor(tempColor((float)d.tempC[i]), C_WHITE, 0.45f),
                               i == 0 ? tint(accent, 0.22f) : C_BG);
                    if (d.rainPct[i] >= 0)
                    {
                        const int16_t bw = u(20), bh = imax(2, u(3));
                        drawNeonBar(g, cx - bw / 2, u(120), bw, bh, d.rainPct[i] / 100.0f, C_ICE, false);
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

        // The Date page: the top strip is "what day is it" (big day number, weekday, month and year, ISO week), the rest
        // of the screen is the month calendar over the whole width, so the day numbers have room (21 px per column).
        // Today is a pink pill with a glow; days before it are dim, days after it bright, and the empty cells at the
        // start / end of the grid show the neighbouring months very faintly.
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

            char buf[32];
            const int16_t mx = u(8); // left / right margin of the top strip

            // ---- top strip: the day number (big, glowing) | weekday over month + year | week chip ----
            snprintf(buf, sizeof(buf), "%d", ti.tm_mday);
            glowText(g, PageTemp36, PAGETEMP36_CAPH, buf, mx, u(21), L_BASELINE, accent, white);
            const int16_t xT = mx + smoothWidth(g, PageTemp36, buf) + u(9); // the text column right of the number

            // week chip (ISO week), top right: a thin rounded outline with "W43" inside
            int16_t chipX = W; // left edge of the chip; W = there is none
            {
                char wk[8];
                strftime(wk, sizeof(wk), "%V", &ti);
                snprintf(buf, sizeof(buf), "W%d", atoi(wk));
                const int16_t tw = smoothWidth(g, capFont(), buf), cw = tw + u(9), ch = u(13);
                chipX = W - mx - cw;
                drawFrame(g, chipX, u(15) - ch / 2, cw, ch, ch / 2, tint(C_SOFT, 0.55f));
                smoothText(g, capFont(), capFontH(), buf, chipX + cw / 2, u(15), C_BASELINE, lav, C_BG);
            }

            strftime(buf, sizeof(buf), "%A", &ti); // weekday: the big font if it fits beside the chip
            {
                const bool fits = xT + smoothWidth(g, PageSm18, buf) + u(6) <= chipX;
                smoothText(g, fits ? PageSm18 : capFont(), fits ? PAGESM18_CAPH : capFontH(), buf, xT, u(15), L_BASELINE, white, C_BG);
            }
            strftime(buf, sizeof(buf), "%B %Y", &ti); // "September 2026" if it fits, else "Sep 2026"
            if (xT + smoothWidth(g, capFont(), buf) > W - mx)
                strftime(buf, sizeof(buf), "%b %Y", &ti);
            smoothText(g, capFont(), capFontH(), buf, xT, u(31), L_BASELINE, lav, C_BG);

            drawDivider(g, mx, W - mx, u(43));

            // ---- the month calendar, full width ----
            static const char *WD2[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"}; // index = tm_wday
            const int16_t sideM = u(6);
            const float colW = (W - 2 * sideM) / 7.0f;
            auto colCx = [&](int c)
            { return (int16_t)(sideM + (c + 0.5f) * colW); };

            const int first = (((ti.tm_wday - (ti.tm_mday - 1)) % 7) + 7) % 7; // weekday of the 1st, 0 = Sunday
            const int off = (first - CALENDAR_WEEK_START + 7) % 7;             // cells before the 1st
            const int dim = daysInMonth(ti.tm_year + 1900, ti.tm_mon);
            const int prevDim = daysInMonth(ti.tm_mon == 0 ? ti.tm_year + 1899 : ti.tm_year + 1900, (ti.tm_mon + 11) % 12);
            const int weeks = (off + dim + 6) / 7;

            const int16_t hdrCy = u(51), y0 = u(59), bottom = H - u(4);
            const int16_t pitch = imin(u(14), (int16_t)((bottom - y0) / weeks)); // 6-week months get a little tighter

            for (int c = 0; c < 7; c++)
            {
                const int wd = (CALENDAR_WEEK_START + c) % 7;
                smoothText(g, capFont(), capFontH(), WD2[wd], colCx(c), hdrCy, C_BASELINE, wd == ti.tm_wday ? C_BLUSH : tint(C_SOFT, 0.85f), C_BG);
            }

            const uint16_t pastCol = tint(C_SOFT, 0.6f);
            const uint16_t futureCol = mixColor(C_SOFT, C_WHITE, 0.6f);
            const uint16_t otherCol = tint(C_SOFT, 0.28f); // the neighbouring months
            for (int idx = 0; idx < weeks * 7; idx++)
            {
                const int16_t cx = colCx(idx % 7);
                const int16_t cy = (int16_t)(y0 + (idx / 7) * pitch + pitch / 2);
                const int day = idx - off + 1; // < 1: previous month, > dim: next month
                int shown = day;
                uint16_t col;
                if (day < 1)
                {
                    shown = prevDim + day;
                    col = otherCol;
                }
                else if (day > dim)
                {
                    shown = day - dim;
                    col = otherCol;
                }
                else
                    col = day < ti.tm_mday ? pastCol : futureCol;
                snprintf(buf, sizeof(buf), "%d", shown);
                if (day == ti.tm_mday) // today: a pink pill with a soft glow
                {
                    const int16_t pw = imax(u(16), (int16_t)(smoothWidth(g, capFont(), buf) + u(6)));
                    const int16_t ph = imin(u(12), (int16_t)(pitch - 1));
                    g.fillRoundRect(cx - pw / 2 - 2, cy - ph / 2 - 2, pw + 4, ph + 4, ph / 2 + 2, tint(C_BLUSH, 0.28f));
                    g.fillRoundRect(cx - pw / 2, cy - ph / 2, pw, ph, ph / 2, C_BLUSH);
                    smoothText(g, capFont(), capFontH(), buf, cx, cy, C_BASELINE, C_WHITE, C_BLUSH);
                }
                else
                    smoothText(g, capFont(), capFontH(), buf, cx, cy, C_BASELINE, col, C_BG);
            }
        }

        // ------------------------------------------------------------------ Weather views 1 and 2: Next days / Air quality
        uint16_t aqiCol(int aqi)
        {
            if (aqi < 0)
                return C_SOFT;
            if (aqi <= 50)
                return rgb(90, 230, 130);
            if (aqi <= 100)
                return C_AMBER;
            if (aqi <= 150)
                return rgb(255, 140, 60);
            if (aqi <= 200)
                return C_RED;
            if (aqi <= 300)
                return rgb(200, 100, 255);
            return rgb(170, 50, 90);
        }

        const char *aqiWordL(int aqi)
        {
            if (aqi < 0)
                return "--";
            if (aqi <= 50)
                return "Good";
            if (aqi <= 100)
                return "Moderate";
            if (aqi <= 150)
                return "Sensitive";
            if (aqi <= 200)
                return "Unhealthy";
            if (aqi <= 300)
                return "V.Unhealthy";
            return "Hazardous";
        }

        // PM2.5 / PM10 in ug/m3, coloured with the US breakpoints
        uint16_t pmCol(float v, float b1, float b2, float b3)
        {
            if (v < 0)
                return C_SOFT;
            if (v <= b1)
                return rgb(90, 230, 130);
            if (v <= b2)
                return C_AMBER;
            if (v <= b3)
                return rgb(255, 140, 60);
            return C_RED;
        }

        const char *pollenWordL(int level)
        {
            static const char *const w[5] = {"None", "Low", "Moderate", "High", "V.High"};
            return (level >= 0 && level <= 4) ? w[level] : "n/a";
        }

        uint16_t pollenCol(int level)
        {
            switch (level)
            {
            case 1:
                return rgb(90, 230, 130);
            case 2:
                return C_AMBER;
            case 3:
                return rgb(255, 140, 60);
            case 4:
                return C_RED;
            default:
                return C_SOFT;
            }
        }

        // Title strip of the two extra views: name on the left, a hint on the right, a rule under it.
        void viewTitle(TFT_eSPI &g, const char *left, const char *right, uint16_t lav)
        {
            const int16_t cy = u(9), mx = u(6);
            smoothText(g, capFont(), capFontH(), left, mx, cy, L_BASELINE, lav, C_BG);
            if (right)
                smoothText(g, capFont(), capFontH(), right, W - u(12), cy, R_BASELINE, C_SOFT, C_BG);
            drawDivider(g, mx, W - mx, u(16));
        }

        // Which views of the Weather page have something to show (0 = Now always).
        struct WxViews
        {
            bool avail[3];
        };

        // "Next days": one row per day - weekday, icon, chance of rain, high / low.
        void drawDaysPage(TFT_eSPI &g, const WeatherExtra &x)
        {
            static const char *const kDow[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            g.fillRect(0, 0, W, H, C_BG);
            const uint16_t accent = C_ACCENT;
            const uint16_t white = neonCore(accent, 0.9f);
            const uint16_t lav = neonCore(accent, 0.6f);
            viewTitle(g, "Next days", "rain %", lav);

            const int n = x.dayCount < WX_MAX_DAYS ? x.dayCount : WX_MAX_DAYS;
            if (n <= 0)
            {
                smoothText(g, PageSm18, PAGESM18_CAPH, "No forecast", W / 2, H / 2, C_BASELINE, lav, C_BG);
                return;
            }
            const int16_t top = u(19), bottom = H - u(3);
            const int16_t pitch = imin(u(15), (int16_t)((bottom - top) / n));
            const int16_t xr = W - u(12); // right edge of the low temperature (the view dots sit right of it)
            for (int i = 0; i < n; i++)
            {
                const WxDay &d = x.day[i];
                const int16_t cy = (int16_t)(top + i * pitch + pitch / 2);
                const uint16_t rowBg = (i & 1) ? tint(C_SOFT, 0.07f) : C_BG; // every second row a faint stripe
                if (i & 1)
                    g.fillRect(u(2), cy - pitch / 2, xr + u(3) - u(2), pitch, rowBg);

                const char *name = (i == 0) ? "Today" : kDow[d.dow % 7];
                smoothText(g, PageSm18, PAGESM18_CAPH, name, u(6), cy, L_BASELINE, i == 0 ? white : lav, rowBg);

                drawWxIcon(g, d.code, true, (float)u(52), (float)cy, (float)u(6.5f), 0); // still: now = 0

                char b[10];
                if (d.rainPct >= 0)
                {
                    snprintf(b, sizeof(b), "%d%%", d.rainPct);
                    smoothText(g, capFont(), capFontH(), b, u(72), cy, L_BASELINE,
                               d.rainPct >= WEATHER_BAR_RAIN_PCT ? C_ICE : tint(C_SOFT, 0.85f), rowBg);
                }
                snprintf(b, sizeof(b), "%d", (int)d.minC);
                smoothText(g, PageSm18, PAGESM18_CAPH, b, xr, cy, R_BASELINE, lav, rowBg);
                snprintf(b, sizeof(b), "%d", (int)d.maxC);
                smoothText(g, PageSm18, PAGESM18_CAPH, b, xr - u(28), cy, R_BASELINE, mixColor(tempColor((float)d.maxC), C_WHITE, 0.45f), rowBg);
            }
        }

        // "Air quality": big US AQI with its word, PM2.5 / PM10, a gauge, pollen, and UV + pressure of the day.
        void drawAirPage(TFT_eSPI &g, const WeatherExtra &x, const WeatherDetail &wd, uint32_t now)
        {
            static const char *const kPollenName[6] = {"Alder", "Birch", "Grass", "Mugwort", "Olive", "Ragweed"};
            g.fillRect(0, 0, W, H, C_BG);
            const uint16_t accent = C_ACCENT;
            const uint16_t lav = neonCore(accent, 0.6f);
            viewTitle(g, "Air quality", "US AQI", lav);

            if (!x.aqValid)
            {
                smoothText(g, PageSm18, PAGESM18_CAPH, "No air data", W / 2, H / 2, C_BASELINE, lav, C_BG);
                return;
            }
            const int16_t mx = u(6);
            const int aqi = x.usAqi;
            const uint16_t col = aqiCol(aqi);
            char buf[28];

            // ---- left: the big number and the word for it ----
            if (aqi >= 0)
            {
                snprintf(buf, sizeof(buf), "%d", aqi);
                glowText(g, PageTemp36, PAGETEMP36_CAPH, buf, mx, u(37), L_BASELINE, col, neonCore(col, 0.8f));
                smoothText(g, PageSm18, PAGESM18_CAPH, aqiWordL(aqi), mx, u(60), L_BASELINE, col, C_BG);
            }
            else
                smoothText(g, PageSm18, PAGESM18_CAPH, "--", mx, u(37), L_BASELINE, C_SOFT, C_BG);

            // ---- right: PM2.5 and PM10 ----
            {
                const int16_t lx = u(92), vx = W - u(12);
                smoothText(g, capFont(), capFontH(), "PM2.5", lx, u(30), L_BASELINE, C_SOFT, C_BG);
                smoothText(g, capFont(), capFontH(), "PM10", lx, u(47), L_BASELINE, C_SOFT, C_BG);
                if (x.pm25 >= 0)
                {
                    snprintf(buf, sizeof(buf), "%d", (int)lroundf(x.pm25));
                    smoothText(g, PageSm18, PAGESM18_CAPH, buf, vx, u(30), R_BASELINE, pmCol(x.pm25, 12.0f, 35.4f, 55.4f), C_BG);
                }
                else
                    smoothText(g, PageSm18, PAGESM18_CAPH, "--", vx, u(30), R_BASELINE, C_SOFT, C_BG);
                if (x.pm10 >= 0)
                {
                    snprintf(buf, sizeof(buf), "%d", (int)lroundf(x.pm10));
                    smoothText(g, PageSm18, PAGESM18_CAPH, buf, vx, u(47), R_BASELINE, pmCol(x.pm10, 54.0f, 154.0f, 254.0f), C_BG);
                }
                else
                    smoothText(g, PageSm18, PAGESM18_CAPH, "--", vx, u(47), R_BASELINE, C_SOFT, C_BG);
                smoothText(g, capFont(), capFontH(), "ug/m3", vx, u(61), R_BASELINE, tint(C_SOFT, 0.6f), C_BG);
            }

            // ---- gauge: five bands from 0 to 300 and a marker ----
            {
                const int16_t gx = mx, gw = W - mx - u(14), gy = u(77), gh = imax(3, u(5));
                static const int kBandLo[5] = {0, 50, 100, 150, 200};
                static const int kBandHi[5] = {50, 100, 150, 200, 300};
                static const int kBandRep[5] = {25, 75, 125, 175, 250}; // a value inside each band: its colour
                for (int b = 0; b < 5; b++)
                {
                    const int16_t xa = gx + (int16_t)((long)gw * kBandLo[b] / 300);
                    const int16_t xb = gx + (int16_t)((long)gw * kBandHi[b] / 300);
                    g.fillRect(xa, gy, xb - xa - 1, gh, tint(aqiCol(kBandRep[b]), 0.65f));
                }
                if (aqi >= 0)
                {
                    const int v = aqi > 300 ? 300 : aqi;
                    const int16_t mk = gx + (int16_t)((long)gw * v / 300);
                    g.fillRect(mk - 1, gy - u(1), 2, gh + 2 * u(1), C_WHITE);
                    g.fillTriangle(mk, gy - u(2), mk - u(3), gy - u(6), mk + u(3), gy - u(6), C_WHITE);
                }
            }

            // ---- pollen ----
            smoothText(g, capFont(), capFontH(), "Pollen", mx, u(97), L_BASELINE, C_SOFT, C_BG);
            smoothText(g, PageSm18, PAGESM18_CAPH, pollenWordL(x.pollenLevel), W - u(12), u(97), R_BASELINE, pollenCol(x.pollenLevel), C_BG);
            if (x.pollenLevel >= 1)
            {
                snprintf(buf, sizeof(buf), "%s %d /m3", kPollenName[x.pollenType % 6], (int)lroundf(x.pollenPeak));
                smoothText(g, capFont(), capFontH(), buf, W - u(12), u(110), R_BASELINE, tint(C_SOFT, 0.85f), C_BG);
            }

            // ---- bottom line: how old the reading is | UV of the day, pressure ----
            {
                char ag[8];
                fmtAge(ag, sizeof(ag), ageSince(now, x.aqMs));
                snprintf(buf, sizeof(buf), "%s ago", ag);
                smoothText(g, capFont(), capFontH(), buf, mx, u(121), L_BASELINE, tint(C_SOFT, 0.7f), C_BG);

                char ex[28] = "";
                if (wd.valid && wd.uvMax >= 0)
                    snprintf(ex, sizeof(ex), "UV %d", (int)lroundf(wd.uvMax));
                if (wd.valid && wd.pressureHpa > 0)
                {
                    const size_t l = strlen(ex);
                    snprintf(ex + l, sizeof(ex) - l, "%s%d hPa", l ? "  " : "", wd.pressureHpa);
                }
                if (ex[0])
                    smoothText(g, capFont(), capFontH(), ex, W - u(12), u(121), R_BASELINE, tint(C_SOFT, 0.85f), C_BG);
            }
        }

        // The little dots at the right edge: one per available view, the one that is up is lit.
        void viewDots(TFT_eSPI &g, int cur, const WxViews &v)
        {
            int n = 0;
            for (int i = 0; i < 3; i++)
                n += v.avail[i] ? 1 : 0;
            if (n < 2)
                return;
            const int16_t x = W - u(4), step = u(8);
            int16_t y = (int16_t)(u(52) - (n - 1) * step / 2);
            for (int i = 0; i < 3; i++)
                if (v.avail[i])
                {
                    drawRingDot(g, x, y, i == cur, C_ACCENT);
                    y += step;
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
        {
            // views: 0 = Now, 1 = Next days, 2 = Air quality (the knob picks it, touch.cpp). One without data falls back to Now.
            WxViews wv = {{true, false, false}};
            WeatherExtra x = {};
            if (weather_extra_get(x))
            {
                wv.avail[1] = WX_DAILY_ENABLED && x.forecastValid && x.dayCount > 0;
                wv.avail[2] = WX_AIRQ_ENABLED && x.aqValid;
            }
            int view = g_state.weatherScrollStep;
            if (view < 0 || view > 2 || !wv.avail[view])
                view = 0;
            if (view == 1)
                drawDaysPage(g, x);
            else if (view == 2)
            {
                WeatherDetail wd;
                readWeatherDetail(wd);
                drawAirPage(g, x, wd, now);
            }
            else
                drawWeatherPage(g, day, now);
            viewDots(g, view, wv);
        }
        else
            drawDatePage(g, haveTime, ti);
    }
} // namespace disp