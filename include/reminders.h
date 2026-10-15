#pragma once
#include <stdint.h>
#include <stddef.h>

// User reminders: text + time, added on the settings page, shown full screen on the clock when due.
//   - weekly:   days = weekday mask (bit0 = Sunday ... bit6 = Saturday, like tm_wday), rings on those days
//   - one time: days = 0 and date = YYYYMMDD; it is removed once it has been shown
// Stored in NVS (namespace "remind"), so they survive reboots and OTA updates.
// A due reminder waits while the alarm rings, then shows until the touch pad is touched, the knob is turned or pushed,
// or REMINDER_SHOW_S has passed. Several due at once are shown one after the other.
#define REMINDER_MAX 12
#define REMINDER_TEXT_MAX 80 // bytes, including the NUL (about 38 Persian letters)

struct Reminder
{
    uint8_t id; // 1..255, assigned by reminders_add(); 0 = not stored yet
    bool enabled;
    uint8_t hour, minute;
    uint8_t days;                 // weekday mask, 0 = one time on `date`
    uint32_t date;                // YYYYMMDD for one-time reminders, else 0
    char text[REMINDER_TEXT_MAX]; // UTF-8, cleaned by reminders_add / update (see fa::sanitize)
};

// Loads from NVS. Call once from setup(), after g_stateMutex exists.
void reminders_init();

// Copies up to cap reminders (sorted as stored), returns how many.
int reminders_list(Reminder *out, int cap);

// Validates (and cleans the text of) r, stores it, assigns r.id. false + *err (short English message) on failure.
bool reminders_add(Reminder &r, const char **err);
// Replaces the reminder with r.id. A changed weekly time never fires for a time that has already passed today.
bool reminders_update(const Reminder &r, const char **err);
bool reminders_remove(uint8_t id);

bool reminders_get_sound();
void reminders_set_sound(bool on);

// Called every ~200 ms from the alarm task: finds due reminders, brings the next one up, repeats the sound, times out.
void reminders_tick();

// ---- What the display shows ----
struct ReminderView
{
    bool active;
    bool test; // a preview from the settings page
    uint8_t id;
    uint8_t hour, minute; // the time it was set for
    uint8_t waiting;      // more reminders queued behind this one
    char text[REMINDER_TEXT_MAX];
    uint32_t seq;       // changes for every reminder that appears
    uint32_t shownMs;   // millis() when it appeared
    uint32_t timeoutMs; // closes by itself after this long (0 = never)
};
bool reminder_get_active(ReminderView &v);

// A button was pressed: closes the visible reminder and returns true (the press is then used up).
// false when no reminder is up, or the alarm is ringing (the touch pad owns that).
bool reminder_consume_press();
void reminder_dismiss();                      // same without the alarm check (settings page)
bool reminder_request_test(const char *text); // shows text now as a preview; false if the text is invalid