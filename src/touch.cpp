#include <Arduino.h>
#include "touch.h"
#include "pins.h"
#include "config.h"
#include "state.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "pet.h"
#include "settings.h"

// Each button wires one leg to its GPIO and the other leg to GND.
// INPUT_PULLUP holds the pin HIGH when idle, so a press reads LOW.

struct DebouncedButton
{
    uint8_t pin;
    bool lastStable = false; // true = pressed
    bool lastReading = false;
    uint32_t lastChangeMs = 0;

    explicit DebouncedButton(uint8_t p) : pin(p) {}
};

static DebouncedButton btnPet{TOUCH_PET_PIN};
static DebouncedButton btnMode{TOUCH_MODE_PIN};
static DebouncedButton btnAlarm{TOUCH_ALARM_PIN};

enum class BtnEvent : uint8_t
{
    NONE,
    PRESSED, // debounced press   (HIGH -> LOW), reported once
    RELEASED // debounced release (LOW -> HIGH), reported once
};

static BtnEvent pollButton(DebouncedButton &btn)
{
    bool pressed = digitalRead(btn.pin) == LOW;
    uint32_t now = millis();

    if (pressed != btn.lastReading)
    {
        btn.lastReading = pressed;
        btn.lastChangeMs = now;
    }

    if ((now - btn.lastChangeMs) > TOUCH_DEBOUNCE_MS && pressed != btn.lastStable)
    {
        btn.lastStable = pressed;
        return pressed ? BtnEvent::PRESSED : BtnEvent::RELEASED;
    }
    return BtnEvent::NONE;
}

// Pet button doesn't interrupt an active alarm — you snooze/confirm that
// with the alarm button instead, so it can't be "pet" into silence.
static void triggerPetHappy()
{
    bool petted = false;
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    if (g_state.mood != Mood::ALARM_ALERT)
    {
        g_state.mood = Mood::PET_HAPPY;
        petted = true;
    }
    xSemaphoreGive(g_stateMutex);

    // Sound is sent outside the mutex; skipped while an alarm is ringing.
    // audio_play_gentle() stays quiet (or muted) during the night window.
    if (petted)
    {
        pet_on_press(millis());                    // picks the reaction the display will draw (streak, night, rain, ...)
        audio_play_gentle(audio_next_pet_chirp()); // up to three versions, so it does not repeat
    }
}

// Holding the Pet button: after PET_PURR_HOLD_MS the clock purrs (Mood::PET_PURR: closed eyes,
// the servo trembles gently) until the button is released or PET_PURR_MAX_MS is over.
// An alarm replaces the mood by itself, so it always wins.
static uint32_t petPressedAtMs = 0;
static bool petPurrHandled = false; // this hold already purred (or was not allowed to)

static void updatePurr(uint32_t now)
{
    bool held = btnPet.lastStable;
    uint32_t heldMs = (uint32_t)(now - petPressedAtMs);

    if (!held)
    {
        petPurrHandled = false;
    }
    else if (!petPurrHandled && heldMs >= PET_PURR_HOLD_MS)
    {
        petPurrHandled = true;
        if (settings_get().petReactions)
        {
            bool purring = false;
            xSemaphoreTake(g_stateMutex, portMAX_DELAY);
            if (g_state.mood == Mood::IDLE_BLINK)
            {
                g_state.mood = Mood::PET_PURR;
                purring = true;
            }
            xSemaphoreGive(g_stateMutex);
            if (purring)
                pet_bond_add(PET_BOND_PURR_GAIN, true); // purring = he feels safe with you
        }
    }

    // stop purring: released, held too long
    if (!held || heldMs > PET_PURR_MAX_MS)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        if (g_state.mood == Mood::PET_PURR)
            g_state.mood = Mood::IDLE_BLINK;
        xSemaphoreGive(g_stateMutex);
    }
}

// Tap on Mode: Clock -> Date -> Weather -> Clock. Diagnostics is not part of the
// cycle (see toggleDiagPage); tapping while on it just goes back to the clock.
static void cycleDisplayMode()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    switch (g_state.displayMode)
    {
    case DisplayMode::CLOCK:
        g_state.displayMode = DisplayMode::DATE;
        break;
    case DisplayMode::DATE:
        g_state.displayMode = DisplayMode::WEATHER;
        break;
    case DisplayMode::WEATHER:
    case DisplayMode::DIAG:
        g_state.displayMode = DisplayMode::CLOCK;
        break;
    }
    xSemaphoreGive(g_stateMutex);
}

// Hold on Mode: open the diagnostics page from anywhere, or leave it.
static void toggleDiagPage()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.displayMode = g_state.displayMode == DisplayMode::DIAG ? DisplayMode::CLOCK : DisplayMode::DIAG;
    xSemaphoreGive(g_stateMutex);
}

