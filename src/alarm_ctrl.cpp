#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "alarm_ctrl.h"
#include "audio.h"
#include "config.h"
#include "settings.h"
#include "state.h"

#ifndef ALARM_GRACE_MIN
#define ALARM_GRACE_MIN 10
#endif

enum class AlarmPhase
{
    WAITING, // idle, watching the clock
    RINGING, // sound + servo + red eyes
    SNOOZED  // silent, counting down to the next ring
};

static const char *ALARM_NVS_NAMESPACE = "alarm";
static const char *ALARM_NVS_KEY_LAST = "lastKey";

static volatile AlarmPhase s_phase = AlarmPhase::WAITING;
static uint32_t s_ringStartMs = 0;
static uint32_t s_snoozeUntilMs = 0;
static int32_t s_lastFiredKey = -1; // date + hh:mm of the last scheduled ring (kept in NVS)
static bool s_keyLoaded = false;
static bool s_haveSig = false;      // settings signature seen at least once
static uint32_t s_lastSig = 0;      // last seen alarm enabled/time/days
static bool s_suppressLate = false; // alarm settings just changed: don't catch up a time already passed
static volatile bool s_snoozeRequested = false;
static volatile bool s_dismissRequested = false;
static volatile bool s_testRequested = false;
static uint8_t s_snoozeCount = 0; // snoozes used by the current alarm

static bool readTimeSynced()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool t = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    return t;
}

// The "already rang today" marker lives in flash too, so a reboot inside the
// catch-up window can't ring an alarm that was already answered.
static void loadLastFiredKey()
{
    s_keyLoaded = true;
    Preferences p;
    if (p.begin(ALARM_NVS_NAMESPACE, false))
    {
        s_lastFiredKey = p.getInt(ALARM_NVS_KEY_LAST, -1);
        p.end();
    }
}

static void setLastFiredKey(int32_t key)
{
    if (key == s_lastFiredKey)
        return;
    s_lastFiredKey = key;
    Preferences p;
    if (p.begin(ALARM_NVS_NAMESPACE, false))
    {
        p.putInt(ALARM_NVS_KEY_LAST, key);
        p.end();
    }
}

// Unique per calendar day + alarm minute of the *scheduled* occurrence.
static int32_t makeKey(const struct tm &day, int hour, int minute)
{
    return ((int32_t)day.tm_year * 366 + day.tm_yday) * 1440 + hour * 60 + minute;
}

