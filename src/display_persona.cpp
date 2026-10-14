// display_persona.cpp - behaviours that last longer than one frame and tie the clock's world to its character:
//   boot wake-up, going to sleep step by step, the hourly chime, the minute before the alarm, snooze / dismiss,
//   sulking after "that's enough!", looking at the sun / moon, and small idle moods (peek-a-boo, eye roll,
//   squint, a sigh of relief when WiFi is back, "oh!" at the first rain or snow, a fit of hiccups).
// personaUpdate() decides, once per tick, what each of them wants and fills `persona` (display_internal.h);
// the drawing and the gaze / lid machines elsewhere only read it.
#include "display_internal.h"
#include "audio.h"
#include "servo_ctrl.h"

namespace disp
{

    Persona persona;

    static float sstep(float t)
    {
        t = clamp01(t);
        return t * t * (3.0f - 2.0f * t);
    }
    static float easeOut(float t)
    {
        float i = 1.0f - clamp01(t);
        return 1.0f - i * i * i;
    }

    // ---- boot ----
    static bool bootInit = false;
    static uint8_t bootStep = 0; // 0 black / line, 1 lids released, 2 looking around, 3 = done
    static uint32_t bootMs = 0;

    // ---- sleep ----
    enum class Sleep : uint8_t
    {
        AWAKE,
        DROWSY,   // lids sink over SLEEP_RAMP_MS, two yawns
        SETTLING, // the last half-closed lids shut
        ASLEEP,
        WAKING // lids open again by themselves (day came)
    };
    static Sleep slp = Sleep::AWAKE;
    static uint32_t slpStartMs = 0;
    static uint8_t yawnsDone = 0;
    static float wakeFrom = 0;
    static uint32_t stayAwakeUntilMs = 0;
    static uint32_t frownUntilMs = 0;
    static uint32_t dreamAtMs = 0, dreamStartMs = 0;
    static uint8_t dreamKind = 0; // 0 none, 1 lid twitch, 2 dream bubble
    static uint8_t dreamIcon = 0;

    // ---- one-off acts ----
    enum class Act : uint8_t
    {
        NONE,
        CHIME,
        PEEK,
        ROLL,
        SQUINT,
        RELIEF,
        OH,
        THINK,    // "hmm": up and to the right, dots, sometimes an idea
        GLANCE,   // looks at the digits, they glow once
        FLY,      // a fly buzzes across; eyes (and head) follow
        FLY_LAND, // the fly lands between the eyes: cross-eyed, head shake, it flies off
        YAWN,     // evening / late-night yawn
        SNEEZE,   // "ah ... ah ... choo!" (rare; likelier in rain / snow / cold)
        HICCUP    // 3-4 small hops of the eyes, a "hic" beside the face, a head jolt; ends with a surprised look (rare)
    };
    static Act act = Act::NONE;
    static uint32_t actStartMs = 0, actDurMs = 0;
    static uint32_t nextMicroAtMs = 0;
    static uint32_t nextBondActMs = 0; // next sigh / self-started smile that comes from the bond level

    // ---- idle life ----
    static uint32_t nextYawnAtMs = 0;
    static bool eveningArmed = false;                                                   // the evening window was entered and the first yawn is scheduled
    static bool lateYawnDone = false;                                                   // already yawned during this "kept awake at night" spell
    static float flySide = 1.0f;                                                        // +1: enters on the right, -1: on the left
    static float flyPhase = 0;                                                          // makes every flight different
    static bool flyLandBuzzIn = false, flyLandBuzzOut = false, flyLandShakeSnd = false; // sounds already played
    static const float FLYLAND_Y = 0.18f;                                               // where the fly sits (flyY): just below the eye line, between the eyes
    static bool thinkIdea = false;                                                      // this "hmm" ends with an "oh!"
    static bool thinkIdeaShown = false;
    static bool sneezeBurstDone = false; // the burst's blink / sound was already triggered in this sneeze
    static const uint8_t HICCUP_MAX = 4;
    static const float HICCUP_HOP_MS = 260.0f;  // one hop of the eyes
    static const float HICCUP_TEXT_MS = 420.0f; // the "hic" stays this long (and fades)
    static uint8_t hiccupN = 0;                 // hiccups in this fit (3..4)
    static uint16_t hicAt[HICCUP_MAX];          // when each one comes, ms after the act started
    static uint16_t hicSurpriseAt = 0;          // when the surprised look starts
    static uint8_t hicSoundNext = 0;            // the next hiccup whose sound is still due
    static bool hicSurpriseDone = false;
    static uint32_t pulseStartMs = 0;
    static bool pulseOn = false;

    // ---- edges ----
    static int lastChimeKey = -1;
    static bool prevOffline = false;
    static bool prevSunrise = false;
    static bool precipPrevValid = false, precipPrev = false;
    static AlarmState prevAlarm = AlarmState::IDLE;

    // ---- sulk ----
    static bool annoyedPending = false;
    static uint32_t sulkUntilMs = 0;
    static bool apologyPending = false;
    static uint32_t apologyUntilMs = 0;

    static const float PEEK_HALF = 0.68f; // persona.peek where the eyes are half hidden

    static void startAct(Act a, uint32_t now, uint32_t dur)
    {
        act = a;
        actStartMs = now;
        actDurMs = dur;
    }

    // A fit of hiccups: 3..4 of them, never evenly spaced, then a short pause before the surprised look.
    static void startHiccup(uint32_t now)
    {
        hiccupN = (uint8_t)random(3, HICCUP_MAX + 1);
        uint16_t t = 450;
        for (uint8_t i = 0; i < hiccupN; i++)
        {
            hicAt[i] = t;
            t += (uint16_t)random(520, 760);
        }
        hicSurpriseAt = hicAt[hiccupN - 1] + 520;
        hicSoundNext = 0;
        hicSurpriseDone = false;
        startAct(Act::HICCUP, now, (uint32_t)hicSurpriseAt + 1250);
    }

