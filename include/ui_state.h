#pragma once
// ui_state.h - the two choices of the landscape UI that survive a reboot / are shared between tasks:
//   * which clock style is shown (Simple, Analog, Minimal, Retro, LED)
//   * which "home" view is up: the face (eyes) or the clock
#include <Arduino.h>

enum class ClockStyle : uint8_t
{
    SIMPLE,
    ANALOG_CLOCK,
    MINIMAL,
    RETRO,
    LED,
    COUNT
};

void ui_state_init();                    // once, from setup() (reads the saved clock style from NVS)
ClockStyle ui_clock_style();             // current style
void ui_set_clock_style(ClockStyle s);   // changes it and saves it
void ui_next_clock_style();              // Simple -> Analog -> ... -> LED -> Simple (saved)
const char *ui_clock_style_name(ClockStyle s);

// Home view: false = the face (eyes), true = the clock. The knob / mode button toggles it.
bool ui_home_is_clock();
void ui_set_home_clock(bool clockView);
void ui_toggle_home();
