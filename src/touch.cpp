#include <Arduino.h>
#include "driver/gpio.h"
#include "touch.h"
#include "pins.h"
#include "config.h"
#include "state.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "pet.h"
#include "settings.h"
#include "reminders.h"

// ---------------------------------------------------------------------------------------
// Debounced digital input (touch pad + rotary push button)
// ---------------------------------------------------------------------------------------
// TTP223 touch pad: OUT is HIGH while touched (active HIGH, INPUT_PULLDOWN).
// KY-040 push button: SW goes LOW when pushed (active LOW, INPUT_PULLUP).

struct DebouncedInput
{
    uint8_t pin;
    bool activeLow;
    uint16_t debounceMs;
    bool lastStable = false; // true = pressed / touched
    bool lastReading = false;
    uint32_t lastChangeMs = 0;

    DebouncedInput(uint8_t p, bool low, uint16_t ms) : pin(p), activeLow(low), debounceMs(ms) {}
};

static DebouncedInput pad{TOUCH_PAD_PIN, false, TOUCH_PAD_DEBOUNCE_MS};
static DebouncedInput knobSw{ROTARY_SW_PIN, true, TOUCH_DEBOUNCE_MS};

enum class BtnEvent : uint8_t
{
    NONE,
    PRESSED, // debounced press, reported once
    RELEASED // debounced release, reported once
};

static BtnEvent pollInput(DebouncedInput &in)
{
    bool pressed = (digitalRead(in.pin) == HIGH) != in.activeLow;
    uint32_t now = millis();

    if (pressed != in.lastReading)
    {
        in.lastReading = pressed;
        in.lastChangeMs = now;
    }

    if ((now - in.lastChangeMs) > in.debounceMs && pressed != in.lastStable)
    {
        in.lastStable = pressed;
        return pressed ? BtnEvent::PRESSED : BtnEvent::RELEASED;
    }
    return BtnEvent::NONE;
}

// ---------------------------------------------------------------------------------------
// Rotary encoder (KY-040): interrupt-driven quadrature decoder
// ---------------------------------------------------------------------------------------
// A state table (full-step, after Ben Buxton's well-known decoder) turns the CLK/DT edges
// into exactly one count per detent and ignores contact bounce, because a bounce can never
// complete a valid CLK/DT sequence. Both pins interrupt on CHANGE; the touch task takes the
// counted detents every tick. +1 = one detent clockwise (CLK leads DT).

#define ROT_START 0x0
#define ROT_CW_FINAL 0x1
#define ROT_CW_BEGIN 0x2
#define ROT_CW_NEXT 0x3
#define ROT_CCW_BEGIN 0x4
#define ROT_CCW_FINAL 0x5
#define ROT_CCW_NEXT 0x6
#define ROT_DIR_CW 0x10
#define ROT_DIR_CCW 0x20

// rows: state, columns: pin state (CLK << 1 | DT)
static const DRAM_ATTR uint8_t kRotTable[7][4] = {
    /* ROT_START     */ {ROT_START, ROT_CW_BEGIN, ROT_CCW_BEGIN, ROT_START},
    /* ROT_CW_FINAL  */ {ROT_CW_NEXT, ROT_START, ROT_CW_FINAL, ROT_START | ROT_DIR_CW},
    /* ROT_CW_BEGIN  */ {ROT_CW_NEXT, ROT_CW_BEGIN, ROT_START, ROT_START},
    /* ROT_CW_NEXT   */ {ROT_CW_NEXT, ROT_CW_BEGIN, ROT_CW_FINAL, ROT_START},
    /* ROT_CCW_BEGIN */ {ROT_CCW_NEXT, ROT_START, ROT_CCW_BEGIN, ROT_START},
    /* ROT_CCW_FINAL */ {ROT_CCW_NEXT, ROT_CCW_FINAL, ROT_START, ROT_START | ROT_DIR_CCW},
    /* ROT_CCW_NEXT  */ {ROT_CCW_NEXT, ROT_CCW_FINAL, ROT_CCW_BEGIN, ROT_START},
};

static volatile uint8_t s_rotState = ROT_START;
static volatile int16_t s_rotSteps = 0; // detents since the last rotaryTakeSteps(), +CW
static portMUX_TYPE s_rotMux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR rotaryIsr()
{
    uint8_t pinState = (uint8_t)((gpio_get_level((gpio_num_t)ROTARY_CLK_PIN) << 1) |
                                 gpio_get_level((gpio_num_t)ROTARY_DT_PIN));
    uint8_t next = kRotTable[s_rotState & 0x0F][pinState];
    s_rotState = next;
    uint8_t dir = next & 0x30;
    if (dir)
    {
        portENTER_CRITICAL_ISR(&s_rotMux);
        s_rotSteps += (dir == ROT_DIR_CW) ? 1 : -1;
        portEXIT_CRITICAL_ISR(&s_rotMux);
    }
}

