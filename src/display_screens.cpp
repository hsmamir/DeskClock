// display_screens.cpp - full-screen setup (WiFi portal) and OTA screens, the WiFi-lost icon and the hold ring.
#include "display_internal.h"
#include <WiFi.h>

namespace disp
{

    // =====================================================================
    //  Setup screen (WiFiManager portal)
    // =====================================================================
    int otaDrawnPct = -1; // last progress drawn by drawOtaScreen(); display_tick() resets it

    static void drawWifiIcon(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
    {
        g.fillCircle(cx, cy, imax(2, r / 7), c);
        for (int k = 1; k <= 3; k++)
        {
            float rad = r * k / 3.0f;
            for (int a = -45; a <= 45; a += 3)
            {
                float t = a * (float)M_PI / 180.0f;
                g.fillCircle(cx + (int16_t)(rad * sinf(t)), cy - (int16_t)(rad * cosf(t)), imax(1, r / 14), c);
            }
        }
    }

    void drawPortalScreen()
    {
        tft.fillScreen(C_BG);
        drawWifiIcon(tft, SW / 2, (int16_t)(H * 0.34f), u(26), C_ACCENT);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(C_WHITE);
        tft.drawString("WiFi setup", SW / 2, (int16_t)(H * 0.50f), 2);
        tft.setTextColor(C_SOFT);
        tft.drawString("Join network:", SW / 2, (int16_t)(H * 0.60f), 2);
        tft.setTextColor(C_AMBER);
        tft.drawString(WIFI_PORTAL_AP_NAME, SW / 2, (int16_t)(H * 0.72f), 4);
        tft.setTextColor(C_SOFT);
        tft.drawString("192.168.4.1", SW / 2, (int16_t)(H * 0.86f), 2);
    }

    // OTA progress screen: static part once per update, then only the bar and the
    // percentage when the value changes (nothing else may draw during a flash write).
    void drawOtaScreen(int pct)
    {
        if (otaDrawnPct < 0)
        {
            tft.fillScreen(C_BG);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(C_WHITE);
            tft.drawString("Updating", SW / 2, (int16_t)(H * 0.30f), 4);
            tft.setTextColor(C_SOFT);
            tft.drawString("Do not unplug", SW / 2, (int16_t)(H * 0.82f), 2);
        }
        if (pct == otaDrawnPct)
            return;
        otaDrawnPct = pct;

        int16_t bx = u(14), bw = SW - 2 * bx, bh = imax(6, u(8));
        int16_t by = (int16_t)(H * 0.46f);
        tft.fillRoundRect(bx, by, bw, bh, bh / 2, C_GHOST);
        int16_t fw = (int16_t)((int32_t)bw * pct / 100);
        if (fw > 0)
            tft.fillRoundRect(bx, by, fw, bh, imin(bh / 2, fw / 2), C_ACCENT);

        char txt[8];
        snprintf(txt, sizeof(txt), "%d%%", pct);
        int16_t py = (int16_t)(H * 0.62f);
        tft.fillRect(0, py - u(10), SW, u(20), C_BG);
        smoothText(tft, ClockSm18, CLOCKSM18_CAPH, txt, SW / 2, py, C_BASELINE, C_ACCENT, C_BG);
    }

    // =====================================================================
    //  Status icon (WiFi lost) — drawn over the face, top-right corner
    // =====================================================================
    // Only visible while the clock is offline: a dimmed WiFi symbol with a red
    // slash. Polled twice a second (WiFi.status() is cheap, but there is no need
    // to ask 30 times a second).
    static bool wifiOkNow = false; // cached WiFi.status() == WL_CONNECTED
    uint32_t wifiDownSinceMs = 0;  // 0 = connected; else millis() when it was first seen down

    void pollWifi(uint32_t now)
    {
        static uint32_t lastPollMs = 0;
        if (lastPollMs != 0 && now - lastPollMs < 500)
            return;
        lastPollMs = now ? now : 1;
        wifiOkNow = (WiFi.status() == WL_CONNECTED);
        if (wifiOkNow)
            wifiDownSinceMs = 0;
        else if (wifiDownSinceMs == 0)
            wifiDownSinceMs = now ? now : 1;
    }

    void drawStatusIcons(TFT_eSPI &g, uint32_t now)
    {
        (void)now;
        if (wifiOkNow)
            return;

        int16_t r = u(7);
        int16_t cx = W - u(13), cy = u(15);
        drawWifiIcon(g, cx, cy, r, dimColor(C_SOFT, 170));
        // slash: bg-coloured shadow first so it stays readable over the arcs
        g.drawLine(cx - r, cy - r - 1, cx + r, cy + 3, C_BG);
        g.drawLine(cx - r, cy - r, cx + r, cy + 4, C_RED);
        g.drawLine(cx - r + 1, cy - r, cx + r + 1, cy + 4, C_RED);
    }

    // Ring that fills while the knob button is held toward the diagnostics page
    // (g_state.modeHoldPct, set by touch.cpp). Drawn over the middle of the face.
    void drawHoldRing(TFT_eSPI &g)
    {
        if (!MODE_HOLD_RING)
            return;
        uint8_t pct = g_state.modeHoldPct;
        if (pct == 0)
            return;

        int16_t cx = W / 2, cy = faceH / 2;
        int16_t r = u(17);
        int16_t dot = imax(1, u(1.8f));
        g.fillCircle(cx, cy, r + dot + u(3), C_BG);
        g.drawCircle(cx, cy, r + dot + u(3), C_GHOST);

        const int N = 40; // dots around the ring, starting at 12 o'clock, clockwise
        int filled = (pct * N + 50) / 100;
        for (int i = 0; i < N; i++)
        {
            float a = -(float)M_PI / 2.0f + i * 2.0f * (float)M_PI / N;
            int16_t x = cx + (int16_t)lroundf(r * cosf(a)), y = cy + (int16_t)lroundf(r * sinf(a));
            g.fillCircle(x, y, dot, i < filled ? C_ACCENT : C_GHOST);
        }
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString("DIAG", cx, cy, sc >= 1.5f ? 2 : 1);
    }

} // namespace disp