// Mode button: a tap acts on RELEASE (so a long press doesn't also flip a page first);
// holding MODE_LONG_PRESS_MS acts while still held. Returns true on any activity.
static uint32_t modePressedAtMs = 0;
static bool modeLongHandled = false;

static bool handleModeButton()
{
    BtnEvent ev = pollButton(btnMode);
    uint32_t now = millis();

    // progress ring for the display task (only when the hold would OPEN the diagnostics
    // page; on that page a plain tap already leaves it)
    uint8_t ring = 0;
    if (MODE_HOLD_RING && btnMode.lastStable && !modeLongHandled && ev != BtnEvent::PRESSED)
    {
        uint32_t held = (uint32_t)(now - modePressedAtMs);
        if (held > MODE_HOLD_RING_DELAY_MS && held < MODE_LONG_PRESS_MS)
        {
            xSemaphoreTake(g_stateMutex, portMAX_DELAY);
            bool onDiag = g_state.displayMode == DisplayMode::DIAG;
            xSemaphoreGive(g_stateMutex);
            if (!onDiag)
            {
                uint32_t pct = (held - MODE_HOLD_RING_DELAY_MS) * 100 / (MODE_LONG_PRESS_MS - MODE_HOLD_RING_DELAY_MS);
                ring = (uint8_t)(pct < 1 ? 1 : (pct > 100 ? 100 : pct));
            }
        }
    }
    g_state.modeHoldPct = ring;

    if (ev == BtnEvent::PRESSED)
    {
        modePressedAtMs = now;
        modeLongHandled = false;
        return true;
    }
    if (btnMode.lastStable && !modeLongHandled && (uint32_t)(now - modePressedAtMs) >= MODE_LONG_PRESS_MS)
    {
        modeLongHandled = true;
        g_state.modeHoldPct = 0;
        toggleDiagPage();
        return true;
    }
    if (ev == BtnEvent::RELEASED)
    {
        if (!modeLongHandled)
            cycleDisplayMode();
        return true;
    }
    return btnMode.lastStable; // held = still interacting
}

// Back to the clock after a while without any button press. The timer restarts
// whenever the page changes or a button is used.
static uint32_t lastActivityMs = 0;
static DisplayMode lastSeenMode = DisplayMode::CLOCK;

static void autoReturnToClock(bool activity)
{
    uint32_t now = millis();
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    DisplayMode m = g_state.displayMode;
    if (activity || m != lastSeenMode)
    {
        lastActivityMs = now;
        lastSeenMode = m;
    }
    uint32_t limit = m == DisplayMode::CLOCK ? 0 : (m == DisplayMode::DIAG ? DIAG_RETURN_MS : DISPLAY_RETURN_MS);
    if (limit != 0 && (uint32_t)(now - lastActivityMs) >= limit)
    {
        g_state.displayMode = DisplayMode::CLOCK;
        lastSeenMode = DisplayMode::CLOCK;
    }
    xSemaphoreGive(g_stateMutex);
}

// Alarm button:
//   tap  while ringing -> snooze at once (the sound stops immediately)
//   hold ALARM_LONG_PRESS_MS (ringing or snoozed) -> alarm off completely
// The snooze fires on the press, not the release, so there is no delay; if the
// finger stays down, the same press then becomes "off". The alarm task owns the
// timing and the snooze limit — this only passes the requests along.
static uint32_t alarmPressedAtMs = 0;
static bool alarmLongHandled = false;

static bool handleAlarmButton()
{
    BtnEvent ev = pollButton(btnAlarm);
    uint32_t now = millis();

    if (ev == BtnEvent::PRESSED)
    {
        alarmPressedAtMs = now;
        alarmLongHandled = false;

        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool ringing = g_state.alarmActive;
        xSemaphoreGive(g_stateMutex);
        if (ringing)
            alarm_request_snooze();
        return true;
    }
    else if (btnAlarm.lastStable && !alarmLongHandled &&
             (uint32_t)(now - alarmPressedAtMs) >= ALARM_LONG_PRESS_MS)
    {
        alarmLongHandled = true;
        alarm_request_dismiss(); // ignored by the alarm task while it is idle
        return true;
    }
    return btnAlarm.lastStable;
}

void touch_init()
{
    pinMode(TOUCH_PET_PIN, INPUT_PULLUP);
    pinMode(TOUCH_MODE_PIN, INPUT_PULLUP);
    pinMode(TOUCH_ALARM_PIN, INPUT_PULLUP);
}

void touch_tick()
{
    bool activity = false;
    if (pollButton(btnPet) == BtnEvent::PRESSED)
    {
        petPressedAtMs = millis();
        petPurrHandled = false;
        triggerPetHappy();
        activity = true;
    }
    updatePurr(millis());
    pet_tick(millis());
    activity |= handleModeButton();
    activity |= handleAlarmButton();
    autoReturnToClock(activity);
}