// Changes when the user edits whether/when/which days the alarm rings.
static uint32_t alarmSignature(const Settings &c)
{
    return (c.alarmEnabled ? 1u : 0u) |
           ((uint32_t)c.alarmHour << 1) |
           ((uint32_t)c.alarmMinute << 6) |
           ((uint32_t)c.alarmDays << 12);
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
    audio_play_loop((uint16_t)settings_get().alarmSound); // the clip chosen on the settings page
    Serial.println("Alarm: ringing");
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

void alarm_request_dismiss()
{
    s_dismissRequested = true;
}

void alarm_request_test()
{
    s_testRequested = true;
}

AlarmState alarm_get_state()
{
    switch (s_phase)
    {
    case AlarmPhase::RINGING:
        return AlarmState::RINGING;
    case AlarmPhase::SNOOZED:
        return AlarmState::SNOOZED;
    default:
        return AlarmState::IDLE;
    }
}

int alarm_snooze_left_s()
{
    if (s_phase != AlarmPhase::SNOOZED)
        return -1;
    int32_t leftMs = (int32_t)(s_snoozeUntilMs - millis()); // wrap-safe
    return leftMs <= 0 ? 0 : (int)((leftMs + 999) / 1000);
}

void alarm_tick()
{
    uint32_t now = millis();

    if (!s_keyLoaded)
        loadLastFiredKey();

    if (s_phase != AlarmPhase::WAITING)
        s_testRequested = false; // a test only makes sense while idle; drop a stale one

    // A full stop wins over everything else, in every phase.
    if (s_dismissRequested)
    {
        s_dismissRequested = false;
        s_snoozeRequested = false;
        if (s_phase != AlarmPhase::WAITING)
        {
            if (s_phase == AlarmPhase::RINGING)
                stopRinging(); // (a snoozed alarm is already silent)
            s_phase = AlarmPhase::WAITING;
            s_snoozeCount = 0;
            Serial.println("Alarm: switched off");
        }
    }

    switch (s_phase)
    {
    case AlarmPhase::WAITING:
    {
        if (s_testRequested)
        {
            // Test ring: works even with the alarm switched off or the clock not
            // synced, and leaves s_lastFiredKey alone so the real alarm still rings.
            s_testRequested = false;
            s_snoozeCount = 0;
            Serial.println("Alarm: test ring");
            startRinging(now);
            break;
        }

        Settings cfg = settings_get();

        // Track edits to the alarm setting (the first look after boot is no edit).
        uint32_t sig = alarmSignature(cfg);
        if (!s_haveSig)
        {
            s_haveSig = true;
            s_lastSig = sig;
        }
        else if (sig != s_lastSig)
        {
            s_lastSig = sig;
            s_suppressLate = true;
        }

        if (!cfg.alarmEnabled)
            break;

        struct tm ti;
        if (!readTimeSynced() || !getLocalTime(&ti, 0))
            break; // no valid clock yet — can't schedule

        // Minutes since the alarm time (0 = this very minute). A negative
        // difference means the alarm time is still ahead today, i.e. the last
        // scheduled occurrence was yesterday (matters for the midnight wrap).
        int lateMin = (ti.tm_hour * 60 + ti.tm_min) - (cfg.alarmHour * 60 + cfg.alarmMinute);
        if (lateMin < 0)
            lateMin += 1440;

        // Calendar day the last scheduled occurrence belongs to: it decides
        // both the weekday check and the once-per-day key.
        time_t sched = time(nullptr) - (time_t)lateMin * 60 - ti.tm_sec;
        struct tm st;
        localtime_r(&sched, &st);
        int32_t key = makeKey(st, cfg.alarmHour, cfg.alarmMinute);

        if (s_suppressLate)
        {
            s_suppressLate = false;
            // Just saved a time that already passed today: that is the user planning
            // for tomorrow, not a missed alarm. (The exact minute still rings.)
            if (lateMin > 0)
                setLastFiredKey(key);
        }

        // tm_wday: 0 = Sunday ... 6 = Saturday, the same numbering as the days mask
        bool dayOk = ((cfg.alarmDays >> st.tm_wday) & 1) != 0;
        if (dayOk && lateMin <= ALARM_GRACE_MIN && key != s_lastFiredKey)
        {
            // Once fired (or caught up), this occurrence can't fire again — not after
            // a snooze/auto-off inside the window, and not after a reboot either.
            setLastFiredKey(key);
            s_snoozeCount = 0; // a fresh alarm: all snoozes available again
            if (lateMin > 0)
                Serial.printf("Alarm: missed its minute, ringing %d min late\n", lateMin);
            startRinging(now);
        }
        break;
    }

    case AlarmPhase::RINGING:
        if (s_snoozeRequested)
        {
            s_snoozeRequested = false;
            stopRinging();
            if (ALARM_MAX_SNOOZES > 0 && s_snoozeCount >= ALARM_MAX_SNOOZES)
            {
                // Out of snoozes: this tap ends the alarm instead of delaying it again.
                s_phase = AlarmPhase::WAITING;
                s_snoozeCount = 0;
                Serial.println("Alarm: no snoozes left, switched off");
            }
            else
            {
                s_snoozeCount++;
                s_snoozeUntilMs = now + ALARM_SNOOZE_MIN * 60UL * 1000UL;
                s_phase = AlarmPhase::SNOOZED;
                Serial.printf("Alarm: snoozed (%u of %u), rings again in %u min\n",
                              (unsigned)s_snoozeCount, (unsigned)ALARM_MAX_SNOOZES, (unsigned)ALARM_SNOOZE_MIN);
            }
        }
        else if ((now - s_ringStartMs) >= ALARM_AUTO_OFF_MIN * 60UL * 1000UL)
        {
            stopRinging();
            s_phase = AlarmPhase::WAITING; // next ring is tomorrow's alarm time
            s_snoozeCount = 0;
            Serial.println("Alarm: nobody answered, switched off");
        }
        break;

    case AlarmPhase::SNOOZED:
        if ((int32_t)(now - s_snoozeUntilMs) >= 0) // wrap-safe compare
            startRinging(now);
        break;
    }
}