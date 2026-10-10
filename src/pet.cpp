#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>
#include "pet.h"
#include "state.h"
#include "settings.h"
#include "sun.h"
#include "config.h"

static const char *NVS_NS = "pet";

static SemaphoreHandle_t s_mutex = nullptr;
static PetEvent s_ev = {PetKind::HAPPY, 0, 0, 0};
static uint32_t s_lastPressMs = 0;
static uint16_t s_streak = 0; // 0 = no tap yet since boot
static uint16_t s_best = 0;
static uint32_t s_total = 0;
static uint16_t s_today = 0;
static uint32_t s_dayKey = 0;    // year * 1000 + day of year of the date s_today belongs to (0 = unknown)
static uint32_t s_lastEpoch = 0; // wall-clock time of the last tap (0 = never / unknown)
static bool s_dirty = false;
static uint32_t s_lastSaveMs = 0;

static uint32_t dayKeyOf(const struct tm &t)
{
    return (uint32_t)(t.tm_year + 1900) * 1000u + (uint32_t)t.tm_yday;
}

void pet_init()
{
    s_mutex = xSemaphoreCreateMutex();

    Preferences p;
    if (p.begin(NVS_NS, false))
    {
        s_total = p.getUInt("tot", 0);
        s_best = p.getUShort("best", 0);
        s_today = p.getUShort("today", 0);
        s_dayKey = p.getUInt("day", 0);
        s_lastEpoch = p.getUInt("last", 0);
        p.end();
    }
}

uint32_t pet_hold_ms(PetKind kind)
{
    switch (kind)
    {
    case PetKind::HEART_EYES:
        return 3000;
    case PetKind::SLEEPY:
        return 2500;
    case PetKind::RAISED_BROW:
        return 2400;
    case PetKind::DIZZY:
        return 1800;
    case PetKind::ANNOYED:
        return 1600;
    case PetKind::MISSED_YOU:
        return 3200;
    case PetKind::MORNING:
        return 3000;
    case PetKind::RARE_WINK:
        return 2200;
    case PetKind::RARE_SURPRISE:
        return 2600;
    case PetKind::HOT:
    case PetKind::COLD:
        return 2200;
    case PetKind::SORRY:
        return 2800;
    case PetKind::HAPPY:
    default:
        return 2000;
    }
}

static bool isRain(int code)
{
    return (code >= 51 && code <= 67) || (code >= 80 && code <= 82); // same codes as the SAD eyes
}

void pet_on_press(uint32_t now)
{
    // ---- what the situation looks like (read before taking our own mutex) ----
    bool synced, approx, wv;
    float tempC;
    int code;
    uint32_t wMs;
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    synced = g_state.timeSynced;
    approx = g_state.timeApprox;
    wv = g_state.weatherValid;
    tempC = g_state.weatherTempC;
    code = g_state.weatherCode;
    wMs = g_state.weatherUpdatedMs;
    xSemaphoreGive(g_stateMutex);

    bool reactions = settings_get().petReactions;

    struct tm ti;
    bool timeOk = synced && getLocalTime(&ti, 0);
    bool exact = timeOk && !approx; // a clock restored from flash may be behind: don't trust it for "morning" / "missed you"
    bool night = timeOk && sun_is_night(ti);
    time_t epoch = timeOk ? time(nullptr) : 0;
    bool weatherOk = wv && (uint32_t)(now - wMs) <= SCENE_WEATHER_MAX_AGE_MS;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // new day -> the daily counter starts again
    if (timeOk && dayKeyOf(ti) != s_dayKey)
    {
        s_dayKey = dayKeyOf(ti);
        s_today = 0;
    }
    bool firstToday = exact && s_today == 0;
    uint32_t awayS = (exact && s_lastEpoch != 0 && (uint32_t)epoch > s_lastEpoch) ? (uint32_t)epoch - s_lastEpoch : 0;

    // streak: taps close together
    if (s_streak > 0 && (uint32_t)(now - s_lastPressMs) <= PET_STREAK_GAP_MS)
    {
        if (s_streak < 0xFFFF)
            s_streak++;
    }
    else
        s_streak = 1;
    s_lastPressMs = now;

    // statistics
    if (s_today < 0xFFFF)
        s_today++;
    s_total++;
    if (s_streak > s_best)
        s_best = s_streak;
    if (epoch > 1700000000) // a real wall-clock time
        s_lastEpoch = (uint32_t)epoch;
    s_dirty = true;

    // ---- pick the reaction (first match wins) ----
    PetKind kind = PetKind::HAPPY;
    if (reactions)
    {
        if (s_streak >= PET_ANNOYED_STREAK)
            kind = PetKind::ANNOYED;
        else if (s_streak >= PET_DIZZY_STREAK)
            kind = PetKind::DIZZY;
        else if (s_streak >= PET_HEART_STREAK)
            kind = PetKind::HEART_EYES;
        else if (firstToday && !night && ti.tm_hour >= PET_MORNING_FROM_H && ti.tm_hour < PET_MORNING_TO_H)
            kind = PetKind::MORNING;
        else if (awayS >= (uint32_t)PET_MISSED_HOURS * 3600u)
            kind = PetKind::MISSED_YOU;
        else if (!night && (esp_random() % 100) < PET_RARE_PERCENT)
            kind = (esp_random() & 1) ? PetKind::RARE_WINK : PetKind::RARE_SURPRISE;
        else if (night)
            kind = PetKind::SLEEPY;
        else if (weatherOk && isRain(code))
            kind = PetKind::RAISED_BROW;
        else if (weatherOk && tempC >= REACT_HOT_C)
            kind = PetKind::HOT;
        else if (weatherOk && tempC <= REACT_COLD_C)
            kind = PetKind::COLD;
    }

    s_ev.kind = kind;
    s_ev.startMs = now;
    s_ev.seq++;
    s_ev.streak = s_streak;
    xSemaphoreGive(s_mutex);
}

void pet_inject_event(PetKind kind)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ev.kind = kind;
    s_ev.startMs = millis();
    s_ev.seq++;
    s_ev.streak = 0;
    xSemaphoreGive(s_mutex);
}

void pet_set_kind(PetKind kind)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ev.kind = kind;
    xSemaphoreGive(s_mutex);
}

PetEvent pet_get_event()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    PetEvent e = s_ev;
    xSemaphoreGive(s_mutex);
    return e;
}

PetStats pet_stats()
{
    struct tm ti;
    bool timeOk = false;
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool synced = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    if (synced)
        timeOk = getLocalTime(&ti, 0);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    PetStats s;
    s.today = (timeOk && dayKeyOf(ti) != s_dayKey) ? 0 : s_today; // a new day has begun, no tap yet
    s.bestStreak = s_best;
    s.streak = s_streak;
    s.total = s_total;
    xSemaphoreGive(s_mutex);
    return s;
}

void pet_tick(uint32_t now)
{
    uint32_t tot, last, day;
    uint16_t best, today;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool due = s_dirty && (uint32_t)(now - s_lastSaveMs) >= PET_SAVE_INTERVAL_MS;
    tot = s_total;
    best = s_best;
    today = s_today;
    day = s_dayKey;
    last = s_lastEpoch;
    if (due)
    {
        s_dirty = false;
        s_lastSaveMs = now;
    }
    xSemaphoreGive(s_mutex);
    if (!due)
        return;

    Preferences p;
    if (p.begin(NVS_NS, false))
    {
        p.putUInt("tot", tot);
        p.putUShort("best", best);
        p.putUShort("today", today);
        p.putUInt("day", day);
        p.putUInt("last", last);
        p.end();
    }
}