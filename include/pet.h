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
    COLD,          // cold weather: happy + shivering + snowflakes
    SORRY          // the first touch after sulking: a small apology (chosen by the display, not by pet.cpp)
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

// Shows a reaction without a tap (e.g. MORNING after the alarm was switched off). streak = 0 marks it as
// injected; no statistics change. Safe from any task.
void pet_inject_event(PetKind kind);
// The display replaces the kind of the reaction that was just picked (e.g. SORRY). Same seq, so nothing restarts.
void pet_set_kind(PetKind kind);
// How long the display keeps the face of this reaction.
uint32_t pet_hold_ms(PetKind kind);

PetStats pet_stats();

// ---- Bond: how much he likes you (see "Bond" in config.h) ----
enum class BondLevel : uint8_t
{
    LONELY,   // neglected: sighs now and then, a washed-out eye colour, sulks longer
    FRIENDLY, // the starting level
    FOND,     // more patient, a faint pink in the eyes, smiles by himself
    DEVOTED   // very patient, pinker eyes, smiles / heart eyes by himself, forgives quickly
};

struct PetBond
{
    uint16_t value;  // 0..1000
    BondLevel level; // derived from value
    bool enabled;    // false when the feature is off (PET_BOND_ENABLED = 0 or "varied reactions" switched off)
};

// Thread-safe snapshot.
PetBond pet_bond();

// Changes the bond (delta in the 0..1000 scale). useDailyCap: a gain counts against PET_BOND_DAILY_CAP. Does nothing
// while the feature is off. Safe from any task (not from inside pet.cpp's own mutex).
void pet_bond_add(int delta, bool useDailyCap);

// Call regularly (touch task): writes changed statistics to flash, at most once per
// PET_SAVE_INTERVAL_MS.
void pet_tick(uint32_t nowMs);