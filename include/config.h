#pragma once

// ---- WiFi ----
// Wokwi's simulated network — open, no password. Change back to your
// real router's SSID/password before flashing actual hardware.
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
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