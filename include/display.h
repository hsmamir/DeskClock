#pragma once

// Initializes the TFT_eSPI display and backlight PWM.
void display_init();

// Called repeatedly from the display task. Renders eyes/face and the
// current DisplayMode based on g_state. Non-blocking, no delay() inside.
void display_tick();
