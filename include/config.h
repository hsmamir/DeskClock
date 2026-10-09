#pragma once

// ---- WiFi ----
// Real hardware: no SSID/password lives in code. On first boot (or whenever
// the saved network can't be reached) the clock opens a setup access point —
// join it from a phone, pick your network and enter its password.
// To forget the saved network later: hold the Mode button while powering on.
#define WIFI_PORTAL_AP_NAME "DeskClock"
#define WIFI_PORTAL_TIMEOUT_SEC 180

// Wokwi build only (USE_WOKWI_WIFI): a simulator has no phone to join the
// portal, so it connects straight to Wokwi's open simulated network.
#define WOKWI_WIFI_SSID "Wokwi-GUEST"
#define WOKWI_WIFI_PASSWORD ""
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ---- NTP ----
#define NTP_SERVER "pool.ntp.org"
#define NTP_RESYNC_INTERVAL_MS (6UL * 60 * 60 * 1000) // resync every 6h

// ---- Settings page (web_ui.cpp) ----
// Open http://deskclock.local/ from a phone on the same WiFi to change the
// alarm time, volume, city and timezone. The values below (alarm, volume,
// city/coordinates, timezone) are only the FACTORY DEFAULTS: once you save
// something from the page, the saved value in flash wins.
#define WEB_HOSTNAME "deskclock" // -> deskclock.local
#define WEB_UI_PASSWORD ""       // "" = open to anyone on your WiFi; otherwise HTTP
                                 // Basic auth with user "admin" and this password

// Default timezone: POSIX rule, so DST is handled automatically. Iran has had
// no DST since 2022, hence a fixed +3:30. Other examples:
//   Germany  "CET-1CEST,M3.5.0,M10.5.0/3"     US Eastern "EST5EDT,M3.2.0,M11.1.0"
#define DEFAULT_TZ_NAME "Asia-Tehran" // id used by the web page's dropdown
#define DEFAULT_TZ_POSIX "<+0330>-3:30"

// ---- Button debounce (mechanical microswitch contact bounce) ----
#define TOUCH_DEBOUNCE_MS 50

// ---- Pages: Mode button and automatic return to the clock ----
// Tap Mode: Clock -> Date -> Weather -> Clock. The diagnostics page is NOT in that
// cycle: HOLD the Mode button for MODE_LONG_PRESS_MS to open it (hold again to leave).
// With no button pressed for DISPLAY_RETURN_MS on the Date / Weather page, or
// DIAG_RETURN_MS on the diagnostics page, the display goes back to the clock.
// 0 = stay where you left it. (The weather page alternates its two views every
// WEATHER_PAGE_SWITCH_MS, so keep DISPLAY_RETURN_MS above 2x that to see both.)
#define MODE_LONG_PRESS_MS 1000
// While Mode is held, a small ring fills on the face and completes exactly when the
// diagnostics page opens. It appears after MODE_HOLD_RING_DELAY_MS so a normal tap
// never flashes it. 0 = no ring.
#define MODE_HOLD_RING 1
#define MODE_HOLD_RING_DELAY_MS 250
static_assert(MODE_HOLD_RING_DELAY_MS < MODE_LONG_PRESS_MS, "ring delay must be shorter than the long press");
#define DISPLAY_RETURN_MS 15000
#define DIAG_RETURN_MS 15000

// ---- Servo angles ----
#define SERVO_REST_ANGLE 90
#define SERVO_PET_ANGLE 130
#define SERVO_ALERT_ANGLE 60

