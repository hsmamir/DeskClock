#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>
#include "display.h"
#include "state.h"
#include "pins.h"
#include "weather.h"

static TFT_eSPI tft = TFT_eSPI();

// ---- Layout — computed from tft.width()/height() so this works on both
// the real 128x160 ST7735 and the 240x320 ILI9341 used for Wokwi ----
static int16_t screenW, screenH;
static int16_t eyeY, eyeRadius, leftEyeX, rightEyeX;

// ---- Idle blink state machine ----
enum class BlinkPhase
{
    OPEN,
    CLOSING,
    CLOSED,
    OPENING
};
static BlinkPhase blinkPhase = BlinkPhase::OPEN;
static uint32_t blinkPhaseStartMs = 0;
static uint32_t nextBlinkAtMs = 0;
static const uint16_t BLINK_TRANSITION_MS = 80;
static const uint16_t BLINK_CLOSED_MS = 100;

// ---- Alarm flash state ----
static bool alarmFlashOn = false;
static uint32_t lastAlarmFlashMs = 0;
static const uint16_t ALARM_FLASH_MS = 200;

// ---- Redraw de-dup (avoid flicker from redrawing every unchanged frame) ----
static Mood lastDrawnMood = Mood::IDLE_BLINK;
static float lastEyeOpenness = -1.0f;
static DisplayMode lastDrawnMode = static_cast<DisplayMode>(-1);
static char lastInfoText[32] = "";

static Mood readMood()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    Mood m = g_state.mood;
    xSemaphoreGive(g_stateMutex);
    return m;
}

static DisplayMode readDisplayMode()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    DisplayMode d = g_state.displayMode;
    xSemaphoreGive(g_stateMutex);
    return d;
}

static bool readTimeSynced()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool t = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);
    return t;
}

static void readWeather(bool &valid, float &tempC, int &code)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    valid = g_state.weatherValid;
    tempC = g_state.weatherTempC;
    code = g_state.weatherCode;
    xSemaphoreGive(g_stateMutex);
}

static void clearEyeArea()
{
    tft.fillRect(0, 0, screenW, eyeY + eyeRadius + 12, TFT_BLACK);
}

static void drawNormalEyes(float openness, uint16_t color)
{
    clearEyeArea();
    int16_t h = (int16_t)(eyeRadius * 2 * openness);
    if (h < 3)
        h = 3;
    for (int16_t cx : {leftEyeX, rightEyeX})
    {
        tft.fillRoundRect(cx - eyeRadius, eyeY - h / 2, eyeRadius * 2, h, 3, color);
    }
}

static void drawHappyEyes(uint16_t color)
{
    clearEyeArea();
    for (int16_t cx : {leftEyeX, rightEyeX})
    {
        // upward "^" shape, drawn as a few offset lines to fake thickness
        for (int8_t off = -1; off <= 1; off++)
        {
            tft.drawLine(cx - eyeRadius, eyeY + off, cx, eyeY - eyeRadius + off, color);
            tft.drawLine(cx, eyeY - eyeRadius + off, cx + eyeRadius, eyeY + off, color);
        }
    }
}

static void drawInfoText(DisplayMode mode, bool timeSynced)
{
    char buf[32];
    struct tm timeinfo;
    bool haveTime = timeSynced && getLocalTime(&timeinfo, 0);

    switch (mode)
    {
    case DisplayMode::CLOCK:
        if (haveTime)
            strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        else
            strcpy(buf, "--:--");
        break;
    case DisplayMode::DATE:
        if (haveTime)
            strftime(buf, sizeof(buf), "%d %b", &timeinfo);
        else
            strcpy(buf, "----");
        break;
    case DisplayMode::WEATHER:
    {
        bool valid;
        float tempC;
        int code;
        readWeather(valid, tempC, code);
        if (valid)
            snprintf(buf, sizeof(buf), "%.0fC %s", tempC, weather_code_to_label(code));
        else
            strcpy(buf, "N/A");
        break;
    }
    default:
        strcpy(buf, "N/A");
        break;
    }

    if (mode == lastDrawnMode && strcmp(buf, lastInfoText) == 0)
        return;

    int16_t textY = eyeY + eyeRadius + 34;
    tft.fillRect(0, textY - 18, screenW, 36, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, screenW / 2, textY, 4);

    lastDrawnMode = mode;
    strncpy(lastInfoText, buf, sizeof(lastInfoText));
}

