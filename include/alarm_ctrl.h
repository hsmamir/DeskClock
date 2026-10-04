#pragma once

// Alarm scheduler. Rings at the alarm time, on the alarm days, from settings (see
// settings.h; config.h only holds the factory default), plays the chosen alarm
// sound on repeat at the alarm volume, and drives g_state.alarmActive / mood.
//   - snooze:   alarm button while ringing -> silent for ALARM_SNOOZE_MIN, then rings again
//               (after ALARM_MAX_SNOOZES snoozes the next tap ends the alarm instead)
//   - off:      hold the alarm button ALARM_LONG_PRESS_MS -> alarm ends, also while snoozed
//   - auto-off: stops by itself after ALARM_AUTO_OFF_MIN without a button press
//   - one ring per scheduled day: it can't re-trigger after it already rang (also not after a reboot)
//   - catch-up: if the alarm minute was missed (busy ringing a test, snoozed, rebooting, clock
//               not synced yet) it still rings up to ALARM_GRACE_MIN minutes late. Saving a changed
//               alarm time/days/enabled never rings a time that has already passed today.
//
//   - test:     alarm_request_test() rings the alarm right now (same sound, saved volume,
//               servo and red eyes as the real one) so it can be tried from the web page.
//               A test behaves like a real ring: snooze / off / auto-off all apply to it.
//
// Called repeatedly from the alarm task (e.g. every 200 ms).
void alarm_tick();

// What the alarm is doing right now (read by the web page).
enum class AlarmState : uint8_t
{
    IDLE,    // waiting for the alarm time
    RINGING, // sound + servo + red eyes
    SNOOZED  // silent, will ring again
};

// Safe to call from another task.
AlarmState alarm_get_state();

// Seconds until a snoozed alarm rings again; -1 when the alarm is not snoozed.
int alarm_snooze_left_s();

// Rings the alarm now, whatever the alarm time / enabled setting is. Safe to
// call from another task; ignored unless the alarm is idle. Does not use up
// today's real alarm.
void alarm_request_test();

// Asks the alarm to snooze. Safe to call from another task (e.g. touch);
// ignored unless the alarm is currently ringing.
void alarm_request_snooze();

// Switches the alarm off completely: stops the sound if it is ringing and
// cancels a pending snooze. The next day's alarm still rings. Safe to call
// from another task; ignored while the alarm is idle.
void alarm_request_dismiss();