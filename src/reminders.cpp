// reminders.cpp - stored reminders, the scheduler that finds the due ones, and the "what is on screen" state.
// See reminders.h. reminders_tick() runs in the alarm task; the settings page (web task) edits the list;
// the display task only reads the view (reminder_get_active); the touch task closes it (reminder_consume_press).
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "reminders.h"
#include "persian_text.h"
#include "config.h"
#include "state.h"
#include "audio.h"
#include "alarm_ctrl.h"

#ifndef REMINDER_GRACE_MIN
#define REMINDER_GRACE_MIN 5
#endif

namespace
{
    const char *NVS_NS = "remind";
    const int QUEUE_MAX = 6;
    const uint32_t GAP_MS = 500; // pause between two reminders shown one after the other

    struct Entry
    {
        Reminder r;
        int32_t lastKey; // day + minute of the last time a weekly reminder was shown (kept in NVS)
    };

    // What is stored: a fixed layout, independent of the compiler's idea of Entry.
    struct Packed
    {
        uint8_t id, enabled, hour, minute, days, pad[3];
        uint32_t date;
        int32_t lastKey;
        char text[REMINDER_TEXT_MAX];
    };
    static_assert(sizeof(Packed) == 16 + REMINDER_TEXT_MAX, "Packed layout");

    struct Pending
    {
        uint8_t id;
        bool test;
        uint8_t hour, minute;
        char text[REMINDER_TEXT_MAX];
    };

    SemaphoreHandle_t s_mtx = nullptr;     // list, queue, NVS
    SemaphoreHandle_t s_viewMtx = nullptr; // s_view only (the display reads it every frame; never held long)
    Entry s_e[REMINDER_MAX];
    int s_n = 0;
    uint8_t s_nextId = 1;
    bool s_sound = true;
    Pending s_q[QUEUE_MAX];
    int s_qn = 0;
    ReminderView s_view = {};
    uint32_t s_seq = 0;
    uint32_t s_gapUntilMs = 0;
    uint8_t s_repeatsLeft = 0;
    uint32_t s_nextRepeatMs = 0;
    time_t s_lastScan = 0;

    struct Lock
    {
        SemaphoreHandle_t m;
        explicit Lock(SemaphoreHandle_t mm) : m(mm) { xSemaphoreTake(m, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(m); }
    };

    bool timeSynced()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool t = g_state.timeSynced;
        xSemaphoreGive(g_stateMutex);
        return t;
    }

    int32_t makeKey(const struct tm &day, int hour, int minute)
    {
        return ((int32_t)day.tm_year * 366 + day.tm_yday) * 1440 + hour * 60 + minute;
    }

    bool validDate(uint32_t d)
    {
        int y = d / 10000, m = (d / 100) % 100, day = d % 100;
        if (y < 2020 || y > 2099 || m < 1 || m > 12 || day < 1)
            return false;
        static const uint8_t dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int max = dm[m - 1] + ((m == 2 && y % 4 == 0) ? 1 : 0); // 2020-2099: every 4th year is a leap year
        return day <= max;
    }

    time_t oneShotTime(const Reminder &r)
    {
        struct tm st = {};
        st.tm_year = (int)(r.date / 10000) - 1900;
        st.tm_mon = (int)((r.date / 100) % 100) - 1;
        st.tm_mday = (int)(r.date % 100);
        st.tm_hour = r.hour;
        st.tm_min = r.minute;
        st.tm_isdst = -1;
        return mktime(&st);
    }

    // ---- NVS ----
    bool persist() // caller holds s_mtx
    {
        Packed *pk = (Packed *)calloc(REMINDER_MAX, sizeof(Packed));
        if (!pk)
            return false;
        for (int i = 0; i < s_n; i++)
        {
            pk[i].id = s_e[i].r.id;
            pk[i].enabled = s_e[i].r.enabled;
            pk[i].hour = s_e[i].r.hour;
            pk[i].minute = s_e[i].r.minute;
            pk[i].days = s_e[i].r.days;
            pk[i].date = s_e[i].r.date;
            pk[i].lastKey = s_e[i].lastKey;
            memcpy(pk[i].text, s_e[i].r.text, REMINDER_TEXT_MAX);
        }
        Preferences p;
        bool ok = p.begin(NVS_NS, false);
        if (ok)
        {
            ok = p.putBytes("b", pk, (size_t)s_n * sizeof(Packed)) == (size_t)s_n * sizeof(Packed) || s_n == 0;
            if (s_n == 0)
                p.remove("b");
            p.putUChar("n", s_nextId);
            p.putBool("snd", s_sound);
            p.end();
        }
        free(pk);
        return ok;
    }

