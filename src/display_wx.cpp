// display_wx.cpp - extra weather: the 7-day part and the air quality part of the (portrait) Weather page.
//
//   drawWxDailySection  "Next days": one row per day - weekday, icon, chance of rain, high / low
//   drawWxAirSection    "Air quality": big US AQI, a gauge, PM2.5, PM10, pollen
//   (The umbrella, the UV sunglasses and the face mask were removed.)
//
// The two page parts are laid out like one block that is a whole page tall (faceH + infoH), from its top edge, and drawn
// into whichever canvas shows a piece of it: display_page_weather.cpp calls them once per canvas with oy = the canvas y of
// the block's top.
#include "display_wx.h"
#include "weather_mood.h"
#include <string.h>

namespace disp
{

    static const uint16_t RAIN_COL = rgb(120, 200, 255);

    static uint8_t lblFont() { return sc >= 1.5f ? 2 : 1; } // small captions: built-in font 1 (6x8) / font 2

    // =====================================================================
    //  Colours and words
    // =====================================================================
    static uint16_t aqiColor(int aqi)
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

    static const char *aqiWord(int aqi)
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

    // PM2.5 / PM10 in ug/m3, coloured with the US breakpoints (good / moderate / sensitive / unhealthy)
    static uint16_t pmColor(float v, float b1, float b2, float b3)
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

    static const char *pollenWord(int level)
    {
        static const char *const w[5] = {"None", "Low", "Moderate", "High", "V.High"};
        return (level >= 0 && level <= 4) ? w[level] : "n/a";
    }

