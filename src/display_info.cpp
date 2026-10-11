// display_info.cpp - the info panel under the face: the clock page (drawInfoPanel). Date and Weather are full-screen
// pages, see display_pages.cpp.
#include "display_internal.h"

namespace disp
{

    // ---- Info panel de-dup state ----
    DisplayMode lastMode = static_cast<DisplayMode>(-1);
    uint32_t lastInfoSig = 0xFFFFFFFFu;

    void drawModeDots(TFT_eSPI &g, DisplayMode shown, uint16_t accent, int16_t y)
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

    // No time yet: the unlit clock digits with a slowly breathing colon (instead of a spinner; the eyes
    // above do the "thinking").
    static void drawDormantClock(TFT_eSPI &g, int16_t dw, int16_t dh, int16_t gap, int16_t dy, uint16_t accent, uint32_t now)
    {
        int16_t cw = imax(4, u(8));
        int16_t total = 4 * dw + 2 * gap + 2 * gap + cw;
        int16_t x0 = (W - total) / 2;
        int16_t xs[4] = {x0, (int16_t)(x0 + dw + gap),
                         (int16_t)(x0 + 2 * dw + gap + 2 * gap + cw),
                         (int16_t)(x0 + 2 * dw + gap + 2 * gap + cw + dw + gap)};
        for (uint8_t i = 0; i < 4; i++)
            drawSegDigit(g, xs[i], dy, dw, dh, -1, accent, C_GHOST);
        uint8_t level = (uint8_t)(7.5f + 7.0f * sinf(now / 450.0f));
        drawColonGlow(g, x0 + 2 * dw + gap + gap + cw / 2, dy, dh, imax(2, dw / 4), tint(accent, 0.5f), level);
    }

    // The clock page: big digits, date line, seconds bar, alarm badge. Only drawn when the face card is above
    // it (an alarm always comes here, whatever page was open).
    void drawInfoPanel(bool alarm, bool haveTime, const struct tm &ti, uint32_t now)
    {
#if ALARM_ICON_ENABLED
        pollAlarmCfg(now);
#endif
        const DisplayMode shown = DisplayMode::CLOCK;
        uint8_t colonLevel = colonLevelNow(now);

        // ---- clock digits: spot a changed digit and start its slide animation ----
        int8_t cd[4] = {0, 0, 0, 0};
        bool digAnimating = false;
        if (haveTime)
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

        // ---- signature: only redraw when something visible changed ----
        uint32_t sig = 0;
        if (haveTime)
            sig = (1u << 28) | (uint32_t)(ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec) |              // bits 0-16
                  (alarmFlashOn ? (1u << 17) : 0u) | ((uint32_t)colonLevel << 18) |                      // colon "breath"
                  ((digAnimating || persona.digitPulse > 0) ? ((((now / 33) & 0x1Fu) + 1u) << 22) : 0u); // redraw every frame while a digit slides / flashes
        else
            sig = (2u << 28) | (now / 90);

        bool modeChanged = (shown != lastMode);
        if (!pageSliding() && !modeChanged && sig == lastInfoSig)
            return; // nothing visible changed (while sliding, every frame is redrawn)
        lastMode = shown;
        lastInfoSig = sig;

        uint16_t accent = C_ACCENT;
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
        bool capSmooth = false; // cap only holds glyphs of the smooth fonts (date, weekday)

        if (haveTime)
        {
            int16_t cw = imax(4, u(8));
            int16_t total = 4 * dw + 2 * gap + 2 * gap + cw;
            int16_t x0 = (W - total) / 2;
            uint16_t on = alarm ? (alarmFlashOn ? C_RED : tint(C_RED, 0.35f)) : mixColor(C_ACCENT, 0xFFFF, 0.6f * persona.digitPulse); // top of the hour: flash
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
            capSmooth = true;

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
            drawDormantClock(g, dw, dh, gap, dy, accent, now); // the eyes "think" about it up in the face card
            strcpy(cap, "Syncing...");
        }

        if (cap[0])
        {
            if (capSmooth)
            {
                bool big = sc >= 1.5f; // 12 px font on the 128 px display, 18 px on bigger ones
                smoothText(g, big ? ClockSm18 : ClockSm12, big ? CLOCKSM18_CAPH : CLOCKSM12_CAPH, cap, W / 2, capY,
                           C_BASELINE, C_SOFT, C_BG);
            }
            else
            {
                g.setTextDatum(MC_DATUM);
                g.setTextColor(C_SOFT);
                g.drawString(cap, W / 2, capY, 2);
            }
        }
        drawModeDots(g, shown, accent, infoH - u(5));

        infoCv.endAt(pageSlideX);
    }

} // namespace disp