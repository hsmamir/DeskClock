#pragma once
#include <stdint.h>

// Pet button reactions. touch.cpp calls pet_on_press() on every tap; this module
// decides WHICH reaction it is (streak, time of day, weather, rare chance, ...) and keeps
// the statistics. display.cpp only draws the reaction it finds in pet_get_event().

enum class PetKind : uint8_t
{
    HAPPY,         // the classic happy face + rising hearts
    HEART_EYES,    // several taps in a row: hearts for eyes + heart burst
    SLEEPY,        // night: contented closed eyes, one small heart, z z Z
    RAISED_BROW,   // rain: one raised eyebrow, then a smile
    DIZZY,         // far too many taps: spiral eyes, circling stars
    ANNOYED,       // even more: "that's enough!"
    MISSED_YOU,    // first tap after a long absence: wide eyes, bounce, heart burst
    MORNING,       // first tap of the morning: yawn, then happy
    RARE_WINK,     // rare: wink with tongue
    RARE_SURPRISE, // rare: surprise, then a big heart pops up
    HOT,           // hot weather: happy + sweat drop
    COLD           // cold weather: happy + shivering + snowflakes
};

struct PetEvent
{
    PetKind kind;
    uint32_t startMs; // millis() of the tap
    uint32_t seq;     // grows with every tap: the display restarts the animation when it changes
    uint16_t streak;  // taps in a row, including this one
};

struct PetStats
{
    uint16_t today;      // taps today (local date; 0 while the clock is unknown after a reboot)
    uint16_t bestStreak; // longest streak ever
    uint16_t streak;     // current streak
    uint32_t total;      // taps ever
};

// Loads the saved statistics. Call once from setup(), after settings_init().
void pet_init();

// A tap (call from a task, not while the alarm rings). Chooses the reaction and updates
// the statistics. Reads the clock / weather itself.
void pet_on_press(uint32_t nowMs);

// Latest reaction (thread-safe snapshot).
PetEvent pet_get_event();

// How long the display keeps the face of this reaction.
uint32_t pet_hold_ms(PetKind kind);

PetStats pet_stats();

// Call regularly (touch task): writes changed statistics to flash, at most once per
// PET_SAVE_INTERVAL_MS.
void pet_tick(uint32_t nowMs);
