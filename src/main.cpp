#include <Arduino.h>
#include <esp_system.h>
#include "pins.h"
#include "config.h"
#include "state.h"
#include "display.h"
#include "touch.h"
#include "servo_ctrl.h"
#include "time_sync.h"
#include "weather.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "settings.h"
#include "web_ui.h"
#include "ota.h"

AppState g_state;
SemaphoreHandle_t g_stateMutex;

// ---- Reset reason (printed once per boot, to find out why it restarted) ----

static void logResetReason()
{
    esp_reset_reason_t r = esp_reset_reason();
    const char *name;
    switch (r)
    {
    case ESP_RST_POWERON:
        name = "POWERON (power applied)";
        break;
    case ESP_RST_EXT:
        name = "EXT (reset pin)";
        break;
    case ESP_RST_SW:
        name = "SW (esp_restart)";
        break;
    case ESP_RST_PANIC:
        name = "PANIC (crash / exception)";
        break;
    case ESP_RST_INT_WDT:
        name = "INT_WDT (interrupt watchdog)";
        break;
    case ESP_RST_TASK_WDT:
        name = "TASK_WDT (task watchdog)";
        break;
    case ESP_RST_WDT:
        name = "WDT (other watchdog)";
        break;
    case ESP_RST_DEEPSLEEP:
        name = "DEEPSLEEP (wake from deep sleep)";
        break;
    case ESP_RST_BROWNOUT:
        name = "BROWNOUT (supply voltage dipped - check power / servo / audio load)";
        break;
    case ESP_RST_SDIO:
        name = "SDIO";
        break;
    default:
        name = "UNKNOWN";
        break;
    }
    Serial.printf("Reset reason: %s [%d]\n", name, (int)r);
}

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

static void alarmTask(void *pvParameters)
{
    const TickType_t tickDelay = pdMS_TO_TICKS(200);
    for (;;)
    {
        alarm_tick();
        vTaskDelay(tickDelay);
    }
}

static void otaTask(void *pvParameters)
{
    // Own task: ota_tick() blocks for the whole transfer while an update runs.
    ota_init();
    const TickType_t tickDelay = pdMS_TO_TICKS(20);
    for (;;)
    {
        ota_tick();
        vTaskDelay(tickDelay);
    }
}

static void webTask(void *pvParameters)
{
    // web_ui_tick() waits for WiFi by itself, then starts mDNS + the server.
    const TickType_t tickDelay = pdMS_TO_TICKS(10);
    for (;;)
    {
        web_ui_tick();
        vTaskDelay(tickDelay);
    }
}

void setup()
{
    Serial.begin(115200);
    logResetReason();

    g_stateMutex = xSemaphoreCreateMutex();
    settings_init();           // load NVS settings + apply timezone before any task reads them
    time_restore_from_flash(); // approximate clock from flash so the alarm works before/without WiFi
    audio_init();

    // Pinned to core 1 (app core) to leave core 0 free for WiFi/BT stack.
    xTaskCreatePinnedToCore(displayTask, "display", 8192, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(touchTask, "touch", 2048, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(servoTask, "servo", 2048, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(alarmTask, "alarm", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(timeTask, "time", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(weatherTask, "weather", 10240, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(otaTask, "ota", 6144, nullptr, 1, nullptr, 0);
}

void loop()
{
    // Empty — all work happens in FreeRTOS tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}