    static uint16_t pollenColor(int level)
    {
        switch (level)
        {
        case 0:
            return C_SOFT;
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

    // =====================================================================
    //  "Next days" part
    // =====================================================================
    void drawWxDailySection(TFT_eSPI &g, const WeatherPage &p, int16_t oy)
    {
        static const char *const kDow[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        const uint8_t lf = lblFont();
        const int16_t m = u(6);

        // header: title on the left, what the blue numbers mean on the right (like the hourly part)
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString("Next days", m, oy + u(10), lf);
        int16_t lw = g.textWidth("rain %", lf);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(RAIN_COL);
        g.drawString("rain %", W - m - u(3), oy + u(10), lf); // u(3): clear of the scroll bar at the right edge
        drawDrop(g, W - m - u(3) - lw - u(6), oy + u(10) + 1, imax(2, u(2)), RAIN_COL);

        WeatherExtra x;
        if (!weather_extra_get(x) || !x.forecastValid || x.dayCount == 0)
        {
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No forecast", W / 2, oy + u(50), 2);
            return;
        }

        const int16_t y0 = oy + u(24); // centre of the first row
        const int16_t pitch = u(17);   // 7 rows end at about u(126), the page dots start at u(141)
        const int16_t xr = W - u(9);   // right edge of the temperatures (the scroll bar sits right of it)
        for (int i = 0; i < x.dayCount; i++)
        {
            const WxDay &d = x.day[i];
            const int16_t cy = y0 + i * pitch;
            if (cy + pitch < 0 || cy - pitch > faceH + infoH) // far outside the page
                continue;

            // every second row a faint stripe, so the eye can follow a row from the weekday to the temperatures
            if (i & 1)
                g.fillRect(u(2), cy - pitch / 2, W - u(7), pitch, tint(C_SOFT, 0.07f));

            // weekday ("Today" is longer than the others: it falls back to the small font when it does not fit)
            const char *name = (i == 0) ? "Today" : kDow[d.dow % 7];
            uint8_t nf = 2;
            if (g.textWidth(name, 2) > u(32))
                nf = lf;
            g.setTextDatum(ML_DATUM);
            g.setTextColor(i == 0 ? C_WHITE : C_SOFT);
            g.drawString(name, m, cy, nf);

            // icon (still: now = 0)
            drawWeatherIcon(g, d.code, u(46), cy, u(6), 0);

            // high / low: "21/9", the high coloured by its temperature, the low soft
            char hb[8], lb[8];
            snprintf(hb, sizeof(hb), "%d", d.maxC);
            snprintf(lb, sizeof(lb), "%d", d.minC);
            const int16_t lwid = g.textWidth(lb, 2), swid = g.textWidth("/", 2), hwid = g.textWidth(hb, 2);
            g.setTextDatum(MR_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString(lb, xr, cy, 2);
            g.setTextColor(tint(C_SOFT, 0.5f));
            g.drawString("/", xr - lwid, cy, 2);
            g.setTextColor(tempColor((float)d.maxC));
            g.drawString(hb, xr - lwid - swid, cy, 2);
            const int16_t tLeft = xr - lwid - swid - hwid; // where the temperatures start

            // chance of rain: right-aligned next to the temperatures, left out when it would run into the icon
            if (d.rainPct >= 0)
            {
                char rb[8];
                snprintf(rb, sizeof(rb), "%d%%", d.rainPct);
                const int16_t rw = g.textWidth(rb, lf);
                if (tLeft - u(4) - rw > u(54))
                {
                    g.setTextDatum(MR_DATUM);
                    g.setTextColor(d.rainPct >= WEATHER_BAR_RAIN_PCT ? RAIN_COL : tint(C_SOFT, 0.6f));
                    g.drawString(rb, tLeft - u(4), cy, lf);
                }
            }
        }
        (void)p;
    }

    // =====================================================================
    //  "Air quality" part
    // =====================================================================
    void drawWxAirSection(TFT_eSPI &g, const WeatherPage &p, int16_t oy)
    {
        static const char *const kPollenName[6] = {"Alder", "Birch", "Grass", "Mugwort", "Olive", "Ragweed"};
        const uint8_t lf = lblFont();
        const int16_t m = u(6);

        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString("Air quality", m, oy + u(10), lf);
        g.setTextDatum(MR_DATUM);
        g.drawString("US AQI", W - m - u(3), oy + u(10), lf);

        WeatherExtra x;
        if (!weather_extra_get(x) || !x.aqValid)
        {
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("No air data", W / 2, oy + u(50), 2);
            return;
        }

        char buf[24];
        const int aqi = x.usAqi;
        const uint16_t col = aqiColor(aqi);

        // the big number and the word for it
        if (aqi >= 0)
        {
            snprintf(buf, sizeof(buf), "%d", aqi);
            g.setTextDatum(ML_DATUM);
            g.setTextColor(col);
            g.drawString(buf, m, oy + u(27), 4);
            g.setTextDatum(ML_DATUM);
            g.drawString(aqiWord(aqi), m, oy + u(48), 2);
        }
        else
        {
            g.setTextDatum(ML_DATUM);
            g.setTextColor(C_SOFT);
            g.drawString("--", m, oy + u(27), 4);
        }

        // gauge: five bands from 0 to 300 (good, moderate, sensitive, unhealthy, very unhealthy and worse), a marker
        {
            const int16_t gx = m, gw = W - 2 * m - u(3), gy = oy + u(64), gh = imax(3, u(5));
            static const int kBandLo[5] = {0, 50, 100, 150, 200};
            static const int kBandHi[5] = {50, 100, 150, 200, 300};
            static const int kBandRep[5] = {25, 75, 125, 175, 250}; // a value inside each band: its colour
            for (int b = 0; b < 5; b++)
            {
                int16_t xa = gx + (int16_t)((long)gw * kBandLo[b] / 300);
                int16_t xb = gx + (int16_t)((long)gw * kBandHi[b] / 300);
                g.fillRect(xa, gy, xb - xa - 1, gh, tint(aqiColor(kBandRep[b]), 0.65f));
            }
            if (aqi >= 0)
            {
                int v = aqi > 300 ? 300 : aqi;
                int16_t mx = gx + (int16_t)((long)gw * v / 300);
                g.fillRect(mx - 1, gy - u(1), 2, gh + 2 * u(1), C_WHITE);
                g.fillTriangle(mx, gy - u(2), mx - u(3), gy - u(6), mx + u(3), gy - u(6), C_WHITE);
            }
        }

        // PM2.5, PM10 (ug/m3) and pollen
        const int16_t rowStep = u(14);
        const int16_t r0 = oy + u(80);
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString("PM2.5 ug/m3", m, r0, lf);
        g.drawString("PM10 ug/m3", m, r0 + rowStep, lf);
        g.drawString("Pollen", m, r0 + 2 * rowStep, lf);

        g.setTextDatum(MR_DATUM);
        const int16_t xr = W - u(9);
        if (x.pm25 >= 0)
        {
            snprintf(buf, sizeof(buf), "%d", (int)lroundf(x.pm25));
            g.setTextColor(pmColor(x.pm25, 12.0f, 35.4f, 55.4f));
            g.drawString(buf, xr, r0, 2);
        }
        else
        {
            g.setTextColor(C_SOFT);
            g.drawString("--", xr, r0, 2);
        }
        if (x.pm10 >= 0)
        {
            snprintf(buf, sizeof(buf), "%d", (int)lroundf(x.pm10));
            g.setTextColor(pmColor(x.pm10, 54.0f, 154.0f, 254.0f));
            g.drawString(buf, xr, r0 + rowStep, 2);
        }
        else
        {
            g.setTextColor(C_SOFT);
            g.drawString("--", xr, r0 + rowStep, 2);
        }
        g.setTextColor(pollenColor(x.pollenLevel));
        g.drawString(pollenWord(x.pollenLevel), xr, r0 + 2 * rowStep, 2);

        // which pollen it is, and how much
        if (x.pollenLevel >= 1)
        {
            snprintf(buf, sizeof(buf), "%s %d /m3", kPollenName[x.pollenType % 6], (int)lroundf(x.pollenPeak));
            g.setTextDatum(MR_DATUM);
            g.setTextColor(tint(C_SOFT, 0.7f));
            g.drawString(buf, xr, r0 + 3 * rowStep - u(2), lf);
        }

        // how old the reading is
        char age[8];
        fmtAge(age, sizeof(age), ageSince(p.now, x.aqMs));
        snprintf(buf, sizeof(buf), "updated %s ago", age);
        g.setTextDatum(ML_DATUM);
        g.setTextColor(tint(C_SOFT, 0.55f));
        g.drawString(buf, m, oy + u(133), lf);
    }

} // namespace disp
