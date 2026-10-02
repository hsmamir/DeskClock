#pragma once

// Wireless firmware updates (ArduinoOTA / espota), so the clock can be
// re-flashed over WiFi instead of USB:
//     pio run -e esp32dev-ota -t upload        (see platformio.ini)
//
// - Reachable at <WEB_HOSTNAME>.local (mDNS comes from web_ui.cpp) or by IP,
//   protected by OTA_PASSWORD (config.h).
// - The new image is written to the other app slot and only becomes the boot
//   image after it was received completely and verified, so an interrupted
//   upload leaves the running firmware untouched.
// - While an update runs, g_state.otaPercent (0-100) drives a progress screen.
// - Disabled in the Wokwi build (USE_WOKWI_WIFI): nothing to flash over there.

// Registers the callbacks. Does not touch the network. Call once from the OTA
// task before ota_tick().
void ota_init();

// Called repeatedly from the OTA task (e.g. every 20 ms). Waits for WiFi, starts
// the OTA listener once, then serves update requests. During an update this call
// blocks until the transfer finishes (then the clock reboots by itself).
void ota_tick();