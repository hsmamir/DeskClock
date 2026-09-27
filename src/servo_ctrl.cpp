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
    ALARM_SWEEP
};
static ServoAnim animState = ServoAnim::REST;
static uint32_t animStepStartMs = 0;
static bool sweepAtAlertAngle = false;

static const uint16_t WIGGLE_LEG_MS = 180; // time for each half of the pet wiggle
static const uint16_t SWEEP_LEG_MS = 220;  // time for each half of the alarm sweep

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

void servo_init()
{
    servo.setPeriodHertz(50);
    servo.attach(SERVO_PIN, 500, 2400);
    servo.write(SERVO_REST_ANGLE);
}

void servo_tick()
{
    uint32_t now = millis();
    Mood mood = readMood();

    if (mood == Mood::PET_HAPPY)
    {
        if (animState == ServoAnim::REST)
        {
            animState = ServoAnim::PET_WIGGLE_OUT;
            animStepStartMs = now;
            servo.write(SERVO_PET_ANGLE);
        }
        else if (animState == ServoAnim::PET_WIGGLE_OUT && (now - animStepStartMs) > WIGGLE_LEG_MS)
        {
            animState = ServoAnim::PET_WIGGLE_BACK;
            animStepStartMs = now;
            servo.write(SERVO_REST_ANGLE);
        }
        else if (animState == ServoAnim::PET_WIGGLE_BACK && (now - animStepStartMs) > WIGGLE_LEG_MS)
        {
            animState = ServoAnim::REST;
            setMood(Mood::IDLE_BLINK); // wiggle's done — hand control back to idle
        }
    }
    else if (mood == Mood::ALARM_ALERT)
    {
        if (animState != ServoAnim::ALARM_SWEEP)
        {
            animState = ServoAnim::ALARM_SWEEP;
            animStepStartMs = now;
            sweepAtAlertAngle = true;
            servo.write(SERVO_ALERT_ANGLE);
        }
        else if ((now - animStepStartMs) > SWEEP_LEG_MS)
        {
            animStepStartMs = now;
            sweepAtAlertAngle = !sweepAtAlertAngle;
            servo.write(sweepAtAlertAngle ? SERVO_ALERT_ANGLE : SERVO_REST_ANGLE);
        }

        if (!readAlarmActive())
        {
            animState = ServoAnim::REST;
            servo.write(SERVO_REST_ANGLE);
            setMood(Mood::IDLE_BLINK);
        }
    }
    else
    {
        if (animState != ServoAnim::REST)
        {
            animState = ServoAnim::REST;
            servo.write(SERVO_REST_ANGLE);
        }
    }
}