    // Minutes until the eyes get drowsy (EYES_DROWSY_START_HOUR); negative once that hour has passed today.
    static float minutesToNight(const struct tm &ti)
    {
        float nowH = ti.tm_hour + ti.tm_min / 60.0f;
        return ((float)EYES_DROWSY_START_HOUR - nowH) * 60.0f;
    }

    static float clampGaze(float v)
    {
        return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }

    static uint32_t bondActDelayMs()
    {
        return (uint32_t)random(PET_BOND_ACT_MIN_MIN, PET_BOND_ACT_MAX_MIN + 1) * 60000UL;
    }

    // How long a sulk lasts: the more he likes you, the quicker he forgives.
    static uint32_t sulkDurationMs()
    {
        const PetBond b = pet_bond();
        if (!b.enabled)
            return SULK_MS;
        switch (b.level)
        {
        case BondLevel::DEVOTED:
            return SULK_MS / 2;
        case BondLevel::FOND:
            return SULK_MS * 3 / 4;
        case BondLevel::LONELY:
            return SULK_MS * 3 / 2;
        default:
            return SULK_MS;
        }
    }

    // A reaction nobody asked for (the head wiggles like after a tap). Same way as the morning stretch below.
    static void smileByHimself(PetKind kind)
    {
        pet_inject_event(kind);
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        if (g_state.mood != Mood::ALARM_ALERT)
            g_state.mood = Mood::PET_HAPPY;
        xSemaphoreGive(g_stateMutex);
    }

    static bool isPrecip(int code)
    {
        return (code >= 51 && code <= 67) || (code >= 71 && code <= 77) || (code >= 80 && code <= 86);
    }

    static void setGaze(float x, float y, bool direct = false)
    {
        persona.gazeOn = true;
        persona.gazeDirect = direct;
        persona.gazeX = x;
        persona.gazeY = y;
    }

    static void scheduleDream(uint32_t now)
    {
        dreamAtMs = now + (uint32_t)random(SLEEP_DREAM_MIN_S, SLEEP_DREAM_MAX_S + 1) * 1000UL;
        dreamKind = 0;
    }

    // ---- touch ----
    // Called once per new tap (display_tick). Decides what the tap does to the persona and which reaction
    // is shown. pe.streak == 0 marks an event the firmware injected (not a real tap).
    TouchResult personaOnTouch(const PetEvent &pe, uint32_t now)
    {
        TouchResult r{pe.kind, 0};
        if (pe.streak == 0)
            return r;

        if (slp == Sleep::ASLEEP || slp == Sleep::SETTLING)
        {
            // woken up: eyes open, a short frown, then the smile (not the night "contented" face)
            r.frownMs = SLEEP_FROWN_MS;
            frownUntilMs = now + SLEEP_FROWN_MS;
            if (r.kind == PetKind::SLEEPY)
                r.kind = PetKind::HAPPY;
            slp = Sleep::AWAKE;
            stayAwakeUntilMs = now + SLEEP_STAY_AWAKE_S * 1000UL;
        }
        else if (slp == Sleep::DROWSY || slp == Sleep::WAKING)
        {
            slp = Sleep::AWAKE;
            stayAwakeUntilMs = now + SLEEP_STAY_AWAKE_S * 1000UL;
        }

        if (r.kind == PetKind::ANNOYED)
        {
            annoyedPending = true; // the sulk starts when this face is over
            sulkUntilMs = 0;
        }
        else if (apologyPending && r.kind != PetKind::DIZZY)
        {
            r.kind = PetKind::SORRY;                  // the first touch after the sulk: a small apology
            pet_bond_add(PET_BOND_SORRY_GAIN, false); // making up brings you closer
            apologyPending = false;
            sulkUntilMs = 0;
        }
        return r;
    }