// ---- Weather (Open-Meteo — free, no API key) ----
// Default location (changeable from the settings page): Tehran
#define WEATHER_CITY_NAME "Tehran"
#define WEATHER_LATITUDE "35.6892"
#define WEATHER_LONGITUDE "51.3890"
#define WEATHER_REFRESH_INTERVAL_MS (15UL * 60 * 1000) // refresh every 15 min
// A reading older than this is shown as "stale" (dimmed + age, e.g. "Clear 3h ago").
// Two missed refreshes: a normal schedule never gets there, an offline clock does.
// ---- Weather page details ----
// The same request that fetches the temperature also brings today's high / low,
// humidity, wind and the next hours' temperature and chance of rain. The weather
// page then alternates between two views: the big temperature (as before) and a
// "details" view with Hi / Lo, humidity, wind and small hourly bars.
// 0 = only the big temperature, as before.
#define WEATHER_DETAILS_ENABLED 1
#define WEATHER_PAGE_SWITCH_MS 6000   // each view stays this long (the page opens on the big temperature)
#define WEATHER_WIND_ARROW_DOWNWIND 1 // details view: wind arrow points where the wind blows TO (1, like weather maps) or where it comes FROM (0)
#define WEATHER_BARS 6                // hours shown as bars, starting with the current hour (2-8)
#define WEATHER_BAR_RAIN_PCT 50       // an hour with at least this chance of rain is marked blue
static_assert(WEATHER_BARS >= 2 && WEATHER_BARS <= 8, "WEATHER_BARS must be 2-8");

// ---- Weather page: smooth view change, growing bars, rolling temperature ----
// WEATHER_VIEW_TRANSITION: the details view slides in from the right over the big temperature
//   (and back), like a page change, instead of swapping instantly. Needs the info-panel sprite.
// WEATHER_BARS_GROW: when the details view has landed, the hourly bars grow up from the baseline,
//   one after the other (STAGGER = delay between neighbouring bars).
// WEATHER_TEMP_ROLL: when the big temperature differs from what was on screen the last time it was
//   drawn (a new reading, also one that arrived while another page was showing), the digits that
//   changed roll - upward when it got warmer, downward when colder. The first reading after boot
//   just appears. Needs the info-panel sprite.
// Set any of them to 0 to turn that effect off.
#define WEATHER_VIEW_TRANSITION 1
#define WEATHER_VIEW_TRANSITION_MS 300
#define WEATHER_BARS_GROW 1
#define WEATHER_BARS_GROW_MS 420
#define WEATHER_BARS_STAGGER_MS 60
#define WEATHER_TEMP_ROLL 1
#define WEATHER_TEMP_ROLL_MS 450

#define WEATHER_STALE_MS (2UL * WEATHER_REFRESH_INTERVAL_MS)

// ---- Audio (JQ6500-16P onboard flash, UART2 @ 9600) ----
#define AUDIO_BAUD 9600
#define AUDIO_VOLUME 30       // default volume of the pet/touch sound, 0-30 (changeable from the settings page)
#define AUDIO_ALARM_VOLUME 30 // default volume of the ALARM, 1-30 (changeable from the settings page).
                              // Separate from the pet volume. Minimum 1: an alarm can't be set to silent.
#define AUDIO_FILE_COUNT 5    // how many clips are on the module; the settings page offers 1..this as alarm sounds

// File index = upload order into the module's onboard flash, starting at 1.
// The module's K1..K5 pins play files 1..5, so the sound wired to K3 is file 3.
// If a clip plays one off from what you expect, change the index below.
#define SOUND_INDEX_PET 3   // pet/touch reaction = the K3 sound
#define SOUND_INDEX_ALARM 2 // default alarm tone (changeable from the settings page) — played on repeat while the alarm rings
                            // Not used yet - assign a file number here when you add it:
                            // #define SOUND_INDEX_CLICK 1  // mode-switch click

// ---- Alarm ----
// Default alarm time (changeable from the settings page), local time of the
// configured timezone.
#define ALARM_ENABLED 1      // 0 = never ring
#define ALARM_HOUR 7         // 0-23
#define ALARM_MINUTE 30      // 0-59
#define ALARM_DAYS_MASK 0x7F // default days it rings (changeable from the settings page):
                             // bit0 = Sunday, bit1 = Monday ... bit6 = Saturday; 0x7F = every day
