#pragma once

// Input devices:
//   - TTP223 touch pad (TOUCH_PAD_PIN, active HIGH): Pet + Snooze in one pad
//   - KY-040 rotary encoder (ROTARY_CLK/DT/SW_PIN): all page / Mode control
// touch_init() sets up the pins (the encoder's CLK/DT get an interrupt + a
// debounce-proof quadrature decoder, so fast turns are not missed).
void touch_init();

// Called repeatedly from the touch task (short poll interval, e.g. 10ms).
// Updates g_state:
//   Touch pad
//     alarm ringing   : touch = snooze at once; keep touching ALARM_LONG_PRESS_MS = alarm off
//     alarm snoozed   : touch = pet; hold ALARM_LONG_PRESS_MS = alarm off (instead of purring,
//                       see PAD_HOLD_DISMISSES_SNOOZE in config.h)
//     otherwise       : touch = Mood::PET_HAPPY (+ servo pet pattern);
//                       hold PET_PURR_HOLD_MS = purring
//     a visible reminder is closed by the touch and the touch is used up
//   Rotary encoder
//     turn clockwise         : next page      Clock -> Date -> Weather -> Clock
//     turn counter-clockwise : previous page  Clock -> Weather -> Date -> Clock
//     (from the diagnostics page any turn goes back to the clock)
//     push (tap)             : back to the clock
//     push and hold          : diagnostics page on/off (a ring fills while holding)
//     a visible reminder is closed by a turn or a push and that input is used up
//   With no input for DISPLAY_RETURN_MS / DIAG_RETURN_MS the display returns to the
//   clock by itself.
void touch_tick();