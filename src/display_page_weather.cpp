// display_page_weather.cpp - the Weather page (full screen). Two views that alternate every WEATHER_PAGE_SWITCH_MS:
//
//   now view       upper half: icon, big temperature, condition, today's high / low
//                  lower half: feels like, humidity, wind (speed + arrow), chance of rain
//   forecast view  upper half: hourly bars with the temperature above and the chance of rain below
//                  lower half: sunrise, sunset (computed on the chip), UV, pressure
//
// Both halves use canvas coordinates (see display_pages.cpp): the upper half is laid out from its top edge, the
// lower half from its bottom edge (the rows are numbered 0..3 from the top and counted up from the page dots), so
// nothing ever crosses the strip between the two cards.
#include "display_internal.h"

namespace disp
{

    static const uint16_t RAIN_COL = rgb(120, 200, 255);

    static uint8_t lblFont() { return sc >= 1.5f ? 2 : 1; } // small captions: built-in font 1 (6x8) / font 2

    // ---- Rows of the lower half: caption on the left, value on the right ----
    static int16_t rowY(int i) // centre of row i (0 = top .. 3 = bottom) inside the lower card
    {
        return (int16_t)(infoH - u(19) - (3 - i) * u(13));
    }

    static void rowLabel(TFT_eSPI &g, int16_t y, const char *label)
    {
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(label, u(7), y, lblFont());
    }

    // reserveRight: room kept free at the right edge for an icon (the wind arrow)
    static void rowValue(TFT_eSPI &g, int16_t y, const char *txt, uint16_t col, int16_t reserveRight = 0)
    {
        g.setTextDatum(MR_DATUM);
        g.setTextColor(col);
        g.drawString(txt, W - u(7) - reserveRight, y, 2);
    }

    // A temperature with a small degree ring after it, right-aligned at xr.
    static void drawDegValue(TFT_eSPI &g, int16_t xr, int16_t cy, int value, uint16_t col)
    {
        int16_t r = imax(1, u(1.8f));
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", value);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(col);
        g.drawString(buf, xr - (2 * r + u(2)), cy, 2);
        g.drawCircle(xr - r - 1, cy - u(4), r, col);
    }