    // ---- per tick ----
    void personaUpdate(const PersonaIn &in)
    {
        Persona &P = persona;
        const uint32_t now = in.now;

        P.lidBias = 0;
        P.lidRamp = false;
        P.asleep = false;
        P.snoozed = false;
        P.thinking = false;
        P.exprOk = true;
        P.sulking = false;
        P.scalePulse = 1.0f;
        P.peek = 0;
        P.shiver = false;
        P.relief = 0;
        P.gazeOn = false;
        P.gazeDirect = false;
        P.celestOk = false;
        P.frown = (int32_t)(now - frownUntilMs) < 0;
        P.digitPulse = 0;
        P.flyOn = false;
        P.flyLanded = false;
        P.cross = 0;
        P.sneezeJerk = 0;
        P.sneezeSpray = 0;
        P.sneezeMouth = false;
        P.hiccupHop = 0;
        P.hiccupText = 0;

        // ---------- 1. wake-up at boot ----------
        if (!bootInit)
        {
            bootInit = true;
            bootMs = now;
            blinkHold(true);
        }
        const bool bootDone = bootStep >= 3;
        if (!bootDone)
        {
            uint32_t t = now - bootMs;
            P.exprOk = false;
            if (t < 300)
                P.stage = PStage::BOOT_BLACK;
            else if (t < 700)
            {
                P.stage = PStage::BOOT_LINE;
                P.lineP = (t - 300) / 400.0f;
            }
            else
            {
                P.stage = PStage::RUN;
                if (bootStep == 0)
                {
                    bootStep = 1;
                    blinkOpenSlow(now, 400); // the lids open with the spring (700..1100 ms)
                }
                if (t >= 1100 && t < 1367)
                    setGaze(-1, 0); // left, right, centre
                else if (t >= 1367 && t < 1633)
                    setGaze(1, 0);
                else if (t >= 1633 && t < 1900)
                    setGaze(0, 0);
                if (t >= 1900)
                {
                    bootStep = 3;
                    blinkTrigger(now);
                    audio_play_async(SOUND_INDEX_BOOT);
                    nextMicroAtMs = now + (uint32_t)random(MICRO_MIN_S, MICRO_MAX_S + 1) * 1000UL;
                    nextBondActMs = now + bondActDelayMs();
                }
            }
            return; // nothing else runs before the wake-up is over
        }
        P.stage = PStage::RUN;

        // ---------- 2. edges ----------
        const bool snoozed = in.alarmState == AlarmState::SNOOZED && !in.alarm;
        const bool preAlarm = in.alarmSecsLeft >= 0 && in.alarmSecsLeft <= PRE_ALARM_S && in.alarmState == AlarmState::IDLE && !in.alarm;

        // alarm over (dismissed / ran out): stretch and "good morning"
        if (prevAlarm != AlarmState::IDLE && in.alarmState == AlarmState::IDLE && !in.alarm)
        {
            pet_inject_event(PetKind::MORNING);
            xSemaphoreTake(g_stateMutex, portMAX_DELAY);
            if (g_state.mood != Mood::ALARM_ALERT)
                g_state.mood = Mood::PET_HAPPY; // the servo stretches
            xSemaphoreGive(g_stateMutex);
        }
        prevAlarm = in.alarmState;

        // top of the hour
        bool chime = false;
        if (in.haveTime && in.ti)
        {
            int key = in.ti->tm_yday * 24 + in.ti->tm_hour;
            if (lastChimeKey < 0)
                lastChimeKey = key;
            else if (key != lastChimeKey)
            {
                lastChimeKey = key;
                chime = (in.ti->tm_min == 0 && in.ti->tm_sec < 10);
            }
        }
        if (chime)
        {
            pulseOn = true;
            pulseStartMs = now;
            if (!in.night && slp == Sleep::AWAKE)
                audio_play_async(SOUND_INDEX_CHIME); // silent at night
        }

        // sulk begins when the "that's enough!" face is over
        if (annoyedPending && !in.happy && !in.alarm)
        {
            annoyedPending = false;
            sulkUntilMs = now + sulkDurationMs();
            apologyPending = true;
            apologyUntilMs = now + 10UL * 60UL * 1000UL;
        }
        if (apologyPending && (int32_t)(now - apologyUntilMs) >= 0)
            apologyPending = false;
        const bool sulking = (int32_t)(now - sulkUntilMs) < 0 && !in.alarm;

        // ---------- 3. sleep ----------
        const bool stayAwake = (int32_t)(now - stayAwakeUntilMs) < 0;
        // Fixed schedule: drowsy from EYES_DROWSY_START_HOUR, fast asleep from EYES_SLEEP_START_HOUR.
        const bool canRest = in.haveTime && !in.alarm && !snoozed && !preAlarm && !sulking && !stayAwake;
        const bool wantDrowsy = (in.drowsyWin || in.sleepWin) && canRest;
        const bool wantSleep = in.sleepWin && canRest;
        if (in.alarm || preAlarm)
            slp = Sleep::AWAKE;
        switch (slp)
        {
        case Sleep::AWAKE:
            if (wantDrowsy)
            {
                slp = Sleep::DROWSY;
                slpStartMs = now;
                yawnsDone = 0;
            }
            break;
        case Sleep::DROWSY:
        {
            uint32_t t = now - slpStartMs;
            P.lidBias = 0.5f * sstep(t / (float)SLEEP_RAMP_MS);
            P.lidRamp = true;
            if (yawnsDone == 0 && t >= SLEEP_RAMP_MS * 3UL / 10)
            {
                yawnsDone = 1;
                exprForce(Expr::YAWN, now, 2200);
            }
            else if (yawnsDone == 1 && t >= SLEEP_RAMP_MS * 7UL / 10)
            {
                yawnsDone = 2;
                exprForce(Expr::YAWN, now, 2200);
            }
            if (!wantDrowsy)
            {
                slp = Sleep::WAKING;
                slpStartMs = now;
                wakeFrom = P.lidBias;
            }
            else if (t >= SLEEP_RAMP_MS && wantSleep) // before 23:00 the lids just stay half closed
            {
                slp = Sleep::SETTLING;
                slpStartMs = now;
            }
            break;
        }
        case Sleep::SETTLING:
        {
            uint32_t t = now - slpStartMs;
            P.lidBias = 0.5f + 0.5f * sstep(t / 1200.0f);
            P.lidRamp = true;
            if (!wantSleep)
            {
                slp = Sleep::WAKING;
                slpStartMs = now;
                wakeFrom = P.lidBias;
            }
            else if (t >= 1200)
            {
                slp = Sleep::ASLEEP;
                scheduleDream(now);
            }
            break;
        }
        case Sleep::ASLEEP:
            P.asleep = true;
            P.lidBias = 1.0f;
            P.lidRamp = true;
            if (!wantSleep) // morning: wake by himself, slowly, with a yawn
            {
                slp = Sleep::WAKING;
                slpStartMs = now;
                wakeFrom = 1.0f;
                exprForce(Expr::YAWN, now, 2200);
            }
            else if (dreamKind == 0 && (int32_t)(now - dreamAtMs) >= 0)
            {
                dreamKind = random(0, 100) < 55 ? 1 : 2;
                dreamIcon = (uint8_t)random(0, 2);
                dreamStartMs = now;
            }
            else if (dreamKind == 1 && now - dreamStartMs > 260)
                scheduleDream(now);
            else if (dreamKind == 2 && now - dreamStartMs > 3300)
                scheduleDream(now);
            break;
        case Sleep::WAKING:
        {
            uint32_t t = now - slpStartMs;
            P.lidBias = wakeFrom * (1.0f - sstep(t / 2000.0f));
            P.lidRamp = true;
            if (t >= 2000)
                slp = Sleep::AWAKE;
            break;
        }
        }
        const bool sleeping = slp != Sleep::AWAKE;

        // ---------- 4. one-off acts ----------
        const bool canAct = !in.alarm && !in.happy && !sulking && !snoozed && !preAlarm && !sleeping && !P.frown;
        if (act != Act::NONE && (now - actStartMs >= actDurMs || !canAct))
            act = Act::NONE;

        // yawns: in the last stretch before night mode, and once when he is kept awake at night
        bool evening = false;
        if (IDLE_YAWN_ENABLED && in.haveTime && in.ti && !in.drowsyWin && !in.sleepWin)
        {
            float toNight = minutesToNight(*in.ti);
            evening = toNight > 0 && toNight <= (float)IDLE_YAWN_EVENING_MIN;
        }
        if (!evening)
            eveningArmed = false;
        else if (!eveningArmed)
        {
            eveningArmed = true;
            nextYawnAtMs = now + (uint32_t)random(IDLE_YAWN_MIN_MIN, IDLE_YAWN_MAX_MIN + 1) * 60000UL / 2; // the first one comes sooner
        }
        if (!((in.drowsyWin || in.sleepWin) && stayAwake))
            lateYawnDone = false;
        const bool lateYawn = IDLE_YAWN_ENABLED && (in.drowsyWin || in.sleepWin) && stayAwake && !lateYawnDone &&
                              (int32_t)(stayAwakeUntilMs - now) <= (int32_t)(SLEEP_STAY_AWAKE_S * 1000UL - IDLE_YAWN_LATE_S * 1000UL);

        if (canAct)
        {
            bool offlineEdge = prevOffline && !in.offline;
            bool sunriseEdge = in.sunrise && !prevSunrise;
            bool precipNow = in.weatherKnown && isPrecip(in.weatherCode);
            bool ohEdge = in.weatherKnown && precipPrevValid && !precipPrev && precipNow;

            if (offlineEdge)
                startAct(Act::RELIEF, now, 1800);
            else if (ohEdge)
            {
                startAct(Act::OH, now, 1100);
                exprForce(Expr::SURPRISE, now, 900);
            }
            else if (chime)
                startAct(Act::CHIME, now, 1600);
            else if (sunriseEdge)
                startAct(Act::SQUINT, now, 2200);
            else if (act == Act::NONE && ((evening && (int32_t)(now - nextYawnAtMs) >= 0) || lateYawn))
            {
                if (lateYawn)
                    lateYawnDone = true;
                nextYawnAtMs = now + (uint32_t)random(IDLE_YAWN_MIN_MIN, IDLE_YAWN_MAX_MIN + 1) * 60000UL;
                startAct(Act::YAWN, now, 2400);
                exprForce(Expr::YAWN, now, 2200); // (the yawn sound is muted at night by the audio module)
            }
            else if (PET_BOND_ENABLED && act == Act::NONE && !in.sleepy && in.haveTime && in.wifiDownMs == 0 &&
                     (int32_t)(now - nextBondActMs) >= 0)
            {
                // what he does by himself because of how much he likes you (daytime, nothing else going on)
                nextBondActMs = now + bondActDelayMs();
                const PetBond b = pet_bond();
                if (b.enabled)
                {
                    if (b.level == BondLevel::LONELY)
                        startAct(Act::RELIEF, now, 2000); // a small sigh: lids sink, a puff
                    else if (b.level == BondLevel::FOND)
                        smileByHimself(PetKind::HAPPY);
                    else if (b.level == BondLevel::DEVOTED)
                        smileByHimself(random(0, 100) < 50 ? PetKind::HEART_EYES : PetKind::HAPPY);
                }
            }
            else if (act == Act::NONE && !in.sleepy && in.haveTime && in.wifiDownMs == 0 && (int32_t)(now - nextMicroAtMs) >= 0)
            {
                int r = random(0, 100);
                bool sneezeNow = false;
                if (IDLE_SNEEZE_ENABLED)
                {
                    int chance = IDLE_SNEEZE_PERCENT;
                    bool coldNow = false;
                    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
                    coldNow = g_state.weatherValid && g_state.weatherTempC <= REACT_COLD_C;
                    xSemaphoreGive(g_stateMutex);
                    if ((in.weatherKnown && isPrecip(in.weatherCode)) || coldNow)
                        chance += IDLE_SNEEZE_WEATHER_BONUS;
                    sneezeNow = random(0, 100) < chance;
                }
                bool hiccupNow = false;
                if (!sneezeNow && IDLE_HICCUP_ENABLED)
                    hiccupNow = random(0, 100) < IDLE_HICCUP_PERCENT;
                if (sneezeNow)
                {
                    sneezeBurstDone = false;
                    startAct(Act::SNEEZE, now, IDLE_SNEEZE_MS);
                }
                else if (hiccupNow)
                    startHiccup(now);
                else if (r < 18)
                    startAct(Act::PEEK, now, 3400);
                else if (r < 30 && in.sunny)
                    startAct(Act::SQUINT, now, 2200);
                else if (r < 46 && IDLE_THINK_ENABLED)
                {
                    thinkIdea = random(0, 100) < 50;
                    thinkIdeaShown = false;
                    startAct(Act::THINK, now, 2600);
                }
                else if (r < 62 && IDLE_GLANCE_ENABLED)
                {
                    startAct(Act::GLANCE, now, 1500);
                    pulseOn = true; // the digits glow once while he looks
                    pulseStartMs = now;
                }
                else if (r < 78 && IDLE_FLY_ENABLED)
                {
                    flySide = random(0, 2) ? 1.0f : -1.0f;
                    flyPhase = random(0, 628) / 100.0f;
                    if (IDLE_FLYLAND_PERCENT > 0 && random(0, 100) < IDLE_FLYLAND_PERCENT)
                    {
                        flyLandBuzzIn = flyLandBuzzOut = flyLandShakeSnd = false;
                        startAct(Act::FLY_LAND, now, IDLE_FLYLAND_MS);
                    }
                    else
                        startAct(Act::FLY, now, IDLE_FLY_MS);
                }
                else
                    startAct(Act::ROLL, now, 1100);
                nextMicroAtMs = now + (uint32_t)random(MICRO_MIN_S, MICRO_MAX_S + 1) * 1000UL;
            }
        }
        prevOffline = in.offline;
        prevSunrise = in.sunrise;
        if (in.weatherKnown)
        {
            precipPrev = isPrecip(in.weatherCode);
            precipPrevValid = true;
        }

        // a running act
        if (act != Act::NONE)
        {
            float t = (float)(now - actStartMs);
            switch (act)
            {
            case Act::CHIME: // eyes to the digits, widen once
                if (t < 1300)
                    setGaze(0, CHIME_LOOK_Y);
                if (t > 250 && t < 700)
                    P.scalePulse = 1.0f + 0.2f * sinf(3.14159f * (t - 250.0f) / 450.0f);
                break;
            case Act::PEEK: // rise from below, look left-right, pop up
            {
                float p;
                if (t < 500)
                    p = 1.0f - sstep(t / 500.0f) * (1.0f - PEEK_HALF);
                else if (t < 1900)
                    p = PEEK_HALF;
                else
                    p = PEEK_HALF * (1.0f - sstep((t - 1900.0f) / 400.0f));
                P.peek = p;
                P.exprOk = false;
                if (t >= 500 && t < 1600)
                    setGaze(-sinf(6.2832f * (t - 500.0f) / 1100.0f), 0, true);
                else
                    setGaze(0, 0, true);
                break;
            }
            case Act::ROLL: // bored: left, over the top, right, back
            {
                P.exprOk = false;
                P.lidBias = 0.28f * sinf(3.14159f * t / 1100.0f);
                if (t < 900)
                {
                    float p = t / 900.0f;
                    setGaze(-cosf(3.14159f * p), -sinf(3.14159f * p), true);
                }
                else
                {
                    float k = 1.0f - sstep((t - 900.0f) / 200.0f);
                    setGaze(k, 0, true);
                }
                break;
            }
            case Act::SQUINT: // bright light
                P.exprOk = false;
                P.lidBias = 0.45f * sstep(t / 250.0f) * (1.0f - sstep((t - 1850.0f) / 350.0f));
                P.scalePulse = 0.96f;
                setGaze(-0.5f, 0.4f);
                break;
            case Act::RELIEF: // whew: lids sink, a small puff, lids come back
            {
                P.exprOk = false;
                float up = t < 500 ? sstep(t / 500.0f) : (t < 1000 ? 1.0f : 1.0f - sstep((t - 1000.0f) / 600.0f));
                P.lidBias = 0.85f * up;
                P.relief = t / (float)actDurMs;
                break;
            }
            case Act::OH:
                if (t < 900)
                    setGaze(0, -1);
                break;
            case Act::THINK: // "hmm": the dots bounce while he looks up and to the right; then maybe "oh!"
            {
                const float ideaAt = (float)actDurMs - 700.0f;
                if (t < ideaAt)
                {
                    P.exprOk = false;
                    P.thinking = true;
                    P.lidBias = 0.10f * sstep(t / 400.0f);
                    setGaze(0.9f, -0.8f);
                }
                else
                {
                    setGaze(0, -0.2f);
                    if (thinkIdea && !thinkIdeaShown)
                    {
                        thinkIdeaShown = true;
                        exprForce(Expr::SURPRISE, now, 650); // exprOk stays true so it is shown
                    }
                }
                break;
            }
            case Act::GLANCE: // check the time
                if (t < 1200)
                    setGaze(0, CHIME_LOOK_Y);
                break;
            case Act::FLY: // in from one side, loops, out on the other; the eyes (and head) follow
            {
                float uu = t / (float)actDurMs;
                if (uu > 1.0f)
                    uu = 1.0f;
                const float env = sinf(3.14159f * uu); // 0 at both ends: it comes and goes in a fairly straight line
                float fx = flySide * 1.2f * (0.65f * (1.0f - 2.0f * uu) + 0.35f * cosf(3.14159f * uu));
                fx += 0.40f * sinf(6.2832f * 2.5f * uu + flyPhase) * env;
                float fy = -0.10f + 0.50f * sinf(6.2832f * 1.5f * uu + flyPhase * 0.7f) * env + 0.18f * sinf(6.2832f * 6.0f * uu) * env;
                P.flyOn = true;
                P.flyX = fx;
                P.flyY = fy;
                P.exprOk = false;
                setGaze(clampGaze(fx), clampGaze(fy), true);
                if (IDLE_FLY_HEAD)
                    servo_look(clampGaze(fx));
                break;
            }
            case Act::FLY_LAND: // in, lands between the eyes, they cross, head shake, the fly leaves
            {
                // phases as fractions of the whole act: ..0.18 it comes in, ..0.27 lands and the eyes cross, ..0.51 he
                // stares at it (the fly cleans its legs), ..0.68 head shake (eyes uncross, lids squint), the fly takes
                // off at 0.54 and is gone by 0.82 (the eyes follow it from 0.62), then back to the middle
                const float tn = t / (float)actDurMs;
                float fx, fy, gx = 0, gy = 0, headX = 0, cross = 0, lid = 0;
                bool followHead = false;
                P.exprOk = false;
                if (tn < 0.18f)
                {
                    const float k = tn / 0.18f;
                    const float e = 1.0f - (1.0f - k) * (1.0f - k); // brakes as it arrives
                    const float wob = (1.0f - e);
                    fx = flySide * 1.25f * (1.0f - e) + 0.30f * sinf(6.2832f * 2.0f * k + flyPhase) * wob;
                    fy = -0.35f + (FLYLAND_Y + 0.35f) * e + 0.25f * sinf(6.2832f * 3.0f * k + flyPhase * 0.7f) * wob;
                    gx = clampGaze(fx);
                    gy = clampGaze(fy);
                    followHead = true;
                    if (!flyLandBuzzIn)
                    {
                        flyLandBuzzIn = true;
                        audio_play_async(SOUND_INDEX_FLY);
                    }
                }
                else if (tn < 0.54f)
                {
                    fx = 0;
                    fy = FLYLAND_Y;
                    P.flyLanded = true;
                    gx = 0;
                    gy = 0.25f;
                }
                else
                {
                    const float k = (tn - 0.54f) / 0.28f; // 0..1 over the fly's exit
                    const float kk = k > 1.0f ? 1.0f : k;
                    fx = flySide * (0.10f * sinf(40.0f * kk) + 1.35f * kk * kk);
                    fy = FLYLAND_Y - 1.15f * kk * sqrtf(kk) + 0.10f * sinf(33.0f * kk);
                    if (!flyLandBuzzOut)
                    {
                        flyLandBuzzOut = true;
                        audio_play_async(SOUND_INDEX_FLY);
                    }
                    if (tn < 0.62f)
                    {
                        gx = 0;
                        gy = 0.25f;
                    }
                    else if (tn < 0.82f)
                    {
                        gx = clampGaze(fx);
                        gy = clampGaze(fy);
                        followHead = true;
                    }
                    else
                    {
                        gx = 0;
                        gy = 0;
                    }
                }
                // the eyes cross while it sits there (a nervous quiver), and uncross when the shake starts
                if (tn < 0.18f)
                    cross = 0;
                else if (tn < 0.27f)
                    cross = sstep((tn - 0.18f) / 0.09f);
                else if (tn < 0.51f)
                    cross = 1.0f + 0.05f * sinf(t / 85.0f);
                else if (tn < 0.58f)
                    cross = 1.0f - sstep((tn - 0.51f) / 0.07f);
                if (tn >= 0.18f && tn < 0.51f)
                    lid = 0.08f;
                if (tn >= 0.51f && tn < 0.68f)
                {
                    // head shake: 2 swings each way, a little smaller towards the end; lids squint while it lasts
                    const float kk = (tn - 0.51f) / 0.17f;
                    const float amp = IDLE_FLYLAND_HEAD ? (IDLE_FLYLAND_SHAKE_DEG / (float)SERVO_LOOK_DEG) : 0.0f;
                    headX = amp * (1.0f - 0.4f * kk) * sinf(6.2832f * 2.0f * kk);
                    lid = 0.40f * sinf(3.14159f * kk);
                    if (!flyLandShakeSnd)
                    {
                        flyLandShakeSnd = true;
                        audio_play_async(SOUND_INDEX_FLY_SHAKE);
                    }
                }
                if (tn < 0.82f)
                {
                    P.flyOn = true;
                    P.flyX = fx;
                    P.flyY = fy;
                }
                P.cross = cross;
                P.lidBias = lid;
                setGaze(gx, gy, true);
                if (IDLE_FLYLAND_HEAD)
                    servo_look(followHead ? clampGaze(IDLE_FLY_HEAD ? gx : 0.0f) : headX);
                break;
            }
            case Act::SNEEZE: // inhale, inhale, hold, CHOO, a sheepish glance away
            {
                // phases as fractions of the whole act: 0..0.23 first "ah", ..0.29 sags back, ..0.54 second (bigger)
                // "ah", ..0.61 holds it (quiver), ..0.77 the burst, ..1.0 recovers
                const float tn = t / (float)actDurMs;
                float lid = 0, big = 1.0f, headX = 0;
                P.exprOk = false;
                if (tn < 0.23f)
                {
                    const float k = sstep(tn / 0.23f);
                    lid = 0.32f * k;
                    big = 1.0f + 0.06f * k;
                    headX = 0.25f * k;
                    setGaze(0, -0.7f * k, true);
                }
                else if (tn < 0.29f)
                {
                    const float k = (tn - 0.23f) / 0.06f;
                    lid = 0.32f - 0.14f * k;
                    big = 1.06f - 0.04f * k;
                    headX = 0.25f - 0.10f * k;
                    setGaze(0, -0.7f + 0.3f * k, true);
                }
                else if (tn < 0.54f)
                {
                    const float k = sstep((tn - 0.29f) / 0.25f);
                    lid = 0.18f + 0.38f * k;
                    big = 1.02f + 0.12f * k;
                    headX = 0.15f + 0.30f * k;
                    setGaze(0, -0.4f - 0.5f * k, true);
                    P.sneezeMouth = true;
                }
                else if (tn < 0.61f)
                {
                    lid = 0.60f;
                    big = 1.14f;
                    headX = 0.45f;
                    P.shiver = true; // the quiver before it comes
                    setGaze(0, -0.9f, true);
                    P.sneezeMouth = true;
                }
                else if (tn < 0.77f)
                {
                    const float k = (tn - 0.61f) / 0.16f;
                    if (!sneezeBurstDone)
                    {
                        sneezeBurstDone = true;
                        blinkTrigger(now);
                        audio_play_async(SOUND_INDEX_SNEEZE);
                    }
                    lid = 0.95f;
                    big = 0.90f;
                    headX = -0.85f * (1.0f - 0.35f * k);    // the head snaps to the other side
                    P.sneezeJerk = 1.0f - sstep(k / 0.75f); // sstep() clamps to 0..1
                    P.sneezeSpray = (tn - 0.61f) / 0.30f;
                    P.sneezeMouth = true;
                    setGaze(0, 0.5f, true);
                }
                else
                {
                    const float k = (tn - 0.77f) / 0.23f;
                    lid = 0.95f * (1.0f - sstep(k / 0.55f));
                    big = 0.90f + 0.10f * sstep(k);
                    headX = -0.55f * (1.0f - sstep(k));
                    if (tn < 0.91f)
                        P.sneezeSpray = (tn - 0.61f) / 0.30f; // the last drops
                    // sheepish: a glance away, then back to the middle
                    if (k < 0.7f)
                        setGaze(-0.8f, 0.3f, true);
                    else
                        setGaze(0, 0, true);
                }
                P.lidBias = lid;
                P.scalePulse = big;
                if (IDLE_SNEEZE_HEAD)
                    servo_look(clampGaze(headX));
                break;
            }
            case Act::HICCUP: // hop, "hic", hop, "hic" ... then a surprised look
            {
                float hop = 0;
                int8_t side = 1;
                for (uint8_t i = 0; i < hiccupN; i++)
                {
                    const float d = t - (float)hicAt[i];
                    if (d < 0 || d >= HICCUP_TEXT_MS)
                        continue;
                    if (i >= hicSoundNext) // one short sound per hiccup
                    {
                        hicSoundNext = i + 1;
                        audio_play_async(SOUND_INDEX_HICCUP);
                    }
                    side = (i & 1) ? -1 : 1;
                    P.hiccupText = (d + 20.0f) / (HICCUP_TEXT_MS + 20.0f); // always > 0 while it shows
                    if (d < HICCUP_HOP_MS)
                    {
                        const float p = d / HICCUP_HOP_MS; // up fast (first quarter), back down slower
                        hop = p < 0.25f ? p / 0.25f : 1.0f - sstep((p - 0.25f) / 0.75f);
                    }
                }
                P.hiccupSide = side;
                if (t < (float)hicSurpriseAt)
                {
                    P.exprOk = false;
                    P.hiccupHop = hop;
                    P.lidBias = 0.18f * hop;       // the lids flinch a little at each hic
                    setGaze(0, -0.7f * hop, true); // the eyes jump up and drop back
                    if (IDLE_HICCUP_HEAD)          // a tiny jolt of the head, then back to rest
                        servo_look(hop > 0.3f ? side * (IDLE_HICCUP_HEAD_DEG / (float)SERVO_LOOK_DEG) : 0.0f);
                }
                else
                {
                    // the fit is over: the eyes widen and look straight ahead in surprise (exprOk stays true so it shows)
                    const float ts = t - (float)hicSurpriseAt;
                    if (!hicSurpriseDone)
                    {
                        hicSurpriseDone = true;
                        exprForce(Expr::SURPRISE, now, 900);
                    }
                    P.scalePulse = 1.0f + 0.12f * sstep(ts / 200.0f) * (1.0f - sstep((ts - 950.0f) / 300.0f));
                    setGaze(0, 0, true);
                }
                break;
            }
            default:
                break;
            }
        }

        if (act != Act::FLY && !(act == Act::FLY_LAND && IDLE_FLYLAND_HEAD) && !(act == Act::SNEEZE && IDLE_SNEEZE_HEAD) && !(act == Act::HICCUP && IDLE_HICCUP_HEAD))
            servo_look_end(); // the head drifts back to rest

        // ---------- 5. longer moods (no act running) ----------
        if (P.asleep)
            P.exprOk = false; // the asleep drawing handles everything
        else if (act == Act::NONE || act == Act::CHIME)
        {
            if (P.frown)
            {
            }
            else if (snoozed)
            {
                P.snoozed = true;
                P.exprOk = false;
                setGaze(0, 0.5f);
            }
            else if (preAlarm)
            {
                // quick glances at the bell (bottom left), faster in the last 10 s, plus a slight shiver
                uint32_t cycle = in.alarmSecsLeft > 10 ? 2200 : 1100;
                float ph = (now % cycle) / (float)cycle;
                if (ph < 0.35f)
                    setGaze(-1, 1);
                else
                    setGaze(0, 0.2f);
                P.shiver = true;
                P.exprOk = false;
            }
            else if (sulking)
            {
                P.sulking = true;
                P.exprOk = false;
                P.lidBias = 0.2f;
                setGaze(-1, 0.1f); // turned away
            }
            else if (!in.offline && !in.alarm && (!in.haveTime || in.wifiDownMs > 0) && !in.happy)
            {
                P.thinking = true; // time / WiFi on its way: "thinking", up and to the right
                P.exprOk = false;
                setGaze(0.9f, -0.8f);
            }
        }

        // the sun / moon as a look target for a share of the idle looks
        P.celestOk = in.celestOk && !in.sleepy;
        P.celestX = in.celestX;
        P.celestY = in.celestY;

        // clock digits flash (even while asleep or in a reaction)
        if (pulseOn)
        {
            float t = (float)(now - pulseStartMs);
            if (t > 1150)
                pulseOn = false;
            else if (t > 150)
                P.digitPulse = sinf(3.14159f * (t - 150.0f) / 1000.0f);
        }
    }

