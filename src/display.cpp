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

void display_init()
{
    // Backlight (see the Backlight section): dims at night, see display_tick.
    backlightInit();

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(C_BG);

    W = tft.width();
    H = tft.height();
    sc = W / 128.0f;
    faceH = (int16_t)(H * 0.55f);
    infoH = H - faceH;

    bool f = faceCv.init(0, W, faceH);
    bool i = infoCv.init(faceH, W, infoH);

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
        portalScreenShown = false;
        lastInfoSig = 0xFFFFFFFFu;
    }

    struct tm ti;
    bool haveTime = timeSynced && getLocalTime(&ti, 0);
    themeUpdate(haveTime, ti, now); // background / accent follow the time of day

    bool alarm = (mood == Mood::ALARM_ALERT);
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
        petEv = pe;
        happyStartMs = now;
        happyUntilMs = now + pet_hold_ms(pe.kind);
    }
    bool purr = !alarm && mood == Mood::PET_PURR; // button held: purring takes over from the tap reaction
    bool happy = !alarm && (purr || now < happyUntilMs);

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
    {
        bool wv = false;
        float wt = 0;
        int wc = 0;
        uint32_t wMs = 0;
        readWeather(wv, wt, wc, wMs);
        scn.weatherKnown = wv && ageSince(now, wMs) <= SCENE_WEATHER_MAX_AGE_MS;
        scn.code = wc;
        scn.tempC = wt;
    }
    bool offline = wifiDownSinceMs != 0 && (now - wifiDownSinceMs) >= REACT_OFFLINE_DELAY_MS;
    Reaction reaction = alarm ? Reaction::NONE : pickReaction(scn, offline, sleepy);

    updateLook(now, alarm, sleepy, reaction);
    float blinkOpen = updateBlink(now);
    float exprP = updateExpression(now, !alarm && !happy && reaction != Reaction::WORRIED, sleepy);

    // Diagnostics page: full screen, replaces face + info panel. An alarm
    // always wins (it falls through to the normal clock/face drawing below).
    if (mode == DisplayMode::DIAG && !alarm)
    {
        lastMode = DisplayMode::DIAG; // so the page change is noticed on the way out
        if (!diagShown || now - lastDiagDrawMs >= 500)
        {
            lastDiagDrawMs = now;
            diagShown = true;
            drawDiagScreen(now);
        }
        return;
    }
    diagShown = false;

    TFT_eSPI &g = faceCv.begin();
    drawFace(g, now, alarm, happy, purr, sleepy, blinkOpen, scn, reaction, exprP);
    drawStatusIcons(g, now);
    drawHoldRing(g);
    faceCv.end();

    drawInfoPanel(mode, alarm, haveTime, ti, now);
}
