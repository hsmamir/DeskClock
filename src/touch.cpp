#include <Arduino.h>
#include "driver/gpio.h"
#include "touch.h"
#include "pins.h"
#include "config.h"
#include "weather_extras.h"
#include "state.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "pet.h"
#include "settings.h"
#include "reminders.h"
#include "quick_menu.h"
#include "ui_state.h"

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

// ---------------------------------------------------------------------------------------
// Easter egg: EGG_CREDITS_TAPS quick taps on the diagnostics page start the credits (display_credits.cpp)
// ---------------------------------------------------------------------------------------
#if EGG_CREDITS_ENABLED
static uint8_t eggTaps = 0;
static uint32_t eggLastTapMs = 0;

// Every touch while no alarm rings and no reminder is up. Returns true when the touch belongs to the diagnostics
// page: it does not pet there (no reaction, no statistics, no bond), it only counts toward the secret.
static bool diagPadTap(uint32_t now)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool onDiag = g_state.displayMode == DisplayMode::DIAG;
    xSemaphoreGive(g_stateMutex);
    if (!onDiag)
    {
        eggTaps = 0;
        return false;
    }
    if (g_state.creditsActive) // a tap while the credits run skips them
    {
        g_state.creditsActive = false;
        eggTaps = 0;
        return true;
    }
    if (eggTaps > 0 && (uint32_t)(now - eggLastTapMs) > EGG_CREDITS_TAP_GAP_MS)
        eggTaps = 0; // too slow: start counting again
    eggLastTapMs = now;
    if (++eggTaps >= EGG_CREDITS_TAPS)
    {
        eggTaps = 0;
        g_state.creditsSeq = (uint8_t)(g_state.creditsSeq + 1); // the display starts the credits
    }
    return true;
}
#else
static bool diagPadTap(uint32_t) { return false; }
#endif

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
    if (diagPadTap(now))
    {
        padPurrHandled = true; // no purring on the diagnostics page either
        return;
    }
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
// Diagnostics and the quick menu are not part of the cycle (see toggleDiagPage / quick_menu.h); turning while on
// the diagnostics page goes back to the clock (while the menu is open a turn drives the menu instead).
//
// The Weather page is ONE scrollable page: while it has something to scroll (the forecast below "now") a turn scrolls it
// (WX_SCROLL_STEPS_TOTAL clicks from top to bottom: WEATHER_SCROLL_STEPS per screen, weather_extras.h) instead of changing pages. A turn at the top / bottom edge moves on to
// the previous / next page, but only after the knob was still for WEATHER_SCROLL_EDGE_PAUSE_MS, so spinning fast scrolls
// to the end and stops there instead of flying through the whole page. Coming from the next page (counter-clockwise) the
// page opens at its bottom, coming from the previous one (clockwise) at its top.
static uint32_t lastWeatherTurnMs = 0; // the last turn while the Weather page was involved (entering counts)

