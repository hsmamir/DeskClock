#pragma once
#include <Arduino.h>

// Persistent boot log: counts boots and remembers why the last few boots
// happened (power on, brownout, panic, watchdog, ...). Kept in NVS (flash), so
// it survives power loss and is readable without a USB cable - on the DIAG
// page of the display and on the web page (GET /api/bootlog).
// A BROWNOUT entry means the supply dipped below the chip's threshold: check
// the power supply, and the servo / audio load that shares it.

static const int BOOT_LOG_HISTORY = 8; // how many boots are remembered

struct BootRecord
{
    uint32_t bootNo; // 1-based boot number since this log started
    uint8_t reason;  // esp_reset_reason_t
};

// Call once at the start of setup(), after Serial.begin(). Records this boot
// (one small flash write) and prints the reason to Serial like before.
void boot_log_init();

uint32_t boot_log_count();     // boots since this log started (this boot included)
uint32_t boot_log_brownouts(); // how many of them were brownouts

// Copies up to `max` records, newest first (index 0 = the current boot).
// Returns how many were copied. Read-only after boot_log_init(): any task may call it.
int boot_log_history(BootRecord *out, int max);

// Short human name ("Brownout", "Power on", ...), at most 10 characters.
const char *boot_log_reason_name(uint8_t reason);

// True for resets that point at a problem (brownout, panic, any watchdog).
bool boot_log_reason_abnormal(uint8_t reason);