#pragma once

// Restores the clock after a reset/power loss: keeps the running system time if
// it is still valid, otherwise loads the last time saved to flash. Marks the
// time as usable-but-approximate (g_state.timeSynced + timeApprox) so the alarm
// works without WiFi. Call once from setup(), after settings_init().
void time_restore_from_flash();

// Writes the current time to flash right now (if it is valid). Normally done
// periodically by time_sync_tick(); call it before a planned restart (OTA).
void time_save_now();

// Connects to WiFi (blocking, up to WIFI_CONNECT_TIMEOUT_MS) and performs
// the first NTP sync. Sets g_state.timeSynced and clears timeApprox on success.
void time_sync_init();

// Called from the time task on a slow interval (e.g. every 30s). Re-syncs
// with NTP every NTP_RESYNC_INTERVAL_MS (or right away while the time is only
// approximate), reconnects WiFi if dropped, and saves the current time to
// flash every TIME_SAVE_INTERVAL_MS (also while offline).
void time_sync_tick();