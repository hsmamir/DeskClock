// display_persona.cpp - behaviours that last longer than one frame and tie the clock's world to its character:
//   boot wake-up, going to sleep step by step, the hourly chime, the minute before the alarm, snooze / dismiss,
//   sulking after "that's enough!", looking at the sun / moon, and small idle moods (peek-a-boo, eye roll,
//   squint, a sigh of relief when WiFi is back, "oh!" at the first rain or snow).
// personaUpdate() decides, once per tick, what each of them wants and fills `persona` (display_internal.h);
// the drawing and the gaze / lid machines elsewhere only read it.
#include "display_internal.h"
#include "audio.h"

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
        OH
    };
    static Act act = Act::NONE;
    static uint32_t actStartMs = 0, actDurMs = 0;
    static uint32_t nextMicroAtMs = 0;
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
            r.kind = PetKind::SORRY; // the first touch after the sulk: a small apology
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
            sulkUntilMs = now + SULK_MS;
            apologyPending = true;
            apologyUntilMs = now + 10UL * 60UL * 1000UL;
        }
        if (apologyPending && (int32_t)(now - apologyUntilMs) >= 0)
            apologyPending = false;
        const bool sulking = (int32_t)(now - sulkUntilMs) < 0 && !in.alarm;

        // ---------- 3. sleep ----------
        const bool stayAwake = (int32_t)(now - stayAwakeUntilMs) < 0;
        const bool wantSleep = in.night && in.haveTime && !in.alarm && !snoozed && !preAlarm && !sulking && !stayAwake;
        if (in.alarm || preAlarm)
            slp = Sleep::AWAKE;
        switch (slp)
        {
        case Sleep::AWAKE:
            if (wantSleep)
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
            if (!wantSleep)
            {
                slp = Sleep::WAKING;
                slpStartMs = now;
                wakeFrom = P.lidBias;
            }
            else if (t >= SLEEP_RAMP_MS)
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
            else if (act == Act::NONE && !in.sleepy && in.haveTime && in.wifiDownMs == 0 && (int32_t)(now - nextMicroAtMs) >= 0)
            {
                int r = random(0, 100);
                if (r < 30)
                    startAct(Act::PEEK, now, 3400);
                else if (r < 70 || !in.sunny)
                    startAct(Act::ROLL, now, 1100);
                else
                    startAct(Act::SQUINT, now, 2200);
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
            default:
                break;
            }
        }

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