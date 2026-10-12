// display_diag.cpp - the diagnostics page (IP, signal, free heap, last sync, firmware version + update).
#include "display_internal.h"
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

        BootRecord cur;
        if (boot_log_history(&cur, 1) == 1)
        {
            strlcpy(d.reset, boot_log_reason_name(cur.reason), sizeof(d.reset));
            d.resetBad = boot_log_reason_abnormal(cur.reason);
        }
        else
        {
            strcpy(d.reset, "--");
            d.resetBad = false;
        }
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

        uint32_t bo = boot_log_brownouts();
        d.bootsWarn = bo > 0;
        if (bo > 0)
            snprintf(d.boots, sizeof(d.boots), "%u | BO %u", (unsigned)boot_log_count(), (unsigned)bo);
        else
            snprintf(d.boots, sizeof(d.boots), "%u", (unsigned)boot_log_count());
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
    static void drawDiagContent(TFT_eSPI &g, int16_t yOff, const DiagData &d)
    {
        auto Y = [&](float v) -> int16_t
        { return (int16_t)(u(v) - yOff); };

        uint16_t accent = d.wifi ? C_ICE : C_RED;
        g.fillRect(0, -yOff, W, imax(2, u(2)), accent);
        g.fillRect(0, imax(2, u(2)) - yOff, W, imax(1, u(1)), tint(accent, 0.235f));

        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_WHITE);
        g.drawString("Diagnostics", W / 2, Y(11), 2);
        g.drawFastHLine(u(6), Y(20), W - 2 * u(6), tint(C_SOFT, 0.31f));

        // IP, centred on its own line (a full 192.168.xxx.xxx would not fit next to a label)
        g.setTextDatum(MC_DATUM);
        g.setTextColor(d.wifi ? C_ACCENT : C_RED);
        g.drawString(d.wifi ? d.ip : "Offline", W / 2, Y(30), 2);

        char buf[24];
        uint16_t c;

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
        drawDiagRow(g, Y(44), "Signal", buf, c);

        // free heap
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)d.heapKb);
        c = d.heapKb < 30 ? C_RED : (d.heapKb < 60 ? C_AMBER : C_WHITE);
        drawDiagRow(g, Y(57), "Heap", buf, c);

        // last NTP sync (clock time + age)
        drawDiagRow(g, Y(70), "Sync", d.sync, d.syncOk ? C_WHITE : C_AMBER);

        // weather reading age
        drawDiagRow(g, Y(83), "Weather", d.weather, d.weatherOk ? (d.weatherStale ? C_AMBER : C_WHITE) : C_AMBER);

        // uptime
        drawDiagRow(g, Y(96), "Uptime", d.uptime, C_WHITE);

        // why this boot happened (kept in flash; red = brownout / panic / watchdog)
        drawDiagRow(g, Y(109), "Reset", d.reset, d.resetBad ? C_RED : C_WHITE);

        // boot counter; "BO n" = brownouts so far (amber once there is one)
        drawDiagRow(g, Y(122), "Boots", d.boots, d.bootsWarn ? C_AMBER : C_WHITE);

        // firmware version, and whether a newer release is waiting (the owner installs it from the settings page)
        drawDiagRow(g, Y(135), "Version", d.fw, C_WHITE);
        uint16_t uc = d.updLevel == 1 ? C_AMBER : (d.updLevel == 2 ? C_RED : (d.updLevel == 3 ? C_SOFT : rgb(90, 230, 130)));
        drawDiagRow(g, Y(148), "Update", d.upd, uc);
    }

    void drawDiagScreen(uint32_t now)
    {
        DiagData d;
        collectDiag(d, now);

        TFT_eSPI &gt = faceCv.begin();
        gt.fillRect(0, 0, W, faceH, C_BG);
        drawDiagContent(gt, 0, d);
        faceCv.end();

        TFT_eSPI &gb = infoCv.begin();
        gb.fillRect(0, 0, W, infoH, C_BG);
        drawDiagContent(gb, faceH, d);
        infoCv.end();
    }

} // namespace disp