#define ALARM_SNOOZE_MIN 5   // alarm button while ringing = silence for this long, then ring again
#define ALARM_AUTO_OFF_MIN 3 // nobody pressed anything: stop by itself after this long
                             // (no more rings until the next day's alarm time)
#define ALARM_GRACE_MIN 10   // missed-alarm catch-up: if the clock was busy (test ring, snooze),
                             // rebooting or not yet synced at the alarm minute, it still rings
                             // when this is up to this many minutes late - once per day, and
                             // never after the alarm already rang. 0 = only in the exact minute

// How to switch the alarm OFF completely (not just snooze it):
//   * hold the alarm button for ALARM_LONG_PRESS_MS (works while it rings AND while it
//     is snoozed - a short tap snoozes at once, holding on turns it off), or
//   * use up all the snoozes: after ALARM_MAX_SNOOZES snoozes the next tap ends the alarm.
// The next day's alarm is not affected either way.
#define ALARM_LONG_PRESS_MS 1000 // hold time for "off"
#define ALARM_MAX_SNOOZES 3      // 0 = unlimited snoozes

// Bell icon + alarm time at the bottom-left of the clock page, shown while the
// alarm is switched on (red and flashing while it rings). 0 = no indicator.
#define ALARM_ICON_ENABLED 1

// ---- Face background scene ----
// The area behind the eyes follows the weather and the time of day: sun / moon,
// drifting clouds, rain, snow, fog, lightning, stars. Everything is drawn dim so
// the eyes stay the focus. 0 = back to the plain twinkling stars.
#define SCENE_ENABLED 1
// Sky gradient behind the eyes: darker at the top, a soft glow of the accent colour toward the
// bottom edge (the horizon). The glow is strongest at dawn / dusk and weaker under clouds, rain
// and fog. 0 = flat background as before.
#define SCENE_SKY_GRADIENT 1
#define SCENE_SKY_TOP_DARKEN 0.60f // 0 = the top has the background colour, 1 = black
#define SCENE_SKY_GLOW_SCALE 1.0f  // multiplies the horizon glow (0 = only the darker top)
// The sun (by day) and the moon (by night) travel across the sky: they rise on the left, climb to
// the top in the middle of the day / night and set on the right, following the real sunrise and
// sunset. Near the horizon the sun is redder and both are dimmer. 0 = fixed in the top-left corner.
#define SCENE_SUN_ARC 1
// Day / night follow the REAL sunrise and sunset of the city set in the web page
// (see "Real sun" below). SCENE_DAY_START/END_HOUR are only the fallback when
// SUN_ENABLED is 0 or the sun times can't be computed.
#define SCENE_DAY_START_HOUR 6 // sun from this hour (inclusive) ...
#define SCENE_DAY_END_HOUR 19  // ... until this hour (exclusive), then moon/stars
// A weather reading older than this is no longer trusted for the scene (the
// background then follows only the time of day).
#define SCENE_WEATHER_MAX_AGE_MS (3UL * 60 * 60 * 1000)

// ---- Real wind on the scene ----
// The background uses the wind speed and direction of the weather reading (the same ones the
// weather page shows): clouds and fog drift with the wind - faster when it is stronger, left or
// right depending on where it blows to - and rain / snow fall at a slant.
// The scene is a side view with the screen's right edge = east, so a west wind blows the clouds to
// the right, an east wind to the left. A pure north / south wind has no sideways part: clouds then
// only crawl (and rain falls straight). Changes of the wind are blended over SCENE_WIND_SMOOTH_MS.
// Without a (recent) reading, or without a direction, clouds drift gently to the right as before.
// 0 = no wind effect: gentle drift to the right, rain and snow fall straight.
#define SCENE_WIND_ENABLED 1
#define SCENE_WIND_MAX_KMH 60.0f          // at / above this the wind looks as strong as it gets
#define SCENE_WIND_CLOUD_CALM_PXS 4.0f    // cloud speed in calm air, px/s on the 128 px display
#define SCENE_WIND_CLOUD_PXS_PER_KMH 0.7f // ... plus this much per km/h (up to SCENE_WIND_MAX_KMH)
#define SCENE_WIND_RAIN_MAX_SLANT 1.0f    // rain at maximum wind: sideways px per px fallen (1.0 = 45 deg)
#define SCENE_WIND_SNOW_MAX_SLANT 2.0f    // snow is lighter, so it is blown further sideways
#define SCENE_WIND_SMOOTH_MS 3000         // a change of wind is blended in over about this long
#define SCENE_WIND_FALLBACK_KMH 10.0f     // drift speed while no wind is known