    // High / low group: a small triangle (up = high, down = low), the number and the ring, centred on cx.
    static void drawHiLo(TFT_eSPI &g, int16_t cx, int16_t y, bool high, float tempC, uint16_t col)
    {
        int16_t tr = imax(2, u(3));
        int16_t r = imax(1, u(1.8f));
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(tempC));
        int16_t tw = g.textWidth(buf, 2);
        int16_t numX = 2 * tr + u(3);
        int16_t total = numX + tw + 2 * r + u(2);
        int16_t x0 = cx - total / 2;
        int16_t xc = x0 + tr;
        if (high)
            g.fillTriangle(xc, y - tr, xc - tr, y + tr - 1, xc + tr, y + tr - 1, col);
        else
            g.fillTriangle(xc, y + tr - 1, xc - tr, y - tr, xc + tr, y - tr, col);
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_WHITE);
        g.drawString(buf, x0 + numX, y, 2);
        g.drawCircle(x0 + numX + tw + r + 1, y - u(4), r, col);
    }

    // =====================================================================
    //  "Now" view
    // =====================================================================
    static void drawNowTop(TFT_eSPI &g, const WeatherPage &p)
    {
        const int16_t rowCy = u(24); // middle of the icon / temperature row

        if (!p.valid)
        {
            drawCloud(g, W / 2, u(34), u(16), tint(C_SOFT, 0.47f));
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No weather yet", W / 2, u(60), 2);
            return;
        }

        drawWeatherIcon(g, p.code, (int16_t)(W * 0.20f), rowCy, u(14), p.now);

        // the big temperature: as large as fits next to the icon ("-12" and "104" are wider than "21")
        int16_t dh = tempReadoutDh(g, p.tempC, 0.57f, u(38));
        int16_t dw = (int16_t)(dh * 0.56f);
        drawTempReadout(g, p.tempC, dw, dh, imax(2, u(3)), rowCy - dh / 2, p.accent, p.now, p.settleMs, 0.40f, 0.98f);

        // condition ("Clear", "Rain" ...), with the age of the reading once it is stale: "Clear 3h ago"
        char cap[24];
        strncpy(cap, weather_code_to_label(p.code), sizeof(cap) - 1);
        cap[sizeof(cap) - 1] = '\0';
        if (p.stale)
        {
            char age[8];
            fmtAge(age, sizeof(age), p.ageMs);
            char full[24];
            snprintf(full, sizeof(full), "%s %s ago", cap, age);
            if (g.textWidth(full, 2) <= W - u(4))
                strcpy(cap, full);
            else
                snprintf(cap, sizeof(cap), "%s %s", weather_code_to_label(p.code), age); // drops "ago" if it would not fit
        }
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_WHITE);
        g.drawString(cap, W / 2, u(53), 2);

        // today's high / low
        if (p.wd.valid)
        {
            drawHiLo(g, (int16_t)(W * 0.28f), u(70), true, p.wd.maxC, tempColor(p.wd.maxC));
            drawHiLo(g, (int16_t)(W * 0.72f), u(70), false, p.wd.minC, tempColor(p.wd.minC));
        }
    }

    static void drawNowBottom(TFT_eSPI &g, const WeatherPage &p)
    {
        if (!p.valid)
            return; // the upper half already says "No weather yet"
        const WeatherDetail &d = p.wd;
        if (!d.valid)
        {
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No details yet", W / 2, infoH / 2 - u(4), 2);
            return;
        }
        char buf[24];

        // feels like
        rowLabel(g, rowY(0), "Feels like");
        if (!isnan(d.feelsC))
            drawDegValue(g, W - u(7), rowY(0), (int)lroundf(d.feelsC), tempColor(d.feelsC));
        else
            rowValue(g, rowY(0), "--", C_SOFT);

        // humidity: a water drop in front of the number
        rowLabel(g, rowY(1), "Humidity");
        if (d.humidityPct >= 0)
        {
            snprintf(buf, sizeof(buf), "%d%%", d.humidityPct);
            int16_t tw = g.textWidth(buf, 2);
            rowValue(g, rowY(1), buf, C_WHITE);
            drawDrop(g, W - u(7) - tw - u(6), rowY(1) + 1, imax(2, u(3)), RAIN_COL);
        }
        else
            rowValue(g, rowY(1), "--", C_SOFT);

        // wind: speed and an arrow for the direction (a direction means nothing in calm air)
        rowLabel(g, rowY(2), "Wind");
        if (d.windKmh >= 0)
        {
            int spd = (int)lroundf(d.windKmh);
            bool arrow = d.windDirDeg >= 0 && spd >= 1;
            int16_t ar = u(5);
            if (spd < 1)
                strcpy(buf, "Calm");
            else
                snprintf(buf, sizeof(buf), "%d km/h", spd);
            rowValue(g, rowY(2), buf, C_WHITE, arrow ? (int16_t)(2 * ar + u(4)) : (int16_t)0);
            if (arrow)
                drawWindArrow(g, W - u(7) - ar, rowY(2), ar, d.windDirDeg, p.accent);
        }
        else
            rowValue(g, rowY(2), "--", C_SOFT);

        // chance of rain this hour (the first hourly entry)
        rowLabel(g, rowY(3), "Rain");
        int rp = d.barCount > 0 ? d.rainPct[0] : -1;
        if (rp >= 0)
        {
            snprintf(buf, sizeof(buf), "%d%%", rp);
            rowValue(g, rowY(3), buf, rp >= WEATHER_BAR_RAIN_PCT ? RAIN_COL : C_WHITE);
        }
        else
            rowValue(g, rowY(3), "--", C_SOFT);
    }

    // =====================================================================
    //  Forecast view
    // =====================================================================
    static void drawForecastTop(TFT_eSPI &g, const WeatherPage &p)
    {
        const WeatherDetail &d = p.wd;
        const uint8_t lf = lblFont();
        const int16_t m = u(6);
        char buf[8];

        // header: title on the left, what the blue numbers mean on the right
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString("Hourly", m, u(10), lf);
        int16_t lw = g.textWidth("rain %", lf);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(RAIN_COL);
        g.drawString("rain %", W - m, u(10), lf);
        drawDrop(g, W - m - lw - u(6), u(10) + 1, imax(2, u(2)), RAIN_COL);

        int n = d.barCount;
        if (n <= 0)
        {
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No forecast", W / 2, u(40), 2);
            return;
        }

        // bars: height = temperature relative to the other shown hours, colour = temperature
        int16_t top = u(27), base = u(55);
        int16_t area = base - top;
        int16_t x0 = u(6);
        int16_t slot = (int16_t)((W - 2 * x0) / n);
        int16_t barW = imax(3, (int16_t)(slot * 6 / 10));
        int tmin = d.tempC[0], tmax = d.tempC[0];
        for (int i = 1; i < n; i++)
        {
            if (d.tempC[i] < tmin)
                tmin = d.tempC[i];
            if (d.tempC[i] > tmax)
                tmax = d.tempC[i];
        }
        int16_t hMin = imax(3, (int16_t)(area / 4));
        // "now" is wider than an hour number; it only gets used when it still clears its neighbour
        bool showNow = (g.textWidth("now", lf) + g.textWidth("00", lf)) / 2 < slot;

        for (int i = 0; i < n; i++)
        {
            int16_t h = tmax > tmin ? hMin + (int16_t)((long)(area - hMin) * (d.tempC[i] - tmin) / (tmax - tmin)) : (int16_t)(area * 3 / 5);
            int16_t bx = x0 + i * slot + (slot - barW) / 2;
            int16_t cxBar = bx + barW / 2;
            bool rain = d.rainPct[i] >= WEATHER_BAR_RAIN_PCT;
            uint16_t col = tempColor((float)d.tempC[i]);

            float ge = 1.0f; // growth of this bar, 0..1: each starts a little after its left neighbour
#if WEATHER_BARS_GROW
            {
                float tp = clamp01((float)(p.growMs - (int32_t)i * WEATHER_BARS_STAGGER_MS) / (float)WEATHER_BARS_GROW_MS);
                float iv = 1.0f - tp;
                ge = 1.0f - iv * iv * iv; // ease-out
            }
#endif
            int16_t hv = (int16_t)(h * ge + 0.5f);
            if (hv > 0)
                g.fillRect(bx, base - hv, barW, hv, i == 0 ? col : tint(col, 0.75f));
            if (rain)
                g.fillRect(bx, base + 1, barW, imax(1, u(1.5f)), RAIN_COL);

            // temperature above the bar (rides up with it while the bar grows)
            snprintf(buf, sizeof(buf), "%d", d.tempC[i]);
            g.setTextDatum(MC_DATUM);
            g.setTextColor(i == 0 ? C_WHITE : C_SOFT);
            g.drawString(buf, cxBar, base - hv - u(5), lf);

            // hour below the bar
            if (i == 0 && showNow)
                strcpy(buf, "now");
            else
                snprintf(buf, sizeof(buf), "%d", d.hour[i]);
            g.setTextColor(rain ? RAIN_COL : (i == 0 ? C_WHITE : C_SOFT));
            g.drawString(buf, cxBar, u(63), lf);

            // chance of rain in that hour
            int rp = d.rainPct[i];
            if (rp >= 0)
            {
                snprintf(buf, sizeof(buf), "%d", rp);
                g.setTextColor(rain ? RAIN_COL : tint(C_SOFT, 0.6f));
            }
            else
            {
                strcpy(buf, "-");
                g.setTextColor(tint(C_SOFT, 0.4f));
            }
            g.drawString(buf, cxBar, u(74), lf);
        }
    }

    // 6.25 -> "06:15" (a sunset after midnight comes as > 24: wrap it)
    static void fmtHM(char *out, size_t n, float h)
    {
        while (h >= 24.0f)
            h -= 24.0f;
        while (h < 0.0f)
            h += 24.0f;
        int hh = (int)h;
        int mm = (int)((h - hh) * 60.0f + 0.5f);
        if (mm >= 60)
        {
            mm = 0;
            hh = (hh + 1) % 24;
        }
        snprintf(out, n, "%02d:%02d", hh, mm);
    }

    static void drawForecastBottom(TFT_eSPI &g, const WeatherPage &p)
    {
        const WeatherDetail &d = p.wd;
        char buf[24];

        // sunrise / sunset: from the date and the saved coordinates, no internet needed
        SunTimes st = {};
        if (p.haveTime && p.ti)
            st = sun_today(*p.ti);
        bool sunOk = st.valid && !st.polarNight && !st.midnightSun;
        const char *noSun = !st.valid ? "--" : (st.polarNight ? "no sun" : "all day");

        rowLabel(g, rowY(0), "Sunrise");
        if (sunOk)
        {
            fmtHM(buf, sizeof(buf), st.riseH);
            rowValue(g, rowY(0), buf, C_AMBER);
        }
        else
            rowValue(g, rowY(0), noSun, C_SOFT);

        rowLabel(g, rowY(1), "Sunset");
        if (sunOk)
        {
            fmtHM(buf, sizeof(buf), st.setH);
            rowValue(g, rowY(1), buf, rgb(255, 140, 60));
        }
        else
            rowValue(g, rowY(1), noSun, C_SOFT);

        // UV: the number, a short word for the level, coloured like a traffic light
        rowLabel(g, rowY(2), "UV max");
        if (d.uvMax >= 0)
        {
            int uv = (int)lroundf(d.uvMax);
            const char *word;
            uint16_t col;
            if (uv < 3)
            {
                word = "Low";
                col = rgb(90, 230, 130);
            }
            else if (uv < 6)
            {
                word = "Mod";
                col = C_AMBER;
            }
            else if (uv < 8)
            {
                word = "High";
                col = rgb(255, 140, 60);
            }
            else if (uv < 11)
            {
                word = "V.High";
                col = C_RED;
            }
            else
            {
                word = "Ext";
                col = rgb(200, 100, 255);
            }
            snprintf(buf, sizeof(buf), "%d %s", uv, word);
            rowValue(g, rowY(2), buf, col);
        }
        else
            rowValue(g, rowY(2), "--", C_SOFT);

        rowLabel(g, rowY(3), "Press");
        if (d.pressureHpa > 0)
        {
            snprintf(buf, sizeof(buf), "%d hPa", d.pressureHpa);
            rowValue(g, rowY(3), buf, C_WHITE);
        }
        else
            rowValue(g, rowY(3), "--", C_SOFT);
    }

    // =====================================================================
    //  Entry points (called by drawFullPage)
    // =====================================================================
    void drawWeatherPageTop(TFT_eSPI &g, const WeatherPage &p)
    {
        if (p.forecastView && p.wd.valid)
            drawForecastTop(g, p);
        else
            drawNowTop(g, p);
    }

    void drawWeatherPageBottom(TFT_eSPI &g, const WeatherPage &p)
    {
        if (p.forecastView && p.wd.valid)
            drawForecastBottom(g, p);
        else
            drawNowBottom(g, p);

        if (p.stale)
        {
            // small clock badge: the reading is old (bottom right, next to the page dots)
            int16_t bx = W - u(10), by = infoH - u(7), br = imax(3, u(4));
            g.drawCircle(bx, by, br, C_AMBER);
            g.drawLine(bx, by, bx, by - br + 1, C_AMBER);
            g.drawLine(bx, by, bx + br - 1, by, C_AMBER);
        }
    }

} // namespace disp