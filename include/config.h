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
#define AUDIO_VOLUME 12 // 0-30 — start low; raise once power draw is confirmed OK

// File index = upload order into the module's onboard flash. Untested
// whether this module's indexing starts at 0 or 1 — if clips play one off
// from what you expect, change AUDIO_INDEX_BASE to 1.
#define AUDIO_INDEX_BASE 0
#define SOUND_INDEX_PET (AUDIO_INDEX_BASE + 0)   // uploaded 1st: pet/touch reaction
#define SOUND_INDEX_CLICK (AUDIO_INDEX_BASE + 1) // uploaded 2nd: mode-switch click
#define SOUND_INDEX_ALARM (AUDIO_INDEX_BASE + 2) // uploaded 3rd: alarm loop