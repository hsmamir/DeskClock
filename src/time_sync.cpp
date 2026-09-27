#include <WiFi.h>
#include <time.h>
#include "time_sync.h"
#include "config.h"
#include "state.h"

static uint32_t s_lastSyncMs = 0;

void time_sync_init() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 10000)) {
            g_state.timeSynced = true;
            s_lastSyncMs = millis();
        }
    }
    // NOTE: if WiFi/NTP fails here, the clock still runs off millis()-based
    // elapsed time but g_state.timeSynced stays false — surface that on
    // the TFT (e.g. a small icon) once display_tick() is implemented.
}

void time_sync_tick() {
    // TODO: if WiFi.status() != WL_CONNECTED, attempt reconnect.
    // if (millis() - s_lastSyncMs > NTP_RESYNC_INTERVAL_MS) re-run
    // configTime()/getLocalTime() and update s_lastSyncMs.
}
