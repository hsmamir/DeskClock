// quick_menu.cpp - the rotary knob's quick menu (logic only; the drawing is in display_menu.cpp).
#include <Arduino.h>
#include "quick_menu.h"
#include "state.h"
#include "settings.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "config.h"

namespace
{
    struct Model
    {
        bool open = false;
        uint8_t cursor = 0;
        bool editing = false;
        uint8_t field = 0;
        // working copy of what the rows show; reloaded from the settings on open and after every save
        bool alarmOn = false;
        int hour = 0, minute = 0;
        int volume = 0, alarmVolume = 1;
        bool petReactions = true;
        uint32_t lastInputMs = 0;
        uint32_t lastTurnMs = 0;
        uint32_t seq = 0;
    };

    Model m;
    SemaphoreHandle_t mx = nullptr;

    struct Lock
    {
        Lock()
        {
            if (mx)
                xSemaphoreTake(mx, portMAX_DELAY);
        }
        ~Lock()
        {
            if (mx)
                xSemaphoreGive(mx);
        }
    };

    // Everything below that touches g_state / settings / audio is done OUTSIDE the lock above, so the display
    // task (which only copies the model) is never held up by a flash write or a 30 ms audio command.

    void setModeIf(DisplayMode from, DisplayMode to)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        if (g_state.displayMode == from)
            g_state.displayMode = to;
        xSemaphoreGive(g_stateMutex);
    }

    void loadFrom(const Settings &s) // caller holds the lock
    {
        m.alarmOn = s.alarmEnabled;
        m.hour = s.alarmHour;
        m.minute = s.alarmMinute;
        m.volume = s.volume;
        m.alarmVolume = s.alarmVolume;
        m.petReactions = s.petReactions;
    }

    int wrapInt(int v, int lo, int hi) // lo..hi inclusive, wraps around
    {
        int n = hi - lo + 1;
        v = (v - lo) % n;
        if (v < 0)
            v += n;
        return v + lo;
    }

    int clampInt(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Short feedback beep (same rules as the web page: never while the alarm rings / is snoozed, and it
    // follows the night-mute rules of audio_play_gentle()).
    void uiBeep(bool enabled, uint16_t clip)
    {
        if (!enabled || clip == 0 || alarm_get_state() != AlarmState::IDLE)
            return;
        audio_play_gentle(clip);
    }

    enum class Save : uint8_t
    {
        NONE,
        ALARM_ON,
        ALARM_TIME,
        VOLUME,
        ALARM_VOLUME,
        PET_REACTIONS
    };

    // Writes ONE changed value to flash. The rest of the settings is taken fresh from settings_get(), so a change
    // made on the web page in the meantime is not overwritten.
    void commit(Save what, bool alarmOn, int hour, int minute, int volume, int alarmVolume, bool pet)
    {
        Settings old = settings_get();
        Settings s = old;
        switch (what)
        {
        case Save::ALARM_ON:
            s.alarmEnabled = alarmOn;
            break;
        case Save::ALARM_TIME:
            s.alarmHour = hour;
            s.alarmMinute = minute;
            break;
        case Save::VOLUME:
            s.volume = volume;
            break;
        case Save::ALARM_VOLUME:
            s.alarmVolume = alarmVolume;
            break;
        case Save::PET_REACTIONS:
            s.petReactions = pet;
            break;
        default:
            return;
        }

        const char *err = "";
        if (!settings_update(s, &err))
        {
            Serial.printf("Quick menu: save failed (%s)\n", err);
            uiBeep(old.uiSounds, SOUND_INDEX_UI_ERROR);
        }
        else
        {
            Serial.printf("Quick menu: saved (%d)\n", (int)what);
            switch (what)
            {
            case Save::ALARM_ON:
                // same rule as the web page: "off" uses the OLD beep setting, "on" the new one
                uiBeep(alarmOn ? s.uiSounds : old.uiSounds, alarmOn ? SOUND_INDEX_UI_ON : SOUND_INDEX_UI_OFF);
                break;
            case Save::PET_REACTIONS:
                uiBeep(pet ? s.uiSounds : old.uiSounds, pet ? SOUND_INDEX_UI_ON : SOUND_INDEX_UI_OFF);
                break;
            case Save::ALARM_TIME:
                uiBeep(s.uiSounds, SOUND_INDEX_UI_ON);
                break;
            case Save::VOLUME:
                // you want to HEAR the new level, also at night, so this is a preview and not a "gentle" sound
                if (alarm_get_state() == AlarmState::IDLE)
                    audio_play_preview(SOUND_INDEX_PET, (uint8_t)volume);
                break;
            case Save::ALARM_VOLUME:
                if (alarm_get_state() == AlarmState::IDLE)
                    audio_play_preview((uint16_t)s.alarmSound, (uint8_t)alarmVolume);
                break;
            default:
                break;
            }
        }

        // show what is really stored now (also undoes the change on screen when saving failed)
        Settings now = settings_get();
        Lock l;
        loadFrom(now);
        m.seq++;
    }
} // namespace

void quickmenu_init()
{
    if (!mx)
        mx = xSemaphoreCreateMutex();
}

void quickmenu_open()
{
    if (!QUICK_MENU_ENABLED || !mx)
        return;
    Settings s = settings_get();
    {
        Lock l;
        m.open = true;
        m.cursor = 0;
        m.editing = false;
        m.field = 0;
        m.lastInputMs = millis();
        m.lastTurnMs = 0;
        loadFrom(s);
        m.seq++;
    }
    setModeIf(DisplayMode::CLOCK, DisplayMode::MENU);
}

void quickmenu_close()
{
    if (!mx)
        return;
    {
        Lock l;
        m.open = false;
        m.editing = false;
        m.seq++;
    }
    setModeIf(DisplayMode::MENU, DisplayMode::CLOCK);
}

bool quickmenu_is_open()
{
    if (!mx)
        return false;
    Lock l;
    return m.open;
}

void quickmenu_turn(int steps)
{
    if (!mx || steps == 0)
        return;
    uint32_t now = millis();
    Lock l;
    if (!m.open)
        return;
    m.lastInputMs = now;
    bool fast = m.lastTurnMs != 0 && (uint32_t)(now - m.lastTurnMs) < QUICK_MENU_FAST_TURN_MS;
    m.lastTurnMs = now;
    m.seq++;

    if (!m.editing)
    {
        // one row per call, however fast the knob was turned
        m.cursor = (uint8_t)wrapInt((int)m.cursor + (steps > 0 ? 1 : -1), 0, (int)MenuItem::COUNT - 1);
        return;
    }

    switch ((MenuItem)m.cursor)
    {
    case MenuItem::ALARM_TIME:
        if (m.field == 0)
            m.hour = wrapInt(m.hour + steps, 0, 23);
        else
            m.minute = wrapInt(m.minute + steps * (fast ? 5 : 1), 0, 59);
        break;
    case MenuItem::VOLUME:
        m.volume = clampInt(m.volume + steps * (fast ? 3 : 1), 0, 30);
        break;
    case MenuItem::ALARM_VOLUME:
        m.alarmVolume = clampInt(m.alarmVolume + steps * (fast ? 3 : 1), 1, 30);
        break;
    default:
        break;
    }
}

void quickmenu_press()
{
    if (!mx)
        return;
    Save what = Save::NONE;
    bool closeMenu = false;
    bool alarmOn, pet;
    int hour, minute, volume, alarmVolume;
    {
        Lock l;
        if (!m.open)
            return;
        m.lastInputMs = millis();
        m.seq++;
        if (!m.editing)
        {
            switch ((MenuItem)m.cursor)
            {
            case MenuItem::ALARM_ON:
                m.alarmOn = !m.alarmOn;
                what = Save::ALARM_ON;
                break;
            case MenuItem::PET_REACTIONS:
                m.petReactions = !m.petReactions;
                what = Save::PET_REACTIONS;
                break;
            case MenuItem::ALARM_TIME:
                m.editing = true;
                m.field = 0;
                break;
            case MenuItem::VOLUME:
            case MenuItem::ALARM_VOLUME:
                m.editing = true;
                break;
            case MenuItem::CLOSE:
            default:
                closeMenu = true;
                break;
            }
        }
        else
        {
            switch ((MenuItem)m.cursor)
            {
            case MenuItem::ALARM_TIME:
                if (m.field == 0)
                    m.field = 1; // hour done -> minute
                else
                {
                    m.editing = false;
                    what = Save::ALARM_TIME;
                }
                break;
            case MenuItem::VOLUME:
                m.editing = false;
                what = Save::VOLUME;
                break;
            case MenuItem::ALARM_VOLUME:
                m.editing = false;
                what = Save::ALARM_VOLUME;
                break;
            default:
                m.editing = false;
                break;
            }
        }
        alarmOn = m.alarmOn;
        pet = m.petReactions;
        hour = m.hour;
        minute = m.minute;
        volume = m.volume;
        alarmVolume = m.alarmVolume;
    }

    if (closeMenu)
    {
        quickmenu_close();
        return;
    }
    if (what != Save::NONE)
        commit(what, alarmOn, hour, minute, volume, alarmVolume, pet);
}

void quickmenu_tick(uint32_t now)
{
    if (!mx)
        return;
    bool open;
    uint32_t last;
    {
        Lock l;
        open = m.open;
        last = m.lastInputMs;
    }

    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    DisplayMode dm = g_state.displayMode;
    bool ringing = g_state.alarmActive;
    xSemaphoreGive(g_stateMutex);

    if (open && (ringing || dm != DisplayMode::MENU || (uint32_t)(now - last) >= QUICK_MENU_TIMEOUT_MS))
    {
        quickmenu_close(); // an edit that was not saved is dropped
        return;
    }
    if (!open && dm == DisplayMode::MENU)
        setModeIf(DisplayMode::MENU, DisplayMode::CLOCK); // never stay on a menu page nobody drives
}

bool quickmenu_get(MenuView &v)
{
    if (!mx)
    {
        v.open = false;
        return false;
    }
    Lock l;
    v.open = m.open;
    v.cursor = m.cursor;
    v.editing = m.editing;
    v.field = m.field;
    v.alarmOn = m.alarmOn;
    v.alarmHour = (uint8_t)m.hour;
    v.alarmMinute = (uint8_t)m.minute;
    v.volume = (uint8_t)m.volume;
    v.alarmVolume = (uint8_t)m.alarmVolume;
    v.petReactions = m.petReactions;
    v.seq = m.seq;
    return m.open;
}
