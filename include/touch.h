#pragma once

// Configures the three microswitch buttons as debounced INPUT_PULLUP pins
// (each wired GPIO -> switch -> GND, so a press reads LOW).
void touch_init();

// Called repeatedly from the touch task (short poll interval, e.g. 10ms).
// Detects debounced press edges on each button and updates g_state:
//   TOUCH_PET_PIN   -> Mood::PET_HAPPY (+ triggers servo pet pattern)
//   TOUCH_MODE_PIN  -> cycles g_state.displayMode
//   TOUCH_ALARM_PIN -> snoozes a ringing alarm (no effect when it isn't ringing)
void touch_tick();