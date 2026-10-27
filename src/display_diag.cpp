// display_diag.cpp - the diagnostics page (IP, signal, free heap, last sync, firmware version + update).
//   portrait layout  (LANDSCAPE_UI 0): two stacked cards, plain rows
//   landscape layout (LANDSCAPE_UI 1): one full-screen page in the look of the Date / Weather pages (see the end of the file)
#include "display_internal.h"
#include "display_style.h"
#include <WiFi.h>
#include "boot_log.h"
#include "version.h"
#include "update_check.h"

namespace disp
{

    // =====================================================================
    //  Diagnostics page (full screen) - IP, signal, free heap, last sync
    // =====================================================================
    // Drawn into the two existing canvases (face area on top, info area below), so
    // it needs no extra RAM and does not flicker. The same content function runs
    // once per canvas with a y offset; anything outside a canvas is clipped.
    struct DiagData
    {
        bool wifi;
        char ip[20];
        int rssi;
        uint32_t heapKb;
        char sync[24]; // last NTP sync: "14:05 (12m)" or "none yet"
        bool syncOk;
        char weather[16]; // age of the last weather reading
        bool weatherOk;
        bool weatherStale;
        char uptime[16];
        char reset[12];   // why this boot happened: "Brownout", "Power on", ...
        bool resetBad;    // brownout / panic / watchdog
        char boots[16];   // "42" or "42 | BO 3" (BO = brownouts so far)
        bool bootsWarn;   // at least one brownout on record
        char fw[16];      // running firmware version ("1.2.0" or "dev")
        char upd[20];     // update status: "up to date", "new 1.3.0", ...
        uint8_t updLevel; // 0 = normal, 1 = a new version is ready (amber), 2 = problem (red), 3 = no info (soft)
    };
    uint32_t lastDiagDrawMs = 0;
    bool diagShown = false;