    // Checks / cleans a reminder. last = oldest time allowed for a one-time reminder (0 = no check).
    bool validate(Reminder &r, const char **err)
    {
        if (r.hour > 23 || r.minute > 59)
        {
            *err = "time must be 00:00-23:59";
            return false;
        }
        if (r.days > 0x7F)
        {
            *err = "invalid weekdays";
            return false;
        }
        char clean[REMINDER_TEXT_MAX];
        r.text[REMINDER_TEXT_MAX - 1] = '\0';
        int n = fa::sanitize(r.text, clean, sizeof(clean));
        if (n == -1)
        {
            *err = "text is not valid UTF-8";
            return false;
        }
        if (n == -2)
        {
            *err = "text too long";
            return false;
        }
        if (n == 0)
        {
            *err = "text is empty";
            return false;
        }
        memcpy(r.text, clean, sizeof(clean));
        if (r.days == 0)
        {
            if (!validDate(r.date))
            {
                *err = "pick a date for a one-time reminder";
                return false;
            }
            if (timeSynced() && oneShotTime(r) < time(nullptr) - 60)
            {
                *err = "that time has already passed";
                return false;
            }
        }
        else
            r.date = 0;
        return true;
    }

    // A weekly reminder saved after its time already passed today waits for the next day.
    int32_t initialKey(const Reminder &r)
    {
        if (r.days == 0 || !timeSynced())
            return -1;
        time_t nowT = time(nullptr);
        struct tm ti;
        localtime_r(&nowT, &ti);
        int late = (ti.tm_hour * 60 + ti.tm_min) - (r.hour * 60 + r.minute);
        if (late < 0)
            late += 1440;
        if (late == 0)
            return -1; // this very minute: shows now
        time_t sched = nowT - (time_t)late * 60 - ti.tm_sec;
        struct tm st;
        localtime_r(&sched, &st);
        return makeKey(st, r.hour, r.minute);
    }

    // ---- queue / view ----
    void enqueue(uint8_t id, bool test, uint8_t hour, uint8_t minute, const char *text) // caller holds s_mtx
    {
        if (!test)
        {
            for (int i = 0; i < s_qn; i++)
                if (s_q[i].id == id)
                    return; // already waiting
            xSemaphoreTake(s_viewMtx, portMAX_DELAY);
            bool showing = s_view.active && s_view.id == id && !s_view.test;
            xSemaphoreGive(s_viewMtx);
            if (showing)
                return;
        }
        if (s_qn >= QUEUE_MAX)
        {
            Serial.println("Reminders: queue full, one dropped");
            return;
        }
        Pending &p = s_q[s_qn++];
        p.id = id;
        p.test = test;
        p.hour = hour;
        p.minute = minute;
        strlcpy(p.text, text, sizeof(p.text));
    }

    void scanDue(time_t nowT) // caller holds s_mtx
    {
        struct tm ti;
        localtime_r(&nowT, &ti);
        bool dirty = false;
        for (int i = 0; i < s_n; i++)
        {
            Entry &e = s_e[i];
            if (!e.r.enabled)
                continue;
            if (e.r.days)
            {
                int late = (ti.tm_hour * 60 + ti.tm_min) - (e.r.hour * 60 + e.r.minute);
                if (late < 0)
                    late += 1440;
                if (late > REMINDER_GRACE_MIN)
                    continue;
                time_t sched = nowT - (time_t)late * 60 - ti.tm_sec;
                struct tm st;
                localtime_r(&sched, &st);
                if (!((e.r.days >> st.tm_wday) & 1))
                    continue;
                int32_t key = makeKey(st, e.r.hour, e.r.minute);
                if (key == e.lastKey)
                    continue;
                e.lastKey = key;
                dirty = true;
                enqueue(e.r.id, false, e.r.hour, e.r.minute, e.r.text);
            }
            else
            {
                long diff = (long)(nowT - oneShotTime(e.r));
                if (diff < 0)
                    continue;
                if (diff <= (long)REMINDER_ONESHOT_LATE_MIN * 60)
                    enqueue(e.r.id, false, e.r.hour, e.r.minute, e.r.text);
                for (int k = i; k + 1 < s_n; k++) // shown (or too old): it is done
                    s_e[k] = s_e[k + 1];
                s_n--;
                i--;
                dirty = true;
            }
        }
        if (dirty && !persist())
            Serial.println("Reminders: saving to flash failed");
    }

