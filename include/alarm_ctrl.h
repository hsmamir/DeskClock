#pragma once

// Alarm scheduler. Rings at ALARM_HOUR:ALARM_MINUTE (config.h), plays
// SOUND_INDEX_ALARM on repeat, and drives g_state.alarmActive / mood.
//   - snooze:   alarm button while ringing -> silent for ALARM_SNOOZE_MIN, then rings again
//   - auto-off: stops by itself after ALARM_AUTO_OFF_MIN without a button press
//   - one ring per scheduled minute: it can't re-trigger inside the same minute
//
// Called repeatedly from the alarm task (e.g. every 200 ms).
void alarm_tick();

// Asks the alarm to snooze. Safe to call from another task (e.g. touch);
// ignored unless the alarm is currently ringing.
void alarm_request_snooze();