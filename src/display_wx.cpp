// display_wx.cpp - extra weather: the 7-day part and the air quality part of the Weather page, and the face accessories.
//
//   drawWxDailySection  "Next days": one row per day - weekday, icon, chance of rain, high / low
//   drawWxAirSection    "Air quality": big US AQI, a gauge, PM2.5, PM10, pollen
//   drawWeatherAccessory  umbrella (rain within WX_RAIN_WARN_MIN minutes), sunglasses (UV index at or above
//                         WX_UV_SHADES_MIN, bright sky only)
//   faceMaskUpdate / drawFaceMask  face mask + cough (AQI at or above WX_AIRQ_MASK_AQI). Not part of the face: shown
//                         whenever the air is bad, in every face state.
//
// The two page parts are laid out like one block that is a whole page tall (faceH + infoH), from its top edge, and drawn
// into whichever canvas shows a piece of it: display_page_weather.cpp calls them once per canvas with oy = the canvas y of
// the block's top. The face accessories are drawn over the eyes in the clock page's face card.
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

    // =====================================================================
    //  Face accessories
    // =====================================================================

    // the eyes as drawFace() places them (display_face.cpp)
    struct FaceGeom
    {
        int16_t R, eyeW, eyeH, cy, lcx, rcx;
    };

    static FaceGeom faceGeom()
    {
        FaceGeom f;
        f.R = W / 8;
        f.eyeW = f.R * 2;
        f.eyeH = (int16_t)(f.R * 2.25f);
        f.cy = (int16_t)(faceH * 0.55f);
        f.lcx = W / 2 - f.R * 2;
        f.rcx = W / 2 + f.R * 2;
        return f;
    }

    // ---- Umbrella over the eyes, with the rain (or snow) falling past it and a countdown at the bottom ----
    static void drawUmbrella(TFT_eSPI &g, uint32_t now, int32_t msLeft, bool snow)
    {
        const FaceGeom f = faceGeom();
        const int16_t cx = W / 2;
        const int16_t eyeTop = f.cy - f.eyeH / 2;
        const int16_t base = eyeTop - u(7); // the canopy's lower edge (the scallops hang a little below it)
        const int16_t rx = u(30);
        const int16_t ry = imin(u(19), (int16_t)(base - u(2)));
        if (ry < u(8))
            return; // the card is too small for it

        const uint16_t canopy = rgb(235, 80, 90), rib = rgb(150, 35, 50), shaft = rgb(200, 205, 225);

        // canopy: a half ellipse, line by line
        for (int16_t dy = 0; dy <= ry; dy++)
        {
            float k = (float)dy / (float)ry;
            int16_t hw = (int16_t)(rx * sqrtf(fmaxf(0.0f, 1.0f - k * k)) + 0.5f);
            g.drawFastHLine(cx - hw, base - dy, 2 * hw + 1, canopy);
        }
        // ribs from the tip
        const int16_t tipY = base - ry;
        g.drawLine(cx, tipY, cx - rx / 2, base, rib);
        g.drawLine(cx, tipY, cx, base, rib);
        g.drawLine(cx, tipY, cx + rx / 2, base, rib);
        // scalloped lower edge: five small half circles
        const int16_t sr = imax(2, (int16_t)(rx / 5));
        for (int k = 0; k < 5; k++)
        {
            int16_t sx = cx - rx + sr + k * (2 * sr);
            g.fillCircle(sx, base, sr, canopy);
        }
        // shaft between the eyes, with a small hook at the bottom
        const int16_t shaftEnd = f.cy + f.eyeH / 2 - u(3);
        g.drawFastVLine(cx, base, shaftEnd - base, shaft);
        g.drawFastVLine(cx + 1, base, shaftEnd - base, shaft);
        g.drawFastHLine(cx - u(5), shaftEnd, u(5) + 2, shaft);
        g.drawFastVLine(cx - u(5), shaftEnd - u(3), u(3) + 1, shaft);

        // rain / snow: a few that hit the canopy (they end on its surface) and two that fall past on the outside
        const uint16_t dropCol = snow ? C_WHITE : RAIN_COL;
        for (int i = 0; i < 6; i++)
        {
            const uint32_t period = 650 + (uint32_t)i * 110;
            const float ph = (float)((now + (uint32_t)i * 233) % period) / (float)period;
            int16_t x, yEnd;
            if (i < 4)
            {
                static const float off[4] = {-0.72f, -0.3f, 0.3f, 0.72f};
                x = cx + (int16_t)(off[i] * rx);
                float k = (float)(x - cx) / (float)rx;
                yEnd = base - (int16_t)(ry * sqrtf(fmaxf(0.0f, 1.0f - k * k)));
            }
            else
            {
                x = (i == 4) ? u(7) : W - u(7);
                yEnd = faceH - u(10);
            }
            const int16_t y = (int16_t)(ph * yEnd);
            if (snow)
                g.fillCircle(x + (int16_t)(sinf(ph * 9.0f + i) * u(2)), y, imax(1, u(1.5f)), dropCol);
            else if (y + u(4) <= yEnd)
                g.drawFastVLine(x, y, imax(2, u(4)), dropCol);
        }

        // countdown at the bottom of the card
        char buf[24];
        const char *what = snow ? "Snow" : "Rain";
        if (msLeft <= 0)
            snprintf(buf, sizeof(buf), "%s now", what);
        else if (msLeft <= 60000)
            snprintf(buf, sizeof(buf), "%s soon", what);
        else
            snprintf(buf, sizeof(buf), "%s in %d min", what, (int)((msLeft + 59999) / 60000));
        g.setTextDatum(BC_DATUM);
        // the face mask sits right where this text is: light blue on the pale cloth cannot be read, so it turns dark
        g.setTextColor(faceMaskUp ? rgb(20, 70, 125) : dropCol);
        g.drawString(buf, W / 2, faceH - u(3), lblFont());
    }

    // ---- Face mask below the eyes; every WX_COUGH_PERIOD_MS a cough: the mask shakes and puffs fly out ----
    // Shown whenever the air is bad (AQI), nothing else: display.cpp draws it over the face in every face state.
    bool faceMaskUp = false;
    static int s_maskAqi = -1; // the AQI the mask was switched on / kept by (how hard he coughs)

    bool faceMaskUpdate(uint32_t now)
    {
        faceMaskUp = false;
#if WX_AIRQ_ENABLED
        if (WX_AIRQ_MASK_AQI <= 0)
            return false;
        WeatherExtra x;
        if (!weather_extra_get(x))
            return false;
        const bool aqFresh = x.aqValid && (uint32_t)(now - x.aqMs) <= 2UL * WX_AIRQ_REFRESH_MS;
        faceMaskUp = aqFresh && wxIsBadAir(x.usAqi);
        s_maskAqi = x.usAqi;
#else
        (void)now;
#endif
        return faceMaskUp;
    }

    // The mask sits below the eyes. It is not a face state: whether it is there depends on the air alone. But it is
    // worn on the head, so it goes where the head goes: dx / dy = how far the eyes are from their resting place (peek-a-boo,
    // the kick of a sneeze, a hiccup, a shiver), puff = it shakes with a sneeze like in a cough.
    void faceMaskRect(int16_t &x0, int16_t &y0, int16_t &w, int16_t &h)
    {
        const FaceGeom f = faceGeom();
        w = u(54);
        h = u(15);
        x0 = W / 2 - w / 2;
        y0 = f.cy + f.eyeH / 2 + u(4);
        if (y0 + h > faceH - u(1))
            y0 = faceH - u(1) - h; // stay inside the card
    }

    void drawFaceMask(TFT_eSPI &g, uint32_t now, int16_t dx, int16_t dy, bool puff)
    {
        int16_t bx, by, mw, mh;
        faceMaskRect(bx, by, mw, mh);
        const int16_t rr = u(6);
        const float bad = wxAirIntensity(s_maskAqi); // the worse the air, the more he coughs
        const uint32_t period = (uint32_t)((float)WX_COUGH_PERIOD_MS - bad * (float)(WX_COUGH_PERIOD_MS - WX_COUGH_PERIOD_MIN_MS));
        const uint32_t ph = now % period;
        const bool cough = ph < 900;
        int16_t jx = 0, jy = 0;
        if (cough || puff)
        {
            jx = ((now / 55) & 1) ? u(1) : -u(1);
            jy = ((now / 110) & 1) ? u(1) : 0;
        }
        const int16_t x0 = bx + dx + jx, y0 = by + dy + jy;
        if (y0 >= faceH || y0 + mh <= 0)
            return; // the head is out of the card (peek-a-boo)
        const uint16_t cloth = rgb(205, 228, 242), edge = rgb(70, 105, 130), pleat = rgb(150, 185, 205), strap = rgb(150, 170, 190);

        // ear straps (behind the mask)
        g.drawLine(x0, y0 + u(4), x0 - u(13), y0 + u(1), strap);
        g.drawLine(x0 + mw, y0 + u(4), x0 + mw + u(13), y0 + u(1), strap);
        // the mask: a rounded rectangle with three pleats
        g.fillRoundRect(x0, y0, mw, mh, rr, cloth);
        g.drawRoundRect(x0, y0, mw, mh, rr, edge);
        for (int k = 1; k <= 3; k++)
            g.drawFastHLine(x0 + u(5), y0 + k * mh / 4, mw - 2 * u(5), pleat);

        if (cough)
        {
            const float t = (float)ph / 900.0f;
            for (int i = 0; i < 3; i++)
            {
                float tp = clamp01((t - i * 0.12f) / 0.7f);
                if (tp <= 0.0f || tp >= 1.0f)
                    continue;
                int16_t px = x0 + mw + u(4) + (int16_t)(tp * u(26)) + i * u(2);
                int16_t py = y0 + mh / 2 - (int16_t)(tp * u(8)) - i * u(3);
                int16_t pr = u(2) + (int16_t)(tp * u(3));
                g.fillCircle(px, py, pr, tint(C_SOFT, 1.0f - tp));
            }
        }
    }

    // ---- Sunglasses (same look as the 13:37 moment, simpler: no glint that runs over them) ----
    // t = ms since they started to fall in; they are on for WX_UV_SHADES_ON_MS in all.
    static void drawUvShades(TFT_eSPI &g, uint32_t t)
    {
        const uint32_t DROP = 450, LIFT = 500;
        const uint32_t liftAt = WX_UV_SHADES_ON_MS - LIFT;
        const FaceGeom f = faceGeom();
        const int16_t lensH = (int16_t)(f.R * 1.75f);
        const int16_t lensW = 2 * f.R + u(6);
        const int16_t top = f.cy - f.R;
        const float away = (float)(top + lensH + u(4)); // how far above the card they wait

        float off = 0;
        if (t < DROP)
        {
            float p = t / (float)DROP;
            off = -(1.0f - p) * (1.0f - p) * away; // falls in, fast at the end
        }
        else if (t < DROP + 150)
            off = -sinf((t - DROP) * (float)M_PI / 150.0f) * u(3); // a small bounce on landing
        else if (t >= liftAt)
        {
            float p = clamp01((t - liftAt) / (float)LIFT);
            off = -p * p * away;
        }
        const int16_t y0 = top + (int16_t)lroundf(off);

        const uint16_t glass = mixColor(rgb(8, 10, 22), C_EYE_BOT, 0.12f); // the tint of the lenses
        const uint16_t rim = mixColor(rgb(95, 100, 125), C_EYE_TOP, 0.25f);
        const uint16_t barCol = rgb(10, 11, 20);
        const int16_t rt = imax(2, u(3));                          // top corners: nearly square
        const int16_t rb = imin(u(10), (int16_t)(lensH - rt - 1)); // bottom corners: round
        const int16_t barH = imax(2, u(3));
        const int16_t xl = f.lcx - lensW / 2, xr = f.rcx - lensW / 2;
        const bool canRead = faceCv.ok && (&g == static_cast<TFT_eSPI *>(&faceCv.spr)); // blending needs the sprite

        // where the lens is on row dy: [xa, xb] (x0 = its left edge)
        auto span = [&](int16_t x0, int16_t dy, int16_t &xa, int16_t &xb)
        {
            float inset = 0;
            if (dy < rt)
            {
                float yy = rt - dy - 0.5f;
                inset = rt - sqrtf(fmaxf(0.0f, (float)rt * rt - yy * yy));
            }
            else if (dy >= lensH - rb)
            {
                float yy = dy + 0.5f - (lensH - rb);
                inset = rb - sqrtf(fmaxf(0.0f, (float)rb * rb - yy * yy));
            }
            int16_t in = (int16_t)lroundf(inset);
            xa = x0 + in;
            xb = x0 + lensW - 1 - in;
        };

        for (int8_t side = 0; side < 2; side++)
        {
            const int16_t x0 = side ? xr : xl;
            for (int16_t dy = 0; dy < lensH; dy++)
            {
                const int16_t y = y0 + dy;
                if (y < 0 || y >= faceH)
                    continue;
                int16_t xa, xb, nxa, nxb;
                span(x0, dy, xa, xb);
                span(x0, (int16_t)imin((int16_t)(dy + 1), (int16_t)(lensH - 1)), nxa, nxb);

                // tint: strong at the top, lighter towards the bottom, so the eyes glow through
                float a = 0.78f - 0.38f * (dy / (float)lensH);
                for (int16_t x = xa; x <= xb; x++)
                {
                    if (x < 0 || x >= W)
                        continue;
                    if (canRead)
                        g.drawPixel(x, y, mixColor(g.readPixel(x, y), glass, a));
                    else
                        g.drawPixel(x, y, mixColor(C_BG, glass, 0.85f));
                }

                // rim: the two ends of the row, joined up where the outline steps sideways; the whole first and last row
                int16_t lo = xa, hi = xa;
                if (nxa > xa)
                    hi = nxa - 1;
                else if (nxa < xa)
                    lo = nxa + 1;
                g.drawFastHLine(lo, y, hi - lo + 1, rim);
                lo = hi = xb;
                if (nxb < xb)
                    lo = nxb + 1;
                else if (nxb > xb)
                    hi = nxb - 1;
                g.drawFastHLine(lo, y, hi - lo + 1, rim);
                if (dy == 0 || dy == lensH - 1)
                    g.drawFastHLine(xa, y, xb - xa + 1, rim);
            }
            // a small shine in the upper left corner of the lens
            g.drawLine(x0 + u(4), y0 + u(7), x0 + u(8), y0 + u(3), mixColor(glass, C_WHITE, 0.55f));
        }

        // the bar across the top, over the nose
        g.fillRect(xl, y0 - 1, (xr + lensW) - xl, barH, barCol);
        g.drawFastHLine(xl, y0 - 1, (xr + lensW) - xl, rim);
    }

    // ---- Which one, and when ----
    static bool s_shadesWas = false; // the UV condition held at the last call
    static uint32_t s_shadesT0 = 0;  // the first time the sunglasses fall in after the UV became high

    // umbrellaOk: the umbrella may be up (it is above the eyes, so it goes with a sneeze, a surprise, a fly, a yawn ...).
    // shadesOk:   the sunglasses may be on (they sit on the eyes, so only while the face is calm). If something starts while
    //             they are on, they lift off like at the end of their time instead of vanishing from one frame to the next.
    // Both come from display.cpp, which also draws the face mask (faceMaskUpdate / drawFaceMask) in every state.
    void drawWeatherAccessory(TFT_eSPI &g, uint32_t now, const SceneInfo &scn, bool umbrellaOk, bool shadesOk)
    {
#if WX_ACCESSORIES
        WeatherExtra x;
        if (!weather_extra_get(x))
            return;
        const bool fcFresh = x.forecastValid && (uint32_t)(now - x.forecastMs) <= (uint32_t)WEATHER_STALE_MS;

        // UV: the first pair of sunglasses comes a few seconds after the UV became high, then once per period.
        // Only under a bright sky (not in rain, a thunderstorm or fog, whatever the model's UV index says), and the
        // UV index has a hysteresis (weather_mood.cpp), the same answer the persona uses for the squint.
        const bool brightSky = !scn.weatherKnown || wxIsBright(scn.code);
        const bool shadesCond = WX_UV_SHADES_ENABLED && fcFresh && scn.day && brightSky && wxIsHighUv(x.uvNow);
        if (shadesCond && !s_shadesWas)
            s_shadesT0 = now + 8000;
        s_shadesWas = shadesCond;

        // above the eyes: the umbrella (rain soon) or the sunglasses over them; never both
        bool aboveEyes = false;
        if (umbrellaOk && WX_RAIN_SOON_ENABLED && fcFresh && x.rainSoon)
        {
            // the umbrella stays until the rain has started (the weather code takes over then) plus a short grace
            const int32_t ms = (int32_t)(x.rainSoonAtMs - now);
            if (ms > -(int32_t)WX_RAIN_GRACE_MS && ms <= (int32_t)WX_RAIN_WARN_MIN * 60000)
            {
                drawUmbrella(g, now, ms, x.rainSoonSnow);
                aboveEyes = true;
            }
        }
        if (!aboveEyes && shadesCond)
        {
            int32_t t = (int32_t)(now - s_shadesT0);
            if (t >= 0)
            {
                const uint32_t DROP_DONE = 450 + 150, LIFT = 500; // as in drawUvShades(): fall + bounce, lift
                const uint32_t liftAt = WX_UV_SHADES_ON_MS - LIFT;
                t %= (int32_t)WX_UV_SHADES_PERIOD_MS;
                if ((uint32_t)t < WX_UV_SHADES_ON_MS)
                {
                    if (!shadesOk && (uint32_t)t >= DROP_DONE && (uint32_t)t < liftAt)
                    {
                        // the face is not calm any more: start the lift from where they are (they sit on the eyes now)
                        s_shadesT0 = now - liftAt;
                        t = (int32_t)liftAt;
                    }
                    if (shadesOk || (uint32_t)t >= liftAt)
                        drawUvShades(g, (uint32_t)t);
                }
            }
        }
#else
        (void)g;
        (void)now;
        (void)scn;
        (void)umbrellaOk;
        (void)shadesOk;
#endif
    }

} // namespace disp