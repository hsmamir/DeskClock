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
#include "boot_log.h"
#include "settings.h"
#include "pet.h"
#include "web_ui.h"
#include "reminders.h"
#include "ota.h"
#include "update_check.h"

AppState g_state;
SemaphoreHandle_t g_stateMutex;

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

// ---- Stack / heap report ----
// Stack sizes below are in BYTES (arduino-esp32). Every STACK_REPORT_INTERVAL_S seconds the
// Serial log shows how much stack each task has never used ("free min"). If a number
// gets close to 0 the task is about to overflow: raise its stack in setup(). The same line shows the heap:
// free now, largest free block now and lowest free ever.
// 0 = no report.
#ifndef STACK_REPORT_INTERVAL_S
#define STACK_REPORT_INTERVAL_S 300
#endif

struct TaskEntry
{
    const char *name;
    TaskHandle_t handle;
};
static TaskEntry s_tasks[8];
static uint8_t s_taskCount = 0;

static void startTask(TaskFunction_t fn, const char *name, uint32_t stackBytes, UBaseType_t priority, BaseType_t core)
{
    TaskHandle_t h = nullptr;
    if (xTaskCreatePinnedToCore(fn, name, stackBytes, nullptr, priority, &h, core) != pdPASS)
    {
        // Not enough heap for the stack: say so instead of silently running without that task.
        Serial.printf("ERROR: task '%s' could not be created (free heap %u)\n", name, (unsigned)ESP.getFreeHeap());
        return;
    }
    if (s_taskCount < sizeof(s_tasks) / sizeof(s_tasks[0]))
        s_tasks[s_taskCount++] = {name, h};
}

#if STACK_REPORT_INTERVAL_S > 0
static void reportStacks()
{
    Serial.print("Stack free min (bytes):");
    for (uint8_t i = 0; i < s_taskCount; i++)
        Serial.printf(" %s=%u", s_tasks[i].name, (unsigned)uxTaskGetStackHighWaterMark(s_tasks[i].handle));
    // largest block = the biggest single allocation that would succeed now (a TLS session needs ~60 KB of it);
    // lowest ever = the least total free heap since boot
    Serial.printf(" | heap free %u, largest block %u, lowest free ever %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
}
#endif

// ---- Task functions ----

static void displayTask(void *pvParameters)
{
    display_init();
    watchdogWatchThisTask("display");
    const uint32_t framePeriodMs = 33; // ~30fps
    for (;;)
    {
        const uint32_t t0 = millis();
        display_tick();
        watchdogFeed();
        // Sleep only for what is left of the frame period. A fixed vTaskDelay(33) made every frame take
        // (drawing time + 33 ms), i.e. ~22 fps when a frame needs ~11 ms. At least one tick is always
        // slept, so a slow frame can never keep the idle task or the equal-priority tasks from running.
        const uint32_t spentMs = millis() - t0;
        TickType_t waitTicks = pdMS_TO_TICKS(spentMs >= framePeriodMs ? 1 : framePeriodMs - spentMs);
        if (waitTicks == 0)
            waitTicks = 1;
        vTaskDelay(waitTicks);
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
        update_tick(); // automatic firmware-update check (first one UPDATE_AUTO_FIRST_DELAY_MS after boot)
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
        reminders_tick(); // due reminders: queue, sound, time-out
        audio_tick();     // yawn / chime / boot sounds queued by the display (gentle, muted at night)
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
    // Before Serial.begin(): the UART baud divider is computed from the clock that is running then.
    setCpuFrequencyMhz(CPU_FREQ_MHZ); // lower peak current (see "Power / brownout" in config.h)
    Serial.begin(115200);
    boot_log_init(); // counts the boot + keeps the reset reason in flash (and prints it)

    g_stateMutex = xSemaphoreCreateMutex();
    settings_init();           // load NVS settings + apply timezone before any task reads them
    pet_init();                // pet button statistics (needs the mutexes above)
    reminders_init();          // user reminders from flash (needs g_stateMutex)
    time_restore_from_flash(); // approximate clock from flash so the alarm works before/without WiFi
    audio_init();
    watchdogInit(); // before the tasks start; each watched task subscribes itself

    // Pinned to core 1 (app core) to leave core 0 free for WiFi/BT stack.
    // touch 4096 / servo 3072 (was 2048). The touch task reaches audio -> sun -> Serial.printf
    // with floats and (pet.cpp) writes NVS, which needs far more stack than the plain button code; check the
    // "Stack free min" log line to see the real margin.
    startTask(displayTask, "display", 8192, 2, 1);
    startTask(touchTask, "touch", 4096, 3, 1);
    startTask(servoTask, "servo", 3072, 2, 1);
    startTask(alarmTask, "alarm", 6144, 2, 1); // audio_tick() reaches audio -> sun -> Serial.printf like the touch task; reminders_tick() writes NVS
    startTask(timeTask, "time", 8192, 1, 0);
    startTask(weatherTask, "weather", 10240, 1, 0);
    startTask(webTask, "web", 8192, 1, 0);
    startTask(otaTask, "ota", 6144, 1, 0);
}

void loop()
{
    // All real work happens in FreeRTOS tasks; this only prints the stack report.
#if STACK_REPORT_INTERVAL_S > 0
    static uint32_t lastReportMs = 0;
    uint32_t now = millis();
    if ((uint32_t)(now - lastReportMs) >= (uint32_t)STACK_REPORT_INTERVAL_S * 1000UL)
    {
        lastReportMs = now;
        reportStacks();
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));
}