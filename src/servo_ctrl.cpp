#include <ESP32Servo.h>
#include "servo_ctrl.h"
#include "pins.h"
#include "config.h"
#include "state.h"
#include "pet.h"

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
    ALARM_SWEEP,
    PET_WAIT, // the head follows the eyes: wait SERVO_FOLLOW_DELAY_MS before moving
    PET_HOLD  // holding the tilt / stretch
};
static ServoAnim animState = ServoAnim::REST;
static uint32_t animStepStartMs = 0;
static bool sweepAtAlertAngle = false;

static const uint16_t WIGGLE_LEG_MS = 180; // time for each half of the pet wiggle
static const uint16_t SWEEP_LEG_MS = 220;  // time for each half of the alarm sweep
static const uint16_t PURR_LEG_MS = 70;    // one tremble step while purring
static bool purrUp = false;

// ---- Eased movement ----
// Visible movements are not one jump: every SERVO_STEP_MS the angle moves along a smoothstep curve from
// where it is to the target, taking `durMs` in total.
static float curAngle = SERVO_REST_ANGLE;
static float moveFrom = SERVO_REST_ANGLE;
static int moveTo = SERVO_REST_ANGLE;
static uint32_t moveStartMs = 0, moveLastStepMs = 0;
static uint16_t moveDurMs = 0;
static bool moveActive = false;

// What the current pet reaction does with the head (chosen when it starts).
struct PetMotion
{
    int outAngle;
    uint16_t outMs, holdMs, backMs;
};
static PetMotion petMotion = {SERVO_PET_ANGLE, WIGGLE_LEG_MS, 0, WIGGLE_LEG_MS};

// The servo is only powered (attached) while it moves. After a movement it
// gets SERVO_SETTLE_MS to arrive, then it is detached: no holding current,
// no buzzing, less load on the USB supply.
static bool releasePending = false;
static uint32_t releaseAtMs = 0;

// ---- Head follows the eyes (servo_look) ----
// The display asks for an angle; the servo task moves toward it in small steps (smooth, quiet) and, once it is back at
// rest, lets go again.
static volatile bool lookWanted = false;
static volatile int lookTargetAngle = SERVO_REST_ANGLE;
static bool lookTurned = false; // the head is (or was) away from rest because of servo_look
static uint32_t lookLastStepMs = 0;
static const uint16_t LOOK_STEP_MS = 30;
static const int LOOK_STEP_DEG = 2;

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
    curAngle = angle;
    releasePending = false;
}

static void moveStart(int to, uint16_t durMs, uint32_t now)
{
    moveFrom = curAngle;
    moveTo = to;
    moveStartMs = now;
    moveLastStepMs = 0;
    moveDurMs = durMs ? durMs : 1;
    moveActive = true;
}

// Call every tick; writes a new angle at most every SERVO_STEP_MS. Returns true while still moving.
static bool moveStep(uint32_t now)
{
    if (!moveActive)
        return false;
    if (moveLastStepMs != 0 && (now - moveLastStepMs) < SERVO_STEP_MS)
        return true;
    moveLastStepMs = now ? now : 1;
    float t = (float)(now - moveStartMs) / (float)moveDurMs;
    if (t >= 1.0f)
    {
        servoMove(moveTo);
        moveActive = false;
        return false;
    }
    float e = t * t * (3.0f - 2.0f * t);
    servoMove((int)lroundf(moveFrom + (moveTo - moveFrom) * e));
    return true;
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

// Idle only: step the head toward what servo_look asked for (or back to rest).
static void lookStep(uint32_t now)
{
    if (moveActive || (uint32_t)(now - lookLastStepMs) < LOOK_STEP_MS)
        return;
    int target = lookWanted ? (int)lookTargetAngle : SERVO_REST_ANGLE;
    int cur = (int)lroundf(curAngle);
    if (cur == target)
    {
        if (lookTurned && !lookWanted)
        {
            lookTurned = false;
            servoReleaseSoon(now); // back at rest: let go
        }
        return;
    }
    lookLastStepMs = now;
    int d = target - cur;
    if (d > LOOK_STEP_DEG)
        d = LOOK_STEP_DEG;
    else if (d < -LOOK_STEP_DEG)
        d = -LOOK_STEP_DEG;
    servoMove(cur + d);
    lookTurned = true;
}

void servo_look(float x)
{
    if (x > 1.0f)
        x = 1.0f;
    if (x < -1.0f)
        x = -1.0f;
    lookTargetAngle = SERVO_REST_ANGLE + (int)lroundf(x * SERVO_LOOK_DEG * SERVO_LOOK_DIR);
    lookWanted = true;
}

void servo_look_end()
{
    lookWanted = false;
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
            // pick the head movement for this reaction; it starts a moment AFTER the eyes reacted
            switch (pet_get_event().kind)
            {
            case PetKind::RAISED_BROW:
            case PetKind::SORRY: // a slight tilt of the head, held while the face does its thing
                petMotion = {SERVO_REST_ANGLE + SERVO_TILT_DEG, 260, 900, 320};
                break;
            case PetKind::MORNING: // stretch: slowly up, hold, slowly back
                petMotion = {SERVO_PET_ANGLE, 700, 300, 700};
                break;
            default: // the classic wiggle
                petMotion = {SERVO_PET_ANGLE, WIGGLE_LEG_MS, 0, WIGGLE_LEG_MS};
                break;
            }
            animState = ServoAnim::PET_WAIT;
            animStepStartMs = now;
        }
        else if (animState == ServoAnim::PET_WAIT && (now - animStepStartMs) >= SERVO_FOLLOW_DELAY_MS)
        {
            animState = ServoAnim::PET_WIGGLE_OUT;
            animStepStartMs = now;
            moveStart(petMotion.outAngle, petMotion.outMs, now);
        }
        else if (animState == ServoAnim::PET_WIGGLE_OUT)
        {
            moveStep(now);
            if (!moveActive && (now - animStepStartMs) >= petMotion.outMs)
            {
                animState = ServoAnim::PET_HOLD;
                animStepStartMs = now;
            }
        }
        else if (animState == ServoAnim::PET_HOLD && (now - animStepStartMs) >= petMotion.holdMs)
        {
            animState = ServoAnim::PET_WIGGLE_BACK;
            animStepStartMs = now;
            moveStart(SERVO_REST_ANGLE, petMotion.backMs, now);
        }
        else if (animState == ServoAnim::PET_WIGGLE_BACK)
        {
            moveStep(now);
            if (!moveActive && (now - animStepStartMs) >= petMotion.backMs)
            {
                animState = ServoAnim::REST;
                servoReleaseSoon(now);
                setMood(Mood::IDLE_BLINK); // the movement is done: hand control back to idle
            }
        }
    }
    else if (mood == Mood::PET_PURR)
    {
        if (animState != ServoAnim::PET_PURR)
        {
            moveActive = false;
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
            moveStart(SERVO_ALERT_ANGLE, SWEEP_LEG_MS, now);
        }
        else if ((now - animStepStartMs) > SWEEP_LEG_MS)
        {
            animStepStartMs = now;
            sweepAtAlertAngle = !sweepAtAlertAngle;
            moveStart(sweepAtAlertAngle ? SERVO_ALERT_ANGLE : SERVO_REST_ANGLE, SWEEP_LEG_MS, now);
        }
        moveStep(now);

        if (!readAlarmActive())
        {
            animState = ServoAnim::REST;
            moveActive = false;
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
            moveActive = false;
            servoMove(SERVO_REST_ANGLE);
            servoReleaseSoon(now);
        }
        else
            lookStep(now); // idle: the head may follow the eyes
    }
}