    static void collectDiag(DiagData &d, uint32_t now)
    {
        d.wifi = (WiFi.status() == WL_CONNECTED);
        d.rssi = 0;
        if (d.wifi)
        {
            IPAddress ip = WiFi.localIP();
            snprintf(d.ip, sizeof(d.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            d.rssi = WiFi.RSSI();
        }
        else
        {
            d.ip[0] = '\0';
        }
        d.heapKb = ESP.getFreeHeap() / 1024;

        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        uint32_t ntpEpoch = g_state.ntpSyncEpoch;
        uint32_t ntpMs = g_state.ntpSyncMs;
        bool wxValid = g_state.weatherValid;
        uint32_t wxMs = g_state.weatherUpdatedMs;
        xSemaphoreGive(g_stateMutex);

        char age[8];
        d.syncOk = ntpEpoch != 0;
        if (d.syncOk)
        {
            time_t t = (time_t)ntpEpoch;
            struct tm lt;
            localtime_r(&t, &lt);
            char hm[8];
            strftime(hm, sizeof(hm), "%H:%M", &lt);
            fmtAge(age, sizeof(age), ageSince(now, ntpMs));
            snprintf(d.sync, sizeof(d.sync), "%s (%s)", hm, age);
        }
        else
        {
            strcpy(d.sync, "none yet");
        }

        d.weatherOk = wxValid;
        d.weatherStale = false;
        if (wxValid)
        {
            uint32_t a = ageSince(now, wxMs);
            d.weatherStale = a > WEATHER_STALE_MS;
            fmtAge(age, sizeof(age), a);
            snprintf(d.weather, sizeof(d.weather), "%s ago", age);
        }
        else
        {
            strcpy(d.weather, "none yet");
        }

        uint32_t s = now / 1000;
        uint32_t days = s / 86400, hours = (s / 3600) % 24, mins = (s / 60) % 60;
        if (days > 0)
            snprintf(d.uptime, sizeof(d.uptime), "%ud %02uh", (unsigned)days, (unsigned)hours);
        else if (hours > 0)
            snprintf(d.uptime, sizeof(d.uptime), "%uh %02um", (unsigned)hours, (unsigned)mins);
        else
            snprintf(d.uptime, sizeof(d.uptime), "%um", (unsigned)mins);

        // Boot-log values never change after boot_log_init(): read them once, not on every refresh.
        static bool s_bootRead = false;
        static char s_reset[12];
        static bool s_resetBad = false;
        static char s_boots[16];
        static bool s_bootsWarn = false;
        if (!s_bootRead)
        {
            s_bootRead = true;
            BootRecord cur;
            if (boot_log_history(&cur, 1) == 1)
            {
                strlcpy(s_reset, boot_log_reason_name(cur.reason), sizeof(s_reset));
                s_resetBad = boot_log_reason_abnormal(cur.reason);
            }
            else
            {
                strcpy(s_reset, "--");
            }
            uint32_t bo = boot_log_brownouts();
            s_bootsWarn = bo > 0;
            if (bo > 0)
                snprintf(s_boots, sizeof(s_boots), "%u | BO %u", (unsigned)boot_log_count(), (unsigned)bo);
            else
                snprintf(s_boots, sizeof(s_boots), "%u", (unsigned)boot_log_count());
        }
        strcpy(d.reset, s_reset);
        d.resetBad = s_resetBad;
        strcpy(d.boots, s_boots);
        d.bootsWarn = s_bootsWarn;
        strlcpy(d.fw, FW_VERSION, sizeof(d.fw));
#if defined(USE_WOKWI_WIFI) || !UPDATE_ENABLED
        strcpy(d.upd, "off");
        d.updLevel = 3;
#else
        UpdateStatus us = update_status();
        d.updLevel = 0;
        switch (us.state)
        {
        case UpdateState::AVAILABLE:
            snprintf(d.upd, sizeof(d.upd), "new %s", us.latest.c_str() + (us.latest.startsWith("v") ? 1 : 0)); // "new 1.3.0"
            d.updLevel = 1;
            break;
        case UpdateState::UP_TO_DATE:
            strcpy(d.upd, "up to date");
            break;
        case UpdateState::CHECKING:
            strcpy(d.upd, "checking");
            d.updLevel = 3;
            break;
        case UpdateState::DOWNLOADING:
            snprintf(d.upd, sizeof(d.upd), "install %u%%", (unsigned)us.percent);
            d.updLevel = 1;
            break;
        case UpdateState::DONE:
            strcpy(d.upd, "restarting");
            d.updLevel = 1;
            break;
        case UpdateState::FAILED:
            strcpy(d.upd, "failed");
            d.updLevel = 2;
            break;
        default: // IDLE: no check has finished yet (the first automatic one runs a few minutes after boot)
            strcpy(d.upd, "none yet");
            d.updLevel = 3;
            break;
        }
#endif
    }

    static void drawDiagRow(TFT_eSPI &g, int16_t y, const char *label, const char *value, uint16_t valueColor)
    {
        g.setTextDatum(ML_DATUM);
        g.setTextColor(C_SOFT);
        g.drawString(label, u(6), y, 2);
        g.setTextDatum(MR_DATUM);
        g.setTextColor(valueColor);
        g.drawString(value, W - u(6), y, 2);
    }

    // yOff = screen y of this canvas' top edge; all coordinates below are screen
    // coordinates, shifted into the canvas.
    //
    // The page is drawn into the two cards (face area on top, info area below) but is ONE page: no strip between
    // them (see geometrySet), only the outer corners are rounded. A row must never sit across the join, so rows are
    // placed in two groups, none of them touches it:
    //   Yt(v) = v px from the top of the upper card (reference 128x160 display; scaled by u()),
    //   Yb(v) = v px from the top of the lower card.
    // Every row has the same pitch (13.5), also from the last upper row to the first lower one.
    // 9 rows + the IP line do not fit with a title line, so the page has no title (the accent bar on top
    // turns red when WiFi is down).
    static void drawDiagContent(TFT_eSPI &g, int16_t yOff, const DiagData &d)
    {
        auto Yt = [&](float v) -> int16_t
        { return (int16_t)(u(v) - yOff); };
        auto Yb = [&](float v) -> int16_t
        { return (int16_t)(faceH + u(v) - yOff); };

        uint16_t accent = d.wifi ? C_ICE : C_RED;
        drawTopBar(g, yOff, accent);

        // IP, centred on its own line (a full 192.168.xxx.xxx would not fit next to a label)
        g.setTextDatum(MC_DATUM);
        g.setTextColor(d.wifi ? C_ACCENT : C_RED);
        g.drawString(d.wifi ? d.ip : "Offline", W / 2, Yt(13), 2);
        drawDivider(g, u(6), W - u(6), Yt(22));

        char buf[24];
        uint16_t c;

        // ---- upper card ----
        // signal strength
        if (d.wifi)
        {
            snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
            c = d.rssi >= -60 ? rgb(90, 230, 130) : (d.rssi >= -75 ? C_AMBER : C_RED);
        }
        else
        {
            strcpy(buf, "--");
            c = C_RED;
        }
        drawDiagRow(g, Yt(35.5f), "Signal", buf, c);

        // free heap
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)d.heapKb);
        c = d.heapKb < 30 ? C_RED : (d.heapKb < 60 ? C_AMBER : C_WHITE);
        drawDiagRow(g, Yt(49), "Heap", buf, c);

        // last NTP sync (clock time + age)
        drawDiagRow(g, Yt(62.5f), "Sync", d.sync, d.syncOk ? C_WHITE : C_AMBER);

        // weather reading age
        drawDiagRow(g, Yt(76), "Weather", d.weather, d.weatherOk ? (d.weatherStale ? C_AMBER : C_WHITE) : C_AMBER);

        // ---- lower card ----
        // uptime
        drawDiagRow(g, Yb(7.5f), "Uptime", d.uptime, C_WHITE);

        // why this boot happened (kept in flash; red = brownout / panic / watchdog)
        drawDiagRow(g, Yb(21), "Reset", d.reset, d.resetBad ? C_RED : C_WHITE);

        // boot counter; "BO n" = brownouts so far (amber once there is one)
        drawDiagRow(g, Yb(34.5f), "Boots", d.boots, d.bootsWarn ? C_AMBER : C_WHITE);

        // firmware version, and whether a newer release is waiting (the owner installs it from the settings page)
        drawDiagRow(g, Yb(48), "Version", d.fw, C_WHITE);
        uint16_t uc = d.updLevel == 1 ? C_AMBER : (d.updLevel == 2 ? C_RED : (d.updLevel == 3 ? C_SOFT : rgb(90, 230, 130)));
        drawDiagRow(g, Yb(61.5f), "Update", d.upd, uc);
    }

