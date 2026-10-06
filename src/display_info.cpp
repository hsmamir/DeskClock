// display_info.cpp - the info panel under the face: clock / date / weather pages (drawInfoPanel).
#include "display_internal.h"

namespace disp
{

// ---- Info panel de-dup / page transition state ----
DisplayMode lastMode = static_cast<DisplayMode>(-1);
uint32_t lastInfoSig = 0xFFFFFFFFu;
static bool transActive = false; // info panel is sliding in
static uint32_t transStartMs = 0;

static void drawModeDots(TFT_eSPI &g, DisplayMode shown, uint16_t accent, int16_t y)
{
    int idx = (int)shown;
    for (int i = 0; i < MODE_COUNT; i++)
    {
        int16_t x = W / 2 + (int16_t)((i - (MODE_COUNT - 1) / 2.0f) * u(10));
        if (i == idx)
            g.fillCircle(x, y, imax(2, u(2.5f)), accent);
        else
            g.fillCircle(x, y, imax(1, u(1.5f)), tint(accent, 0.27f));
    }
}

void drawInfoPanel(DisplayMode mode, bool alarm, bool haveTime, const struct tm &ti, uint32_t now)
{
#if ALARM_ICON_ENABLED
    pollAlarmCfg(now);
#endif
    DisplayMode shown = alarm ? DisplayMode::CLOCK : mode; // an alarm always shows the time
    uint8_t colonLevel = colonLevelNow(now);

    // ---- clock digits: spot a changed digit and start its slide animation ----
    int8_t cd[4] = {0, 0, 0, 0};
    bool digAnimating = false;
    if (shown == DisplayMode::CLOCK && haveTime)
    {
        cd[0] = (int8_t)(ti.tm_hour / 10);
        cd[1] = (int8_t)(ti.tm_hour % 10);
        cd[2] = (int8_t)(ti.tm_min / 10);
        cd[3] = (int8_t)(ti.tm_min % 10);
        bool fresh = (lastMode != DisplayMode::CLOCK) || digCur[0] < 0; // came from another page / first time
        for (uint8_t i = 0; i < 4; i++)
        {
            if (fresh)
            {
                digCur[i] = digPrev[i] = cd[i];
                digChangeMs[i] = 0;
            }
            else if (cd[i] != digCur[i])
            {
                digPrev[i] = digCur[i];
                digCur[i] = cd[i];
                digChangeMs[i] = (CLOCK_DIGIT_ANIM && digSprOk) ? now : 0;
            }
            if (digChangeMs[i] != 0)
            {
                if (now - digChangeMs[i] < (uint32_t)CLOCK_DIGIT_ANIM_MS)
                    digAnimating = true;
                else
                    digChangeMs[i] = 0; // finished: from now on a plain static digit
            }
        }
    }
    else
        digCur[0] = -1; // no time shown: the next time we show one it appears without animating

    bool wValid = false;
    float tempC = 0;
    int wCode = 0;
    uint32_t wUpdatedMs = 0;
    if (shown == DisplayMode::WEATHER)
        readWeather(wValid, tempC, wCode, wUpdatedMs);
    uint32_t wAgeMs = wValid ? ageSince(millis(), wUpdatedMs) : 0;
    bool wStale = wValid && wAgeMs > WEATHER_STALE_MS; // offline / fetches failing: reading is old

    // Weather page: opens on the big temperature, then alternates with the details view.
    static uint32_t weatherEnterMs = 0;
    if (shown == DisplayMode::WEATHER && lastMode != DisplayMode::WEATHER)
        weatherEnterMs = now;
    WeatherDetail wd = {};
    bool detailView = false;
    bool twoViews = false; // both views available -> show the view indicator
    if (WEATHER_DETAILS_ENABLED && shown == DisplayMode::WEATHER)
    {
        readWeatherDetail(wd);
        twoViews = wd.valid && wValid;
        detailView = twoViews && (((now - weatherEnterMs) / WEATHER_PAGE_SWITCH_MS) & 1);
    }

    // ---- signature: only redraw when something visible changed ----
    uint32_t sig = 0;
    switch (shown)
    {
    case DisplayMode::CLOCK:
        if (haveTime)
            sig = (1u << 28) | (uint32_t)(ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec) | // bits 0-16
                  (alarmFlashOn ? (1u << 17) : 0u) | ((uint32_t)colonLevel << 18) |         // colon "breath"
                  (digAnimating ? ((((now / 33) & 0x1Fu) + 1u) << 22) : 0u);                // redraw every frame while a digit slides
        else
            sig = (2u << 28) | (now / 90);
        break;
    case DisplayMode::DATE:
        sig = (3u << 28) | (haveTime ? (uint32_t)(ti.tm_mday | (ti.tm_mon << 5) | ((ti.tm_year & 0x7F) << 9)) : 0u) |
              ((themeStamp & 0xFFFu) << 16); // theme moved -> redraw
        break;
    default:
        sig = (detailView ? (6u << 28) | ((now / 1000) & 0xFF) : (4u << 28) | ((now / 120) & 0xFF)) | // details don't animate
              (((uint32_t)((int)lroundf(tempC) + 128) & 0xFF) << 8) |
              (((uint32_t)wCode & 0x7F) << 16) | (wValid ? (1u << 23) : 0u);
        break;
    }
    bool modeChanged = (shown != lastMode);
    // start a slide-in between two normal pages (not from boot / the diagnostics page / an alarm)
    if (TRANSITION_ENABLED && modeChanged && !alarm && infoCv.ok &&
        (int)lastMode >= 0 && lastMode != DisplayMode::DIAG)
    {
        transActive = true;
        transStartMs = now;
    }
    if (!transActive && !modeChanged && sig == lastInfoSig)
        return; // nothing visible changed (while sliding, every frame is redrawn)
    lastMode = shown;
    lastInfoSig = sig;

    uint16_t accent = shown == DisplayMode::CLOCK ? C_ACCENT : (shown == DisplayMode::DATE ? C_AMBER : C_SOFT);
    if (shown == DisplayMode::WEATHER && wValid)
        accent = wStale ? tint(tempColor(tempC), 0.43f) : tempColor(tempC); // stale = dimmed
    if (alarm)
        accent = alarmFlashOn ? C_RED : tint(C_RED, 0.35f);

    TFT_eSPI &g = infoCv.begin();
    g.fillRect(0, 0, W, infoH, C_BG);

    // accent line with a soft glow underneath
    g.fillRect(0, 0, W, imax(2, u(2)), accent);
    g.fillRect(0, imax(2, u(2)), W, imax(1, u(1)), tint(accent, 0.235f));

    int16_t dh = (int16_t)(infoH * 0.42f);
    int16_t dw = (int16_t)(dh * 0.56f);
    int16_t gap = imax(2, u(3));
    int16_t dy = (int16_t)(infoH * 0.10f);
    int16_t capY = (int16_t)(infoH * 0.68f);
    char cap[24] = "";

    if (shown == DisplayMode::CLOCK)
    {
        if (haveTime)
        {
            int16_t cw = imax(4, u(8));
            int16_t total = 4 * dw + 2 * gap + 2 * gap + cw;
            int16_t x0 = (W - total) / 2;
            uint16_t on = alarm ? (alarmFlashOn ? C_RED : tint(C_RED, 0.35f)) : C_ACCENT;
            int16_t xs[4] = {x0, (int16_t)(x0 + dw + gap),
                             (int16_t)(x0 + 2 * dw + gap + 2 * gap + cw),
                             (int16_t)(x0 + 2 * dw + gap + 2 * gap + cw + dw + gap)};
            for (uint8_t i = 0; i < 4; i++)
            {
                if (digChangeMs[i] != 0 && digSprOk)
                {
                    float pr = (now - digChangeMs[i]) / (float)CLOCK_DIGIT_ANIM_MS;
                    drawAnimDigit(xs[i], dy, dw, dh, digPrev[i], digCur[i], pr > 1.0f ? 1.0f : pr, on);
                }
                else
                    drawSegDigit(g, xs[i], dy, dw, dh, digCur[i], on, C_GHOST);
            }
            drawColonGlow(g, x0 + 2 * dw + gap + gap + cw / 2, dy, dh, imax(2, dw / 4), on, colonLevel);
            strftime(cap, sizeof(cap), "%a %d %b", &ti);

            // seconds progress bar
            int16_t bx = u(12), bw = W - 2 * u(12), bh = imax(2, u(2.5f));
            int16_t by = infoH - u(15);
            g.fillRoundRect(bx, by, bw, bh, bh / 2, C_GHOST);
            int16_t fw = (int16_t)((int32_t)bw * (ti.tm_sec + 1) / 60);
            if (fw > 0)
                g.fillRoundRect(bx, by, fw, bh, bh / 2, accent);

#if ALARM_ICON_ENABLED
            if (alarmCfgOn || alarm)
                drawAlarmBadge(g, alarm, infoH - u(7), ti);
#endif
        }
        else
        {
            drawSpinner(g, W / 2, (int16_t)(infoH * 0.38f), u(10), accent);
            strcpy(cap, "Syncing...");
        }
    }
    else if (shown == DisplayMode::DATE)
    {
        if (haveTime)
        {
            int8_t d[2] = {(int8_t)(ti.tm_mday / 10), (int8_t)(ti.tm_mday % 10)};
            int16_t dayW = 2 * dw + gap;
            int16_t x0 = (int16_t)(W * 0.10f);
            drawSegNumber(g, x0, dy, dw, dh, gap, d, 2, C_AMBER);
            // thin divider
            int16_t divX = x0 + dayW + u(9);
            g.drawFastVLine(divX, dy + 2, dh - 4, tint(C_AMBER, 0.27f));
            char mon[8];
            strftime(mon, sizeof(mon), "%b", &ti);
            for (char *p = mon; *p; p++)
                if (*p >= 'a' && *p <= 'z')
                    *p -= 32;
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_AMBER);
            g.drawString(mon, divX + (W - divX) / 2, dy + dh / 2, 4);
            strftime(cap, sizeof(cap), "%A", &ti);
        }
        else
        {
            drawSpinner(g, W / 2, (int16_t)(infoH * 0.38f), u(10), accent);
            strcpy(cap, "Syncing...");
        }
    }
    else // WEATHER
    {
        if (wValid && detailView)
        {
            drawWeatherDetails(g, wd, accent);
        }
        else if (wValid)
        {
            int16_t r = u(14);
            drawWeatherIcon(g, wCode, (int16_t)(W * 0.22f), dy + dh / 2, r, now);

            drawTempReadout(g, tempC, dw, dh, gap, dy, accent);

            strncpy(cap, weather_code_to_label(wCode), sizeof(cap) - 1);
            cap[sizeof(cap) - 1] = '\0';

            if (wStale)
            {
                // "Clear 3h ago" (drops "ago" if it would not fit), plus a small clock badge
                char age[8];
                fmtAge(age, sizeof(age), wAgeMs);
                char full[24];
                snprintf(full, sizeof(full), "%s %s ago", cap, age);
                g.setTextDatum(MC_DATUM);
                if (g.textWidth(full, 2) <= W - u(4))
                    strcpy(cap, full);
                else
                    snprintf(cap, sizeof(cap), "%s %s", weather_code_to_label(wCode), age);

                int16_t bx = W - u(10), by = infoH - u(7), br = imax(3, u(4));
                g.drawCircle(bx, by, br, C_AMBER);
                g.drawLine(bx, by, bx, by - br + 1, C_AMBER);
                g.drawLine(bx, by, bx + br - 1, by, C_AMBER);
            }
        }
        else
        {
            drawCloud(g, W / 2, dy + dh / 2, u(14), tint(C_SOFT, 0.47f));
            strcpy(cap, "No weather yet");
        }
    }

    if (cap[0])
    {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(cap, W / 2, capY, 2);
    }
    drawModeDots(g, shown, accent, infoH - u(5));
    if (twoViews)
        drawViewDots(g, detailView ? 1 : 0, accent, infoH - u(5));

    int16_t slideX = 0;
    if (transActive)
    {
        uint32_t el = now - transStartMs;
        if (el >= (uint32_t)TRANSITION_MS)
            transActive = false; // last frame lands exactly at x = 0
        else
        {
            float inv = 1.0f - el / (float)TRANSITION_MS; // ease-out: fast start, soft landing
            slideX = (int16_t)(W * inv * inv * inv);
        }
    }
    infoCv.endAt(slideX);
}

} // namespace disp
