#pragma once
#include <Arduino.h>

// Firmware update from GitHub Releases - ALWAYS with the user's confirmation.
//
//   1. "Check for updates" on the settings page asks GitHub for the latest release of
//      UPDATE_REPO (config.h). Nothing is downloaded yet.
//   2. If a newer version exists for this clock's display variant (FW_VARIANT), the page
//      shows it. The user presses "Install" to confirm.
//   3. The clock downloads deskclock-<variant>.bin, checks it against the published
//      SHA-256 (deskclock-<variant>.bin.sha256), writes it to the other OTA slot and
//      reboots. A file that fails the check is never activated.
//
// The connection is TLS with certificate checking (root certificates in update_certs.h).
// It needs the real time (NTP), because certificates have validity dates.
// Each request runs in its own short-lived task, so nothing is reserved while idle.
// The check also runs by itself (update_tick(), UPDATE_AUTO_CHECK in config.h): twice a day it asks
// GitHub and updates the status. An automatic check never installs anything, and when it fails it
// leaves the previous status alone (no error message for a check the user did not start).
// Disabled in the Wokwi build and with UPDATE_ENABLED 0.

enum class UpdateState : uint8_t
{
    IDLE,        // nothing checked yet
    CHECKING,    // asking GitHub
    UP_TO_DATE,  // already on the latest release
    AVAILABLE,   // a newer release exists, waiting for the user's confirmation
    DOWNLOADING, // confirmed: downloading / writing (g_state.otaPercent shows progress on the screen)
    DONE,        // written; the clock is about to reboot
    FAILED       // see `error`
};

struct UpdateStatus
{
    UpdateState state = UpdateState::IDLE;
    String latest;   // release tag, e.g. "v1.3.0"
    String notesUrl; // the release page on GitHub
    String error;    // short code, e.g. "time", "tls", "http_404" (the settings page translates it)
    uint32_t sizeBytes = 0;
    uint8_t percent = 0;    // download progress while DOWNLOADING
    uint32_t checkedMs = 0; // millis() of the last successful check (manual or automatic); 0 = none yet
};

// Call once from setup(), before the tasks start.
void update_init();

// Call every few seconds from any task: starts the automatic check when it is due
// (UPDATE_AUTO_CHECK_INTERVAL_MS) and the clock is ready for it (WiFi, real time, memory).
void update_tick();

// Thread-safe copy of the current status (web task).
UpdateStatus update_status();

// Starts a check. False if a check or an install is already running.
bool update_request_check();

// Starts the install of `version` (must equal the tag found by the last check).
// False with a short code in `err` ("not_available", "alarm_busy", "busy", "heap").
bool update_request_install(const String &version, String &err);