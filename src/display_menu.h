#pragma once
// The quick menu page (display_menu.cpp). Included by display.cpp only.
#include <stdint.h>

namespace disp
{
    // Draws the menu page (both cards, one seamless page). Redraws only when something visible changed.
    void drawMenuScreen(uint32_t now);
    // Call every tick the menu is NOT shown, so it is drawn from scratch the next time it opens.
    void menuScreenLeft();
} // namespace disp
