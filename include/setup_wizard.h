#pragma once
// setup_wizard.h - the first-boot setup of the landscape UI. It runs by itself the first time the clock starts (and
// the first time after a factory reset), as soon as the boot wake-up and the WiFi setup are over, and takes the whole
// screen. The knob does everything:
//
//   turn  = change the value on the page      push = next page      hold = one page back (on the first page: skip)
//
//   Welcome -> Place (city = weather location + time zone) -> Volume -> Clock style (the real clock, live)
//           -> Alarm (on / off, hour, minute) -> All set
//
// Everything is saved at the end ("All set" + push). Skipping keeps the defaults. Either way the wizard never comes
// back (settings_setup_done()). The page is drawn by display_setup.cpp; this file is the model and the input.
#include <Arduino.h>

#ifndef SETUP_WIZARD_ENABLED
#define SETUP_WIZARD_ENABLED 1 // 0 = the clock never starts the first-boot setup
#endif
#ifndef SETUP_START_DELAY_MS
#define SETUP_START_DELAY_MS 6000 // wait for the boot wake-up animation
#endif

enum class SetupStep : uint8_t
{
    WELCOME,
    PLACE,
    VOLUME,
    STYLE,
    ALARM,
    DONE,
    COUNT
};

struct SetupView
{
    bool active;
    uint8_t step;   // SetupStep
    uint8_t place;  // index into the place list
    uint8_t volume; // 0-30
    uint8_t style;  // ClockStyle
    bool alarmOn;
    uint8_t alarmHour, alarmMinute;
    uint8_t field;  // ALARM page: 0 = on / off, 1 = hour, 2 = minute
    uint32_t seq;   // changes with every change: the display redraws when it changes
};

// The place list (city, its UTC offset as text): one entry sets the weather location and the time zone.
int setup_place_count();
const char *setup_place_name(int i);
const char *setup_place_utc(int i); // e.g. "+3:30", "+1/+2" (with daylight saving)

void setup_wizard_init(); // once (touch_init)
bool setup_is_active();
bool setup_get(SetupView &v); // false when the wizard is not up
void setup_tick(uint32_t now); // touch task: starts the wizard when it is time, plays the volume preview
void setup_turn(int steps);
void setup_press();
void setup_back(); // hold
