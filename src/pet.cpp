#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>
#include <math.h>
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

// ---- inner life ----
static float s_energy = 60;     // 0..100
static bool s_energyInit = false; // false: no saved value yet, the first step jumps straight to the target
static float s_bondAtTap = 40;  // bond right after the last tap; the absence decay is computed from it (never stored twice)
static float s_bondCur = 40;    // bond right now (s_bondAtTap minus the absence decay)
static float s_bored = 0;       // 0..100
static uint32_t s_bondHour = 0; // hour key of the taps counted below
static uint8_t s_bondHourGain = 0;
static uint8_t s_savedEn = 0, s_savedBor = 0; // what flash holds, to avoid writing for tiny drifts
static bool s_fidget = false;
static uint32_t s_nextFidgetMs = 0;

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
        if (p.isKey("en"))
        {
            s_energy = p.getUChar("en", 60);
            s_energyInit = true;
        }
        s_bondAtTap = s_bondCur = p.getUChar("bond", 40);
        s_bored = p.getUChar("bor", 0);
        s_savedEn = (uint8_t)s_energy;
        s_savedBor = (uint8_t)s_bored;
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
    case PetKind::SULK:
        return PET_SULK_MS + 2000;
    case PetKind::HAPPY:
    default:
        return 2000;
    }
}

static bool isRain(int code)
{
    return (code >= 51 && code <= 67) || (code >= 80 && code <= 82); // same codes as the SAD eyes
}


// =====================================================================
//  Inner life
// =====================================================================

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Bond right now: the value after the last tap minus 1 for every PET_BOND_ABSENT_STEP_H hours
// of absence beyond PET_BOND_ABSENT_FROM_H, never below PET_BOND_FLOOR (a value that is already
// lower stays as it is). Computed from the last tap, so a reboot can not apply it twice.
// Needs an exact wall clock (not one restored from flash).
static float bondNowLocked(uint32_t epoch, bool exact)
{
    float b = s_bondAtTap;
    if (exact && s_lastEpoch != 0 && epoch > s_lastEpoch)
    {
        uint32_t away = epoch - s_lastEpoch, from = (uint32_t)PET_BOND_ABSENT_FROM_H * 3600u;
        if (away > from && b > PET_BOND_FLOOR)
        {
            uint32_t steps = (away - from) / ((uint32_t)PET_BOND_ABSENT_STEP_H * 3600u);
            b = fmaxf((float)PET_BOND_FLOOR, b - (float)steps);
        }
    }
    return b;
}

// Where the energy wants to be right now: the same key hours as the day theme (display_theme.cpp)
// -> night 20, dawn 40, day 90 (until the day ends), dusk 60, night 20 again; linear in between.
static float energyTarget(const struct tm &ti)
{
    float h[6] = {THEME_NIGHT_END_H, THEME_DAWN_H, THEME_DAY_FROM_H, THEME_DAY_TO_H, THEME_DUSK_H, THEME_NIGHT_FROM_H};
    if (SUN_ENABLED)
    {
        SunTimes st = sun_today(ti);
        if (st.valid)
        {
            if (st.midnightSun)
                return PET_ENERGY_DAY;
            if (st.polarNight)
                return PET_ENERGY_NIGHT;
            h[0] = st.riseH + THEME_NIGHT_END_FROM_SUNRISE_H;
            h[1] = st.riseH + THEME_DAWN_FROM_SUNRISE_H;
            h[2] = st.riseH + THEME_DAY_FROM_SUNRISE_H;
            h[3] = st.setH + THEME_DAY_TO_FROM_SUNSET_H;
            h[4] = st.setH + THEME_DUSK_FROM_SUNSET_H;
            h[5] = st.setH + THEME_NIGHT_FROM_SUNSET_H;
            if (h[3] < h[2])
                h[2] = h[3] = (h[2] + h[3]) / 2.0f;
            for (int i = 1; i < 6; i++)
                if (h[i] < h[i - 1])
                    h[i] = h[i - 1];
        }
    }
    static const float v[6] = {PET_ENERGY_NIGHT, PET_ENERGY_DAWN, PET_ENERGY_DAY, PET_ENERGY_DAY, PET_ENERGY_DUSK, PET_ENERGY_NIGHT};
    float hour = ti.tm_hour + ti.tm_min / 60.0f + ti.tm_sec / 3600.0f;
    if (hour < h[0] && hour + 24.0f < h[5]) // a sunset after midnight: the small hours still belong to yesterday evening
        hour += 24.0f;
    if (hour <= h[0] || hour >= h[5])
        return PET_ENERGY_NIGHT;
    for (int i = 0; i < 5; i++)
        if (hour <= h[i + 1])
        {
            float span = h[i + 1] - h[i];
            float t = span > 0.001f ? (hour - h[i]) / span : 1.0f;
            return v[i] + (v[i + 1] - v[i]) * t;
        }
    return PET_ENERGY_NIGHT;
}

