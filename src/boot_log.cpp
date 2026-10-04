#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>
#include "boot_log.h"

static const char *BOOT_NVS_NAMESPACE = "bootlog";
static const char *BOOT_NVS_KEY = "log";
static const uint32_t BOOT_LOG_MAGIC = 0xB007106Au;

struct Entry
{
    uint32_t bootNo;
    uint8_t reason;
    uint8_t pad[3];
};

// One blob = one flash write per boot, and it is either all there or not at all.
struct Store
{
    uint32_t magic;
    uint32_t boots;
    uint32_t brownouts;
    Entry e[BOOT_LOG_HISTORY]; // newest first
};

static Store s_store;

const char *boot_log_reason_name(uint8_t reason)
{
    switch ((esp_reset_reason_t)reason)
    {
    case ESP_RST_POWERON:
        return "Power on";
    case ESP_RST_EXT:
        return "Reset pin";
    case ESP_RST_SW:
        return "Software";
    case ESP_RST_PANIC:
        return "Panic";
    case ESP_RST_INT_WDT:
        return "Int WDT";
    case ESP_RST_TASK_WDT:
        return "Task WDT";
    case ESP_RST_WDT:
        return "Other WDT";
    case ESP_RST_DEEPSLEEP:
        return "Deep sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "Unknown";
    }
}

bool boot_log_reason_abnormal(uint8_t reason)
{
    switch ((esp_reset_reason_t)reason)
    {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
        return true;
    default:
        return false;
    }
}

void boot_log_init()
{
    esp_reset_reason_t r = esp_reset_reason();

    memset(&s_store, 0, sizeof(s_store));
    Preferences p;
    bool open = p.begin(BOOT_NVS_NAMESPACE, false);
    if (open)
    {
        Store loaded;
        size_t n = p.getBytes(BOOT_NVS_KEY, &loaded, sizeof(loaded));
        if (n == sizeof(loaded) && loaded.magic == BOOT_LOG_MAGIC)
            s_store = loaded; // otherwise: first run (or a different layout) - start fresh
    }
    s_store.magic = BOOT_LOG_MAGIC;

    // Newest first: shift the old entries down, put this boot at index 0.
    memmove(&s_store.e[1], &s_store.e[0], sizeof(Entry) * (BOOT_LOG_HISTORY - 1));
    s_store.boots++;
    s_store.e[0].bootNo = s_store.boots;
    s_store.e[0].reason = (uint8_t)r;
    if (r == ESP_RST_BROWNOUT)
        s_store.brownouts++;

    if (open)
    {
        if (p.putBytes(BOOT_NVS_KEY, &s_store, sizeof(s_store)) != sizeof(s_store))
            Serial.println("Boot log: could not save to flash");
        p.end();
    }

    Serial.printf("Reset reason: %s [%d] (boot #%u, %u brownouts so far)\n",
                  boot_log_reason_name((uint8_t)r), (int)r,
                  (unsigned)s_store.boots, (unsigned)s_store.brownouts);
    if (r == ESP_RST_BROWNOUT)
        Serial.println("  -> supply voltage dipped: check power / servo / audio load");
}

uint32_t boot_log_count()
{
    return s_store.boots;
}

uint32_t boot_log_brownouts()
{
    return s_store.brownouts;
}

int boot_log_history(BootRecord *out, int max)
{
    int n = 0;
    for (int i = 0; i < BOOT_LOG_HISTORY && n < max; i++)
    {
        if (s_store.e[i].bootNo == 0)
            break; // unused slot: nothing older either
        out[n].bootNo = s_store.e[i].bootNo;
        out[n].reason = s_store.e[i].reason;
        n++;
    }
    return n;
}