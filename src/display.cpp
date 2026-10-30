// display.cpp - public display API: display_init() and display_tick(). The drawing code lives in display_*.cpp;
// shared declarations are in display_internal.h.
#include "display_internal.h"
#include <WiFi.h>
#include "tls_lock.h"
#include "display_menu.h"
#include "display_wx.h"
#include "weather_mood.h"
#include "display_landscape.h"
#include "ui_state.h"

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

// A TLS connection (the weather fetch, the update check and the update itself) wants ~60 KB of contiguous heap
// at once, the three off-screen sprites below hold ~40 KB permanently. They are only released when the heap is
// too small for TLS (tls_heap_ok() false -> tls_wants_memory(), see tls_lock.h) or an update runs, and created
// again afterwards
// (Canvas::begin() already falls back to drawing straight on the screen when a sprite is missing).
static bool spritesReleased = false;
static int16_t digSprW = 0, digSprH = 0;

// If a sprite could not be created (the heap was too fragmented right after a TLS connection), try again every
// SPRITE_RETRY_MS instead of giving up for good: without the sprite every frame is drawn straight on the panel
// and the screen flickers.
#ifndef SPRITE_RETRY_MS
#define SPRITE_RETRY_MS 1000
#endif
static uint32_t lastSpriteRetryMs = 0;
static bool clockWasShown = false; // landscape clock page: drawn at least once since the sprite / page changed
static uint32_t lastClockSig = 0;

