#pragma once
#include <stdint.h>

// Quick menu: change the most used settings with the rotary knob alone, without opening the web page.
//
//   Clock page, tap the knob  -> the menu opens
//   turn                      -> move the cursor (or change the value being edited)
//   push                      -> toggle an ON/OFF row, start editing a value row, save the value being edited
//   hold                      -> leave the menu (an unsaved edit is dropped)
//   no input for QUICK_MENU_TIMEOUT_MS, or the alarm starts ringing -> the menu closes by itself
//
// Rows: Alarm ON/OFF | Time (hour, then minute) | Volume | Alarm volume | Pet reactions | Close
// Every change is saved to flash through settings_update() (the same path as the web page), so the alarm task
// and the audio code pick it up by themselves. Nothing is written to flash while a value is only being turned.
//
// All the functions below are called from the touch task; quickmenu_get() is called from the display task.

// ---- Tunables (move them to config.h if you like; the #ifndef lets config.h win) ----
#ifndef QUICK_MENU_ENABLED
#define QUICK_MENU_ENABLED 1 // 0 = a tap on the clock page does nothing (as before)
#endif
#ifndef QUICK_MENU_TIMEOUT_MS
#define QUICK_MENU_TIMEOUT_MS 15000 // no input for this long -> back to the clock
#endif
#ifndef QUICK_MENU_FAST_TURN_MS
#define QUICK_MENU_FAST_TURN_MS 90 // detents closer together than this count as a fast turn (minutes x5, volume x3)
#endif
#ifndef KNOB_CONTROLS_ALARM
#define KNOB_CONTROLS_ALARM 1 // 1 = while the alarm rings: push = snooze, hold = alarm off (like the touch pad)
#endif

enum class MenuItem : uint8_t
{
    ALARM_ON,
    ALARM_TIME,
    VOLUME,
    ALARM_VOLUME,
    PET_REACTIONS,
    CLOCK_STYLE, // landscape UI: push = next clock style (Simple, Analog, Minimal, Retro, LED)
    CLOSE,
    COUNT
};

// A copy of the menu state for the display task.
struct MenuView
{
    bool open;
    uint8_t cursor; // MenuItem
    bool editing;
    uint8_t field; // while editing the alarm time: 0 = hour, 1 = minute
    bool alarmOn;
    uint8_t alarmHour, alarmMinute;
    uint8_t volume;      // 0-30
    uint8_t alarmVolume; // 1-30
    bool petReactions;
    uint8_t clockStyle; // ClockStyle (ui_state.h)
    uint32_t seq; // changes with every change: the display redraws when it changes
};

void quickmenu_init(); // once, from touch_init()
void quickmenu_open();
void quickmenu_close();
bool quickmenu_is_open();
void quickmenu_turn(int steps); // +CW / -CCW detents
void quickmenu_press();         // short push
void quickmenu_tick(uint32_t now);
bool quickmenu_get(MenuView &v); // false when the menu is closed
