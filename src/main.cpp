#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
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

// ---- Task watchdog (see "Task watchdog" in config.h) ----
// The system already runs a task watchdog (it watches the core-0 idle task); here
// it is reconfigured to TASK_WDT_TIMEOUT_S and our own tasks subscribe to it.
// The two IDF generations (arduino-esp32 2.x = IDF 4, 3.x = IDF 5) have different APIs.

static void watchdogInit()
{
#if TASK_WDT_ENABLED
    esp_err_t e;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms = (uint32_t)TASK_WDT_TIMEOUT_S * 1000UL;
    cfg.idle_core_mask = 1; // keep watching the core-0 idle task, as the default setup does
    cfg.trigger_panic = true;
    e = esp_task_wdt_reconfigure(&cfg);
    if (e == ESP_ERR_INVALID_STATE)
        e = esp_task_wdt_init(&cfg); // the system had not started one
#else
    e = esp_task_wdt_init(TASK_WDT_TIMEOUT_S, true); // true = reset the chip on timeout
#endif
    Serial.printf("Task watchdog: %u s timeout (%s)\n", (unsigned)TASK_WDT_TIMEOUT_S, esp_err_to_name(e));
#endif
}

// Call from inside the task, once its init is done (display_init() etc. can take a while).
static void watchdogWatchThisTask(const char *name)
{
#if TASK_WDT_ENABLED
    esp_err_t e = esp_task_wdt_add(nullptr);
    if (e != ESP_OK)
        Serial.printf("Task watchdog: could not watch '%s' (%s)\n", name, esp_err_to_name(e));
#else
    (void)name;
#endif
}

// "Still alive": call once per loop pass of a watched task.
static inline void watchdogFeed()
{
#if TASK_WDT_ENABLED
    esp_task_wdt_reset();
#endif
}

// ---- Task functions ----

static void displayTask(void *pvParameters)
{
    display_init();
    watchdogWatchThisTask("display");
    const TickType_t frameDelay = pdMS_TO_TICKS(33); // ~30fps
    for (;;)
    {
        display_tick();
        watchdogFeed();
        vTaskDelay(frameDelay);
    }
}

static void touchTask(void *pvParameters)
{
    touch_init();
    watchdogWatchThisTask("touch");
    const TickType_t pollDelay = pdMS_TO_TICKS(10);
    for (;;)
    {
        touch_tick();
        watchdogFeed();
        vTaskDelay(pollDelay);
    }
}

static void servoTask(void *pvParameters)
{
    servo_init();
    watchdogWatchThisTask("servo");
    const TickType_t tickDelay = pdMS_TO_TICKS(20);
    for (;;)
    {
        servo_tick();
        watchdogFeed();
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
    watchdogWatchThisTask("alarm");
    const TickType_t tickDelay = pdMS_TO_TICKS(200);
    for (;;)
    {
        alarm_tick();
#if TASK_WDT_SELFTEST
        // Pretend this task hung: stop reporting in. The watchdog must reset the chip
        // TASK_WDT_TIMEOUT_S seconds later ("Reset reason: TASK_WDT" on the next boot).
        if (millis() > 20000)
        {
            Serial.println("WDT selftest: alarm task stops reporting in now");
            for (;;)
                vTaskDelay(pdMS_TO_TICKS(1000));
        }
#endif
        watchdogFeed();
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
    watchdogInit(); // before the tasks start; each watched task subscribes itself

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