void display_init()
{
    // NOTE: PWM dimming via LEDC removed for now — the ledc API differs
    // between arduino-esp32 core versions (ledcAttach vs ledcSetup+
    // ledcAttachPin) and isn't worth pinning down until the core version
    // is fixed. Backlight is just full-on here.
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    screenW = tft.width();
    screenH = tft.height();
    eyeY = screenH / 3;
    eyeRadius = screenW / 8;
    leftEyeX = screenW / 2 - eyeRadius * 2;
    rightEyeX = screenW / 2 + eyeRadius * 2;
}

void display_tick()
{
    uint32_t now = millis();
    Mood mood = readMood();
    DisplayMode mode = readDisplayMode();
    bool timeSynced = readTimeSynced();

    if (mood == Mood::ALARM_ALERT)
    {
        if (now - lastAlarmFlashMs > ALARM_FLASH_MS)
        {
            alarmFlashOn = !alarmFlashOn;
            lastAlarmFlashMs = now;
        }
        float openness = alarmFlashOn ? 1.0f : 0.15f;
        if (mood != lastDrawnMood || fabsf(openness - lastEyeOpenness) > 0.02f)
        {
            drawNormalEyes(openness, TFT_RED);
            lastDrawnMood = mood;
            lastEyeOpenness = openness;
        }
    }
    else if (mood == Mood::PET_HAPPY)
    {
        if (mood != lastDrawnMood)
        {
            drawHappyEyes(TFT_CYAN);
            lastDrawnMood = mood;
            lastEyeOpenness = -1.0f; // forces a redraw once mood changes away
        }
    }
    else
    {
        // ---- idle blink state machine ----
        float openness = 1.0f;
        switch (blinkPhase)
        {
        case BlinkPhase::OPEN:
            if (nextBlinkAtMs == 0)
                nextBlinkAtMs = now + random(3000, 6000);
            if (now >= nextBlinkAtMs)
            {
                blinkPhase = BlinkPhase::CLOSING;
                blinkPhaseStartMs = now;
            }
            break;
        case BlinkPhase::CLOSING:
        {
            float t = (now - blinkPhaseStartMs) / (float)BLINK_TRANSITION_MS;
            openness = 1.0f - (t < 1.0f ? t : 1.0f);
            if (t >= 1.0f)
            {
                blinkPhase = BlinkPhase::CLOSED;
                blinkPhaseStartMs = now;
            }
            break;
        }
        case BlinkPhase::CLOSED:
            openness = 0.05f;
            if (now - blinkPhaseStartMs > BLINK_CLOSED_MS)
            {
                blinkPhase = BlinkPhase::OPENING;
                blinkPhaseStartMs = now;
            }
            break;
        case BlinkPhase::OPENING:
        {
            float t = (now - blinkPhaseStartMs) / (float)BLINK_TRANSITION_MS;
            openness = (t < 1.0f ? t : 1.0f);
            if (t >= 1.0f)
            {
                blinkPhase = BlinkPhase::OPEN;
                nextBlinkAtMs = 0;
            }
            break;
        }
        }
        if (mood != lastDrawnMood || fabsf(openness - lastEyeOpenness) > 0.02f)
        {
            drawNormalEyes(openness, TFT_CYAN);
            lastDrawnMood = mood;
            lastEyeOpenness = openness;
        }
    }

    drawInfoText(mode, timeSynced);
}