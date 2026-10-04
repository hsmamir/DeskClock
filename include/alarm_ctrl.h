#pragma once

// Alarm scheduler. Rings at the alarm time from settings (see settings.h;
// config.h only holds the factory default), plays
// SOUND_INDEX_ALARM on repeat, and drives g_state.alarmActive / mood.
//   - snooze:   alarm button while ringing -> silent for ALARM_SNOOZE_MIN, then rings again
//               (after ALARM_MAX_SNOOZES snoozes the next tap ends the alarm instead)
//   - off:      hold the alarm button ALARM_LONG_PRESS_MS -> alarm ends, also while snoozed
//   - auto-off: stops by itself after ALARM_AUTO_OFF_MIN without a button press
//   - one ring per scheduled minute: it can't re-trigger inside the same minute
//
// Called repeatedly from the alarm task (e.g. every 200 ms).
void alarm_tick();

// Asks the alarm to snooze. Safe to call from another task (e.g. touch);
// ignored unless the alarm is currently ringing.
void alarm_request_snooze();

// Switches the alarm off completely: stops the sound if it is ringing and
// cancels a pending snooze. The next day's alarm still rings. Safe to call
// from another task; ignored while the alarm is idle.
void alarm_request_dismiss();