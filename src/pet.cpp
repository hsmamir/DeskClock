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

// ---- bond (see "Bond" in config.h) ----
static uint16_t s_bond = PET_BOND_START;   // 0..1000
static uint32_t s_bondEpoch = 0;           // wall-clock time up to which the fading has been counted (0 = unknown)
static uint16_t s_gainToday = 0;           // bond gained from taps / purring on the date s_dayKey (daily cap)
static volatile bool s_reactionsOn = true; // "varied reactions" switch, refreshed by pet_tick()
static bool s_bondWasOff = false;

static uint32_t dayKeyOf(const struct tm &t)
{
    return (uint32_t)(t.tm_year + 1900) * 1000u + (uint32_t)t.tm_yday;
}

static BondLevel levelOf(uint16_t v)
{
    if (v >= PET_BOND_DEVOTED_AT)
        return BondLevel::DEVOTED;
    if (v >= PET_BOND_FOND_AT)
        return BondLevel::FOND;
    if (v >= PET_BOND_FRIENDLY_AT)
        return BondLevel::FRIENDLY;
    return BondLevel::LONELY;
}

// How the level changes the tap reactions: extra taps before dizzy / annoyed, one tap sooner for hearts, more rare ones.
static int bondTolerance(BondLevel l)
{
    return l == BondLevel::DEVOTED ? 4 : (l == BondLevel::FOND ? 2 : 0);
}
static int bondHeartAt(BondLevel l)
{
    int n = PET_HEART_STREAK - (l == BondLevel::DEVOTED ? 1 : 0);
    return n < 2 ? 2 : n;
}
static int bondRarePercent(BondLevel l)
{
    return PET_RARE_PERCENT + (l == BondLevel::DEVOTED ? 5 : (l == BondLevel::FOND ? 2 : 0));
}

// ---- the next functions run with s_mutex taken ----

// Adds delta to the bond. A gain with useDailyCap is limited by what is left of today's allowance.
static void bondAddLocked(int delta, bool useDailyCap)
{
    if (delta > 0 && useDailyCap)
    {
        int room = PET_BOND_DAILY_CAP - (int)s_gainToday;
        if (room <= 0)
            return;
        if (delta > room)
            delta = room;
        s_gainToday += (uint16_t)delta;
    }
    int v = (int)s_bond + delta;
    if (v > 1000)
        v = 1000;
    if (v < PET_BOND_FLOOR)
        v = PET_BOND_FLOOR;
    if (v != (int)s_bond)
    {
        s_bond = (uint16_t)v;
        s_dirty = true;
    }
    else if (delta != 0)
        s_dirty = true; // the daily allowance changed
}

// Counts the fading since the last time. epoch = a trustworthy wall-clock time (NTP).
static void bondDecayLocked(uint32_t epoch)
{
    if (PET_BOND_DECAY_PER_DAY <= 0)
        return;
    if (s_bondEpoch == 0 || s_bondEpoch > epoch) // nothing counted yet, or the clock moved back
    {
        s_bondEpoch = epoch;
        return;
    }
    uint32_t from = s_bondEpoch;
    if (s_lastEpoch != 0)
    {
        uint32_t graceEnd = s_lastEpoch + (uint32_t)PET_BOND_GRACE_H * 3600u; // no fading right after a tap
        if (graceEnd > from)
            from = graceEnd;
    }
    if (epoch <= from)
        return;
    uint32_t loss = (uint32_t)(((uint64_t)(epoch - from) * (uint32_t)PET_BOND_DECAY_PER_DAY) / 86400u);
    if (loss == 0)
        return; // less than one unit so far: keep counting from the same point
    if (s_bond <= PET_BOND_FLOOR)
    {
        s_bondEpoch = epoch;
        return;
    }
    s_bondEpoch = from + (uint32_t)(((uint64_t)loss * 86400u) / (uint32_t)PET_BOND_DECAY_PER_DAY); // only what was used
    uint32_t nv = (loss >= s_bond) ? 0u : (uint32_t)s_bond - loss;
    s_bond = (uint16_t)(nv < (uint32_t)PET_BOND_FLOOR ? PET_BOND_FLOOR : nv);
    s_dirty = true;
}

