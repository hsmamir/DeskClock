#include <ESP32Servo.h>
#include "servo_ctrl.h"
#include "pins.h"
#include "config.h"
#include "state.h"

static Servo servo;

// This owns the *timing* of each mood's movement, not just its target angle:
// a pet wiggle is one out-and-back motion that then hands mood back to
// IDLE_BLINK itself, and an alarm sweep repeats until alarmActive clears.
enum class ServoAnim
{
    REST,
    PET_WIGGLE_OUT,
    PET_WIGGLE_BACK,
    PET_PURR, // gentle tremble around the rest angle while the Pet button is held
    ALARM_SWEEP
};
static ServoAnim animState = ServoAnim::REST;
static uint32_t animStepStartMs = 0;
static bool sweepAtAlertAngle = false;

static const uint16_t WIGGLE_LEG_MS = 180; // time for each half of the pet wiggle
static const uint16_t SWEEP_LEG_MS = 220;  // time for each half of the alarm sweep
static const uint16_t PURR_LEG_MS = 70;    // one tremble step while purring
static bool purrUp = false;

// The servo is only powered (attached) while it moves. After a movement it
// gets SERVO_SETTLE_MS to arrive, then it is detached: no holding current,
// no buzzing, less load on the USB supply.
static bool releasePending = false;
static uint32_t releaseAtMs = 0;

static Mood readMood()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    Mood m = g_state.mood;
    xSemaphoreGive(g_stateMutex);
    return m;
}

static void setMood(Mood m)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_state.mood = m;
    xSemaphoreGive(g_stateMutex);
}

static bool readAlarmActive()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool a = g_state.alarmActive;
    xSemaphoreGive(g_stateMutex);
    return a;
}

// Moves to `angle`, attaching first if the servo was released. Cancels any
// pending release.
static void servoMove(int angle)
{
    if (!servo.attached())
    {
        servo.setPeriodHertz(50);
        servo.attach(SERVO_PIN, 500, 2400);
    }
    servo.write(angle);
    releasePending = false;
}

// Call right after the last move of a sequence: detach after the settle time.
static void servoReleaseSoon(uint32_t now)
{
    releasePending = true;
    releaseAtMs = now + SERVO_SETTLE_MS;
}

static void servoReleaseIfDue(uint32_t now)
{
    if (!releasePending || (int32_t)(now - releaseAtMs) < 0) // wrap-safe compare
        return;
    releasePending = false;
    servo.detach();
    // Park the signal line LOW so it can't float and make the servo twitch.
    pinMode(SERVO_PIN, OUTPUT);
    digitalWrite(SERVO_PIN, LOW);
}

void servo_init()
{
    servoMove(SERVO_REST_ANGLE);
    servoReleaseSoon(millis()); // let it reach rest, then let go
}

void servo_tick()
{
    uint32_t now = millis();
    servoReleaseIfDue(now);
    Mood mood = readMood();

    if (mood == Mood::PET_HAPPY)
    {
        if (animState == ServoAnim::REST || animState == ServoAnim::PET_PURR)
        {
            animState = ServoAnim::PET_WIGGLE_OUT;
            animStepStartMs = now;
            servoMove(SERVO_PET_ANGLE);
        }
        else if (animState == ServoAnim::PET_WIGGLE_OUT && (now - animStepStartMs) > WIGGLE_LEG_MS)
        {
            animState = ServoAnim::PET_WIGGLE_BACK;
            animStepStartMs = now;
            servoMove(SERVO_REST_ANGLE);
        }
        else if (animState == ServoAnim::PET_WIGGLE_BACK && (now - animStepStartMs) > WIGGLE_LEG_MS)
        {
            animState = ServoAnim::REST;
            servoReleaseSoon(now);
            setMood(Mood::IDLE_BLINK); // wiggle's done — hand control back to idle
        }
    }
    else if (mood == Mood::PET_PURR)
    {
        if (animState != ServoAnim::PET_PURR)
        {
            animState = ServoAnim::PET_PURR;
            animStepStartMs = now;
            purrUp = true;
            servoMove(SERVO_REST_ANGLE + PET_PURR_SERVO_DEG);
        }
        else if ((now - animStepStartMs) > PURR_LEG_MS)
        {
            animStepStartMs = now;
            purrUp = !purrUp;
            servoMove(SERVO_REST_ANGLE + (purrUp ? PET_PURR_SERVO_DEG : -PET_PURR_SERVO_DEG));
        }
    }
    else if (mood == Mood::ALARM_ALERT)
    {
        if (animState != ServoAnim::ALARM_SWEEP)
        {
            animState = ServoAnim::ALARM_SWEEP;
            animStepStartMs = now;
            sweepAtAlertAngle = true;
            servoMove(SERVO_ALERT_ANGLE);
        }
        else if ((now - animStepStartMs) > SWEEP_LEG_MS)
        {
            animStepStartMs = now;
            sweepAtAlertAngle = !sweepAtAlertAngle;
            servoMove(sweepAtAlertAngle ? SERVO_ALERT_ANGLE : SERVO_REST_ANGLE);
        }

        if (!readAlarmActive())
        {
            animState = ServoAnim::REST;
            servoMove(SERVO_REST_ANGLE);
            servoReleaseSoon(now);
            setMood(Mood::IDLE_BLINK);
        }
    }
    else
    {
        if (animState != ServoAnim::REST)
        {
            animState = ServoAnim::REST;
            servoMove(SERVO_REST_ANGLE);
            servoReleaseSoon(now);
        }
    }
}