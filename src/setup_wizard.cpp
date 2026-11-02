// setup_wizard.cpp - model and input of the first-boot setup (see setup_wizard.h).
#include <Arduino.h>
#include <string.h>
#include "setup_wizard.h"
#include "quick_menu.h"
#include "ui_state.h"
#include "state.h"
#include "settings.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "config.h"

namespace
{
    // One entry = city name (the weather location) + its time zone. The zone ids and rules are the ones of the web
    // page's dropdown, so the settings page shows the right zone afterwards. The first entry is the default.
    struct Place
    {
        const char *name;
        float lat, lon;
        const char *tzId;
        const char *tzPosix;
        const char *utc; // shown next to the name
    };
    const char *const TZ_THR = "<+0330>-3:30";
    const Place kPlaces[] = {
        {"Tehran", 35.6892f, 51.3890f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Mashhad", 36.2605f, 59.6168f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Isfahan", 32.6546f, 51.6680f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Shiraz", 29.5918f, 52.5837f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Tabriz", 38.0962f, 46.2738f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Karaj", 35.8400f, 50.9391f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Ahvaz", 31.3183f, 48.6706f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Qom", 34.6416f, 50.8746f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Rasht", 37.2808f, 49.5832f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Kermanshah", 34.3142f, 47.0650f, "Asia-Tehran", TZ_THR, "+3:30"},
        {"Kabul", 34.5553f, 69.2075f, "Asia-Kabul", "<+0430>-4:30", "+4:30"},
        {"Baghdad", 33.3152f, 44.3661f, "UTC+3", "<+03>-3", "+3"},
        {"Istanbul", 41.0082f, 28.9784f, "UTC+3", "<+03>-3", "+3"},
        {"Riyadh", 24.7136f, 46.6753f, "UTC+3", "<+03>-3", "+3"},
        {"Moscow", 55.7558f, 37.6173f, "UTC+3", "<+03>-3", "+3"},
        {"Dubai", 25.2048f, 55.2708f, "UTC+4", "<+04>-4", "+4"},
        {"Baku", 40.4093f, 49.8671f, "UTC+4", "<+04>-4", "+4"},
        {"Karachi", 24.8607f, 67.0011f, "UTC+5", "<+05>-5", "+5"},
        {"Delhi", 28.6139f, 77.2090f, "Asia-Kolkata", "IST-5:30", "+5:30"},
        {"Bangkok", 13.7563f, 100.5018f, "UTC+7", "<+07>-7", "+7"},
        {"Singapore", 1.3521f, 103.8198f, "UTC+8", "<+08>-8", "+8"},
        {"Shanghai", 31.2304f, 121.4737f, "UTC+8", "<+08>-8", "+8"},
        {"Tokyo", 35.6762f, 139.6503f, "UTC+9", "<+09>-9", "+9"},
        {"Seoul", 37.5665f, 126.9780f, "UTC+9", "<+09>-9", "+9"},
        {"Sydney", -33.8688f, 151.2093f, "Australia-East", "AEST-10AEDT,M10.1.0,M4.1.0/3", "+10/+11"},
        {"Auckland", -36.8485f, 174.7633f, "Pacific-Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3", "+12/+13"},
        {"Cairo", 30.0444f, 31.2357f, "Africa-Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24", "+2/+3"},
        {"Athens", 37.9838f, 23.7275f, "Europe-Eastern", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+2/+3"},
        {"Berlin", 52.5200f, 13.4050f, "Europe-Central", "CET-1CEST,M3.5.0,M10.5.0/3", "+1/+2"},
        {"Paris", 48.8566f, 2.3522f, "Europe-Central", "CET-1CEST,M3.5.0,M10.5.0/3", "+1/+2"},
        {"Rome", 41.9028f, 12.4964f, "Europe-Central", "CET-1CEST,M3.5.0,M10.5.0/3", "+1/+2"},
        {"Madrid", 40.4168f, -3.7038f, "Europe-Central", "CET-1CEST,M3.5.0,M10.5.0/3", "+1/+2"},
        {"Amsterdam", 52.3676f, 4.9041f, "Europe-Central", "CET-1CEST,M3.5.0,M10.5.0/3", "+1/+2"},
        {"London", 51.5072f, -0.1276f, "Europe-London", "GMT0BST,M3.5.0/1,M10.5.0", "0/+1"},
        {"Sao Paulo", -23.5505f, -46.6333f, "UTC-3", "<-03>3", "-3"},
        {"New York", 40.7128f, -74.0060f, "US-Eastern", "EST5EDT,M3.2.0,M11.1.0", "-5/-4"},
        {"Toronto", 43.6532f, -79.3832f, "US-Eastern", "EST5EDT,M3.2.0,M11.1.0", "-5/-4"},
        {"Chicago", 41.8781f, -87.6298f, "US-Central", "CST6CDT,M3.2.0,M11.1.0", "-6/-5"},
        {"Denver", 39.7392f, -104.9903f, "US-Mountain", "MST7MDT,M3.2.0,M11.1.0", "-7/-6"},
        {"Los Angeles", 34.0522f, -118.2437f, "US-Pacific", "PST8PDT,M3.2.0,M11.1.0", "-8/-7"},
        {"Vancouver", 49.2827f, -123.1207f, "US-Pacific", "PST8PDT,M3.2.0,M11.1.0", "-8/-7"},
    };
    const int kPlaceCount = (int)(sizeof(kPlaces) / sizeof(kPlaces[0]));

    struct Model
    {
        bool needed = false; // the first-boot setup has not been done (or skipped) yet
        bool active = false;
        uint8_t step = 0;
        uint8_t place = 0;
        uint8_t volume = 30;
        uint8_t style = 0;
        bool alarmOn = false;
        uint8_t hour = 7, minute = 0;
        uint8_t field = 0;
        bool previewPending = false;
        uint32_t turnMs = 0;
        uint32_t seq = 0;
    } m;

    SemaphoreHandle_t mx = nullptr;
    struct Lock
    {
        Lock() { xSemaphoreTake(mx, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(mx); }
    };

    int wrapInt(int v, int lo, int hi) // lo..hi inclusive, wraps around
    {
        const int n = hi - lo + 1;
        v = (v - lo) % n;
        if (v < 0)
            v += n;
        return v + lo;
    }
    int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void setMode(DisplayMode to)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        g_state.displayMode = to;
        xSemaphoreGive(g_stateMutex);
    }

    bool portalOpen() { return g_state.wifiPortalActive; }

    // Leaves the wizard: save = write the choices (place, volume, alarm); the clock style was applied while choosing.
    void finish(bool save)
    {
        uint8_t place, volume, hour, minute;
        bool alarmOn;
        {
            Lock l;
            if (!m.active)
                return;
            place = m.place;
            volume = m.volume;
            alarmOn = m.alarmOn;
            hour = m.hour;
            minute = m.minute;
            m.active = false;
            m.needed = false;
            m.seq++;
        }
        bool beep = false;
        if (save)
        {
            Settings s = settings_get();
            const Place &p = kPlaces[place < kPlaceCount ? place : 0];
            s.volume = volume;
            s.alarmEnabled = alarmOn;
            s.alarmHour = hour;
            s.alarmMinute = minute;
            snprintf(s.city, sizeof(s.city), "%s", p.name);
            s.latitude = p.lat;
            s.longitude = p.lon;
            snprintf(s.tzName, sizeof(s.tzName), "%s", p.tzId);
            snprintf(s.tzPosix, sizeof(s.tzPosix), "%s", p.tzPosix);
            const char *err = "";
            if (settings_update(s, &err))
                beep = s.uiSounds;
            else
                Serial.printf("Setup: saving failed (%s)\n", err);
        }
        settings_mark_setup_done(); // also when skipped or when saving failed: the wizard never comes back
        setMode(DisplayMode::CLOCK);
        if (beep && alarm_get_state() == AlarmState::IDLE)
            audio_play_gentle(SOUND_INDEX_UI_ON);
        Serial.printf("Setup: %s\n", save ? "finished" : "skipped");
    }
} // namespace

int setup_place_count() { return kPlaceCount; }
const char *setup_place_name(int i) { return kPlaces[wrapInt(i, 0, kPlaceCount - 1)].name; }
const char *setup_place_utc(int i) { return kPlaces[wrapInt(i, 0, kPlaceCount - 1)].utc; }

void setup_wizard_init()
{
    mx = xSemaphoreCreateMutex();
    m.needed = SETUP_WIZARD_ENABLED && LANDSCAPE_UI && !settings_setup_done();
    Serial.printf("Setup: first-boot setup %s\n", m.needed ? "needed" : "not needed");
}

bool setup_is_active()
{
    if (!mx)
        return false;
    Lock l;
    return m.active;
}

bool setup_get(SetupView &v)
{
    if (!mx)
        return false;
    Lock l;
    v.active = m.active;
    v.step = m.step;
    v.place = m.place;
    v.volume = m.volume;
    v.style = (uint8_t)ui_clock_style();
    v.alarmOn = m.alarmOn;
    v.alarmHour = m.hour;
    v.alarmMinute = m.minute;
    v.field = m.field;
    v.seq = m.seq;
    return m.active;
}

void setup_tick(uint32_t now)
{
    if (!mx)
        return;

    bool preview = false;
    uint8_t vol = 0;
    bool tryStart;
    {
        Lock l;
        if (m.active && m.previewPending && (uint32_t)(now - m.turnMs) >= 350) // the turning has stopped: let him hear it
        {
            m.previewPending = false;
            preview = true;
            vol = m.volume;
        }
        tryStart = m.needed && !m.active && now >= SETUP_START_DELAY_MS;
    }
    if (preview && alarm_get_state() == AlarmState::IDLE)
        audio_play_preview(SOUND_INDEX_PET, vol);
    if (!tryStart)
        return;

    // Start only on a quiet clock page: no alarm, no WiFi portal (that comes first), no menu; the time must be known
    // (WiFi is up) - or give up waiting after a minute, a clock without WiFi needs its setup too.
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    const bool quiet = g_state.displayMode == DisplayMode::CLOCK && !g_state.alarmActive && !g_state.wifiPortalActive &&
                       (g_state.timeSynced || now >= 60000UL);
    xSemaphoreGive(g_stateMutex);
    if (!quiet || quickmenu_is_open())
        return;

    const Settings s = settings_get();
    {
        Lock l;
        m.active = true;
        m.step = (uint8_t)SetupStep::WELCOME;
        m.field = 0;
        m.place = 0;
        for (int i = 0; i < kPlaceCount; i++) // start on the place that is set now (the default one on a new clock)
            if (strcmp(kPlaces[i].name, s.city) == 0)
                m.place = (uint8_t)i;
        m.volume = (uint8_t)clampInt(s.volume, 0, 30);
        m.alarmOn = s.alarmEnabled;
        m.hour = s.alarmHour;
        m.minute = s.alarmMinute;
        m.previewPending = false;
        m.seq++;
    }
    setMode(DisplayMode::SETUP);
    Serial.println("Setup: first-boot setup started");
}

void setup_turn(int steps)
{
    if (!mx || steps == 0 || portalOpen())
        return;
    int newStyle = -1;
    {
        Lock l;
        if (!m.active)
            return;
        switch ((SetupStep)m.step)
        {
        case SetupStep::PLACE:
            m.place = (uint8_t)wrapInt((int)m.place + steps, 0, kPlaceCount - 1);
            break;
        case SetupStep::VOLUME:
            m.volume = (uint8_t)clampInt((int)m.volume + steps, 0, 30);
            m.previewPending = true;
            m.turnMs = millis();
            break;
        case SetupStep::STYLE:
            newStyle = wrapInt((int)ui_clock_style() + (steps > 0 ? 1 : -1), 0, (int)ClockStyle::COUNT - 1);
            break;
        case SetupStep::ALARM:
            if (m.field == 0)
                m.alarmOn = !m.alarmOn;
            else if (m.field == 1)
                m.hour = (uint8_t)wrapInt((int)m.hour + steps, 0, 23);
            else
                m.minute = (uint8_t)wrapInt((int)m.minute + steps, 0, 59);
            break;
        default:
            return; // Welcome / All set: nothing to turn
        }
        m.seq++;
    }
    if (newStyle >= 0) // the real clock page shows it at once (and it is saved)
    {
        ui_set_clock_style((ClockStyle)newStyle);
        Lock l;
        m.seq++; // the display redraws now that the style really changed
    }
}

void setup_press()
{
    if (!mx || portalOpen())
        return;
    bool finishNow = false;
    {
        Lock l;
        if (!m.active)
            return;
        switch ((SetupStep)m.step)
        {
        case SetupStep::WELCOME:
            m.step = (uint8_t)SetupStep::PLACE;
            break;
        case SetupStep::PLACE:
            m.step = (uint8_t)SetupStep::VOLUME;
            break;
        case SetupStep::VOLUME:
            m.step = (uint8_t)SetupStep::STYLE;
            break;
        case SetupStep::STYLE:
            m.step = (uint8_t)SetupStep::ALARM;
            m.field = 0;
            break;
        case SetupStep::ALARM:
            if (m.field == 0 && m.alarmOn)
                m.field = 1;
            else if (m.field == 1)
                m.field = 2;
            else
                m.step = (uint8_t)SetupStep::DONE;
            break;
        default: // All set
            finishNow = true;
            break;
        }
        m.seq++;
    }
    if (finishNow)
        finish(true);
}

void setup_back()
{
    if (!mx || portalOpen())
        return;
    bool skip = false;
    {
        Lock l;
        if (!m.active)
            return;
        switch ((SetupStep)m.step)
        {
        case SetupStep::WELCOME:
            skip = true; // hold on the first page = skip the whole setup
            break;
        case SetupStep::PLACE:
            m.step = (uint8_t)SetupStep::WELCOME;
            break;
        case SetupStep::VOLUME:
            m.step = (uint8_t)SetupStep::PLACE;
            break;
        case SetupStep::STYLE:
            m.step = (uint8_t)SetupStep::VOLUME;
            break;
        case SetupStep::ALARM:
            if (m.field > 0)
                m.field--;
            else
                m.step = (uint8_t)SetupStep::STYLE;
            break;
        default: // All set -> back to the last alarm field
            m.step = (uint8_t)SetupStep::ALARM;
            m.field = m.alarmOn ? 2 : 0;
            break;
        }
        m.seq++;
    }
    if (skip)
        finish(false);
}