void pet_init()
{
    s_mutex = xSemaphoreCreateMutex();
    s_reactionsOn = settings_get().petReactions;

    Preferences p;
    if (p.begin(NVS_NS, false))
    {
        s_total = p.getUInt("tot", 0);
        s_best = p.getUShort("best", 0);
        s_today = p.getUShort("today", 0);
        s_dayKey = p.getUInt("day", 0);
        s_lastEpoch = p.getUInt("last", 0);
        uint16_t b = p.getUShort("bond", PET_BOND_START);
        s_bond = b > 1000 ? 1000 : (b < PET_BOND_FLOOR ? PET_BOND_FLOOR : b);
        s_bondEpoch = p.getUInt("bep", 0);
        s_gainToday = p.getUShort("gt", 0);
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
    bool exact = timeOk && !approx;                           // a clock restored from flash may be behind: don't trust it for "morning" / "missed you"
    bool night = timeOk && eyes_phase(ti) != EyePhase::AWAKE; // sleepy-pet reaction only when the eyes are sleepy
    time_t epoch = timeOk ? time(nullptr) : 0;
    bool weatherOk = wv && (uint32_t)(now - wMs) <= SCENE_WEATHER_MAX_AGE_MS;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // new day -> the daily counter starts again
    if (timeOk && dayKeyOf(ti) != s_dayKey)
    {
        s_dayKey = dayKeyOf(ti);
        s_today = 0;
        s_gainToday = 0;
    }
    bool firstToday = exact && s_today == 0;
    uint32_t awayS = (exact && s_lastEpoch != 0 && (uint32_t)epoch > s_lastEpoch) ? (uint32_t)epoch - s_lastEpoch : 0;

    // bond: count the fading up to now (before s_lastEpoch moves on), then the level decides how patient he is
    const bool bondOn = PET_BOND_ENABLED && reactions;
    if (bondOn && exact && epoch > 1700000000)
        bondDecayLocked((uint32_t)epoch);
    const BondLevel lv = levelOf(s_bond);
    const int dizzyAt = PET_DIZZY_STREAK + bondTolerance(lv);
    const int annoyedAt = PET_ANNOYED_STREAK + bondTolerance(lv);
    const int heartAt = bondHeartAt(lv);

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
    if (bondOn && s_streak < dizzyAt)
        bondAddLocked(PET_BOND_TAP_GAIN, true); // being petted gently makes him like you; spamming does not

    // ---- pick the reaction (first match wins) ----
    PetKind kind = PetKind::HAPPY;
    if (reactions)
    {
        if (s_streak >= annoyedAt)
            kind = PetKind::ANNOYED;
        else if (s_streak >= dizzyAt)
            kind = PetKind::DIZZY;
        else if (s_streak >= heartAt)
            kind = PetKind::HEART_EYES;
        else if (firstToday && !night && ti.tm_hour >= PET_MORNING_FROM_H && ti.tm_hour < PET_MORNING_TO_H)
            kind = PetKind::MORNING;
        else if (awayS >= (uint32_t)PET_MISSED_HOURS * 3600u)
            kind = PetKind::MISSED_YOU;
        else if (!night && (int)(esp_random() % 100) < (bondOn ? bondRarePercent(lv) : PET_RARE_PERCENT))
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

    if (bondOn && kind == PetKind::ANNOYED && s_streak == annoyedAt)
        bondAddLocked(-PET_BOND_ANNOYED_LOSS, false); // once per "that's enough!"

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

PetBond pet_bond()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    PetBond b;
    b.value = s_bond;
    b.level = levelOf(s_bond);
    b.enabled = PET_BOND_ENABLED && s_reactionsOn;
    xSemaphoreGive(s_mutex);
    return b;
}

void pet_bond_add(int delta, bool useDailyCap)
{
    if (!PET_BOND_ENABLED || !s_reactionsOn)
        return;

    // today's date for the daily allowance (read before taking our own mutex)
    struct tm ti;
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool synced = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    bool timeOk = synced && getLocalTime(&ti, 0);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (timeOk && dayKeyOf(ti) != s_dayKey)
    {
        s_dayKey = dayKeyOf(ti);
        s_today = 0;
        s_gainToday = 0;
    }
    bondAddLocked(delta, useDailyCap);
    xSemaphoreGive(s_mutex);
}

void pet_tick(uint32_t now)
{
    // the "varied reactions" switch, once a second (settings_get() copies the whole struct)
    static uint32_t lastFlagMs = 0;
    if ((uint32_t)(now - lastFlagMs) >= 1000)
    {
        lastFlagMs = now;
        s_reactionsOn = settings_get().petReactions;
    }

    // the bond fades while nobody pets him: checked once a minute, needs a clock that really is right
    static uint32_t lastBondMs = 0;
    if (PET_BOND_ENABLED && (uint32_t)(now - lastBondMs) >= 60000UL)
    {
        lastBondMs = now;
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool trusted = g_state.timeSynced && !g_state.timeApprox;
        xSemaphoreGive(g_stateMutex);
        struct tm ti;
        if (trusted && getLocalTime(&ti, 0))
        {
            time_t epoch = time(nullptr);
            if (epoch > 1700000000)
            {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (!s_reactionsOn)
                {
                    s_bondWasOff = true;
                    s_bondEpoch = (uint32_t)epoch; // no fading while the feature is off
                }
                else
                {
                    if (s_bondWasOff)
                    {
                        s_bondWasOff = false;
                        s_dirty = true; // save the fresh time point so a reboot cannot count the off period
                    }
                    bondDecayLocked((uint32_t)epoch);
                }
                xSemaphoreGive(s_mutex);
            }
        }
    }

    uint32_t tot, last, day, bep;
    uint16_t best, today, bond, gt;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool due = s_dirty && (uint32_t)(now - s_lastSaveMs) >= PET_SAVE_INTERVAL_MS;
    tot = s_total;
    best = s_best;
    today = s_today;
    day = s_dayKey;
    last = s_lastEpoch;
    bond = s_bond;
    bep = s_bondEpoch;
    gt = s_gainToday;
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
        p.putUShort("bond", bond);
        p.putUInt("bep", bep);
        p.putUShort("gt", gt);
        p.end();
    }
}