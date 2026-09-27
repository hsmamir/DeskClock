#pragma once
#include <Arduino.h>

// Character mood — drives eye animation on the TFT task
enum class Mood
{
    IDLE_BLINK, // default resting animation
    PET_HAPPY,  // pad 1 touched — happy eyes + servo wiggle
    ALARM_ALERT // alarm firing — urgent animation + servo pattern
};

// What's currently shown on screen — cycled by pad 2
enum class DisplayMode
{
    CLOCK,
    DATE,
    WEATHER
};

// Shared app state, written by touch/time tasks, read by display/servo tasks.
// Kept intentionally small and flat; guard with the mutex below when a task
// does more than a single atomic read/write.
struct AppState
{
    Mood mood = Mood::IDLE_BLINK;
    DisplayMode displayMode = DisplayMode::CLOCK;
    bool alarmActive = false;
    bool timeSynced = false;

    // Weather cache, refreshed periodically by weather_tick()
    bool weatherValid = false;
    float weatherTempC = 0.0f;
    int weatherCode = 0; // Open-Meteo WMO weather code
};

extern AppState g_state;
extern SemaphoreHandle_t g_stateMutex;