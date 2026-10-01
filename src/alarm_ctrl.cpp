#include <Arduino.h>
#include <time.h>
#include "alarm_ctrl.h"
#include "audio.h"
#include "config.h"
#include "settings.h"
#include "state.h"

enum class AlarmPhase
{
    WAITING, // idle, watching the clock
    RINGING, // sound + servo + red eyes
    SNOOZED  // silent, counting down to the next ring
};

static AlarmPhase s_phase = AlarmPhase::WAITING;
static uint32_t s_ringStartMs = 0;
static uint32_t s_snoozeUntilMs = 0;
static int32_t s_lastFiredKey = -1; // date + hh:mm of the last scheduled ring
static volatile bool s_snoozeRequested = false;

static bool readTimeSynced()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool t = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    return t;
}

// Flips the shared state the servo/display tasks react to.
static void setRinging(bool on)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.alarmActive = on;
    if (on)
        g_state.mood = Mood::ALARM_ALERT;
    else if (g_state.mood == Mood::ALARM_ALERT)
        g_state.mood = Mood::IDLE_BLINK;
    xSemaphoreGive(g_stateMutex);
}

static void startRinging(uint32_t now)
{
    s_phase = AlarmPhase::RINGING;
    s_ringStartMs = now;
    s_snoozeRequested = false; // drop any stale request from before this ring
    setRinging(true);
    audio_play_loop(SOUND_INDEX_ALARM);
}

static void stopRinging()
{
    setRinging(false);
    audio_stop();
}

void alarm_request_snooze()
{
    s_snoozeRequested = true;
}

void alarm_tick()
{
    uint32_t now = millis();

    switch (s_phase)
    {
    case AlarmPhase::WAITING:
    {
        Settings cfg = settings_get();
        if (!cfg.alarmEnabled)
            break;

        struct tm ti;
        if (!readTimeSynced() || !getLocalTime(&ti, 0))
            break; // no valid clock yet — can't schedule

        if (ti.tm_hour == cfg.alarmHour && ti.tm_min == cfg.alarmMinute)
        {
            // Unique per calendar day + minute. Once fired, this exact minute
            // can't fire again — even after a snooze/auto-off inside it.
            int32_t key = ((int32_t)ti.tm_year * 366 + ti.tm_yday) * 1440 +
                          ti.tm_hour * 60 + ti.tm_min;
            if (key != s_lastFiredKey)
            {
                s_lastFiredKey = key;
                startRinging(now);
            }
        }
        break;
    }

    case AlarmPhase::RINGING:
        if (s_snoozeRequested)
        {
            s_snoozeRequested = false;
            stopRinging();
            s_snoozeUntilMs = now + ALARM_SNOOZE_MIN * 60UL * 1000UL;
            s_phase = AlarmPhase::SNOOZED;
        }
        else if ((now - s_ringStartMs) >= ALARM_AUTO_OFF_MIN * 60UL * 1000UL)
        {
            stopRinging();
            s_phase = AlarmPhase::WAITING; // next ring is tomorrow's alarm time
        }
        break;

    case AlarmPhase::SNOOZED:
        if ((int32_t)(now - s_snoozeUntilMs) >= 0) // wrap-safe compare
            startRinging(now);
        break;
    }
}