static void releaseSprites()
{
    faceCv.spr.deleteSprite();
    faceCv.ok = false;
    if (infoCv.w > 0)
        infoCv.spr.deleteSprite();
    infoCv.ok = false;
    digSpr.deleteSprite();
    digSprOk = false;
    spritesReleased = true;
    Serial.printf("display: sprites released for TLS / the update (free heap %u, largest block %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

// true when a sprite that should exist is missing (a failed createSprite: the display then draws straight on the
// panel, which is what makes the screen flicker)
static bool spritesMissing()
{
    if (!faceCv.ok)
        return true;
    if (infoCv.w > 0 && !infoCv.ok)
        return true;
    return false;
}

// Creates the sprites that are missing (existing ones are left alone), so it is safe to call it repeatedly.
// Returns true when everything is there.
static bool ensureSprites(const char *why, bool logFail = true)
{
    if (!faceCv.ok)
    {
        faceCv.spr.setColorDepth(16);
        faceCv.ok = (faceCv.spr.createSprite(faceCv.w, faceCv.sh) != nullptr);
    }
    if (infoCv.w > 0 && !infoCv.ok) // (the landscape UI has no second card)
    {
        infoCv.spr.setColorDepth(16);
        infoCv.ok = (infoCv.spr.createSprite(infoCv.w, infoCv.sh) != nullptr);
    }
    if (CLOCK_DIGIT_ANIM && infoCv.ok && digSprW > 0 && !digSprOk)
    {
        digSpr.setColorDepth(16);
        digSprOk = (digSpr.createSprite(digSprW, digSprH) != nullptr);
    }
    spritesReleased = false;
    if (!logFail && spritesMissing())
        return false; // a failed retry stays quiet (it runs once a second)
    Serial.printf("display: sprites %s: face=%s info=%s digit=%s (free heap %u, largest block %u)\n", why,
                  faceCv.ok ? "ok" : "fallback", infoCv.ok ? "ok" : "fallback", digSprOk ? "ok" : "off",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return !spritesMissing();
}

static void restoreSprites()
{
    ensureSprites("restored");
}

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
    tft.setRotation(DISPLAY_ROTATION);

    // two rounded cards (face on top, info panel below) on a darker frame: sizes, corner table, frame fill
    // (landscape UI: ONE full-screen page, see cardsInit)
    cardsInit();

    bool f = faceCv.init(cardX, faceY, W, faceH, faceHMax);
#if LANDSCAPE_UI
    bool i = false; // no second card; the clock styles draw into the one big canvas
    infoCv.ok = false;
#else
    bool i = infoCv.init(cardX, infoY, W, infoH, infoHMax);
#endif

    // small sprite for the clock-digit slide animation (needs the info panel to be a sprite too)
    if (CLOCK_DIGIT_ANIM && i)
    {
        int16_t dw, dh;
        clockMetrics(dw, dh);
        digSprW = dw + 2 * DIG_PAD;
        digSprH = dh + 2 * DIG_PAD;
        digSpr.setColorDepth(16);
        digSprOk = (digSpr.createSprite(digSprW, digSprH) != nullptr);
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

    // Memory for a TLS connection: while one is open (or a task waits for its turn) the sprites are freed and the
    // normal screens are drawn straight on the display. The guard in tls_lock.cpp waits for this.
    const bool tlsActive = readTlsActive();

    // A firmware update owns the screen: full brightness, progress only.
    int otaPct = readOtaPercent();
    // Sprites are freed only when a TLS guard really needs the memory (tls_wants_memory) or an update runs;
    // otherwise the display keeps animating during the weather fetch.
    if ((otaPct >= 0 || (tlsActive && tls_wants_memory())) && !spritesReleased)
        releaseSprites();
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
        clockWasShown = false; // FIX: the landscape clock / Date page was wiped by fillScreen(): draw it again at once
    }
    if (spritesReleased && !tlsActive)
    {
        restoreSprites();
        lastInfoSig = 0xFFFFFFFFu; // the new sprites are empty: the info panel has to be drawn again
        clockWasShown = false;
        lastSpriteRetryMs = now;
    }
    else if (!spritesReleased && !tlsActive && spritesMissing() && (uint32_t)(now - lastSpriteRetryMs) >= SPRITE_RETRY_MS)
    {
        // an earlier createSprite failed: the heap is usually in one piece again a moment later
        lastSpriteRetryMs = now;
        {
            // diagnostics: a missing sprite = flicker; say so (at most every 5 s) together with the heap state
            static uint32_t lastMissLogMs = 0;
            if ((uint32_t)(now - lastMissLogMs) >= 5000)
            {
                lastMissLogMs = now;
                Serial.printf("display: SPRITE MISSING (face=%d, need %u B in one block) free heap %u, largest block %u -> drawing straight on the panel = flicker\n",
                              (int)faceCv.ok, (unsigned)faceCv.w * faceCv.sh * 2, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
            }
        }
        if (ensureSprites("retry", false))
        {
            lastInfoSig = 0xFFFFFFFFu;
            clockWasShown = false;
        }
        else if (faceCv.ok)
        {
            lastInfoSig = 0xFFFFFFFFu; // at least the big one is back
            clockWasShown = false;
        }
    }

    // No sprites while a TLS connection is open: drawing the face straight on the panel is what made the screen
    // flicker at every weather fetch / update check. The panel keeps showing the last finished frame, so simply
    // hold it (a second or two) and carry on when the sprites are back. An alarm and the setup portal still draw.
    if (spritesReleased && tlsActive && mood != Mood::ALARM_ALERT && !readPortalActive())
        return;

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
        clockWasShown = false; // FIX: same as after a failed update
    }

    bool alarm = (mood == Mood::ALARM_ALERT);
    // Only the clock page shows the face (the eyes). Date and Weather are full-screen pages; an alarm always
    // brings the face and the clock back, whatever page was open. (The diagnostics page draws its own two cards.)
    fullLayout = !LANDSCAPE_UI && !alarm && isFullPage(mode);
    // The Date / Weather / diagnostics / reminder pages are ONE page: no strip between the two cards (seamless).
    ReminderView rvNow;
    const bool reminderOn = !alarm && reminder_get_active(rvNow);
    const bool seamless = reminderOn || fullLayout || (!alarm && (mode == DisplayMode::DIAG || mode == DisplayMode::MENU));
    geometrySet(fullLayout && mode == DisplayMode::DATE && !reminderOn, seamless); // the Date page has its own card split

    struct tm ti;
    bool haveTime = timeSynced && getLocalTime(&ti, 0);
    themeUpdate(haveTime, ti, now); // background / accent follow the time of day
#if !LANDSCAPE_UI
    frameUpdate(); // the frame around the cards follows the card background
#endif

    bool night = haveTime && sun_is_night(ti); // backlight dimming, muted sound: still follows the sun
    // The eyes follow their own fixed schedule: normal until 21:00, drowsy 21-23, asleep 23 -> morning.
    const EyePhase eyePhase = haveTime ? eyes_phase(ti) : EyePhase::AWAKE;
    bool sleepy = eyePhase != EyePhase::AWAKE && !alarm;

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
    Reaction reaction = alarm ? Reaction::NONE : pickReaction(scn, offline, sleepy, now);
    const PetBond bond = pet_bond();
    emoUpdate(now, emoPick(alarm, happy, purr, sleepy, reaction, bond)); // eye colour follows the feeling (and the bond)

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
        pin.drowsyWin = eyePhase == EyePhase::DROWSY;
        pin.sleepWin = eyePhase == EyePhase::SLEEP;
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
        pin.storm = scn.weatherKnown && wxIsStorm(scn.code);
        pin.flash = lightningActive(now);
        pin.pollenHigh = false;
        pin.brightUv = false;
#if WX_ACCESSORIES
        if (scn.weatherKnown)
        {
            WeatherExtra wx;
            if (weather_extra_get(wx))
            {
                // the same answers as the face accessories (hysteresis inside), so the sneezes, the squint and the
                // sunglasses / mask never disagree with each other
                pin.pollenHigh = wx.pollenLevel >= WX_POLLEN_SNEEZE_LEVEL;
                const bool fcFresh = wx.forecastValid && (uint32_t)(now - wx.forecastMs) <= (uint32_t)WEATHER_STALE_MS;
                pin.brightUv = fcFresh && scn.day && wxIsBright(scn.code) && wxIsHighUv(wx.uvNow);
            }
        }
#endif
        personaUpdate(pin);
    }
    // FIX: on the landscape UI the home view can be the clock page. The face is not drawn there, so a special minute
    // (easter egg) must not start (it would play invisibly and be marked "done for today").
#if LANDSCAPE_UI
    const bool faceIsHome = !ui_home_is_clock();
#else
    const bool faceIsHome = true;
#endif
    momentUpdate(haveTime ? &ti : nullptr, mode == DisplayMode::CLOCK && !fullLayout && faceIsHome, alarm, happy, now); // special minutes (easter eggs)

    occasionUpdate(haveTime ? &ti : nullptr, now);

    eyesSetWindDir(windBlowX(scn)); // the WIND reaction looks the way the gust blows
    updateLook(now, alarm, sleepy, reaction);
    updateBlink(now, sleepy);
    float exprP = updateExpression(now, !alarm && !happy && reaction != Reaction::WORRIED && persona.exprOk, sleepy);

#if LANDSCAPE_UI
    // ---- Landscape pages: reminder / diagnostics / quick menu, each one full screen. An alarm always wins. ----
    {
        ReminderView rv;
        if (!alarm && reminder_get_active(rv))
        {
            if (REMINDER_FULL_BRIGHT)
                backlightUpdate(false, true);
            diagShown = false;
            creditsStop();         // FIX: a reminder ends the credits
            clockWasShown = false; // FIX: the reminder replaced the page on screen: redraw the page below when it closes
            menuScreenLeft();
            drawReminderScreenL(rv, now);
            return;
        }
        if (mode == DisplayMode::DIAG && !alarm)
        {
            clockWasShown = false; // FIX: otherwise the Analog / Minimal clock and the Date page stay frozen after leaving
            menuScreenLeft();
            if (drawCreditsScreen(now)) // FIX: the hidden credits (easter egg) replace the diagnostics page while they run
            {
                diagShown = false; // the diagnostics page is drawn again at once when they end
                return;
            }
            if (!diagShown || now - lastDiagDrawMs >= 500)
            {
                lastDiagDrawMs = now;
                diagShown = true;
                drawDiagScreenL(now);
            }
            return;
        }
        diagShown = false;
        creditsStop(); // FIX: not on the diagnostics page any more: the credits end
        if (mode == DisplayMode::MENU && !alarm)
        {
            clockWasShown = false; // FIX: same as the diagnostics page
            drawMenuScreenL(now);
            return;
        }
        menuScreenLeft();

        // Date / Weather: the side pages of the knob cycle Face -> Clock -> Date -> Weather -> Face (touch.cpp).
        // An alarm always brings the face back; the boot wake-up is not interrupted.
        if (!alarm && (mode == DisplayMode::DATE || mode == DisplayMode::WEATHER) && persona.stage == PStage::RUN)
        {
            // FIX: the "hold for diagnostics" ring (touch.cpp) was only drawn on the face; it is part of the signature now
            const uint32_t sig = sidePageSig(mode, haveTime, ti, now) ^ ((uint32_t)g_state.modeHoldPct * 0x9E3779B1u);
            if (!clockWasShown || sig != lastClockSig) // (clockWasShown / lastClockSig are shared with the clock page)
            {
                lastClockSig = sig;
                clockWasShown = true;
                TFT_eSPI &g = faceCv.begin();
                drawSidePage(g, mode, haveTime, ti, scn.day, now);
                drawHoldRing(g); // FIX
                faceCv.endAt(0);
            }
            return;
        }

        // Home: the face (eyes) or the clock, as the knob left it. The boot wake-up and the alarm always show the face.
        if (ui_home_is_clock() && !alarm && persona.stage == PStage::RUN)
        {
            const uint32_t sig = clockScreenSig(haveTime, ti, now) ^ ((uint32_t)g_state.modeHoldPct * 0x9E3779B1u); // FIX: ring
            if (!clockWasShown || sig != lastClockSig)
            {
                lastClockSig = sig;
                clockWasShown = true;
                TFT_eSPI &g = faceCv.begin();
                drawClockScreen(g, haveTime, ti, now);
                drawHoldRing(g); // FIX
                faceCv.endAt(0);
            }
            return;
        }
        clockWasShown = false;
    }
#endif

    // A reminder that is due: full screen, replaces whatever page is open (also the diagnostics page).
    // An alarm always wins (the reminder comes back after it). Drawn like the diagnostics page, in the two
    // cards, so leaving it simply falls back to the normal drawing below.
    {
        ReminderView rv;
        if (!alarm && reminder_get_active(rv))
        {
            fullLayout = false;
            geometrySet(false, true); // the clock split (one piece), whatever page was open
            if (REMINDER_FULL_BRIGHT)
                backlightUpdate(false, true);
            lastMode = DisplayMode::DIAG; // so the page change back is noticed (and does not slide)
            diagShown = false;
            creditsStop();
            // (the strip between the cards is painted by drawReminderScreen: one seamless page)
            drawReminderScreen(rv, now);
            return;
        }
    }

    // Diagnostics page: full screen, replaces face + info panel. An alarm
    // always wins (it falls through to the normal clock/face drawing below).
    if (mode == DisplayMode::DIAG && !alarm)
    {
        lastMode = DisplayMode::DIAG; // so the page change is noticed on the way out
        if (drawCreditsScreen(now))   // the hidden credits (easter egg) replace the page while they run
        {
            diagShown = false; // the diagnostics page is drawn again at once when they end
            return;
        }
        // drawDiagScreen() paints the strip between the cards as page background and sets gapIsFull; if something
        // repainted it as frame colour meanwhile (gapIsFull false) the page is redrawn at once
        if (!diagShown || !gapIsFull || now - lastDiagDrawMs >= 500)
        {
            lastDiagDrawMs = now;
            diagShown = true;
            drawDiagScreen(now);
        }
        return;
    }
    diagShown = false;
    creditsStop();

    // Quick menu (rotary knob): full screen like the diagnostics page, one seamless page. An alarm always wins
    // (the touch task also closes the menu when the alarm starts). Redraws itself only when something changed.
    if (mode == DisplayMode::MENU && !alarm)
    {
        lastMode = DisplayMode::DIAG; // so the page change back to the clock is noticed (and does not slide)
        drawMenuScreen(now);
        return;
    }
    menuScreenLeft(); // not shown: it is drawn from scratch the next time it opens

    // Page flow: the slide between pages, and which layout is up
    DisplayMode shown = alarm ? DisplayMode::CLOCK : mode;
#if !LANDSCAPE_UI
    pageFlowUpdate(shown, alarm, now);
#endif

    // Date / Weather: one full-screen page, no eyes (the face is only drawn on the clock page, below)
    if (fullLayout)
    {
        drawFullPage(shown, haveTime, ti, now);
        return;
    }

    // Clock page: face card above the info card
#if !LANDSCAPE_UI
    if (pageSliding() || gapIsFull)
        gapPaint(pageSlideX); // coming back from a full-screen page: the strip between the cards turns into frame again
#endif

    TFT_eSPI &g = faceCv.begin();
    uint16_t eyeTopSaved = C_EYE_TOP, eyeBotSaved = C_EYE_BOT; // emoUpdate() recomputes them every tick
    if (!alarm)
        occasionTintEyes();
    drawFace(g, now, alarm, happy, purr, sleepy, scn, reaction, exprP);
    C_EYE_TOP = eyeTopSaved;
    C_EYE_BOT = eyeBotSaved;
    if (persona.stage == PStage::RUN) // not over the black boot screen
    {
        if (!alarm)
        {
            drawOccasionFront(g, now);
            drawMomentFront(g, now);
            // (umbrella and sunglasses removed on request; the face mask is switched off with WX_AIRQ_MASK_AQI=0)
        }
        drawStatusIcons(g, now);
        drawHoldRing(g);
#if LANDSCAPE_UI
        if (alarm)
            drawAlarmOverlay(g, haveTime, ti, alarmFlashOn); // the time under the angry eyes
#endif
    }
#if LANDSCAPE_UI
    faceCv.endAt(0);
#else
    faceCv.endAt(pageSlideX); // slides in together with the info card when the clock page comes back

    drawInfoPanel(alarm, haveTime, ti, now);
#endif
}