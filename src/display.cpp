#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>
#include "display.h"
#include "state.h"
#include "pins.h"
#include "config.h"
#include "weather.h"

static TFT_eSPI tft = TFT_eSPI();

// ---- Palette (RGB565) ----
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static constexpr uint16_t COL_AMBER = rgb(255, 200, 60);
static constexpr uint16_t COL_DIM = rgb(70, 70, 70);
static constexpr uint16_t COL_SOFT = rgb(150, 150, 150);
static constexpr uint16_t COL_CHEEK = rgb(255, 105, 150);

// ---- Layout — computed from tft.width()/height() so this works on both
// the real 128x160 ST7735 and the 240x320 ILI9341 used for Wokwi ----
static int16_t screenW, screenH;
static int16_t eyeY, eyeRadius, leftEyeX, rightEyeX;
static int16_t dividerY, textY, captionY, dotsY;

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
static char lastInfoText[48] = "";

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

static bool readPortalActive()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool p = g_state.wifiPortalActive;
    xSemaphoreGive(g_stateMutex);
    return p;
}

static bool portalScreenShown = false;

static void readWeather(bool &valid, float &tempC, int &code)
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    valid = g_state.weatherValid;
    tempC = g_state.weatherTempC;
    code = g_state.weatherCode;
    xSemaphoreGive(g_stateMutex);
}

// Clears only the two eye boxes (incl. cheeks) instead of the whole top
// area — much less SPI traffic, so blinking looks smoother.
static void clearEyes()
{
    for (int16_t cx : {leftEyeX, rightEyeX})
    {
        tft.fillRect(cx - eyeRadius - 2, eyeY - eyeRadius - 3,
                     eyeRadius * 2 + 4, eyeRadius * 2 + 8, TFT_BLACK);
    }
}

static void drawNormalEyes(float openness, uint16_t color, bool shine)
{
    clearEyes();
    int16_t h = (int16_t)(eyeRadius * 2 * openness);
    if (h < 3)
        h = 3;
    int16_t corner = (int16_t)(eyeRadius * 0.7f);
    if (corner > h / 2)
        corner = h / 2;
    int16_t shineR = eyeRadius / 5;
    if (shineR < 2)
        shineR = 2;
    for (int16_t cx : {leftEyeX, rightEyeX})
    {
        tft.fillRoundRect(cx - eyeRadius, eyeY - h / 2, eyeRadius * 2, h, corner, color);
        // little highlight makes the eyes look glossy — only when open enough
        if (shine && h > eyeRadius)
            tft.fillCircle(cx - eyeRadius / 3, eyeY - h / 4, shineR, TFT_WHITE);
    }
}

static void drawHappyEyes(uint16_t color)
{
    clearEyes();
    for (int16_t cx : {leftEyeX, rightEyeX})
    {
        // upward "^" shape, drawn as a few offset lines to fake thickness
        for (int8_t off = -1; off <= 1; off++)
        {
            tft.drawLine(cx - eyeRadius, eyeY + off, cx, eyeY - eyeRadius + off, color);
            tft.drawLine(cx, eyeY - eyeRadius + off, cx + eyeRadius, eyeY + off, color);
        }
        // blush cheeks
        int16_t ry = eyeRadius / 5;
        if (ry < 2)
            ry = 2;
        tft.fillEllipse(cx, eyeY + eyeRadius / 2 + 5, eyeRadius / 2, ry, COL_CHEEK);
    }
}

static uint16_t tempColor(float tempC)
{
    if (tempC < 5)
        return rgb(100, 170, 255); // cold
    if (tempC < 18)
        return rgb(110, 225, 220); // cool
    if (tempC < 28)
        return COL_AMBER;     // mild
    return rgb(255, 120, 60); // hot
}