// Detents turned since the last call (positive = clockwise), then resets the counter.
static int rotaryTakeSteps()
{
    portENTER_CRITICAL(&s_rotMux);
    int s = s_rotSteps;
    s_rotSteps = 0;
    portEXIT_CRITICAL(&s_rotMux);
    return ROTARY_REVERSE ? -s : s;
}

static void rotaryInit()
{
    pinMode(ROTARY_CLK_PIN, INPUT_PULLUP);
    pinMode(ROTARY_DT_PIN, INPUT_PULLUP);
    s_rotState = ROT_START;
    s_rotSteps = 0;
    attachInterrupt(digitalPinToInterrupt(ROTARY_CLK_PIN), rotaryIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ROTARY_DT_PIN), rotaryIsr, CHANGE);
}

// ---------------------------------------------------------------------------------------
// Touch pad: Pet + Snooze / Off
// ---------------------------------------------------------------------------------------
// While the alarm RINGS the touch snoozes it (the pet reaction is skipped, so it can't be
// "petted" into silence) and keeping the finger on for ALARM_LONG_PRESS_MS switches the alarm
// off. While it is SNOOZED a touch is a normal pet, but a hold of ALARM_LONG_PRESS_MS switches
// the alarm off instead of purring (PAD_HOLD_DISMISSES_SNOOZE). Otherwise: pet, and a hold
// of PET_PURR_HOLD_MS purrs until released or PET_PURR_MAX_MS is over.
static uint32_t padPressedAtMs = 0;
static bool padPurrHandled = false; // this hold already purred (or was not allowed to)
static bool padSwallow = false;     // this touch closed a reminder: it is used up (no pet, no purr)
static bool padOffArmed = false;    // holding this touch switches the alarm off (and never purrs)
static bool padOffHandled = false;  // ...and that already happened

// Sound is sent outside the mutex. audio_play_gentle() stays quiet (or muted) during the
// night window.
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

    if (petted)
    {
        pet_on_press(millis());                    // picks the reaction the display will draw (streak, night, rain, ...)
        audio_play_gentle(audio_next_pet_chirp()); // up to three versions, so it does not repeat
    }
}

static void onPadTouched(uint32_t now)
{
    padPressedAtMs = now;
    padPurrHandled = false;
    padSwallow = false;
    padOffArmed = false;
    padOffHandled = false;

    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool ringing = g_state.alarmActive;
    xSemaphoreGive(g_stateMutex);

    if (ringing)
    {
        alarm_request_snooze(); // the alarm task owns the timing and the snooze limit
        padOffArmed = true;     // keep touching -> alarm off
        return;
    }
    if (reminder_consume_press())
    {
        padSwallow = true; // closes a visible reminder, nothing else
        return;
    }
    if (PAD_HOLD_DISMISSES_SNOOZE && alarm_get_state() == AlarmState::SNOOZED)
        padOffArmed = true; // pet as usual, but a long hold switches the pending alarm off
    triggerPetHappy();
}

static void updatePadHold(uint32_t now)
{
    bool held = pad.lastStable && !padSwallow;
    uint32_t heldMs = (uint32_t)(now - padPressedAtMs);

    // Hold -> alarm off (works while it rings and while it is snoozed; ignored when idle)
    if (held && padOffArmed && !padOffHandled && heldMs >= ALARM_LONG_PRESS_MS)
    {
        padOffHandled = true;
        alarm_request_dismiss();
    }

    // Hold -> purr
    if (!held)
    {
        padPurrHandled = false;
    }
    else if (!padPurrHandled && !padOffArmed && heldMs >= PET_PURR_HOLD_MS)
    {
        padPurrHandled = true;
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

    // stop purring: released, held too long (an alarm replaces the mood by itself, so it always wins)
    if (!held || padOffArmed || heldMs > PET_PURR_MAX_MS)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        if (g_state.mood == Mood::PET_PURR)
            g_state.mood = Mood::IDLE_BLINK;
        xSemaphoreGive(g_stateMutex);
    }
}

static bool handlePad()
{
    BtnEvent ev = pollInput(pad);
    uint32_t now = millis();

    if (ev == BtnEvent::RELEASED)
        padSwallow = false;
    if (ev == BtnEvent::PRESSED)
        onPadTouched(now);
    updatePadHold(now);
    return ev == BtnEvent::PRESSED || pad.lastStable;
}

