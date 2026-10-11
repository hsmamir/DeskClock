// display_pages.cpp - page flow: the slide between pages, the strip between the two cards, and drawFullPage().
//
// Only the clock page shows the face (eyes) above the info card. The Date and Weather pages are ONE full-screen
// page: its upper half is drawn into faceCv, its lower half into infoCv (the same two canvases, so no extra RAM),
// and the 3 px strip between the cards gets the page background instead of the frame colour. The contents of the
// pages are in display_page_date.cpp and display_page_weather.cpp.
#include "display_internal.h"

namespace disp
{

    // ---- Slide between pages ----
    int16_t pageSlideX = 0; // offset of the page that is sliding in; 0 = settled
    bool gapIsFull = false; // what gapPaint() last painted (page background = true, frame colour = false)

    static bool slideOn = false;    // a slide is running (it ends on the frame that lands at x = 0)
    static bool slideFrame = false; // this frame belongs to a slide, the landing frame included
    static uint32_t slideStartMs = 0;
    static uint32_t slideMs = TRANSITION_MS;

    bool isFullPage(DisplayMode m)
    {
        return m == DisplayMode::DATE || m == DisplayMode::WEATHER;
    }

    bool pageSliding()
    {
        return slideFrame;
    }

    // Sets pageSlideX for this frame: ease-out, fast start, soft landing; the last frame lands exactly at x = 0.
    static void slideCompute(uint32_t now)
    {
        pageSlideX = 0;
        slideFrame = slideOn;
        if (!slideOn)
            return;
        uint32_t el = now - slideStartMs;
        if (el >= slideMs)
        {
            slideOn = false; // landing frame: x = 0
            return;
        }
        float inv = 1.0f - el / (float)slideMs;
        pageSlideX = (int16_t)(W * inv * inv * inv);
    }

    void pageSlideStart(uint32_t now, uint32_t ms)
    {
        slideOn = true;
        slideStartMs = now;
        slideMs = ms;
        slideCompute(now);
    }

    // When the page has finished sliding in (animations that start on arrival wait for this); `now` if it is not sliding.
    uint32_t pageSlideSettleMs(uint32_t now)
    {
        return slideFrame ? slideStartMs + slideMs : now;
    }

    // Once per tick, before anything is drawn. lastMode is still the page that was on screen last frame (the
    // drawers update it), so this sees the page change. No slide from boot, from / to the diagnostics page
    // or because of an alarm.
    void pageFlowUpdate(DisplayMode shown, bool alarm, uint32_t now)
    {
        if (TRANSITION_ENABLED && shown != lastMode && !alarm && faceCv.ok && infoCv.ok &&
            (int)lastMode >= 0 && lastMode != DisplayMode::DIAG)
        {
            slideOn = true;
            slideStartMs = now;
            slideMs = TRANSITION_MS;
        }
        slideCompute(now);
    }

    // ---- The strip between the two cards ----
    // Clock layout: frame colour (two separate cards). Full-screen page: the page background, with a thin line
    // in the middle, so the two halves read as one page. dx = how far the page is still to slide in: only the part
    // the new page already covers is repainted.
    //
    // No flicker: the strip is repainted only when its look really changed (layout, background, frame or line
    // colour), not on every redraw of the page, and every pixel is written once, straight in its final colour
    // (the old "fill the strip, then draw the line over it" showed the bare strip for a moment, most visibly at
    // the two ends of the line).
    static bool gapSigOk = false; // what is on screen in the strip right now is described by the values below
    static bool gapSigFull = false;
    static uint16_t gapSigBg = 0, gapSigFrame = 0, gapSigLine = 0;

    void gapInvalidate()
    {
        gapSigOk = false; // something wiped the strip: the next gapPaint() must repaint it
    }

    void gapPaint(int16_t dx)
    {
        if (cardGap > 0)
        {
            int16_t gy = faceY + faceH;
            int16_t x = cardX + (dx > 0 ? dx : 0);
            int16_t right = cardX + W;
            uint16_t lineCol = tint(C_SOFT, 0.3f);
            bool same = gapSigOk && gapSigFull == fullLayout && gapSigBg == C_BG && gapSigFrame == C_FRAME &&
                        gapSigLine == lineCol;
            if (x < right && !same)
            {
                if (fullLayout)
                {
                    bool line = false;
#if FULLPAGE_DIVIDER
                    line = cardGap >= 3;
#endif
                    if (!line)
                        tft.fillRect(x, gy, right - x, cardGap, C_BG);
                    else
                    {
                        int16_t ly = gy + cardGap / 2; // the row of the line
                        int16_t x1 = imax(x, (int16_t)(cardX + u(8)));
                        int16_t x2 = (int16_t)(right - u(8));
                        tft.startWrite();
                        if (ly > gy)
                            tft.fillRect(x, gy, right - x, ly - gy, C_BG); // above the line
                        if (x2 > x1)
                        {
                            if (x1 > x)
                                tft.fillRect(x, ly, x1 - x, 1, C_BG);    // left of the line
                            tft.drawFastHLine(x1, ly, x2 - x1, lineCol); // the line
                            tft.fillRect(x2, ly, right - x2, 1, C_BG);   // right of the line
                        }
                        else
                            tft.fillRect(x, ly, right - x, 1, C_BG);
                        if (gy + cardGap > ly + 1)
                            tft.fillRect(x, ly + 1, right - x, gy + cardGap - (ly + 1), C_BG); // below the line
                        tft.endWrite();
                    }
                }
                else
                    tft.fillRect(x, gy, right - x, cardGap, C_FRAME);
            }
            if (dx == 0)
            {
                gapSigOk = true;
                gapSigFull = fullLayout;
                gapSigBg = C_BG;
                gapSigFrame = C_FRAME;
                gapSigLine = lineCol;
            }
        }
        if (dx == 0)
            gapIsFull = fullLayout;
    }

