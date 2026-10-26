// display_page_weather.cpp - the Weather page (full screen): ONE page that the rotary knob scrolls.
//
// The page is (at least) twice as tall as the screen. Its first half ("now") and second half ("forecast") are laid out the way
// the two views used to be, one below the other:
//
//   now        upper part: icon, big temperature, condition, today's high / low
//              lower part: feels like, humidity, wind (speed + arrow), chance of rain
//   forecast   upper part: hourly bars with the temperature above and the chance of rain below
//              lower part: sunrise, sunset (computed on the chip), UV, pressure
//
// Two more screens follow when they are enabled (weather_extras.h, drawn in display_wx.cpp): "Next days" (the 7-day
// forecast, one row per day) and "Air quality" (US AQI, PM2.5, PM10, pollen). Each of them is one block that is a whole
// screen tall.
//
// Turning the knob scrolls the page (touch.cpp sets the target, display_pages.cpp eases the pixel offset toward it).
// Without the extra details (no forecast yet) only the "now" part exists and nothing scrolls.
//
// Drawing: like every full-screen page it is drawn into the two canvases (upper card = faceCv, lower card = infoCv).
// Each canvas draws the part of the tall page that is in view: a block (upper / lower part of a section) is laid out
// in its own canvas coordinates, shifted by `oy` = the canvas y of the block's top edge; the sprite clips whatever
// sticks out. Fixed parts (the coloured line at the top, the footer with the page dots, the scroll bar) are drawn last.
#include "display_internal.h"
#include "display_style.h"
#include "display_wx.h"

namespace disp
{

    static const uint16_t RAIN_COL = rgb(120, 200, 255);

    static uint8_t lblFont() { return sc >= 1.5f ? 2 : 1; } // small captions: built-in font 1 (6x8) / font 2

