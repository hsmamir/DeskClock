#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include "esp_sntp.h"
#include "time_sync.h"
#include "config.h"
#include "pins.h"
#include "state.h"
#include "settings.h"

#ifndef USE_WOKWI_WIFI
#include <WiFiManager.h>
#endif

// Fallback NTP servers: with only one name, a single unreachable / filtered pool
// leaves the clock on the (approximate) time restored from flash.
#ifndef NTP_SERVER2
#define NTP_SERVER2 "time.cloudflare.com"
#endif
#ifndef NTP_SERVER3
#define NTP_SERVER3 "time.google.com"
#endif

static uint32_t s_lastSyncMs = 0;
static uint32_t s_lastSaveMs = 0; // 0 = not saved since boot
static volatile bool s_sntpDone = false;

static const char *CLK_NAMESPACE = "clkstate";
static const char *CLK_KEY = "lastT";

// Called by the SNTP client (tcpip thread) each time the system time has been
// set from a server. Only sets a flag.
static void onSntpSync(struct timeval *)
{
    s_sntpDone = true;
}

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

static bool readTimeApprox()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool a = g_state.timeApprox;
    xSemaphoreGive(g_stateMutex);
    return a;
}

// ---- Time persistence ----

void time_save_now()
{
    time_t t = time(nullptr);
    if (t < (time_t)TIME_MIN_VALID_EPOCH)
        return; // never store a bogus time
    Preferences p;
    if (p.begin(CLK_NAMESPACE, false))
    {
        p.putULong(CLK_KEY, (uint32_t)t);
        p.end();
    }
    s_lastSaveMs = millis();
}

static void saveTimeIfDue()
{
    if (!readTimeSynced())
        return;
    if (s_lastSaveMs != 0 && (millis() - s_lastSaveMs) < TIME_SAVE_INTERVAL_MS)
        return;
    time_save_now();
}

void time_restore_from_flash()
{
    bool have = false;

    if (time(nullptr) >= (time_t)TIME_MIN_VALID_EPOCH)
    {
        // The system clock survived the reset (software reset) — keep it.
        have = true;
        Serial.println("Clock: system time kept across reset");
    }
    else
    {
        uint32_t saved = 0;
        Preferences p;
        if (p.begin(CLK_NAMESPACE, false))
        {
            saved = p.getULong(CLK_KEY, 0);
            p.end();
        }
        if (saved >= TIME_MIN_VALID_EPOCH)
        {
            struct timeval tv = {(time_t)saved, 0};
            settimeofday(&tv, nullptr);
            have = true;
            Serial.printf("Clock: restored from flash (epoch %u), waiting for NTP\n", (unsigned)saved);
        }
        else
        {
            Serial.println("Clock: no saved time, waiting for NTP");
        }
    }

    if (have)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        g_state.timeSynced = true; // alarm + display can run right away...
        g_state.timeApprox = true; // ...and NTP still replaces this as soon as possible
        xSemaphoreGive(g_stateMutex);
    }
}

static void syncTime()
{
    // POSIX TZ rule (incl. DST) from settings; configTzTime sets TZ and starts SNTP.
    // Success = the SNTP callback fired, NOT "getLocalTime() works": after a
    // restore from flash the time is already valid but not yet confirmed.
    Settings cfg = settings_get();
    s_sntpDone = false;
    configTzTime(cfg.tzPosix, NTP_SERVER, NTP_SERVER2, NTP_SERVER3);

    uint32_t start = millis();
    while (!s_sntpDone && (millis() - start) < 10000)
        delay(100);

    if (s_sntpDone)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        g_state.timeSynced = true;
        g_state.timeApprox = false;
        g_state.ntpSyncEpoch = (uint32_t)time(nullptr);
        g_state.ntpSyncMs = millis();
        xSemaphoreGive(g_stateMutex);
        s_lastSyncMs = millis();
        time_save_now(); // fresh baseline right after a real sync
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
    sntp_set_time_sync_notification_cb(onSntpSync);
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
    saveTimeIfDue(); // before the WiFi check, so it also works offline

    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.reconnect(); // retries the saved network; does nothing if none is saved
        return;
    }

    if (!readTimeSynced() || readTimeApprox() || (millis() - s_lastSyncMs) > NTP_RESYNC_INTERVAL_MS)
    {
        syncTime();
    }
}