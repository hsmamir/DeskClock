#pragma once
#include <stdint.h>

// Fixed yearly occasions with their own face animations (display_occasion.cpp).
// Not to be confused with the personal dates (birthdays ...) the user enters in the settings.
enum class Occasion : uint8_t
{
    NONE = 0,
    HALLOWEEN,    // 1
    EASTER,       // 2
    CHRISTMAS,    // 3
    NEW_YEAR_EVE, // 4
    VALENTINE,    // 5
    COUNT
};

// Shows an occasion right now for durationMs (settings page "preview"), whatever the date is.
// id 0 = stop the preview. For NEW_YEAR_EVE the preview runs the whole story:
// fireworks -> 10..1 countdown -> the new year. Safe to call from the web task.
void occasion_preview(uint8_t id, uint32_t durationMs);