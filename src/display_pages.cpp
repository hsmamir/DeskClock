// display_pages.cpp - page flow: the slide between pages, the strip between the two cards, and drawFullPage().
//
// Only the clock page shows the face (eyes) above the info card. The Date and Weather pages are ONE full-screen
// page: its upper half is drawn into faceCv, its lower half into infoCv (the same two canvases, so no extra RAM),
// and the 3 px strip between the cards gets the page background instead of the frame colour. The contents of the
// pages are in display_page_date.cpp and display_page_weather.cpp. The Weather page is twice as tall as the screen and
// scrolls with the rotary knob: this file owns the scroll position (it eases toward the step touch.cpp has set).
#include "display_internal.h"

namespace disp
{

    // ---- Slide between pages ----
    int16_t pageSlideX = 0; // offset of the page that is sliding in; 0 = settled, > 0 = coming in from the right, < 0 = from the left
    bool gapIsFull = false; // what gapPaint() last painted (page background = true, frame colour = false)

    static bool slideOn = false;    // a slide is running (it ends on the frame that lands at x = 0)
    static bool slideFrame = false; // this frame belongs to a slide, the landing frame included
    static uint32_t slideStartMs = 0;
    static uint32_t slideMs = TRANSITION_MS;
    static int8_t slideDir = 1; // +1: the new page comes in from the right (next page), -1: from the left (previous page)

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
        pageSlideX = (int16_t)(W * inv * inv * inv) * slideDir;
    }

    void pageSlideStart(uint32_t now, uint32_t ms)
    {
        slideOn = true;
        slideDir = 1;
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
            // Direction follows the knob: the pages form a ring CLOCK -> DATE -> WEATHER -> CLOCK. One step forward
            // (clockwise) = the new page comes in from the right, one step back (counter-clockwise) = from the left.
            int fwd = ((int)shown - (int)lastMode + 3) % 3; // 1 = next page, 2 = previous page
            slideDir = (fwd == 2) ? -1 : 1;
        }
        slideCompute(now);
    }

    // ---- The strip between the two cards ----
    // Clock layout: frame colour (two separate cards). Full-screen page: the page background (no divider line), so
    // the two halves read as one page. dx = how far the page is still to slide in (+ from the right, - from the
    // left): only the part the new page already covers is repainted.
    //
    // No flicker: the strip is repainted only when its look really changed (layout, background or frame colour),
    // not on every redraw of the page, and every pixel is written once, straight in its final colour.
    static bool gapSigOk = false; // what is on screen in the strip right now is described by the values below
    static bool gapSigFull = false;
    static uint16_t gapSigBg = 0, gapSigFrame = 0;
    static int16_t gapSigY = 0;

    void gapInvalidate()
    {
        gapSigOk = false; // something wiped the strip: the next gapPaint() must repaint it
    }

    void gapPaint(int16_t dx)
    {
        if (cardGap > 0)
        {
            int16_t gy = faceY + faceH;
            int16_t x = cardX + (dx > 0 ? dx : 0);         // dx > 0: the page covers [cardX + dx, cardX + W)
            int16_t right = cardX + W + (dx < 0 ? dx : 0); // dx < 0: the page covers [cardX, cardX + W + dx)
            bool same = gapSigOk && gapSigFull == fullLayout && gapSigBg == C_BG && gapSigFrame == C_FRAME &&
                        gapSigY == gy;
            if (x < right && !same)
            {
                if (fullLayout)
                    tft.fillRect(x, gy, right - x, cardGap, C_BG);
                else
                    tft.fillRect(x, gy, right - x, cardGap, C_FRAME);
            }
            if (dx == 0)
            {
                gapSigOk = true;
                gapSigFull = fullLayout;
                gapSigBg = C_BG;
                gapSigFrame = C_FRAME;
                gapSigY = gy;
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

    // ---- Weather page scroll ----
    // touch.cpp sets the step the knob has scrolled to (g_state.weatherScrollStep: 0 = top .. WEATHER_SCROLL_STEPS =
    // bottom); the pixel offset below glides toward it. The page is twice the height of the screen's page area, so
    // the largest offset is one page height and every step is an equal share of it.
    static float scrollF = 0;        // current offset, px (0 = top of the page)
    static uint32_t scrollMs = 0;    // when it was last advanced
    static bool barsOn = false;      // the hourly bars are (about to be) in view
    static uint32_t barsStartMs = 0; // when they started to grow

    static int readWeatherScrollStep()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        int s = g_state.weatherScrollStep;
        xSemaphoreGive(g_stateMutex);
        if (s > WEATHER_SCROLL_STEPS)
            s = WEATHER_SCROLL_STEPS;
        return s;
    }

    // ---- The full-screen pages ----
    void drawFullPage(DisplayMode shown, bool haveTime, const struct tm &ti, uint32_t now)
    {
        // ---- weather state (only the Weather page needs it) ----
        WeatherPage wp = {};
        wp.now = now;
        wp.haveTime = haveTime;
        wp.ti = &ti;

        const float pageH = (float)(faceH + infoH);
        bool scrollMoving = false; // the page is gliding: redraw every frame
        bool tempInView = false;   // the big temperature (top of the page) is on screen

        if (shown == DisplayMode::WEATHER)
        {
            uint32_t wUpdatedMs = 0;
            readWeather(wp.valid, wp.tempC, wp.code, wUpdatedMs);
            wp.ageMs = wp.valid ? ageSince(millis(), wUpdatedMs) : 0;
            wp.stale = wp.valid && wp.ageMs > WEATHER_STALE_MS; // offline / fetches failing: the reading is old
            if (WEATHER_DETAILS_ENABLED)
            {
                readWeatherDetail(wp.wd);
                wp.scrollable = wp.wd.valid && wp.valid;
            }
            wp.maxScrollPx = wp.scrollable ? (int16_t)pageH : (int16_t)0;

            // where the knob wants the page, and the glide toward it
            float target = wp.scrollable ? pageH * (float)readWeatherScrollStep() / (float)WEATHER_SCROLL_STEPS : 0.0f;
            if (lastMode != DisplayMode::WEATHER)
            {
                scrollF = target; // the page opens right where the knob left it (no glide from the old position)
                scrollMs = now;
                barsOn = false;
            }
            else
            {
                uint32_t dt = now - scrollMs;
                if (dt > 100)
                    dt = 100;
                scrollMs = now;
                float diff = target - scrollF;
                if (WEATHER_SCROLL_EASE_MS <= 0 || fabsf(diff) < 0.6f)
                    scrollF = target;
                else
                    scrollF += diff * (1.0f - expf(-(float)dt / (float)WEATHER_SCROLL_EASE_MS)); // exponential ease-out
                scrollMoving = (scrollF != target);
            }
            wp.scrollPx = (int16_t)lroundf(scrollF);

            // the hourly bars grow when they scroll into view, and start over after they left it
            if (!wp.scrollable)
                barsOn = false;
            else if (!barsOn && target >= pageH * 0.25f - 0.5f)
            {
                barsOn = true;
                barsStartMs = pageSlideSettleMs(now); // wait for the page to land if it is still sliding in
            }
            else if (barsOn && scrollF < pageH * 0.10f)
                barsOn = false; // fully scrolled out of view
            wp.growMs = barsOn ? (int32_t)(now - barsStartMs) : 0;
            tempInView = scrollF < (float)faceH;
        }
        else
            barsOn = false;

        // ---- signature: only redraw when something visible changed ----
        uint32_t sig = 0;
        if (shown == DisplayMode::DATE)
        {
            sig = (3u << 28) | (haveTime ? (uint32_t)(ti.tm_mday | (ti.tm_mon << 5) | ((ti.tm_year & 0x7F) << 9)) : 0u) |
                  ((themeStamp & 0xFFFu) << 16); // theme moved -> redraw
        }
        else
        {
            // every frame while the page glides / the bars grow / the temperature rolls, otherwise as slowly as before
            // (the weather icon on top of the page animates, so it needs more redraws than the rest of the page)
            bool growing = barsOn && (int32_t)(now - barsStartMs) < (int32_t)barsGrowTotalMs(wp.wd.barCount);
            bool rolling = tempInView && wp.valid && tempRollActive(wp.tempC, now);
            uint32_t tick = (scrollMoving || growing || rolling) ? now / 33 : (tempInView ? now / 120 : now / 1000);
            sig = (4u << 28) | (tick & 0xFF) |
                  (((uint32_t)((int)lroundf(wp.tempC) + 128) & 0xFF) << 8) |
                  (((uint32_t)wp.code & 0x7F) << 16) | (wp.valid ? (1u << 23) : 0u);
            sig = (sig * 131u) ^ ((uint32_t)wp.scrollPx & 0x3FFu) ^ (wp.scrollable ? 0x400u : 0u);
        }
        sig = (sig * 131u) ^ (uint32_t)g_state.modeHoldPct; // the "hold for diagnostics" ring is drawn over the page

        uint32_t settleMs = pageSlideSettleMs(now);

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

        // ---- upper half: the upper card ----
        TFT_eSPI &gt = faceCv.begin();
        gt.fillRect(0, 0, W, faceH, C_BG);
        if (shown == DisplayMode::DATE)
        {
            drawPageAccent(gt, accent);
            drawDatePageTop(gt, haveTime, ti);
        }
        else
        {
            drawWeatherPageTop(gt, wp);
            drawPageAccent(gt, accent); // fixed at the top edge: the page scrolls beneath it
        }
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
        infoCv.endAt(pageSlideX, CORNERS_BOT);

        gapPaint(pageSlideX);
    }

} // namespace disp