// ---- Real sun (sunrise / sunset) ----
// Sunrise and sunset are computed on the chip from the saved latitude / longitude,
// today's date and the timezone (NOAA formulas, no internet). They drive:
//   - the face background (sun by day, moon + stars by night),
//   - the colour theme (dawn at sunrise, dusk at sunset, see THEME_*_H below),
//   - night mode (dim backlight, muted pet sound, sleepy eyes).
// 0 = back to the fixed hours in this file.
#ifndef SUN_ENABLED
#define SUN_ENABLED 1
#endif

// Moon phase: the night sky shows the real phase (new, crescent, quarter, gibbous,
// full), computed from the date, mirrored for the southern hemisphere. 0 = the
// old fixed crescent.
#define MOON_PHASE_ENABLED 1

// ---- Page transition ----
// Pressing the Mode button slides the new page in from the right over the old one
// instead of swapping instantly. Only between clock / date / weather (the
// diagnostics page switches instantly). 0 = instant swap.
#define TRANSITION_ENABLED 1
#define TRANSITION_MS 220

// ---- Clock digit effects ----
// DIGIT_ANIM: when a clock digit changes, the old one slides up and fades out while
//             the new one rises from below (only the digits that actually change).
// GLOW:       faint halo around every lit 7-segment digit.
// COLON_PULSE: the colon dots breathe softly instead of blinking on/off.
// Set any of them to 0 to turn that effect off.
#define CLOCK_DIGIT_ANIM 1
#define CLOCK_DIGIT_ANIM_MS 380
#define CLOCK_GLOW 1
#define CLOCK_COLON_PULSE 1
#define CLOCK_COLON_PERIOD_MS 1600 // one full "breath" of the colon

// ---- Eye expressions & reactions ----
// Idle eyes sometimes wink, yawn (mostly at night) or get startled, and they react
// to the situation: worried when WiFi has been gone for a while, sad in rain,
// startled by lightning, sweating when hot, shivering when cold.
// 0 = only the plain blinking/looking-around eyes.
#define EXPR_ENABLED 1
#define EXPR_MIN_GAP_MS 12000 // a random idle expression every MIN..MAX ms
#define EXPR_MAX_GAP_MS 30000
#define REACT_HOT_C 35               // at or above (deg C): sweating, heavy lids
#define REACT_COLD_C 0               // at or below (deg C): shivering
#define REACT_OFFLINE_DELAY_MS 20000 // WiFi down this long -> worried eyes

