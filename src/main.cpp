#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "state.h"
#include "display.h"
#include "touch.h"
#include "servo_ctrl.h"
#include "time_sync.h"
#include "weather.h"

AppState g_state;
SemaphoreHandle_t g_stateMutex;

// ---- Task functions ----

static void displayTask(void *pvParameters)
{
    display_init();
    const TickType_t frameDelay = pdMS_TO_TICKS(33); // ~30fps
    for (;;)
    {
        display_tick();
        vTaskDelay(frameDelay);
    }
}

static void touchTask(void *pvParameters)
{
    touch_init();
    const TickType_t pollDelay = pdMS_TO_TICKS(10);
    for (;;)
    {
        touch_tick();
        vTaskDelay(pollDelay);
    }
}

static void servoTask(void *pvParameters)
{
    servo_init();
    const TickType_t tickDelay = pdMS_TO_TICKS(20);
    for (;;)
    {
        servo_tick();
        vTaskDelay(tickDelay);
    }
}

static void timeTask(void *pvParameters)
{
    time_sync_init();
    const TickType_t tickDelay = pdMS_TO_TICKS(30000);
    for (;;)
    {
        time_sync_tick();
        vTaskDelay(tickDelay);
    }
}

static void weatherTask(void *pvParameters)
{
    weather_init();
    // Polls every 30s, but weather_tick() itself only actually fetches
    // every WEATHER_REFRESH_INTERVAL_MS — the frequent poll just means it
    // picks up WiFi becoming available quickly rather than waiting a full
    // refresh interval after boot.
    const TickType_t tickDelay = pdMS_TO_TICKS(30000);
    for (;;)
    {
        weather_tick();
        vTaskDelay(tickDelay);
    }
}

void setup()
{
    Serial.begin(115200);

    g_stateMutex = xSemaphoreCreateMutex();

    // Pinned to core 1 (app core) to leave core 0 free for WiFi/BT stack.
    xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(touchTask, "touch", 2048, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(servoTask, "servo", 2048, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(timeTask, "time", 4096, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(weatherTask, "weather", 10240, nullptr, 1, nullptr, 0);
}

void loop()
{
    // Empty — all work happens in FreeRTOS tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}