    void playSound()
    {
#if SOUND_INDEX_REMINDER
        if (s_sound)
            audio_play_gentle(SOUND_INDEX_REMINDER);
#endif
    }
} // namespace

// ---------------------------------------------------------------------------------------------------------

void reminders_init()
{
    s_mtx = xSemaphoreCreateMutex();
    s_viewMtx = xSemaphoreCreateMutex();
    s_n = 0;
    s_qn = 0;
    memset(&s_view, 0, sizeof(s_view));
    Preferences p;
    if (!p.begin(NVS_NS, false))
        return;
    s_sound = p.getBool("snd", true);
    s_nextId = p.getUChar("n", 1);
    if (s_nextId == 0)
        s_nextId = 1;
    size_t len = p.getBytesLength("b");
    if (len > 0 && len % sizeof(Packed) == 0 && len / sizeof(Packed) <= REMINDER_MAX)
    {
        Packed *pk = (Packed *)malloc(len);
        if (pk && p.getBytes("b", pk, len) == len)
        {
            for (size_t i = 0; i < len / sizeof(Packed); i++)
            {
                Entry &e = s_e[s_n];
                memset(&e, 0, sizeof(e));
                e.r.id = pk[i].id;
                e.r.enabled = pk[i].enabled != 0;
                e.r.hour = pk[i].hour;
                e.r.minute = pk[i].minute;
                e.r.days = pk[i].days;
                e.r.date = pk[i].date;
                memcpy(e.r.text, pk[i].text, REMINDER_TEXT_MAX);
                e.r.text[REMINDER_TEXT_MAX - 1] = '\0';
                e.lastKey = pk[i].lastKey;
                if (e.r.id != 0 && e.r.hour <= 23 && e.r.minute <= 59 && e.r.days <= 0x7F && e.r.text[0])
                    s_n++;
            }
        }
        free(pk);
    }
    p.end();
    Serial.printf("Reminders: %d stored\n", s_n);
}

int reminders_list(Reminder *out, int cap)
{
    Lock l(s_mtx);
    int n = s_n < cap ? s_n : cap;
    for (int i = 0; i < n; i++)
        out[i] = s_e[i].r;
    return n;
}

bool reminders_add(Reminder &r, const char **err)
{
    Lock l(s_mtx);
    if (s_n >= REMINDER_MAX)
    {
        *err = "too many reminders";
        return false;
    }
    if (!validate(r, err))
        return false;
    uint8_t id = 0;
    for (int tries = 0; tries < 256 && id == 0; tries++) // next free id (ids are never reused right away)
    {
        bool used = false;
        for (int i = 0; i < s_n; i++)
            used |= s_e[i].r.id == s_nextId;
        if (!used)
            id = s_nextId;
        s_nextId = s_nextId == 255 ? 1 : s_nextId + 1;
    }
    r.id = id;
    Entry &e = s_e[s_n];
    e.r = r;
    e.lastKey = initialKey(r);
    s_n++;
    if (!persist())
    {
        s_n--;
        *err = "saving to flash failed";
        return false;
    }
    return true;
}

bool reminders_update(const Reminder &in, const char **err)
{
    Lock l(s_mtx);
    int idx = -1;
    for (int i = 0; i < s_n; i++)
        if (s_e[i].r.id == in.id)
            idx = i;
    if (idx < 0)
    {
        *err = "no such reminder";
        return false;
    }
    Reminder r = in;
    if (!validate(r, err))
        return false;
    Entry old = s_e[idx];
    s_e[idx].r = r;
    s_e[idx].lastKey = initialKey(r);
    if (!persist())
    {
        s_e[idx] = old;
        *err = "saving to flash failed";
        return false;
    }
    return true;
}

