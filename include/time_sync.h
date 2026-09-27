#pragma once

// Connects to WiFi (blocking, up to WIFI_CONNECT_TIMEOUT_MS) and performs
// the first NTP sync. Sets g_state.timeSynced on success.
void time_sync_init();

// Called from the time task on a slow interval (e.g. every 30s). Re-syncs
// with NTP every NTP_RESYNC_INTERVAL_MS and reconnects WiFi if dropped.
void time_sync_tick();