    void drawDiagScreen(uint32_t now)
    {
        DiagData d;
        collectDiag(d, now);

        TFT_eSPI &gt = faceCv.begin();
        gt.fillRect(0, 0, W, faceH, C_BG);
        drawDiagContent(gt, 0, d);
        faceCv.endAt(0, CORNERS_TOP); // outer corners only: one page, not two cards

        TFT_eSPI &gb = infoCv.begin();
        gb.fillRect(0, 0, W, infoH, C_BG);
        drawDiagContent(gb, faceH, d);
        infoCv.endAt(0, CORNERS_BOT);

        gapInvalidate();
        gapIsFull = true; // one-piece page: the clock page then paints the strip between its cards again (see display.cpp)
    }

} // namespace disp
// =====================================================================
//  Landscape UI: the diagnostics page as one full-screen page (160 x 128)
// =====================================================================
// Same look as the Date and Weather pages (display_page_side.cpp): navy background, smooth Page fonts, sparkle divider,
// faint tinted pills for what needs attention.
//
//   y  0..22   header:  signal bars | IP address (or "Offline")                         dBm
//   y 23       sparkle divider
//   y 26..125  2 x 4 cells:  Heap | Sync        - a status dot + a small caption, the value under it
//                            Weather | Uptime     (green = fine, amber = look at it, red = problem, grey = no info)
//                            Reset | Boots        A cell that is amber / red also sits on a faint pill of that colour.
//                            Update | Version     Heap has a small bar next to its caption.
//
// Cheaper than before:
//   * the data is compared with the last drawn one and the screen is only drawn + pushed again when something that
//     is shown has changed (plus a safety refresh every 10 s and whenever the page was away);
//   * only 3 smooth-font loads per frame (all big text, then all small text) instead of one per string;
//   * the boot-log values are read once (collectDiag).
#if LANDSCAPE_UI
#include "display_landscape.h"
#include "display_fonts_pages.h"