bool reminders_remove(uint8_t id)
{
    Lock l(s_mtx);
    for (int i = 0; i < s_n; i++)
    {
        if (s_e[i].r.id != id)
            continue;
        for (int k = i; k + 1 < s_n; k++)
            s_e[k] = s_e[k + 1];
        s_n--;
        persist();
        return true;
    }
    return false;
}

bool reminders_get_sound()
{
    Lock l(s_mtx);
    return s_sound;
}

void reminders_set_sound(bool on)
{
    Lock l(s_mtx);
    s_sound = on;
    persist();
}

bool reminder_get_active(ReminderView &v)
{
    Lock l(s_viewMtx);
    v = s_view;
    return v.active;
}

void reminder_dismiss()
{
    Lock l(s_viewMtx);
    if (s_view.active)
    {
        s_view.active = false;
        s_gapUntilMs = millis() + GAP_MS;
    }
}

bool reminder_consume_press()
{
    if (alarm_get_state() == AlarmState::RINGING)
        return false;
    Lock l(s_viewMtx);
    if (!s_view.active)
        return false;
    s_view.active = false;
    s_gapUntilMs = millis() + GAP_MS;
    return true;
}

bool reminder_request_test(const char *text)
{
    char clean[REMINDER_TEXT_MAX];
    if (fa::sanitize(text, clean, sizeof(clean)) <= 0)
        return false;
    time_t nowT = time(nullptr);
    struct tm ti;
    localtime_r(&nowT, &ti);
    Lock l(s_mtx);
    enqueue(0, true, (uint8_t)ti.tm_hour, (uint8_t)ti.tm_min, clean);
    return true;
}

void reminders_tick()
{
    uint32_t now = millis();

    // 1. the visible one: time out, repeat the sound
    xSemaphoreTake(s_viewMtx, portMAX_DELAY);
    bool active = s_view.active;
    if (active && s_view.timeoutMs && (uint32_t)(now - s_view.shownMs) >= s_view.timeoutMs)
    {
        s_view.active = active = false;
        s_gapUntilMs = now + GAP_MS;
        Serial.println("Reminders: closed by itself");
    }
    xSemaphoreGive(s_viewMtx);
    if (active && s_repeatsLeft && (int32_t)(now - s_nextRepeatMs) >= 0 && alarm_get_state() != AlarmState::RINGING)
    {
        s_repeatsLeft--;
        s_nextRepeatMs = now + (uint32_t)REMINDER_REPEAT_S * 1000UL;
        playSound();
    }

    Lock l(s_mtx);

    // 2. once a second: which reminders are due?
    time_t nowT = time(nullptr);
    if (nowT != s_lastScan && timeSynced())
    {
        s_lastScan = nowT;
        scanDue(nowT);
    }

    // 3. bring the next one up when the screen is free and the alarm is quiet
    xSemaphoreTake(s_viewMtx, portMAX_DELAY);
    active = s_view.active;
    if (!active && s_qn > 0 && (int32_t)(now - s_gapUntilMs) >= 0 && alarm_get_state() != AlarmState::RINGING)
    {
        Pending p = s_q[0];
        for (int i = 0; i + 1 < s_qn; i++)
            s_q[i] = s_q[i + 1];
        s_qn--;
        s_view.active = true;
        s_view.test = p.test;
        s_view.id = p.id;
        s_view.hour = p.hour;
        s_view.minute = p.minute;
        strlcpy(s_view.text, p.text, sizeof(s_view.text));
        s_view.seq = ++s_seq;
        s_view.shownMs = now ? now : 1;
        s_view.timeoutMs = (uint32_t)REMINDER_SHOW_S * 1000UL;
        active = true;
        s_repeatsLeft = REMINDER_REPEATS;
        s_nextRepeatMs = now + (uint32_t)REMINDER_REPEAT_S * 1000UL;
        Serial.printf("Reminders: showing #%u\n", (unsigned)p.id);
        s_view.waiting = (uint8_t)s_qn;
        xSemaphoreGive(s_viewMtx);
        playSound();
        return;
    }
    if (active)
        s_view.waiting = (uint8_t)s_qn;
    xSemaphoreGive(s_viewMtx);
}
