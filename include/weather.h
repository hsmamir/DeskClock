#pragma once

// Nothing to set up beyond WiFi (already brought up by time_sync_init());
// present for symmetry with the other modules' init/tick pairs.
void weather_init();

// Called repeatedly from the weather task (e.g. every 30s). Internally
// throttles actual HTTP fetches to WEATHER_REFRESH_INTERVAL_MS and skips
// the attempt entirely if WiFi isn't connected yet. Updates g_state on
// success; leaves the previous cached reading in place on failure.
void weather_tick();

// Call after the location changed: drops the old city's reading (so the TFT
// never shows another city's temperature) and fetches the new one on the
// next weather_tick().
void weather_refresh_now();

// Maps an Open-Meteo WMO weather code to a short label for the TFT.
const char *weather_code_to_label(int code);