namespace disp
{
    namespace
    {
        constexpr uint16_t C_OK = rgb(90, 230, 130);

        enum Lvl : uint8_t
        {
            LV_NONE, // no information / neutral
            LV_OK,
            LV_WARN,
            LV_BAD
        };

        // The small caption font: 12 px on the real display, 18 px on the bigger simulator screens (as on the other pages).
        inline const uint8_t *capFont() { return sc >= 1.5f ? PageSm18 : PageSm12; }
        inline int16_t capFontH() { return sc >= 1.5f ? PAGESM18_CAPH : PAGESM12_CAPH; }

        inline uint16_t lvlColor(Lvl l)
        {
            switch (l)
            {
            case LV_OK:
                return C_OK;
            case LV_WARN:
                return C_AMBER;
            case LV_BAD:
                return C_RED;
            default:
                return C_SOFT;
            }
        }

        // ---- change detection (FNV-1a over everything the page shows) ----
        inline uint32_t fnv(uint32_t h, const void *p, size_t n)
        {
            const uint8_t *b = (const uint8_t *)p;
            for (size_t i = 0; i < n; i++)
                h = (h ^ b[i]) * 16777619u;
            return h;
        }

        uint32_t diagSignature(const DiagData &d)
        {
            const uint32_t flags = (d.wifi ? 1u : 0u) | (d.syncOk ? 2u : 0u) | (d.weatherOk ? 4u : 0u) | (d.weatherStale ? 8u : 0u) |
                                   (d.resetBad ? 16u : 0u) | (d.bootsWarn ? 32u : 0u) | ((uint32_t)d.updLevel << 6);
            const uint32_t v[5] = {flags, (uint32_t)d.rssi, d.heapKb, (uint32_t)C_BG, (uint32_t)C_ACCENT};
            uint32_t h = fnv(2166136261u, v, sizeof(v));
            const char *s[8] = {d.ip, d.sync, d.weather, d.uptime, d.reset, d.boots, d.fw, d.upd};
            for (const char *p : s)
                h = fnv(h, p, strlen(p) + 1);
            return h;
        }

        // ---- the little signal icon: four bars, lit by how strong the WiFi is ----
        void drawSignalBars(TFT_eSPI &g, int16_t x, int16_t baseY, bool online, int rssi, uint16_t col)
        {
            const int lit = !online ? 0 : (rssi >= -55 ? 4 : (rssi >= -65 ? 3 : (rssi >= -75 ? 2 : 1)));
            const int16_t bw = imax(2, u(3)), gap = imax(1, u(1));
            const uint16_t off = online ? tint(C_SOFT, 0.30f) : tint(C_RED, 0.35f);
            for (int i = 0; i < 4; i++)
            {
                const int16_t h = u(3.0f + 2.5f * i);
                g.fillRoundRect(x + i * (bw + gap), baseY - h, bw, h, 1, i < lit ? col : off);
            }
        }