static void drawInfoText(DisplayMode mode, bool timeSynced)
{
    char big[16];
    char cap[24] = "";
    uint16_t accent = TFT_CYAN;
    uint16_t bigColor = TFT_WHITE;
    bool degreeStyle = false;

    struct tm timeinfo;
    bool haveTime = timeSynced && getLocalTime(&timeinfo, 0);

    switch (mode)
    {
    case DisplayMode::CLOCK:
        if (haveTime)
        {
            strftime(big, sizeof(big), "%H:%M", &timeinfo);
            strftime(cap, sizeof(cap), "%a %d %b", &timeinfo);
        }
        else
        {
            strcpy(big, "--:--");
            strcpy(cap, "Syncing...");
        }
        break;
    case DisplayMode::DATE:
        accent = COL_AMBER;
        bigColor = COL_AMBER;
        if (haveTime)
        {
            strftime(big, sizeof(big), "%d %b", &timeinfo);
            strftime(cap, sizeof(cap), "%A", &timeinfo);
        }
        else
            strcpy(big, "----");
        break;
    case DisplayMode::WEATHER:
    {
        bool valid;
        float tempC;
        int code;
        readWeather(valid, tempC, code);
        if (valid)
        {
            snprintf(big, sizeof(big), "%d", (int)lroundf(tempC));
            strncpy(cap, weather_code_to_label(code), sizeof(cap) - 1);
            cap[sizeof(cap) - 1] = '\0';
            accent = bigColor = tempColor(tempC);
            degreeStyle = true;
        }
        else
        {
            strcpy(big, "N/A");
            accent = bigColor = COL_SOFT;
        }
        break;
    }
    default:
        strcpy(big, "N/A");
        break;
    }

    char key[48];
    snprintf(key, sizeof(key), "%s|%s", big, cap);
    if (mode == lastDrawnMode && strcmp(key, lastInfoText) == 0)
        return;

    // wipe everything below the eyes (divider, text, caption, page dots)
    tft.fillRect(0, dividerY - 2, screenW, screenH - dividerY + 2, TFT_BLACK);

    // divider: dim line with a short accent segment in the middle
    int16_t lineW = screenW * 3 / 5;
    tft.drawFastHLine((screenW - lineW) / 2, dividerY, lineW, COL_DIM);
    int16_t accW = screenW / 5;
    tft.fillRect((screenW - accW) / 2, dividerY - 1, accW, 2, accent);

    // main text
    tft.setTextColor(bigColor, TFT_BLACK);
    if (degreeStyle)
    {
        // font 4 has no degree sign, so draw a small ring after the number
        int16_t dr = screenW / 40;
        if (dr < 2)
            dr = 2;
        const int16_t gap = 2;
        int16_t wNum = tft.textWidth(big, 4);
        int16_t wC = tft.textWidth("C", 4);
        int16_t total = wNum + gap + dr * 2 + gap + wC;
        int16_t x = (screenW - total) / 2;
        tft.setTextDatum(ML_DATUM);
        tft.drawString(big, x, textY, 4);
        int16_t ringX = x + wNum + gap + dr;
        int16_t ringY = textY - tft.fontHeight(4) / 2 + dr + 2;
        tft.drawCircle(ringX, ringY, dr, bigColor);
        if (dr > 2)
            tft.drawCircle(ringX, ringY, dr - 1, bigColor);
        tft.drawString("C", x + wNum + gap * 2 + dr * 2, textY, 4);
    }
    else
    {
        tft.setTextDatum(MC_DATUM);
        tft.drawString(big, screenW / 2, textY, 4);
    }

    // caption (weekday / weather label)
    if (cap[0])
    {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_SOFT, TFT_BLACK);
        tft.drawString(cap, screenW / 2, captionY, 2);
    }

    // page dots: which of the 3 display modes is active
    for (int i = 0; i < 3; i++)
    {
        int16_t dx = screenW / 2 + (i - 1) * 12;
        if (i == (int)mode)
            tft.fillCircle(dx, dotsY, 3, accent);
        else
            tft.fillCircle(dx, dotsY, 2, COL_DIM);
    }

    lastDrawnMode = mode;
    strncpy(lastInfoText, key, sizeof(lastInfoText) - 1);
    lastInfoText[sizeof(lastInfoText) - 1] = '\0';
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
    dividerY = eyeY + eyeRadius + 16;
    textY = dividerY + 22;
    captionY = textY + 22;
    dotsY = screenH - 12;
}

void display_tick()
{
    uint32_t now = millis();
    Mood mood = readMood();
    DisplayMode mode = readDisplayMode();
    bool timeSynced = readTimeSynced();

    // While the WiFi setup portal is open, show how to join it instead of the face.
    if (readPortalActive())
    {
        if (!portalScreenShown)
        {
            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("WiFi setup", screenW / 2, screenH / 4, 2);
            tft.drawString("Join WiFi:", screenW / 2, screenH / 2 - 10, 2);
            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            tft.drawString(WIFI_PORTAL_AP_NAME, screenW / 2, screenH / 2 + 14, 4);
            portalScreenShown = true;
        }
        return;
    }
    if (portalScreenShown)
    {
        // Portal closed: wipe the screen and force everything to redraw.
        tft.fillScreen(TFT_BLACK);
        portalScreenShown = false;
        lastDrawnMood = static_cast<Mood>(-1);
        lastEyeOpenness = -1.0f;
        lastDrawnMode = static_cast<DisplayMode>(-1);
        lastInfoText[0] = '\0';
    }

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
            drawNormalEyes(openness, TFT_RED, false);
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
            drawNormalEyes(openness, TFT_CYAN, true);
            lastDrawnMood = mood;
            lastEyeOpenness = openness;
        }
    }

    drawInfoText(mode, timeSynced);
}