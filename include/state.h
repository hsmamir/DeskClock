#pragma once
#include <Arduino.h>

// Character mood — drives eye animation on the TFT task
enum class Mood
{
    IDLE_BLINK, // default resting animation
    PET_HAPPY,  // touch pad touched — happy eyes + servo wiggle
    PET_PURR,   // touch pad held — purring (closed eyes, gentle servo tremble)
    ALARM_ALERT // alarm firing — urgent animation + servo pattern
};

// What's currently shown on screen — changed by turning the rotary knob
enum class DisplayMode
{
    CLOCK,
    DATE,
    WEATHER,
    DIAG // full-screen diagnostics page: IP, signal, free heap, last sync
};

// Shared app state, written by touch/time tasks, read by display/servo tasks.
// Kept intentionally small and flat; guard with the mutex below when a task
// does more than a single atomic read/write.
struct AppState
{
    Mood mood = Mood::IDLE_BLINK;
    DisplayMode displayMode = DisplayMode::CLOCK;
    bool alarmActive = false;
    bool timeSynced = false;          // clock holds a usable time (NTP, or restored from flash)
    bool timeApprox = false;          // ...but it came from flash, not NTP yet (may be behind)
    bool wifiPortalActive = false;    // WiFiManager setup portal is open (real hardware)
    volatile uint8_t modeHoldPct = 0; // Knob button held toward the diagnostics page: 0 = not shown, 1-100 = ring progress
    int8_t otaPercent = -1;           // OTA update progress 0-100; -1 = no update running
    uint8_t tlsUsers = 0;             // tasks holding or waiting for a TLS connection (tls_lock.h); while > 0 the display frees its sprites

    // Weather cache, refreshed periodically by weather_tick()
    bool weatherValid = false;
    float weatherTempC = 0.0f;
    int weatherCode = 0;           // Open-Meteo WMO weather code
    uint32_t weatherUpdatedMs = 0; // millis() of the last successful fetch (age = millis() - this)

    // Extra weather details, from the same request as the reading above
    // (weather_tick()). Valid together with weatherValid; detailValid is false if the
    // response lacked them (the basic reading is still shown then).
    static const int WEATHER_MAX_BARS = 8;
    bool detailValid = false;
    float tMaxC = 0.0f; // today's high / low
    float tMinC = 0.0f;
    int humidityPct = -1;                     // relative humidity, 0-100; -1 = unknown
    float windKmh = -1.0f;                    // wind speed at 10 m; < 0 = unknown
    int windDirDeg = -1;                      // direction the wind comes FROM, 0-359; -1 = unknown
    float apparentC = NAN;                    // "feels like" temperature; NAN = unknown
    int pressureHpa = -1;                     // sea-level pressure in hPa; -1 = unknown
    float uvMax = -1.0f;                      // today's highest UV index; < 0 = unknown
    uint8_t barCount = 0;                     // hourly forecast entries below (starting with the current hour)
    uint8_t barHour[WEATHER_MAX_BARS] = {};   // local hour of the entry, 0-23
    int8_t barTempC[WEATHER_MAX_BARS] = {};   // temperature, rounded
    int8_t barRainPct[WEATHER_MAX_BARS] = {}; // chance of rain, 0-100; -1 = unknown

    // Last successful NTP sync (diagnostics page). 0 = no NTP sync since boot.
    uint32_t ntpSyncEpoch = 0; // wall-clock time of that sync
    uint32_t ntpSyncMs = 0;    // millis() of that sync (age = millis() - this)
};

extern AppState g_state;
extern SemaphoreHandle_t g_stateMutex;