        inline int16_t signalBarsWidth() { return 4 * imax(2, u(3)) + 3 * imax(1, u(1)); }
    } // namespace

    void drawDiagScreenL(uint32_t now)
    {
        static uint32_t s_sig = 0, s_lastCall = 0, s_lastPush = 0;

        DiagData d;
        collectDiag(d, now);

        // Nothing shown has changed -> keep the pixels that are already on the screen. "Away" = this page was not
        // asked for in the last 700 ms (it is asked for every 500 ms while it is up), then the canvas holds another page.
        const bool away = (uint32_t)(now - s_lastCall) > 700;
        s_lastCall = now;
        const uint32_t sig = diagSignature(d);
        if (!away && sig == s_sig && (uint32_t)(now - s_lastPush) < 10000)
            return;
        s_sig = sig;
        s_lastPush = now;

        TFT_eSPI &g = faceCv.begin();
        g.fillRect(0, 0, W, H, C_BG);

        const uint16_t accent = C_ACCENT;
        const uint16_t white = neonCore(accent, 0.9f);
        const uint16_t lav = neonCore(accent, 0.6f);

        // ---- what to show: 2 x 4 cells, in reading order ----
        char heapBuf[12];
        snprintf(heapBuf, sizeof(heapBuf), "%u KB", (unsigned)d.heapKb);
        const Lvl heapLv = d.heapKb < 30 ? LV_BAD : (d.heapKb < 60 ? LV_WARN : LV_OK);
        const Lvl updLv = d.updLevel == 1 ? LV_WARN : (d.updLevel == 2 ? LV_BAD : (d.updLevel == 3 ? LV_NONE : LV_OK));

        struct Cell
        {
            const char *label;
            const char *value;
            Lvl lvl;
            uint16_t valCol; // colour of the value text
        };
        const Cell cells[8] = {
            {"Heap", heapBuf, heapLv, heapLv == LV_OK ? white : lvlColor(heapLv)},
            {"Sync", d.sync, d.syncOk ? LV_OK : LV_WARN, d.syncOk ? white : C_AMBER},
            {"Weather", d.weather, !d.weatherOk ? LV_WARN : (d.weatherStale ? LV_WARN : LV_OK), (!d.weatherOk || d.weatherStale) ? C_AMBER : white},
            {"Uptime", d.uptime, LV_NONE, white},
            {"Reset", d.reset, d.resetBad ? LV_BAD : LV_OK, d.resetBad ? C_RED : white},
            {"Boots", d.boots, d.bootsWarn ? LV_WARN : LV_NONE, d.bootsWarn ? C_AMBER : white},
            {"Update", d.upd, updLv, updLv == LV_WARN ? C_AMBER : (updLv == LV_BAD ? C_RED : (updLv == LV_NONE ? lav : white))},
            {"Version", d.fw, LV_NONE, white},
        };

        // ---- geometry ----
        const int16_t hdrCy = u(11);
        const int16_t pillW = W / 2 - u(5), pillH = u(24), pillR = u(6);
        const int16_t pillX[2] = {u(3), (int16_t)(W / 2 + u(2))};
        const int16_t textX[2] = {u(8), (int16_t)(W / 2 + u(7))};
        const int16_t y0 = u(26), pitch = u(25);
        const int16_t availW = pillW - (textX[0] - pillX[0]) - u(3); // room for a value inside its pill
        auto capCy = [&](int row) -> int16_t
        { return (int16_t)(y0 + row * pitch + u(4)); };
        auto valCy = [&](int row) -> int16_t
        { return (int16_t)(y0 + row * pitch + u(16)); };
        auto cellBg = [&](const Cell &c) -> uint16_t
        { return (c.lvl == LV_WARN || c.lvl == LV_BAD) ? tint(lvlColor(c.lvl), 0.16f) : C_BG; };

        // ---- shapes first (they must be under the text) ----
        const uint16_t wifiCol = !d.wifi ? C_RED : (d.rssi >= -60 ? C_OK : (d.rssi >= -75 ? C_AMBER : C_RED));
        drawSignalBars(g, u(8), hdrCy + u(5), d.wifi, d.rssi, wifiCol);
        drawDivider(g, u(8), W - u(8), u(23));

        for (int i = 0; i < 8; i++)
        {
            const int col = i & 1, row = i >> 1;
            const Cell &c = cells[i];
            if (c.lvl == LV_WARN || c.lvl == LV_BAD)
                g.fillRoundRect(pillX[col], y0 + row * pitch - u(1), pillW, pillH, pillR, cellBg(c));
            const uint16_t dc = lvlColor(c.lvl);
            const int16_t dr = imax(2, u(2));
            const int16_t dx = textX[col] + dr, dy = capCy(row);
            g.fillCircle(dx, dy, dr + 1, tint(dc, 0.25f)); // halo
            g.fillCircle(dx, dy, dr, dc);
        }
        { // heap: a small bar beside its caption (full = 160 KB)
            const int16_t bw = u(24), bh = imax(2, u(3));
            drawNeonBar(g, pillX[0] + pillW - u(5) - bw, capCy(0) - bh / 2, bw, bh, d.heapKb / 160.0f, lvlColor(heapLv), false);
        }

        // ---- text, big font: the IP address and every value that fits its column ----
        const int16_t ipX = u(8) + signalBarsWidth() + u(6);
        const int16_t ipRoom = W - u(8) - ipX - u(44); // keeps room for the dBm on the right
        bool ipSmall = false;
        bool fallback[8] = {false};
        g.loadFont(PageSm18);
        g.setTextDatum(L_BASELINE);
        {
            const char *ip = d.wifi ? d.ip : "Offline";
            if (g.textWidth(ip, 1) <= ipRoom)
            {
                g.setTextColor(d.wifi ? white : C_RED, C_BG);
                g.drawString(ip, ipX, hdrCy + PAGESM18_CAPH / 2);
            }
            else
                ipSmall = true;
        }
        for (int i = 0; i < 8; i++)
        {
            const int col = i & 1, row = i >> 1;
            const Cell &c = cells[i];
            if (g.textWidth(c.value, 1) > availW)
            {
                fallback[i] = true;
                continue;
            }
            g.setTextColor(c.valCol, cellBg(c));
            g.drawString(c.value, textX[col], valCy(row) + PAGESM18_CAPH / 2);
        }
        g.unloadFont();

        // ---- text, small font: dBm, captions, and the values that did not fit in the big one ----
        g.loadFont(capFont());
        g.setTextDatum(L_BASELINE);
        const int16_t ch = capFontH();
        if (ipSmall)
        {
            g.setTextColor(d.wifi ? white : C_RED, C_BG);
            g.drawString(d.wifi ? d.ip : "Offline", ipX, hdrCy + ch / 2);
        }
        if (d.wifi)
        {
            char db[12];
            snprintf(db, sizeof(db), "%d dBm", d.rssi);
            g.setTextDatum(R_BASELINE);
            g.setTextColor(wifiCol, C_BG);
            g.drawString(db, W - u(8), hdrCy + ch / 2);
            g.setTextDatum(L_BASELINE);
        }
        for (int i = 0; i < 8; i++)
        {
            const int col = i & 1, row = i >> 1;
            const Cell &c = cells[i];
            const uint16_t bg = cellBg(c);
            g.setTextColor(C_SOFT, bg);
            g.drawString(c.label, textX[col] + u(8), capCy(row) + ch / 2);
            if (fallback[i])
            {
                g.setTextColor(c.valCol, bg);
                g.drawString(c.value, textX[col], valCy(row) + ch / 2);
            }
        }
        g.unloadFont();

        faceCv.endAt(0);
    }
} // namespace disp
#endif