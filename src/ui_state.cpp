// ui_state.cpp - clock style + home view of the landscape UI.
#include "ui_state.h"
#include <Preferences.h>
#include "config.h"

#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE "deskclock"
#endif

static volatile uint8_t s_style = 0;
static volatile bool s_homeClock = false;

void ui_state_init()
{
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true))
    {
        uint8_t v = p.getUChar("cStyle", 0);
        p.end();
        if (v < (uint8_t)ClockStyle::COUNT)
            s_style = v;
    }
}

ClockStyle ui_clock_style() { return (ClockStyle)s_style; }

void ui_set_clock_style(ClockStyle s)
{
    if ((uint8_t)s >= (uint8_t)ClockStyle::COUNT)
        s = ClockStyle::SIMPLE;
    if (s_style == (uint8_t)s)
        return;
    s_style = (uint8_t)s;
    Preferences p;
    if (p.begin(NVS_NAMESPACE, false))
    {
        p.putUChar("cStyle", (uint8_t)s);
        p.end();
    }
}

void ui_next_clock_style()
{
    ui_set_clock_style((ClockStyle)(((uint8_t)s_style + 1) % (uint8_t)ClockStyle::COUNT));
}

const char *ui_clock_style_name(ClockStyle s)
{
    switch (s)
    {
    case ClockStyle::SIMPLE:
        return "Simple";
    case ClockStyle::ANALOG_CLOCK:
        return "Analog";
    case ClockStyle::MINIMAL:
        return "Minimal";
    case ClockStyle::RETRO:
        return "Retro";
    case ClockStyle::LED:
        return "LED";
    default:
        return "?";
    }
}

bool ui_home_is_clock() { return s_homeClock; }
void ui_set_home_clock(bool c) { s_homeClock = c; }
void ui_toggle_home() { s_homeClock = !s_homeClock; }