// ---- Pet button reactions (pet.cpp decides, display.cpp draws) ----
// The Pet button no longer always gives the same happy face. In priority order:
//   fast repeated taps ... hearts for the eyes, then dizzy, then annoyed
//   first tap of the morning ... yawn + happy; first tap after a long absence ... "missed you"
//   a rare random one ... wink with tongue / surprise + big heart
//   night ... sleepy contented eyes; rain ... one raised eyebrow, then a smile
//   hot / cold weather ... sweating / shivering happy face; anything else ... the classic happy face
//   holding the button ... purring (closed eyes, the servo trembles gently)
// Everything can be switched off from the settings page (that setting is the default below).
#define PET_REACTIONS_DEFAULT 1
#define PET_STREAK_GAP_MS 3000     // taps closer together than this count as one streak
#define PET_HEART_STREAK 4         // this many taps in a row -> heart eyes
#define PET_DIZZY_STREAK 12        // ... dizzy spiral eyes
#define PET_ANNOYED_STREAK 15      // ... "that's enough!" (until you pause)
#define PET_PURR_HOLD_MS 1000      // hold the button this long -> purring
#define PET_PURR_MAX_MS 8000       // purring stops by itself after this (keeps the servo/supply calm)
#define PET_PURR_SERVO_DEG 3       // trembling amplitude around the rest angle while purring
#define PET_MISSED_HOURS 6         // first tap after this many hours -> "missed you"
#define PET_MORNING_FROM_H 5       // first tap of the day between these local hours -> yawn + happy
#define PET_MORNING_TO_H 11        // (exclusive)
#define PET_RARE_PERCENT 5         // chance (%) of a rare reaction instead of the usual one
#define PET_SAVE_INTERVAL_MS 60000 // pet statistics are written to flash at most this often

// ---- Pet identity + inner life (pet.cpp: three meters, display_eyes.cpp: what they do to the eyes) ----
// Name:   Tika (تیکا)
// Traits: curious (boredom meter), early-to-bed (energy meter), sulky-but-soft (bond meter)
// The three meters are 0..100 and live in NVS next to the pet statistics.
#define PET_NAME_FA "تیکا"
// Energy: its target follows the day theme (night / dawn / day / dusk) and the real value follows
// the target slowly. Long tap streaks tire it out.
#define PET_ENERGY_NIGHT 20
#define PET_ENERGY_DAWN 40
#define PET_ENERGY_DAY 90
#define PET_ENERGY_DUSK 60
#define PET_ENERGY_TAU_S 2700    // time constant of the smooth follow (45 min)
#define PET_ENERGY_STREAK_MIN 12 // a streak this long ...
#define PET_ENERGY_STREAK_COST 5 // ... costs this much energy (once per streak)
#define PET_ENERGY_LOW 30        // below this: heavy lids, slow blinks, 3x yawns
// Bond (affection): taps raise it (anti-spam), absence lowers it.
#define PET_BOND_TAP_GAIN 1       // every tap
#define PET_BOND_FIRST_TAP_GAIN 3 // the first tap of every hour
#define PET_BOND_MAX_PER_HOUR 5   // at most this much from taps per hour
#define PET_BOND_ABSENT_FROM_H 6  // after this many hours without a tap ...
#define PET_BOND_ABSENT_STEP_H 2  // ... minus 1 for every further 2 hours ...
#define PET_BOND_FLOOR 10         // ... but never below this
#define PET_BOND_FOND 70          // above: faint smile + blush
#define PET_SULK_BOND_BELOW 20    // low bond + a long absence: the first tap is met with a sulk
#define PET_SULK_AWAY_H 24
#define PET_SULK_MS 800 // the sulk melts into a smile after this long
// Boredom / curiosity: +PET_BORED_PER_MIN per idle minute by day, any interaction resets it to 0.
#define PET_BORED_PER_MIN 0.5f
#define PET_BORED_HIGH 60   // from here more glances + sighs (full effect at +30)
#define PET_FIDGET_MIN_S 90 // very bored: a tiny servo noise every MIN..MAX seconds
#define PET_FIDGET_MAX_S 240
// Signature move: a little four-point star (✦) glints on the eye highlight about this often.
#define PET_GLINT_EVERY_MS 600000
#define PET_GLINT_FIRST_MS 60000 // the first one after boot
#define PET_GLINT_MS 900