static void turnPages(int steps, uint32_t now)
{
    static const DisplayMode kOrder[3] = {DisplayMode::CLOCK, DisplayMode::DATE, DisplayMode::WEATHER};
    const int dir = steps > 0 ? 1 : -1;
#if LANDSCAPE_UI
    // Views of the Weather page: 0 = Now (always), 1 = Next days, 2 = Air quality. A view without data is skipped.
    // Asked BEFORE g_stateMutex is taken (weather_extra_get takes it itself).
    bool wxAvail[3] = {true, false, false};
    {
        WeatherExtra x = {};
        if (weather_extra_get(x))
        {
            wxAvail[1] = WX_DAILY_ENABLED && x.forecastValid && x.dayCount > 0;
            wxAvail[2] = WX_AIRQ_ENABLED && x.aqValid;
        }
    }
#endif
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
#if LANDSCAPE_UI
    {
        // Landscape UI: the knob walks through the pages  Face -> Clock -> Date -> Weather -> Face  (clockwise) and the
        // same way back (counter-clockwise). Face and Clock are the two "home" views (ui_home_is_clock), Date and
        // Weather are the DisplayModes of the same name (display.cpp draws them, display_page_side.cpp). Leaving the
        // diagnostics page / the menu with a turn goes home without moving on. The Weather page is one screen per VIEW
        // (Now / Next days / Air quality, g_state.weatherScrollStep = the view): a turn steps to the next view, and at the
        // last one (after WEATHER_SCROLL_EDGE_PAUSE_MS without turning) on to the next page. After DISPLAY_RETURN_MS
        // without input autoReturnToClock() brings Date / Weather back home.
        const bool leaving = g_state.displayMode == DisplayMode::DIAG || g_state.displayMode == DisplayMode::MENU;
        if (leaving)
        {
            g_state.displayMode = DisplayMode::CLOCK;
        }
        else
        {
            if (g_state.displayMode == DisplayMode::WEATHER)
            {
                const bool still = (uint32_t)(now - lastWeatherTurnMs) >= (uint32_t)WEATHER_SCROLL_EDGE_PAUSE_MS;
                lastWeatherTurnMs = now;
                int v = g_state.weatherScrollStep;
                if (v < 0 || v > 2)
                    v = 0;
                int nv = -1; // the next view in the turning direction that has data
                for (int c = v + dir; c >= 0 && c <= 2; c += dir)
                    if (wxAvail[c])
                    {
                        nv = c;
                        break;
                    }
                if (nv >= 0)
                {
                    g_state.weatherScrollStep = (uint8_t)nv;
                    xSemaphoreGive(g_stateMutex);
                    return;
                }
                if (!still) // at the last view, but the knob was just moving: stay on the page
                {
                    xSemaphoreGive(g_stateMutex);
                    return;
                }
                // still at the last view: fall through and move on to the neighbouring page
            }
            int cur; // 0 = face, 1 = clock, 2 = date, 3 = weather
            if (g_state.displayMode == DisplayMode::DATE)
                cur = 2;
            else if (g_state.displayMode == DisplayMode::WEATHER)
                cur = 3;
            else
                cur = ui_home_is_clock() ? 1 : 0;
            const int next = (cur + (dir > 0 ? 1 : 3)) % 4; // +1 or -1 (mod 4)
            if (next == 2)
                g_state.displayMode = DisplayMode::DATE;
            else if (next == 3)
            {
                g_state.displayMode = DisplayMode::WEATHER;
                // coming from Date: the first view; coming back from the face: the last view that has data
                int first = 0;
                if (dir < 0)
                    for (int c = 2; c > 0; c--)
                        if (wxAvail[c])
                        {
                            first = c;
                            break;
                        }
                g_state.weatherScrollStep = (uint8_t)first;
                lastWeatherTurnMs = now; // the turn that brought us here must not also leave again
            }
            else
            {
                ui_set_home_clock(next == 1);
                g_state.displayMode = DisplayMode::CLOCK;
            }
        }
        xSemaphoreGive(g_stateMutex);
        return;
    }
#endif

    // ---- scrolling the Weather page ----
    if (WEATHER_DETAILS_ENABLED && g_state.displayMode == DisplayMode::WEATHER && g_state.weatherValid && g_state.detailValid)
    {
        bool still = (uint32_t)(now - lastWeatherTurnMs) >= (uint32_t)WEATHER_SCROLL_EDGE_PAUSE_MS;
        lastWeatherTurnMs = now;
        int cur = g_state.weatherScrollStep;
        if (cur > WX_SCROLL_STEPS_TOTAL)
            cur = WX_SCROLL_STEPS_TOTAL;
        int next = cur + steps;
        if (next < 0)
            next = 0;
        if (next > WX_SCROLL_STEPS_TOTAL)
            next = WX_SCROLL_STEPS_TOTAL;
        if (next != cur)
        {
            g_state.weatherScrollStep = (uint8_t)next;
            xSemaphoreGive(g_stateMutex);
            return;
        }
        if (!still) // at the edge, but the knob was just moving: stay on the page
        {
            xSemaphoreGive(g_stateMutex);
            return;
        }
        // still at the edge: fall through and move on to the neighbouring page
    }

    // ---- changing the page ----
    int idx = 0;
    if (g_state.displayMode != DisplayMode::DIAG && g_state.displayMode != DisplayMode::MENU)
    {
        for (int i = 0; i < 3; i++)
            if (kOrder[i] == g_state.displayMode)
                idx = i;
        idx = (idx + (dir > 0 ? 1 : 2)) % 3; // +1 or -1 (mod 3)
    }
    if (kOrder[idx] == DisplayMode::WEATHER && g_state.displayMode != DisplayMode::WEATHER)
    {
        // entering the Weather page: from the top when coming from the Date page, from the bottom when coming back from the clock
        g_state.weatherScrollStep = dir > 0 ? 0 : (uint8_t)WX_SCROLL_STEPS_TOTAL;
        lastWeatherTurnMs = now; // the turn that brought us here must not also leave again
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
    if (g_state.displayMode != DisplayMode::MENU) // inside the menu a hold leaves the menu (handleKnobButton)
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

    // While the alarm rings the knob does not flip pages or edit anything behind the red face (a push snoozes).
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool ringing = g_state.alarmActive;
    xSemaphoreGive(g_stateMutex);
    if (ringing)
        return true;

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

    if (quickmenu_is_open())
    {
        quickmenu_turn(steps); // moves the cursor / changes the value being edited
        return true;
    }

    turnPages(steps, now); // scrolls the Weather page, else one page per tick, however fast it was turned
    return true;
}

// Knob push. What it does depends on the moment:
//   alarm ringing : push = snooze at once (like the touch pad); keep holding ALARM_LONG_PRESS_MS = alarm off
//   reminder up   : the push closes it and is used up
//   quick menu    : tap = select / save, hold MODE_LONG_PRESS_MS = leave the menu
//   clock page    : tap = open the quick menu; hold = diagnostics page (a ring fills while holding)
//   other pages   : tap = back to the clock; hold = diagnostics page
// A tap acts on RELEASE (so a long press doesn't also do the tap action first); a hold acts while still held.
// Returns true on any activity.
static uint32_t knobPressedAtMs = 0;
static bool knobLongHandled = false;
static bool knobSwallow = false;      // this push closed a reminder
static bool knobAlarmArmed = false;   // this push belongs to the alarm (it snoozed it); holding switches it off
static bool knobAlarmOffDone = false; // ...and that already happened

static void onKnobTap()
{
    if (quickmenu_is_open())
    {
        quickmenu_press();
        return;
    }
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool onClock = g_state.displayMode == DisplayMode::CLOCK;
    xSemaphoreGive(g_stateMutex);
    if (QUICK_MENU_ENABLED && onClock)
        quickmenu_open();
    else
        goToClock();
}

static bool handleKnobButton()
{
    BtnEvent ev = pollInput(knobSw);
    uint32_t now = millis();

    if (ev == BtnEvent::PRESSED)
    {
        knobPressedAtMs = now;
        knobLongHandled = false;
        knobSwallow = false;
        knobAlarmArmed = false;
        knobAlarmOffDone = false;

        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool ringing = g_state.alarmActive;
        xSemaphoreGive(g_stateMutex);

        if (KNOB_CONTROLS_ALARM && ringing)
        {
            alarm_request_snooze(); // the alarm task owns the timing and the snooze limit
            knobAlarmArmed = true;  // keep holding -> alarm off
        }
        else if (reminder_consume_press())
        {
            knobSwallow = true; // closes a visible reminder, nothing else
        }
    }

    // The alarm owns this push until it is released (no page change, no menu, no diagnostics ring).
    if (knobAlarmArmed)
    {
        g_state.modeHoldPct = 0;
        if (knobSw.lastStable && !knobAlarmOffDone && (uint32_t)(now - knobPressedAtMs) >= ALARM_LONG_PRESS_MS)
        {
            knobAlarmOffDone = true;
            alarm_request_dismiss();
        }
        if (ev == BtnEvent::RELEASED)
            knobAlarmArmed = false;
        return true;
    }

    // A visible reminder: this push closes it and is used up (no page change, no hold ring) until released.
    if (knobSwallow)
    {
        g_state.modeHoldPct = 0;
        if (ev == BtnEvent::RELEASED)
            knobSwallow = false;
        return true;
    }

    // progress ring for the display task (only when the hold would OPEN the diagnostics
    // page; on that page and inside the menu a hold leaves instead)
    uint8_t ring = 0;
    if (MODE_HOLD_RING && knobSw.lastStable && !knobLongHandled && ev != BtnEvent::PRESSED)
    {
        uint32_t held = (uint32_t)(now - knobPressedAtMs);
        if (held > MODE_HOLD_RING_DELAY_MS && held < MODE_LONG_PRESS_MS)
        {
            xSemaphoreTake(g_stateMutex, portMAX_DELAY);
            bool onDiagOrMenu = g_state.displayMode == DisplayMode::DIAG || g_state.displayMode == DisplayMode::MENU;
            xSemaphoreGive(g_stateMutex);
            if (!onDiagOrMenu)
            {
                uint32_t pct = (held - MODE_HOLD_RING_DELAY_MS) * 100 / (MODE_LONG_PRESS_MS - MODE_HOLD_RING_DELAY_MS);
                ring = (uint8_t)(pct < 1 ? 1 : (pct > 100 ? 100 : pct));
            }
        }
    }
    g_state.modeHoldPct = ring;

    if (ev == BtnEvent::PRESSED)
        return true;
    if (knobSw.lastStable && !knobLongHandled && (uint32_t)(now - knobPressedAtMs) >= MODE_LONG_PRESS_MS)
    {
        knobLongHandled = true;
        g_state.modeHoldPct = 0;
        if (quickmenu_is_open())
            quickmenu_close(); // leaves the menu; an unsaved edit is dropped
        else
            toggleDiagPage();
        return true;
    }
    if (ev == BtnEvent::RELEASED)
    {
        if (!knobLongHandled)
            onKnobTap();
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
    if (activity || g_state.creditsActive || m != lastSeenMode) // the credits keep the diagnostics page open
    {
        lastActivityMs = now;
        lastSeenMode = m;
    }
    // the quick menu has its own timeout (quickmenu_tick), which also drops an unsaved edit
    uint32_t limit = (m == DisplayMode::CLOCK || m == DisplayMode::MENU) ? 0 : (m == DisplayMode::DIAG ? DIAG_RETURN_MS : DISPLAY_RETURN_MS);
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
    quickmenu_init();
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
    quickmenu_tick(now); // menu time-out; closes it when the alarm starts to ring
    autoReturnToClock(activity);
}