// Once a second (called from pet_tick): energy follows its target, boredom grows, the absence
// is applied to the bond, a very bored pet may ask for a servo fidget.
static void stepMeters(uint32_t now)
{
    static uint32_t lastMs = 0;
    if (lastMs != 0 && (uint32_t)(now - lastMs) < 1000)
        return;
    float dt = lastMs ? (now - lastMs) / 1000.0f : 1.0f;
    if (dt > 60.0f)
        dt = 60.0f;
    lastMs = now;

    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool synced = g_state.timeSynced, approx = g_state.timeApprox;
    xSemaphoreGive(g_stateMutex);
    struct tm ti;
    bool timeOk = synced && getLocalTime(&ti, 0);
    bool exact = timeOk && !approx;
    bool night = timeOk && sun_is_night(ti);
    time_t epoch = timeOk ? time(nullptr) : 0;
    float target = timeOk ? energyTarget(ti) : -1.0f;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (target >= 0)
    {
        if (!s_energyInit)
        {
            s_energy = target;
            s_energyInit = true;
        }
        else
            s_energy += (target - s_energy) * fminf(1.0f, dt / (float)PET_ENERGY_TAU_S);
    }
    if (!night) // asleep at night: no boredom piles up
        s_bored = fminf(100.0f, s_bored + dt / 60.0f * PET_BORED_PER_MIN);
    s_bondCur = bondNowLocked((uint32_t)epoch, exact);

    // a very bored pet fidgets with the servo now and then (day only; the servo task fetches the request)
    if (!night && s_bored >= PET_BORED_HIGH + 15)
    {
        if (s_nextFidgetMs == 0)
            s_nextFidgetMs = now + 1000u * (PET_FIDGET_MIN_S + esp_random() % (PET_FIDGET_MAX_S - PET_FIDGET_MIN_S + 1));
        else if ((int32_t)(now - s_nextFidgetMs) >= 0)
        {
            s_fidget = true;
            s_nextFidgetMs = now + 1000u * (PET_FIDGET_MIN_S + esp_random() % (PET_FIDGET_MAX_S - PET_FIDGET_MIN_S + 1));
        }
    }
    else
    {
        s_fidget = false;
        s_nextFidgetMs = 0;
    }

    // a visible change since the last write -> worth a flash write (rate-limited by pet_tick)
    if (fabsf(s_energy - s_savedEn) >= 3.0f || fabsf(s_bored - s_savedBor) >= 3.0f)
        s_dirty = true;
    xSemaphoreGive(s_mutex);
}

PetMeters pet_meters()
{
    PetMeters m;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    m.energy = (uint8_t)(clampf(s_energy, 0, 100) + 0.5f);
    m.bond = (uint8_t)(clampf(s_bondCur, 0, 100) + 0.5f);
    m.bored = (uint8_t)(clampf(s_bored, 0, 100) + 0.5f);
    xSemaphoreGive(s_mutex);
    return m;
}

// 0..1 ramp that is 0 at `from` and 1 at `from + span` (span may be negative: 0 at `from`, 1 below it).
static float ramp(float v, float from, float span) { return clampf((v - from) / span, 0.0f, 1.0f); }

PetLook pet_look()
{
    PetMeters m = pet_meters();
    float tired = ramp(m.energy, PET_ENERGY_LOW, -5.0f); // fades in over 5 points below 30
    float fond = ramp(m.bond, PET_BOND_FOND, 5.0f);      // ... and above 70
    PetLook l;
    l.lidTop = 0.25f * tired;
    l.lidBot = 0.12f * fond;
    l.cheek = 0.5f * fond;
    l.bored = ramp(m.bored, PET_BORED_HIGH, 30.0f);
    l.yawnMul = tired > 0.5f ? 3.0f : 1.0f;
    l.slowBlink = tired > 0.5f;
    return l;
}

void pet_note_interaction()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_bored = 0;
    s_fidget = false;
    xSemaphoreGive(s_mutex);
}

bool pet_take_fidget()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool f = s_fidget;
    s_fidget = false;
    xSemaphoreGive(s_mutex);
    return f;
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
    float bondBefore = bondNowLocked((uint32_t)epoch, exact); // before this tap changes anything

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

    // ---- inner life: bond up (anti-spam), boredom gone, a long streak tires the pet out ----
    {
        uint32_t hourKey = (epoch > 1700000000) ? (uint32_t)epoch / 3600u : 0x80000000u + now / 3600000u;
        uint8_t gain = PET_BOND_TAP_GAIN;
        if (hourKey != s_bondHour)
        {
            s_bondHour = hourKey;
            s_bondHourGain = 0;
            gain = PET_BOND_FIRST_TAP_GAIN; // first tap of the hour
        }
        if (s_bondHourGain + gain > PET_BOND_MAX_PER_HOUR)
            gain = (s_bondHourGain < PET_BOND_MAX_PER_HOUR) ? PET_BOND_MAX_PER_HOUR - s_bondHourGain : 0;
        s_bondHourGain += gain;
        s_bondAtTap = s_bondCur = fminf(100.0f, bondBefore + gain);
        s_bored = 0;
        s_fidget = false;
        if (s_streak == PET_ENERGY_STREAK_MIN) // exactly once per long streak
            s_energy = fmaxf(0.0f, s_energy - PET_ENERGY_STREAK_COST);
    }

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
        else if (exact && bondBefore < PET_SULK_BOND_BELOW && awayS >= (uint32_t)PET_SULK_AWAY_H * 3600u)
            kind = PetKind::SULK; // sulks first, then melts
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
    stepMeters(now);

    uint32_t tot, last, day;
    uint16_t best, today;
    uint8_t en, bond, bor;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool due = s_dirty && (uint32_t)(now - s_lastSaveMs) >= PET_SAVE_INTERVAL_MS;
    tot = s_total;
    best = s_best;
    today = s_today;
    day = s_dayKey;
    last = s_lastEpoch;
    en = (uint8_t)(clampf(s_energy, 0, 100) + 0.5f);
    bond = (uint8_t)(clampf(s_bondAtTap, 0, 100) + 0.5f);
    bor = (uint8_t)(clampf(s_bored, 0, 100) + 0.5f);
    if (due)
    {
        s_dirty = false;
        s_lastSaveMs = now;
        s_savedEn = en;
        s_savedBor = bor;
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
        p.putUChar("en", en);
        p.putUChar("bond", bond);
        p.putUChar("bor", bor);
        p.end();
    }
}
