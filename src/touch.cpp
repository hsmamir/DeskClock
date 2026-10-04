#include <Arduino.h>
#include "touch.h"
#include "pins.h"
#include "config.h"
#include "state.h"
#include "audio.h"
#include "alarm_ctrl.h"

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
        audio_play_gentle(SOUND_INDEX_PET);
}

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
        g_state.displayMode = DisplayMode::DIAG;
        break;
    case DisplayMode::DIAG:
        g_state.displayMode = DisplayMode::CLOCK;
        break;
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

static void handleAlarmButton()
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
    }
    else if (btnAlarm.lastStable && !alarmLongHandled &&
             (uint32_t)(now - alarmPressedAtMs) >= ALARM_LONG_PRESS_MS)
    {
        alarmLongHandled = true;
        alarm_request_dismiss(); // ignored by the alarm task while it is idle
    }
}

void touch_init()
{
    pinMode(TOUCH_PET_PIN, INPUT_PULLUP);
    pinMode(TOUCH_MODE_PIN, INPUT_PULLUP);
    pinMode(TOUCH_ALARM_PIN, INPUT_PULLUP);
}

void touch_tick()
{
    if (pollButton(btnPet) == BtnEvent::PRESSED)
        triggerPetHappy();
    if (pollButton(btnMode) == BtnEvent::PRESSED)
        cycleDisplayMode();
    handleAlarmButton();
}