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
#define GMT_OFFSET_SEC (3 * 3600 + 1800) // Iran Standard Time, UTC+3:30
#define DST_OFFSET_SEC 0
#define NTP_RESYNC_INTERVAL_MS (6UL * 60 * 60 * 1000) // resync every 6h

// ---- Button debounce (mechanical microswitch contact bounce) ----
#define TOUCH_DEBOUNCE_MS 50

// ---- Servo angles ----
#define SERVO_REST_ANGLE 90
#define SERVO_PET_ANGLE 130
#define SERVO_ALERT_ANGLE 60

// ---- Weather (Open-Meteo — free, no API key) ----
// Set to your city's coordinates: https://open-meteo.com/en/docs
#define WEATHER_LATITUDE "35.6892" // default: Tehran — change to your city
#define WEATHER_LONGITUDE "51.3890"
#define WEATHER_REFRESH_INTERVAL_MS (15UL * 60 * 1000) // refresh every 15 min

// ---- Audio (JQ6500-16P onboard flash, UART2 @ 9600) ----
#define AUDIO_BAUD 9600
#define AUDIO_VOLUME 30 // 0-30 — start low; raise once power draw is confirmed OK

// File index = upload order into the module's onboard flash, starting at 1.
// The module's K1..K5 pins play files 1..5, so the sound wired to K3 is file 3.
// If a clip plays one off from what you expect, change the index below.
#define SOUND_INDEX_PET 3   // pet/touch reaction = the K3 sound
#define SOUND_INDEX_ALARM 2 // alarm tone — played on repeat while the alarm rings
                            // Not used yet - assign a file number here when you add it:
                            // #define SOUND_INDEX_CLICK 1  // mode-switch click

// ---- Alarm ----
#define ALARM_ENABLED 1      // 0 = never ring
#define ALARM_HOUR 7         // 0-23, local time (see GMT_OFFSET_SEC)
#define ALARM_MINUTE 30      // 0-59
#define ALARM_SNOOZE_MIN 5   // alarm button while ringing = silence for this long, then ring again
#define ALARM_AUTO_OFF_MIN 3 // nobody pressed anything: stop by itself after this long
                             // (no more rings until the next day's alarm time)