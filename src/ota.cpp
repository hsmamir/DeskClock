#include <Arduino.h>
#include "ota.h"

#ifdef USE_WOKWI_WIFI

// The simulator can't receive OTA updates.
void ota_init() {}
void ota_tick() {}

#else

#include <WiFi.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "state.h"
#include "time_sync.h"

static bool s_started = false;
static int s_lastPct = -1;

static void setOtaPercent(int pct)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.otaPercent = (int8_t)pct;
    xSemaphoreGive(g_stateMutex);
}

void ota_init()
{
    ArduinoOTA.setHostname(WEB_HOSTNAME);
    ArduinoOTA.setMdnsEnabled(false); // web_ui.cpp already owns mDNS under the same hostname
    if (sizeof(OTA_PASSWORD) > 1)
        ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]()
                       {
        s_lastPct = -1;
        setOtaPercent(0);
        Serial.println("OTA: update starting"); });

    ArduinoOTA.onProgress([](unsigned int done, unsigned int total)
                          {
        int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
        if (pct != s_lastPct)
        {
            s_lastPct = pct;
            setOtaPercent(pct);
        } });

    ArduinoOTA.onEnd([]()
                     {
        setOtaPercent(100);
        time_save_now(); // the reboot that follows would otherwise cost up to one save interval of clock accuracy
        Serial.println("OTA: done, rebooting"); });

    ArduinoOTA.onError([](ota_error_t e)
                       {
        setOtaPercent(-1); // back to the normal screen
        Serial.printf("OTA: error %u\n", (unsigned)e); });
}

void ota_tick()
{
    if (!s_started)
    {
        if (WiFi.status() != WL_CONNECTED)
            return; // the UDP listener needs a network; try again next tick
        ArduinoOTA.begin();
        s_started = true;
        Serial.printf("OTA ready: %s.local (%s)\n", WEB_HOSTNAME, WiFi.localIP().toString().c_str());
    }
    ArduinoOTA.handle();
}

#endif