    // ---------- drawing ----------

    // Wake-up: a thin line (3 px) grows from the middle of the eye pair, glow around it.
    void drawBootLine(TFT_eSPI &g, int16_t cy, int16_t R)
    {
        int16_t half = (int16_t)(3 * R * easeOut(persona.lineP));
        if (half < 1)
            return;
        int16_t h = imax(3, u(3));
        int16_t x = W / 2 - half, w = 2 * half;
        g.fillRoundRect(x - 1, cy - h / 2 - 2, w + 2, h + 4, h / 2 + 2, tint(C_EYE_BOT, 0.25f));
        g.fillRoundRect(x, cy - h / 2, w, h, h / 2, mixColor(C_EYE_TOP, C_EYE_BOT, 0.3f));
    }

    static void drawDreamBubble(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t lcx, int16_t R)
    {
        float p = (now - dreamStartMs) / 3200.0f;
        if (p < 0 || p > 1)
            return;
        float env = sinf(3.14159f * p);
        int16_t bx = lcx - u(4), by = cy - R - u(17), br = imax(4, u(8));
        // two small bubbles lead up to the big one (they pop in one after the other)
        if (p > 0.05f)
            g.fillCircle(lcx + u(6), cy - R - u(4), imax(1, u(1.5f)), tint(C_ICE, 0.5f * env));
        if (p > 0.15f)
            g.fillCircle(lcx + u(2), cy - R - u(9), imax(1, u(2.2f)), tint(C_ICE, 0.5f * env));
        if (p > 0.25f)
        {
            g.fillCircle(bx, by, br, tint(C_ICE, 0.12f * env));
            g.drawCircle(bx, by, br, tint(C_ICE, 0.5f * env));
            if (dreamIcon == 0)
                drawHeart(g, bx, by - u(2), u(5), tint(C_HEART, 0.85f * env));
            else // a small star
            {
                uint16_t c = tint(C_AMBER, 0.9f * env);
                int16_t a = imax(2, u(3));
                g.fillCircle(bx, by, imax(1, u(1.5f)), c);
                g.drawFastHLine(bx - a, by, 2 * a + 1, c);
                g.drawFastVLine(bx, by - a, 2 * a + 1, c);
            }
        }
    }

