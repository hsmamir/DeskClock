// display.cpp - public display API: display_init() and display_tick(). The drawing code lives in display_*.cpp;
// shared declarations are in display_internal.h.
#include "display_internal.h"
#include <WiFi.h>

using namespace disp;

// ---- Alarm flash timing ----
static uint32_t lastAlarmFlashMs = 0;
static const uint16_t ALARM_FLASH_MS = 200;

// ---- Petted state: when the reaction face stops / which tap it already saw ----
static uint32_t petSeqSeen = 0;
static uint32_t happyUntilMs = 0;

// ---- Setup / OTA screen bookkeeping ----
static bool portalScreenShown = false;
static bool otaScreenShown = false;

// How far the sun (by day, 0 = sunrise .. 1 = sunset) or the moon (by night, 0 = sunset ..
// 1 = next sunrise) has travelled along its path across the sky. -1 = no sensible value
// (polar day / night): the scene then keeps the sun / moon in the corner.
static float skyArc(const struct tm &ti, bool day)
{
    float riseH, setH;
    SunTimes st = sun_today(ti);
    if (st.valid)
    {
        if (st.polarNight || st.midnightSun)
            return -1.0f;
        riseH = st.riseH;
        setH = st.setH; // always > riseH, may be > 24
    }
    else
    {
        riseH = SCENE_DAY_START_HOUR; // same fallback sun_is_day() uses
        setH = SCENE_DAY_END_HOUR;
    }
    float h = ti.tm_hour + ti.tm_min / 60.0f + ti.tm_sec / 3600.0f;
    float len, p;
    if (day)
    {
        if (h < riseH)
            h += 24.0f; // a sunset after midnight: the small hours still belong to yesterday's day
        len = setH - riseH;
        p = (h - riseH) / len;
    }
    else
    {
        while (h < setH)
            h += 24.0f;
        len = riseH + 24.0f - setH;
        p = (h - setH) / len;
    }
    if (len < 0.05f)
        return -1.0f;
    return p < 0 ? 0 : (p > 1 ? 1 : p);
}

void display_init()
{
    // Backlight (see the Backlight section): dims at night, see display_tick.
    backlightInit();

    tft.init();
    tft.setRotation(0);

    // two rounded cards (face on top, info panel below) on a darker frame: sizes, corner table, frame fill
    cardsInit();

    bool f = faceCv.init(cardX, faceY, W, faceH);
    bool i = infoCv.init(cardX, infoY, W, infoH);

    // small sprite for the clock-digit slide animation (needs the info panel to be a sprite too)
    if (CLOCK_DIGIT_ANIM && i)
    {
        int16_t dw, dh;
        clockMetrics(dw, dh);
        digSpr.setColorDepth(16);
        digSprOk = (digSpr.createSprite(dw + 2 * DIG_PAD, dh + 2 * DIG_PAD) != nullptr);
    }
    Serial.printf("display sprites: face=%s info=%s digit=%s (free heap %u)\n",
                  f ? "ok" : "fallback", i ? "ok" : "fallback", digSprOk ? "ok" : "off",
                  (unsigned)ESP.getFreeHeap());

    blinkPhaseStartMs = millis();
}

