#pragma once

// User-changeable settings, kept in NVS (flash) so they survive reboots and
// editable from the web page (web_ui.cpp). config.h only supplies the
// factory defaults used until something is saved.
struct Settings
{
    bool alarmEnabled;
    int alarmHour;     // 0-23
    int alarmMinute;   // 0-59
    int alarmDays;     // weekday mask: bit0 = Sunday ... bit6 = Saturday (same numbering as tm_wday); 1-127
    int alarmSound;    // clip number on the audio module, 1..AUDIO_FILE_COUNT
    int alarmVolume;   // 1-30, volume of the alarm only
    int volume;        // 0-30, volume of the pet/touch sound only
    bool petReactions; // varied Pet button reactions (hearts, dizzy, sleepy, ...); off = always the classic happy face

    float latitude; // weather location
    float longitude;
    char city[48]; // display name only (UTF-8)

    char tzName[40];  // id picked in the web page (e.g. "Europe-Central", "custom")
    char tzPosix[64]; // POSIX TZ rule incl. DST, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
};

// Loads settings from NVS (falling back to config.h defaults) and applies the
// timezone. Call once from setup(), after g_stateMutex exists and before the
// tasks start.
void settings_init();

// Thread-safe snapshot of the current settings.
Settings settings_get();

// Validates, saves to NVS, and applies the timezone. On failure returns false
// and sets *err to a short English message; nothing is changed.
// Volume / weather side effects are the caller's job (see web_ui.cpp).
bool settings_update(const Settings &s, const char **err);