    // Fast asleep: contented closed eyes, Zs; now and then a lid twitch or a dream bubble.
    void drawSleepFace(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t lcx, int16_t rcx, int16_t R)
    {
        int16_t jx = 0, jy = 0;
        if (dreamKind == 1 && now - dreamStartMs < 260)
        {
            jx = ((now / 55) & 1) ? 1 : -1;
            jy = -1;
        }
        int16_t br = (int16_t)lroundf(sinf(now / 1400.0f) * 0.6f * (sc >= 1.5f ? 2 : 1)); // slow breathing
        drawContentEye(g, lcx + jx, cy + jy + br, R);
        drawContentEye(g, rcx + jx, cy + jy + br, R);
        drawSleepZ(g, now, cy, R);
        if (dreamKind == 2)
            drawDreamBubble(g, now, cy, lcx, R);
    }

    // Small additions on top of the normal face: the thinking dots, the snooze dots, the sigh's puff.
    void drawPersonaExtras(TFT_eSPI &g, uint32_t now, int16_t cy, int16_t R, int16_t eyeH)
    {
        if (persona.thinking)
        {
            int16_t y = cy + eyeH / 2 + u(9), gap = u(8), r = imax(1, u(2));
            for (int i = 0; i < 3; i++)
            {
                float ph = fmodf(now / 450.0f - i * 0.28f, 1.0f);
                if (ph < 0)
                    ph += 1.0f;
                float b = sinf(ph * 6.2832f);
                if (b < 0)
                    b = 0;
                g.fillCircle(W / 2 + (i - 1) * gap, y - (int16_t)(b * u(3)), r, tint(C_EYE_TOP, 0.35f + 0.6f * b));
            }
        }
        if (persona.snoozed)
        {
            for (int i = 0; i < 3; i++)
            {
                float a = sinf(now / 700.0f - i * 0.9f) * 0.5f + 0.5f;
                g.fillCircle(W * 3 / 4 + i * u(6), cy - R - u(6) - i * u(2), imax(1, u(1.5f)), tint(C_ICE, 0.25f + 0.45f * a));
            }
        }
        if (persona.flyOn)
        {
            int16_t fx = W / 2 + (int16_t)(persona.flyX * (W / 2));
            int16_t fy = cy + (int16_t)(persona.flyY * (faceH * 0.36f));
            const int16_t r = imax(1, u(1.4f));
            // flying: the wings flick up and down; landed: folded on the back, now and then a quick twitch
            int16_t wy = ((now / 45) & 1) ? -imax(1, u(1.8f)) : -imax(1, u(0.8f));
            if (persona.flyLanded)
            {
                wy = -imax(1, u(0.4f));
                if ((now / 350) % 4 == 0) // the legs rub: a small jiggle of the whole fly
                    fx += ((now / 60) & 1) ? 1 : -1;
            }
            const uint16_t wing = rgb(200, 215, 230);
            g.fillCircle(fx - u(1.8f), fy + wy, r, wing);
            g.fillCircle(fx + u(1.8f), fy + wy, r, wing);
            g.fillCircle(fx, fy, r + 1, rgb(190, 190, 200)); // light rim: visible on the dark card and on the bright eyes
            g.fillCircle(fx, fy, r, rgb(25, 25, 30));
        }
        if (persona.hiccupText > 0.0f && persona.hiccupText < 1.0f)
        {
            // "hic": pops up beside the face (above the eye on the side of the jolt), rises a little and fades;
            // the side alternates with every hiccup
            const float p = persona.hiccupText;
            const int16_t hx = W / 2 + persona.hiccupSide * (2 * R + u(8));
            const int16_t hy = cy - R - u(15) - (int16_t)(p * u(4));
            g.setTextDatum(MC_DATUM);
            g.setTextColor(tint(C_AMBER, clamp01(1.4f * (1.0f - p))));
            g.drawString("hic", hx, hy, 2);
        }
        if (persona.sneezeSpray > 0.0f && persona.sneezeSpray < 1.0f)
        {
            // the spray of the sneeze: a fan of small drops from the mouth, flying down and outwards, fading as they go
            const float p = persona.sneezeSpray;
            const float fly = 1.0f - (1.0f - p) * (1.0f - p); // quick at first, then slower
            const int16_t mx = W / 2, my = cy + eyeH / 2 + u(10);
            for (int i = 0; i < 7; i++)
            {
                const float a = 3.14159f * (0.22f + 0.56f * i / 6.0f); // 0.22 pi .. 0.78 pi: downwards, fanned out
                const float reach = u(14) + u(10) * ((i * 5) % 3);     // each drop goes a bit different
                const int16_t px = mx + (int16_t)(cosf(a) * reach * fly * 1.6f);
                const int16_t py = my + (int16_t)(sinf(a) * reach * fly) + (int16_t)(p * p * u(6)); // gravity pulls them down
                const int16_t pr = imax(1, (int16_t)(u(2.2f) * (1.0f - 0.6f * p)));
                g.fillCircle(px, py, pr, tint(C_ICE, 0.9f * (1.0f - p)));
            }
        }
        if (persona.relief > 0.2f)
        {
            int16_t mouthY = cy + eyeH / 2 + u(10);
            for (int k = 0; k < 3; k++)
            {
                float pk = clamp01((persona.relief - 0.2f - 0.1f * k) / 0.6f);
                if (pk <= 0)
                    continue;
                g.fillCircle(W / 2 + u(4) + (int16_t)(pk * u(14)) + k * u(3), mouthY + u(2) + (int16_t)(pk * u(5)),
                             u(2) + (int16_t)(pk * u(3)), tint(C_ICE, 0.4f * (1.0f - pk)));
            }
        }
    }

} // namespace disp