void display_tick()
{
    uint32_t now = millis();
    Mood mood = readMood();
    DisplayMode mode = readDisplayMode();
    bool timeSynced = readTimeSynced();

    // A firmware update owns the screen: full brightness, progress only.
    int otaPct = readOtaPercent();
    if (otaPct >= 0)
    {
        backlightUpdate(false, true);
        drawOtaScreen(otaPct);
        otaScreenShown = true;
        return;
    }
    if (otaScreenShown)
    {
        // update failed (on success the clock reboots): back to normal
        tft.fillScreen(C_BG);
        frameDirty = true;
        otaScreenShown = false;
        otaDrawnPct = -1;
        lastInfoSig = 0xFFFFFFFFu;
    }

    // While the WiFi setup portal is open, show how to join it instead of the face.
    if (readPortalActive())
    {
        backlightUpdate(false, true); // setup screen must always be readable
        if (!portalScreenShown)
        {
            drawPortalScreen();
            portalScreenShown = true;
        }
        return;
    }
    if (portalScreenShown)
    {
        tft.fillScreen(C_BG);
        frameDirty = true;
        portalScreenShown = false;
        lastInfoSig = 0xFFFFFFFFu;
    }

    bool alarm = (mood == Mood::ALARM_ALERT);
    // Only the clock page shows the face (the eyes). Date and Weather are full-screen pages; an alarm always
    // brings the face and the clock back, whatever page was open. (The diagnostics page draws its own two cards.)
    fullLayout = !alarm && isFullPage(mode);

    struct tm ti;
    bool haveTime = timeSynced && getLocalTime(&ti, 0);
    themeUpdate(haveTime, ti, now); // background / accent follow the time of day
    frameUpdate();                  // the frame around the cards follows the card background

    bool night = haveTime && sun_is_night(ti);
    bool sleepy = night && !alarm;

    // Dim at night; an alarm always gets full brightness, immediately.
    backlightUpdate(night && !alarm, alarm);

    // petted: hold the reaction face for a while even after the servo finishes.
    // A new tap (new seq) restarts the animation, also while the old one is still running.
    PetEvent pe = pet_get_event();
    if (pe.seq != petSeqSeen)
    {
        petSeqSeen = pe.seq;
        // the persona may turn the reaction into another one (apology after a sulk) or ask for a frown first (woken up)
        TouchResult tr = personaOnTouch(pe, now);
        if (tr.kind != pe.kind)
        {
            pe.kind = tr.kind;
            pet_set_kind(tr.kind); // the servo reads the kind too
        }
        petEv = pe;
        happyStartMs = now + tr.frownMs;
        happyUntilMs = happyStartMs + pet_hold_ms(pe.kind);
    }
    bool purr = !alarm && mood == Mood::PET_PURR; // button held: purring takes over from the tap reaction
    bool happy = !alarm && (purr || ((int32_t)(now - happyStartMs) >= 0 && now < happyUntilMs));

    if (alarm && now - lastAlarmFlashMs > ALARM_FLASH_MS)
    {
        alarmFlashOn = !alarmFlashOn;
        lastAlarmFlashMs = now;
    }

    // glance down at the panel whenever the page changes
    if (mode != lastMode && !alarm)
        glanceUntilMs = now + 900;

    pollWifi(now);

    // what the face background / reactions need to know
    SceneInfo scn;
    scn.timeKnown = haveTime;
    scn.day = haveTime && sun_is_day(ti);
    scn.moonPhase = haveTime ? moon_phase_view() : 0.5f;
    scn.arc = (SCENE_SUN_ARC && haveTime) ? skyArc(ti, scn.day) : -1.0f;
    {
        bool wv = false;
        float wt = 0;
        int wc = 0;
        uint32_t wMs = 0;
        readWeather(wv, wt, wc, wMs);
        scn.weatherKnown = wv && ageSince(now, wMs) <= SCENE_WEATHER_MAX_AGE_MS;
        scn.code = wc;
        scn.tempC = wt;
        scn.windKmh = -1.0f;
        scn.windDirDeg = -1;
        if (scn.weatherKnown)
        {
            WeatherDetail wd;
            readWeatherDetail(wd);
            scn.windKmh = wd.windKmh;
            scn.windDirDeg = wd.windDirDeg;
        }
    }
    ambientUpdate(scn, now); // spots the sunrise for the warm flash
    bool offline = wifiDownSinceMs != 0 && (now - wifiDownSinceMs) >= REACT_OFFLINE_DELAY_MS;
    Reaction reaction = alarm ? Reaction::NONE : pickReaction(scn, offline, sleepy);
    emoUpdate(now, emoPick(alarm, happy, purr, sleepy, reaction)); // eye colour follows the feeling

    // behaviours that span many frames: boot wake-up, sleep, chime, pre-alarm, sulk, micro moods ...
    {
        PersonaIn pin;
        pin.now = now;
        pin.alarm = alarm;
        pin.happy = happy;
        pin.haveTime = haveTime;
        pin.ti = haveTime ? &ti : nullptr;
        pin.night = night;
        pin.sleepy = sleepy;
        pin.sunny = scn.day && scn.weatherKnown && scn.code == 0 && scn.arc >= 0.25f && scn.arc <= 0.75f;
        pin.sunrise = ambientSunriseFlash(now) > 0.001f;
        pin.offline = offline;
        pin.wifiDownMs = wifiDownSinceMs ? (uint32_t)(now - wifiDownSinceMs) : 0;
        pin.reaction = reaction;
        pin.weatherKnown = scn.weatherKnown;
        pin.weatherCode = scn.code;
        pin.alarmState = alarm_get_state();
        pin.alarmSecsLeft = haveTime ? alarmSecondsLeft(ti) : -1;
        pin.celestX = pin.celestY = 0;
        pin.celestOk = celestialGaze(scn, pin.celestX, pin.celestY);
        personaUpdate(pin);
    }

    updateLook(now, alarm, sleepy, reaction);
    updateBlink(now, sleepy);
    float exprP = updateExpression(now, !alarm && !happy && reaction != Reaction::WORRIED && persona.exprOk, sleepy);

    // Diagnostics page: full screen, replaces face + info panel. An alarm
    // always wins (it falls through to the normal clock/face drawing below).
    if (mode == DisplayMode::DIAG && !alarm)
    {
        lastMode = DisplayMode::DIAG; // so the page change is noticed on the way out
        if (gapIsFull)
            gapPaint(0); // coming from a full-screen page: the strip between the cards is frame colour again
        if (!diagShown || now - lastDiagDrawMs >= 500)
        {
            lastDiagDrawMs = now;
            diagShown = true;
            drawDiagScreen(now);
        }
        return;
    }
    diagShown = false;

    // Page flow: the slide between pages, and which layout is up
    DisplayMode shown = alarm ? DisplayMode::CLOCK : mode;
    pageFlowUpdate(shown, alarm, now);

    // Date / Weather: one full-screen page, no eyes (the face is only drawn on the clock page, below)
    if (fullLayout)
    {
        drawFullPage(shown, haveTime, ti, now);
        return;
    }

    // Clock page: face card above the info card
    if (pageSliding() || gapIsFull)
        gapPaint(pageSlideX); // coming back from a full-screen page: the strip between the cards turns into frame again

    TFT_eSPI &g = faceCv.begin();
    drawFace(g, now, alarm, happy, purr, sleepy, scn, reaction, exprP);
    if (persona.stage == PStage::RUN) // not over the black boot screen
    {
        drawStatusIcons(g, now);
        drawHoldRing(g);
    }
    faceCv.endAt(pageSlideX); // slides in together with the info card when the clock page comes back

    drawInfoPanel(alarm, haveTime, ti, now);
}