    // ---- Rows of the lower part: caption on the left, value on the right ----
    // oy = canvas y of the top edge of the lower block (see the header comment)
    static int16_t rowY(int16_t oy, int i) // centre of row i (0 = top .. 3 = bottom) inside the lower block
    {
        return (int16_t)(oy + infoH - u(19) - (3 - i) * u(13));
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
    //  "Now" section
    // =====================================================================
    static void drawNowTop(TFT_eSPI &g, const WeatherPage &p, int16_t oy)
    {
        const int16_t rowCy = oy + u(24); // middle of the icon / temperature row

        if (!p.valid)
        {
            drawCloud(g, W / 2, oy + u(34), u(16), tint(C_SOFT, 0.47f));
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No weather yet", W / 2, oy + u(60), 2);
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
        g.drawString(cap, W / 2, oy + u(53), 2);

        // today's high / low
        if (p.wd.valid)
        {
            drawHiLo(g, (int16_t)(W * 0.28f), oy + u(70), true, p.wd.maxC, tempColor(p.wd.maxC));
            drawHiLo(g, (int16_t)(W * 0.72f), oy + u(70), false, p.wd.minC, tempColor(p.wd.minC));
        }
    }

    static void drawNowBottom(TFT_eSPI &g, const WeatherPage &p, int16_t oy)
    {
        if (!p.valid)
            return; // the upper half already says "No weather yet"
        const WeatherDetail &d = p.wd;
        if (!d.valid)
        {
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No details yet", W / 2, oy + infoH / 2 - u(4), 2);
            return;
        }
        char buf[24];

        // feels like
        rowLabel(g, rowY(oy, 0), "Feels like");
        if (!isnan(d.feelsC))
            drawDegValue(g, W - u(7), rowY(oy, 0), (int)lroundf(d.feelsC), tempColor(d.feelsC));
        else
            rowValue(g, rowY(oy, 0), "--", C_SOFT);

        // humidity: a water drop in front of the number
        rowLabel(g, rowY(oy, 1), "Humidity");
        if (d.humidityPct >= 0)
        {
            snprintf(buf, sizeof(buf), "%d%%", d.humidityPct);
            int16_t tw = g.textWidth(buf, 2);
            rowValue(g, rowY(oy, 1), buf, C_WHITE);
            drawDrop(g, W - u(7) - tw - u(6), rowY(oy, 1) + 1, imax(2, u(3)), RAIN_COL);
        }
        else
            rowValue(g, rowY(oy, 1), "--", C_SOFT);

        // wind: speed and an arrow for the direction (a direction means nothing in calm air)
        rowLabel(g, rowY(oy, 2), "Wind");
        if (d.windKmh >= 0)
        {
            int spd = (int)lroundf(d.windKmh);
            bool arrow = d.windDirDeg >= 0 && spd >= 1;
            int16_t ar = u(5);
            if (spd < 1)
                strcpy(buf, "Calm");
            else
                snprintf(buf, sizeof(buf), "%d km/h", spd);
            rowValue(g, rowY(oy, 2), buf, C_WHITE, arrow ? (int16_t)(2 * ar + u(4)) : (int16_t)0);
            if (arrow)
                drawWindArrow(g, W - u(7) - ar, rowY(oy, 2), ar, d.windDirDeg, p.accent);
        }
        else
            rowValue(g, rowY(oy, 2), "--", C_SOFT);

        // chance of rain this hour (the first hourly entry)
        rowLabel(g, rowY(oy, 3), "Rain");
        int rp = d.barCount > 0 ? d.rainPct[0] : -1;
        if (rp >= 0)
        {
            snprintf(buf, sizeof(buf), "%d%%", rp);
            rowValue(g, rowY(oy, 3), buf, rp >= WEATHER_BAR_RAIN_PCT ? RAIN_COL : C_WHITE);
        }
        else
            rowValue(g, rowY(oy, 3), "--", C_SOFT);
    }

    // =====================================================================
    //  Forecast section
    // =====================================================================
    static void drawForecastTop(TFT_eSPI &g, const WeatherPage &p, int16_t oy)
    {
        const WeatherDetail &d = p.wd;
        const uint8_t lf = lblFont();
        const int16_t m = u(6);
        char buf[8];

        // header: title on the left, what the blue numbers mean on the right
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString("Hourly", m, oy + u(10), lf);
        int16_t lw = g.textWidth("rain %", lf);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(RAIN_COL);
        g.drawString("rain %", W - m, oy + u(10), lf);
        drawDrop(g, W - m - lw - u(6), oy + u(10) + 1, imax(2, u(2)), RAIN_COL);

        int n = d.barCount;
        if (n <= 0)
        {
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No forecast", W / 2, oy + u(40), 2);
            return;
        }

        // bars: height = temperature relative to the other shown hours, colour = temperature
        int16_t top = oy + u(27), base = oy + u(55);
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
            {
                uint16_t bc = i == 0 ? col : tint(col, 0.75f);
#if UI_STYLE
                // rounded tops; the first (now) bar gets a bright core line like the neon digits
                int16_t r = imin((int16_t)(barW / 2), (int16_t)imax(1, u(2)));
                if (hv > 2 * r)
                {
                    g.fillRect(bx, base - hv + r, barW, hv - r, bc);
                    g.fillRoundRect(bx, base - hv, barW, 2 * r, r, bc);
                }
                else
                    g.fillRect(bx, base - hv, barW, hv, bc);
                if (i == 0 && barW > 2)
                    g.drawFastVLine(bx + barW / 2, base - hv + r, imax(0, hv - r - 1), neonCore(bc, 0.6f));
#else
                g.fillRect(bx, base - hv, barW, hv, bc);
#endif
            }
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
            g.drawString(buf, cxBar, oy + u(63), lf);

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
            g.drawString(buf, cxBar, oy + u(74), lf);
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

    static void drawForecastBottom(TFT_eSPI &g, const WeatherPage &p, int16_t oy)
    {
        const WeatherDetail &d = p.wd;
        char buf[24];

        // sunrise / sunset: from the date and the saved coordinates, no internet needed
        SunTimes st = {};
        if (p.haveTime && p.ti)
            st = sun_today(*p.ti);
        bool sunOk = st.valid && !st.polarNight && !st.midnightSun;
        const char *noSun = !st.valid ? "--" : (st.polarNight ? "no sun" : "all day");

        rowLabel(g, rowY(oy, 0), "Sunrise");
        if (sunOk)
        {
            fmtHM(buf, sizeof(buf), st.riseH);
            rowValue(g, rowY(oy, 0), buf, C_AMBER);
        }
        else
            rowValue(g, rowY(oy, 0), noSun, C_SOFT);

        rowLabel(g, rowY(oy, 1), "Sunset");
        if (sunOk)
        {
            fmtHM(buf, sizeof(buf), st.setH);
            rowValue(g, rowY(oy, 1), buf, rgb(255, 140, 60));
        }
        else
            rowValue(g, rowY(oy, 1), noSun, C_SOFT);

        // UV: the number, a short word for the level, coloured like a traffic light
        rowLabel(g, rowY(oy, 2), "UV max");
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
            rowValue(g, rowY(oy, 2), buf, col);
        }
        else
            rowValue(g, rowY(oy, 2), "--", C_SOFT);

        rowLabel(g, rowY(oy, 3), "Press");
        if (d.pressureHpa > 0)
        {
            snprintf(buf, sizeof(buf), "%d hPa", d.pressureHpa);
            rowValue(g, rowY(oy, 3), buf, C_WHITE);
        }
        else
            rowValue(g, rowY(oy, 3), "--", C_SOFT);
    }

    // =====================================================================
    //  Fixed parts: scroll bar and footer
    // =====================================================================

    // Thin position bar at the right edge, only while the page can scroll. Fixed on screen (page y, not scrolled);
    // base = page y of the canvas' first row.
    static void drawScrollBar(TFT_eSPI &g, const WeatherPage &p, int16_t base)
    {
#if WEATHER_SCROLLBAR
        if (!p.scrollable || p.maxScrollPx <= 0)
            return;
        const int16_t ph = (int16_t)(faceH + infoH);
        const int16_t bw = imax(2, u(2));
        const int16_t x = (int16_t)(W - bw - u(1));
        const int16_t top = u(8), bot = (int16_t)(ph - u(14));
        const int16_t len = (int16_t)(bot - top);
        int16_t th = (int16_t)((long)len * ph / (ph + p.maxScrollPx)); // thumb = the share of the page that is in view
        if (th < u(10))
            th = u(10);
        const int16_t ty = (int16_t)(top + (long)(len - th) * p.scrollPx / p.maxScrollPx);
        g.fillRoundRect(x, top - base, bw, len, bw / 2, tint(C_SOFT, 0.22f));
        g.fillRoundRect(x, ty - base, bw, th, bw / 2, tint(p.accent, 0.85f));
#else
        (void)g;
        (void)p;
        (void)base;
#endif
    }

    // Bottom strip of the lower canvas: a plain band that content scrolls under (the page dots are drawn on it by
    // drawFullPage afterwards), and the small clock badge when the reading is old.
    static void drawFooter(TFT_eSPI &g, const WeatherPage &p)
    {
        const int16_t band = u(12);
        g.fillRect(0, infoH - band, W, band, C_BG);
        if (p.stale)
        {
            // small clock badge: the reading is old (bottom right, next to the page dots)
            int16_t bx = W - u(10), by = infoH - u(7), br = imax(3, u(4));
            g.drawCircle(bx, by, br, C_AMBER);
            g.drawLine(bx, by, bx, by - br + 1, C_AMBER);
            g.drawLine(bx, by, bx + br - 1, by, C_AMBER);
        }
    }

    // =====================================================================
    //  The page
    // =====================================================================

    // Section s (>= 2) of the page: the 7-day part first (when enabled), then the air quality part (when enabled).
    static void drawExtraSection(TFT_eSPI &g, const WeatherPage &p, int s, int16_t oy)
    {
        int idx = s - 2;
        if (WX_DAILY_ENABLED)
        {
            if (idx == 0)
            {
                drawWxDailySection(g, p, oy);
                return;
            }
            idx--;
        }
        if (WX_AIRQ_ENABLED && idx == 0)
            drawWxAirSection(g, p, oy);
    }

    // Draws the part of the tall page that lies in this canvas. lower = the lower card (infoCv), else the upper card
    // (faceCv); the page runs through both: the upper card shows page rows 0 .. faceH, the lower card faceH .. faceH + infoH,
    // moved up by the scroll offset.
    static void drawWeatherPage(TFT_eSPI &g, const WeatherPage &p, bool lower)
    {
        const int16_t ph = (int16_t)(faceH + infoH); // height of one section = height of the screen's page area
        const int16_t base = lower ? faceH : (int16_t)0;
        const int16_t ch = lower ? infoH : faceH;
        const int sections = p.scrollable ? WX_SECTIONS : 1;
        for (int s = 0; s < sections; s++)
        {
            const bool forecast = (s == 1);
            const int16_t oyTop = (int16_t)(s * ph - p.scrollPx - base); // canvas y of this section's upper block
            const int16_t oyBot = (int16_t)(oyTop + faceH);              // ... and of its lower block
            if (s >= 2)                                                  // "Next days" / "Air quality": one block that is a whole screen tall (display_wx.cpp)
            {
                if (oyTop < ch && oyTop + ph > 0)
                    drawExtraSection(g, p, s, oyTop);
                continue;
            }
            if (oyTop < ch && oyTop + faceH > 0)
            {
                if (forecast)
                    drawForecastTop(g, p, oyTop);
                else
                    drawNowTop(g, p, oyTop);
            }
            if (oyBot < ch && oyBot + infoH > 0)
            {
                if (forecast)
                    drawForecastBottom(g, p, oyBot);
                else
                    drawNowBottom(g, p, oyBot);
            }
        }
        drawScrollBar(g, p, base);
        if (lower)
            drawFooter(g, p);
    }

    // =====================================================================
    //  Entry points (called by drawFullPage)
    // =====================================================================
    void drawWeatherPageTop(TFT_eSPI &g, const WeatherPage &p)
    {
        drawWeatherPage(g, p, false);
    }

    void drawWeatherPageBottom(TFT_eSPI &g, const WeatherPage &p)
    {
        drawWeatherPage(g, p, true);
    }

} // namespace disp