// ---- Night mode ----
// Between NIGHT_START_HOUR (inclusive) and NIGHT_END_HOUR (exclusive), local
// time, the backlight is dimmed and the pet sound is muted. The alarm
// is NOT affected: it always rings at the saved alarm volume and full brightness.
// A window that crosses midnight works too (e.g. start 23, end 6).
// (The #ifndef guards let a build flag override these, e.g. the
// esp32dev-nighttest env in platformio.ini forces "always night" for testing.)
// NIGHT_FOLLOWS_SUN 1 (needs SUN_ENABLED): night = from sunset to sunrise, and
// NIGHT_START_HOUR / NIGHT_END_HOUR below are only the fallback. Shift the edges
// with the two offsets (hours, may be negative / fractional), e.g. start 2.0 =
// night mode begins 2 h after sunset; end -0.5 = ends 30 min before sunrise.
// 0 = always use the fixed NIGHT_START_HOUR..NIGHT_END_HOUR window.
#ifndef NIGHT_FOLLOWS_SUN
#define NIGHT_FOLLOWS_SUN 1
#endif
#define SUN_NIGHT_START_AFTER_SUNSET_H 0.0f
#define SUN_NIGHT_END_AFTER_SUNRISE_H 0.0f
#ifndef NIGHT_START_HOUR
#define NIGHT_START_HOUR 0 // 00:00
#endif
#ifndef NIGHT_END_HOUR
#define NIGHT_END_HOUR 6 // up to 05:59
#endif

// How the backlight is dimmed at night:
//   0 = NO PWM. The pin stays steady HIGH and only its drive strength is
//       lowered (BACKLIGHT_NIGHT_DRIVE_CAP). No switching edges, so no PWM noise
//       on the display. It only dims a module whose LED pin is fed straight from
//       the GPIO; on a module with its own backlight transistor the brightness
//       will NOT change -> use 1 there.
//   1 = PWM with a smooth fade. Dims any module, but the current pulses can
//       show up as random pixels on a direct-wired backlight.
#ifndef BACKLIGHT_NIGHT_PWM
#define BACKLIGHT_NIGHT_PWM 0
#endif

// Pin drive strength: 0 = ~5 mA, 1 = ~10 mA, 2 = ~20 mA (ESP32 default), 3 = ~40 mA.
// Night too dark? raise NIGHT_DRIVE_CAP (mode 0) or BACKLIGHT_NIGHT (mode 1).
#define BACKLIGHT_DAY_DRIVE_CAP 2
#ifndef BACKLIGHT_NIGHT_DRIVE_CAP
#define BACKLIGHT_NIGHT_DRIVE_CAP 0
#endif

// ---- PWM-mode only (BACKLIGHT_NIGHT_PWM 1) ----
#define BACKLIGHT_DAY 255 // 0-255 duty
#ifndef BACKLIGHT_NIGHT
#define BACKLIGHT_NIGHT 20 // 0-255 duty; raise it if the screen is too dark at night
#endif
#define BACKLIGHT_FADE_STEP 4 // duty change per display frame (~30 fps) -> smooth fade

#define BACKLIGHT_PWM_FREQ 5000 // Hz, well above visible flicker
#define BACKLIGHT_PWM_BITS 8
#define BACKLIGHT_PWM_CHANNEL 8 // only used on arduino-esp32 2.x (ledcSetup); 3.x picks its own.
                                // 8 = low-speed group, so it never shares a timer with the servo (50 Hz)

// Pet sound volume at night, 0-30. 0 = completely silent (no command is sent).
// Any other value is capped to the saved volume.
#define AUDIO_NIGHT_PET_VOLUME 0

static inline bool isNightHour(int hour)
{
    if (NIGHT_START_HOUR <= NIGHT_END_HOUR)
        return hour >= NIGHT_START_HOUR && hour < NIGHT_END_HOUR;
    return hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR; // crosses midnight
}

// ---- Time persistence (power loss / reset without WiFi) ----
// The last valid time is saved to flash this often. After a reset the clock
// restarts from that saved time (so the alarm still works offline) and is
// corrected by NTP as soon as WiFi is back. While running on the saved time
// the clock is BEHIND by (power-off duration + up to one save interval).
#define TIME_SAVE_INTERVAL_MS (10UL * 60 * 1000) // 10 min
#define TIME_MIN_VALID_EPOCH 1577836800UL        // 2020-01-01: anything older is "no time"

