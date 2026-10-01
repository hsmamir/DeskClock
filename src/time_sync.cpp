#include <WiFi.h>
#include <time.h>
#include "time_sync.h"
#include "config.h"
#include "pins.h"
#include "state.h"
#include "settings.h"

#ifndef USE_WOKWI_WIFI
#include <WiFiManager.h>
#endif

static uint32_t s_lastSyncMs = 0;

static void setPortalActive(bool active)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.wifiPortalActive = active;
    xSemaphoreGive(g_stateMutex);
}

static bool readTimeSynced()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool t = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    return t;
}

static void syncTime()
{
    // POSIX TZ rule (incl. DST) from settings; configTzTime sets TZ and starts SNTP.
    Settings cfg = settings_get();
    configTzTime(cfg.tzPosix, NTP_SERVER);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000))
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        g_state.timeSynced = true;
        xSemaphoreGive(g_stateMutex);
        s_lastSyncMs = millis();
    }
}

#ifdef USE_WOKWI_WIFI

// Simulator: no phone to join a portal, so connect to the fixed open network.
static void connectWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WOKWI_WIFI_SSID, WOKWI_WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(250);
    }
}

#else

// Real hardware: WiFiManager tries the saved network first and only opens the
// setup portal (blocking, up to WIFI_PORTAL_TIMEOUT_SEC) if that fails.
static void connectWifi()
{
    WiFi.mode(WIFI_STA);

    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SEC);
    wm.setAPCallback([](WiFiManager *)
                     { setPortalActive(true); });

    // Holding the Mode button while powering on forgets the saved network,
    // which forces the portal to open — the way to switch WiFi without
    // reflashing. (touch_init() sets this pin up too, but that task may not
    // have run yet, so set it here before reading.)
    pinMode(TOUCH_MODE_PIN, INPUT_PULLUP);
    delay(50);
    if (digitalRead(TOUCH_MODE_PIN) == LOW)
    {
        wm.resetSettings();
    }

    wm.autoConnect(WIFI_PORTAL_AP_NAME);
    setPortalActive(false);
}

#endif

void time_sync_init()
{
    connectWifi();

    if (WiFi.status() == WL_CONNECTED)
    {
        syncTime();
    }
    // NOTE: if WiFi/NTP fails here, g_state.timeSynced stays false and the
    // TFT shows "--:--"; time_sync_tick() keeps retrying in the background.
}

void time_sync_tick()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.reconnect(); // retries the saved network; does nothing if none is saved
        return;
    }

    if (!readTimeSynced() || (millis() - s_lastSyncMs) > NTP_RESYNC_INTERVAL_MS)
    {
        syncTime();
    }
}