    // Coloured line + soft glow along the top edge of a page (the same look as the clock page).
    void drawPageAccent(TFT_eSPI &g, uint16_t accent)
    {
        g.fillRect(0, 0, W, imax(2, u(2)), accent);
        g.fillRect(0, imax(2, u(2)), W, imax(1, u(1)), tint(accent, 0.235f));
    }

    // ---- The full-screen pages ----
    void drawFullPage(DisplayMode shown, bool haveTime, const struct tm &ti, uint32_t now)
    {
        // ---- weather state (only the Weather page needs it) ----
        WeatherPage wp = {};
        wp.now = now;
        wp.haveTime = haveTime;
        wp.ti = &ti;

        // The weather page opens on the "now" view, then alternates with the forecast view.
        static uint32_t weatherEnterMs = 0;
        static bool lastForecastView = false;
        static uint32_t forecastStartMs = 0; // when the forecast view landed = when its bars start to grow
        bool forecastView = false;
        bool viewChanged = false; // the weather page just swapped between its two views (not a page change)

        if (shown == DisplayMode::WEATHER)
        {
            uint32_t wUpdatedMs = 0;
            readWeather(wp.valid, wp.tempC, wp.code, wUpdatedMs);
            wp.ageMs = wp.valid ? ageSince(millis(), wUpdatedMs) : 0;
            wp.stale = wp.valid && wp.ageMs > WEATHER_STALE_MS; // offline / fetches failing: the reading is old
            if (lastMode != DisplayMode::WEATHER)
                weatherEnterMs = now;
            if (WEATHER_DETAILS_ENABLED)
            {
                readWeatherDetail(wp.wd);
                wp.twoViews = wp.wd.valid && wp.valid;
                forecastView = wp.twoViews && (((now - weatherEnterMs) / WEATHER_PAGE_SWITCH_MS) & 1);
            }
            viewChanged = lastMode == DisplayMode::WEATHER && forecastView != lastForecastView;
            lastForecastView = forecastView;
        }
        else
            lastForecastView = false;
        wp.forecastView = forecastView;

        // ---- signature: only redraw when something visible changed ----
        uint32_t sig = 0;
        if (shown == DisplayMode::DATE)
        {
            sig = (3u << 28) | (haveTime ? (uint32_t)(ti.tm_mday | (ti.tm_mon << 5) | ((ti.tm_year & 0x7F) << 9)) : 0u) |
                  ((themeStamp & 0xFFFu) << 16); // theme moved -> redraw
        }
        else
        {
            // every frame while the bars grow / the temperature rolls, otherwise as slowly as before
            bool growing = forecastView && (int32_t)(now - forecastStartMs) < (int32_t)barsGrowTotalMs(wp.wd.barCount);
            bool rolling = !forecastView && wp.valid && tempRollActive(wp.tempC, now);
            uint32_t tick = forecastView ? (growing ? now / 33 : now / 1000) : (rolling ? now / 33 : now / 120);
            sig = ((forecastView ? 6u : 4u) << 28) | (tick & 0xFF) |
                  (((uint32_t)((int)lroundf(wp.tempC) + 128) & 0xFF) << 8) |
                  (((uint32_t)wp.code & 0x7F) << 16) | (wp.valid ? (1u << 23) : 0u);
        }
        sig = (sig * 131u) ^ (uint32_t)g_state.modeHoldPct; // the "hold for diagnostics" ring is drawn over the page

        if (WEATHER_VIEW_TRANSITION && viewChanged && faceCv.ok && infoCv.ok)
            pageSlideStart(now, WEATHER_VIEW_TRANSITION_MS); // the other weather view slides in the same way
        uint32_t settleMs = pageSlideSettleMs(now);
        if (viewChanged && forecastView)
            forecastStartMs = settleMs;

        bool modeChanged = (shown != lastMode);
        if (!pageSliding() && !modeChanged && sig == lastInfoSig)
            return; // nothing visible changed (while sliding, every frame is redrawn)
        lastMode = shown;
        lastInfoSig = sig;

        uint16_t accent = C_AMBER;
        if (shown == DisplayMode::WEATHER)
        {
            accent = C_SOFT;
            if (wp.valid)
                accent = wp.stale ? tint(tempColor(wp.tempC), 0.43f) : tempColor(wp.tempC); // stale = dimmed
        }
        wp.accent = accent;
        wp.settleMs = settleMs;
        wp.growMs = (int32_t)(now - forecastStartMs);

        // ---- upper half: the upper card ----
        TFT_eSPI &gt = faceCv.begin();
        gt.fillRect(0, 0, W, faceH, C_BG);
        drawPageAccent(gt, accent);
        if (shown == DisplayMode::DATE)
            drawDatePageTop(gt, haveTime, ti);
        else
            drawWeatherPageTop(gt, wp);
        drawHoldRing(gt);
        faceCv.endAt(pageSlideX, CORNERS_TOP);

        // ---- lower half: the lower card ----
        TFT_eSPI &gb = infoCv.begin();
        gb.fillRect(0, 0, W, infoH, C_BG);
        if (shown == DisplayMode::DATE)
            drawDatePageBottom(gb, haveTime, ti);
        else
            drawWeatherPageBottom(gb, wp);
        drawModeDots(gb, shown, accent, infoH - u(5));
        if (wp.twoViews && shown == DisplayMode::WEATHER)
            drawViewDots(gb, forecastView ? 1 : 0, accent, infoH - u(5));
        infoCv.endAt(pageSlideX, CORNERS_BOT);

        gapPaint(pageSlideX);
    }

} // namespace disp