// ---- Task watchdog ----
// The display, touch, servo and alarm tasks each have to report in (every loop
// pass) within TASK_WDT_TIMEOUT_S seconds. If one of them hangs - a deadlocked
// mutex, a stuck SPI/UART call - the chip resets itself instead of sitting there
// with a dead alarm. Time and settings are in flash, so it comes back by itself
// (see the "Time persistence" block below). The WiFi-side tasks (time, weather,
// web, OTA) are deliberately NOT watched: connecting, the setup portal and an OTA
// transfer legitimately block for a long time. 0 = no watchdog.
// After a watchdog reset the Serial log starts with "Reset reason: TASK_WDT".
#define TASK_WDT_ENABLED 1
#define TASK_WDT_TIMEOUT_S 10
// Test only: 1 = the alarm task deliberately stops reporting in 20 s after boot, so
// you can see the reset happen. Use the esp32dev-wdttest env in platformio.ini.
#ifndef TASK_WDT_SELFTEST
#define TASK_WDT_SELFTEST 0
#endif

// ---- Servo ----
// After each movement the servo gets this long to reach its position, then it
// is detached (no holding current, no buzzing) until the next movement.
#define SERVO_SETTLE_MS 400

// ---- OTA (wireless firmware update, see ota.h / platformio.ini) ----
// Must equal the --auth=... value of env:esp32dev-ota in platformio.ini.
// CHANGE IT: "" would let anyone on your WiFi flash the clock.
#define OTA_PASSWORD "change-me-ota"

// ---- Time-of-day colour theme ----
// Background + accent colour (clock digits, accent lines, progress bars, eyes)
// follow the time of day and blend smoothly between four looks:
//   night  deep blue + cyan          (the original look)
//   dawn   warm rose + peach
//   day    bright sky blue + sunny yellow
//   dusk   ember + orange sunset
// (a short lilac twilight is blended in between night and dawn / dusk and night)
// With SUN_ENABLED the keys below are OFFSETS (hours) from today's real sunrise /
// sunset, so the dawn glow follows the sun through the seasons. The defaults give
// the old look at an equinox (sunrise ~06:00, sunset ~18:00). With SUN_ENABLED 0
// they are fixed local hours instead (the *_H_FIXED values).
// Before the clock knows the time the night look is used.
#define THEME_ENABLED 1
#define THEME_EYES 1 // 0 = keep the eyes cyan, theme only the rest
// offsets from sunrise (+) / sunset (+), in hours:
#define THEME_NIGHT_END_FROM_SUNRISE_H -1.5f // deep blue until here, then it starts warming up ...
#define THEME_DAWN_FROM_SUNRISE_H 0.5f       // ... full dawn glow ...
#define THEME_DAY_FROM_SUNRISE_H 3.0f        // ... bright day from here ...
#define THEME_DAY_TO_FROM_SUNSET_H -2.0f     // ... until here, then it turns ...
#define THEME_DUSK_FROM_SUNSET_H 0.5f        // ... orange at sunset ...
#define THEME_NIGHT_FROM_SUNSET_H 3.0f       // ... and fully deep blue again from here
// fixed local hours (0-24, increasing) used when SUN_ENABLED is 0 / sun unknown:
#define THEME_NIGHT_END_H 4.5f
#define THEME_DAWN_H 6.5f
#define THEME_DAY_FROM_H 9.0f
#define THEME_DAY_TO_H 16.0f
#define THEME_DUSK_H 18.5f
#define THEME_NIGHT_FROM_H 21.0f

// Preview: 1 = ignore the real time and replay a whole day in THEME_PREVIEW_DAY_MS
// (a colour time-lapse). Use the esp32dev-themetest env in platformio.ini.
#ifndef THEME_PREVIEW
#define THEME_PREVIEW 0
#endif
#define THEME_PREVIEW_DAY_MS 48000UL