// ---------------------------------------------------------------------------------------
// Rotary encoder: pages
// ---------------------------------------------------------------------------------------

// Turn: Clock -> Date -> Weather -> Clock (clockwise) and the same way back (counter-clockwise).
// Diagnostics is not part of the cycle (see toggleDiagPage); turning while on it goes back to the clock.
static void stepDisplayMode(int dir)
{
    static const DisplayMode kOrder[3] = {DisplayMode::CLOCK, DisplayMode::DATE, DisplayMode::WEATHER};
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    int idx = 0;
    if (g_state.displayMode != DisplayMode::DIAG)
    {
        for (int i = 0; i < 3; i++)
            if (kOrder[i] == g_state.displayMode)
                idx = i;
        idx = (idx + (dir > 0 ? 1 : 2)) % 3; // +1 or -1 (mod 3)
    }
    g_state.displayMode = kOrder[idx];
    xSemaphoreGive(g_stateMutex);
}

static void goToClock()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.displayMode = DisplayMode::CLOCK;
    xSemaphoreGive(g_stateMutex);
}

// Hold on the knob: open the diagnostics page from anywhere, or leave it.
static void toggleDiagPage()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.displayMode = g_state.displayMode == DisplayMode::DIAG ? DisplayMode::CLOCK : DisplayMode::DIAG;
    xSemaphoreGive(g_stateMutex);
}

// After a turn closed a reminder, the rest of that turn must not also flip pages.
static bool rotIgnoring = false;
static uint32_t rotIgnoreUntilMs = 0;

static bool handleRotation(uint32_t now)
{
    int steps = rotaryTakeSteps();
    if (steps == 0)
        return false;

    if (reminder_consume_press())
    {
        rotIgnoring = true;
        rotIgnoreUntilMs = now + ROTARY_REMINDER_IGNORE_MS;
        return true;
    }
    if (rotIgnoring)
    {
        if ((int32_t)(now - rotIgnoreUntilMs) < 0)
            return true;
        rotIgnoring = false;
    }

    stepDisplayMode(steps > 0 ? 1 : -1); // one page per tick, however fast it was turned
    return true;
}

// Knob push: a tap acts on RELEASE (so a long press doesn't also jump to the clock first)
// and goes back to the clock; holding MODE_LONG_PRESS_MS acts while still held and toggles
// the diagnostics page. Returns true on any activity.
static uint32_t knobPressedAtMs = 0;
static bool knobLongHandled = false;
static bool knobSwallow = false; // this push closed a reminder

static bool handleKnobButton()
{
    BtnEvent ev = pollInput(knobSw);
    uint32_t now = millis();

    // A visible reminder: this push closes it and is used up (no page change, no hold ring) until released.
    if (ev == BtnEvent::PRESSED && reminder_consume_press())
        knobSwallow = true;
    if (knobSwallow)
    {
        g_state.modeHoldPct = 0;
        if (ev == BtnEvent::RELEASED)
            knobSwallow = false;
        return true;
    }

    // progress ring for the display task (only when the hold would OPEN the diagnostics
    // page; on that page a plain tap already leaves it)
    uint8_t ring = 0;
    if (MODE_HOLD_RING && knobSw.lastStable && !knobLongHandled && ev != BtnEvent::PRESSED)
    {
        uint32_t held = (uint32_t)(now - knobPressedAtMs);
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
        knobPressedAtMs = now;
        knobLongHandled = false;
        return true;
    }
    if (knobSw.lastStable && !knobLongHandled && (uint32_t)(now - knobPressedAtMs) >= MODE_LONG_PRESS_MS)
    {
        knobLongHandled = true;
        g_state.modeHoldPct = 0;
        toggleDiagPage();
        return true;
    }
    if (ev == BtnEvent::RELEASED)
    {
        if (!knobLongHandled)
            goToClock();
        return true;
    }
    return knobSw.lastStable; // held = still interacting
}

// Back to the clock after a while without any input. The timer restarts
// whenever the page changes or the pad / knob is used.
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

void touch_init()
{
    pinMode(TOUCH_PAD_PIN, INPUT_PULLDOWN); // TTP223 drives the pin itself; the pull-down only keeps it defined (and makes a plain test button work)
    pinMode(ROTARY_SW_PIN, INPUT_PULLUP);
    rotaryInit();
}

void touch_tick()
{
    uint32_t now = millis();
    bool activity = false;
    activity |= handlePad();
    pet_tick(now);
    activity |= handleRotation(now);
    activity |= handleKnobButton();
    autoReturnToClock(activity);
}