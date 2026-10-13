#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include "web_ui.h"
#include "web_page.h"
#include "settings.h"
#include "audio.h"
#include "alarm_ctrl.h"
#include "occasion.h"
#include "boot_log.h"
#include "pet.h"
#include "weather.h"
#include "reminders.h"
#include "persian_text.h"
#include "update_check.h"
#include "version.h"
#include "config.h"

static WebServer server(80);
static bool s_serverStarted = false;
static bool s_mdnsStarted = false;
static bool s_wasConnected = false;

// A sound preview (pet sound or a candidate alarm tone) is temporary: a few seconds
// later it is cut off and the saved pet volume is put back, so "tried it, didn't
// save" can't leave the module at a different level than the one in flash.
static bool s_restoreVolume = false;
static uint32_t s_restoreAtMs = 0;
static const uint32_t PREVIEW_MS = 5000;

// Optional HTTP Basic auth (user "admin"); off while WEB_UI_PASSWORD is "".
static bool authorized()
{
    if (sizeof(WEB_UI_PASSWORD) <= 1)
        return true;
    if (server.authenticate("admin", WEB_UI_PASSWORD))
        return true;
    server.requestAuthentication();
    return false;
}

static void sendJson(int code, const JsonDocument &doc)
{
    String out;
    serializeJson(doc, out);
    server.send(code, "application/json", out);
}

static void sendError(int code, const char *msg)
{
    JsonDocument d;
    d["error"] = msg;
    sendJson(code, d);
}

static void sendSettings(int code)
{
    Settings s = settings_get();
    JsonDocument d;
    d["alarmEnabled"] = s.alarmEnabled;
    d["alarmHour"] = s.alarmHour;
    d["alarmMinute"] = s.alarmMinute;
    d["alarmDays"] = s.alarmDays;
    d["alarmSound"] = s.alarmSound;
    d["alarmVolume"] = s.alarmVolume;
    JsonArray snd = d["alarmSounds"].to<JsonArray>();
    for (int i = 0; i < settings_alarm_sound_count(); i++)
        snd.add(settings_alarm_sound_at(i));
    d["volume"] = s.volume;
    d["petReactions"] = s.petReactions;
    d["uiSounds"] = s.uiSounds;
    d["occasions"] = s.occasions;
    d["latitude"] = s.latitude;
    d["longitude"] = s.longitude;
    d["city"] = s.city;
    d["tzName"] = s.tzName;
    d["tzPosix"] = s.tzPosix;

    struct tm ti;
    if (getLocalTime(&ti, 0))
    {
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &ti);
        d["time"] = buf;
    }
    sendJson(code, d);
}

static void copyString(char *dst, size_t cap, JsonVariantConst v)
{
    if (v.is<const char *>())
        strlcpy(dst, v.as<const char *>(), cap);
}

static void handleGetSettings()
{
    if (!authorized())
        return;
    sendSettings(200);
}

// Short feedback beep for the settings page (clips 11 switched on / 12 switched off / 14 rejected).
// Never while the alarm is ringing or snoozed (it would cut the alarm off); at night it follows
// the pet volume rules (silent by default). `enabled` is passed in so that turning the beeps
// off still gets its own "off" beep.
static void uiBeep(bool enabled, uint16_t clip)
{
    if (!enabled || clip == 0 || alarm_get_state() != AlarmState::IDLE)
        return;
    audio_play_gentle(clip);
}

static void handlePostSettings()
{
    if (!authorized())
        return;

    // Browsers send a cross-site POST without a preflight only for "simple"
    // content types. Insisting on JSON means a random web page can't change
    // the settings behind your back.
    if (!server.header("Content-Type").startsWith("application/json"))
    {
        sendError(415, "send application/json");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok)
    {
        sendError(400, "bad JSON");
        return;
    }

    // Start from the current values so a partial update keeps the rest.
    Settings old = settings_get();
    Settings s = old;
    s.alarmEnabled = doc["alarmEnabled"] | old.alarmEnabled;
    s.alarmHour = doc["alarmHour"] | old.alarmHour;
    s.alarmMinute = doc["alarmMinute"] | old.alarmMinute;
    s.alarmDays = doc["alarmDays"] | old.alarmDays;
    s.alarmSound = doc["alarmSound"] | old.alarmSound;
    s.alarmVolume = doc["alarmVolume"] | old.alarmVolume;
    s.volume = doc["volume"] | old.volume;
    s.petReactions = doc["petReactions"] | old.petReactions;
    s.uiSounds = doc["uiSounds"] | old.uiSounds;
    s.occasions = doc["occasions"] | old.occasions;
    s.latitude = doc["latitude"] | old.latitude;
    s.longitude = doc["longitude"] | old.longitude;
    copyString(s.city, sizeof(s.city), doc["city"]);
    copyString(s.tzName, sizeof(s.tzName), doc["tzName"]);
    copyString(s.tzPosix, sizeof(s.tzPosix), doc["tzPosix"]);

    const char *err = "invalid settings";
    if (!settings_update(s, &err))
    {
        sendError(400, err);
        uiBeep(old.uiSounds, SOUND_INDEX_UI_ERROR);
        return;
    }

    // One beep per save (two beeps in a row would be dropped by the module's rate limit):
    // "off" wins if anything was switched off, else "on" if anything was switched on.
    bool turnedOff = (old.alarmEnabled && !s.alarmEnabled) || (old.petReactions && !s.petReactions) ||
                     (old.uiSounds && !s.uiSounds);
    bool turnedOn = (!old.alarmEnabled && s.alarmEnabled) || (!old.petReactions && s.petReactions) ||
                    (!old.uiSounds && s.uiSounds);
    if (turnedOff)
        uiBeep(old.uiSounds, SOUND_INDEX_UI_OFF);
    else if (turnedOn)
        uiBeep(s.uiSounds, SOUND_INDEX_UI_ON);

    // No volume command here: every sound sets the volume it needs just before it
    // plays (pet = saved volume, alarm = saved alarm volume), so a changed value
    // takes effect on the next sound without disturbing one that is playing.
    if (s.latitude != old.latitude || s.longitude != old.longitude)
        weather_refresh_now();

    sendSettings(200);
}

// Previews a clip once at the given volume: /api/test-sound?volume=N[&sound=K].
// No "sound" = the pet sound. Not while the alarm is ringing or snoozed: it would
// cut the alarm off.
static void handleTestSound()
{
    if (!authorized())
        return;
    if (alarm_get_state() != AlarmState::IDLE)
    {
        sendError(409, "alarm is active");
        return;
    }
    int v = server.hasArg("volume") ? server.arg("volume").toInt() : settings_get().volume;
    if (v < 0)
        v = 0;
    if (v > 30)
        v = 30;
    int sound = server.hasArg("sound") ? server.arg("sound").toInt() : SOUND_INDEX_PET;
    if (sound < 1 || sound > AUDIO_FILE_COUNT)
    {
        sendError(400, "sound out of range");
        return;
    }
    audio_play_preview((uint16_t)sound, (uint8_t)v); // volume + play as one sequence, with settle delay
    s_restoreVolume = true;
    s_restoreAtMs = millis() + PREVIEW_MS;
    server.send(200, "application/json", "{\"ok\":true}");
}

// Shows a yearly occasion on the face for 25 s: /api/test-occasion?id=0..5 (0 = stop, 1 Halloween,
// 2 Easter, 3 Christmas, 4 New Year's Eve with the countdown, 5 Valentine). Works whatever today's date is.
static void handleTestOccasion()
{
    if (!authorized())
        return;
    int id = server.hasArg("id") ? server.arg("id").toInt() : 0;
    if (id < 0 || id >= (int)Occasion::COUNT)
    {
        sendError(400, "id out of range");
        return;
    }
    occasion_preview((uint8_t)id, 25000);
    server.send(200, "application/json", "{\"ok\":true}");
}

// The alarm buttons change what the clock does, so (like the settings POST) they
// insist on a JSON content type: a random web page can't fire them cross-site.
static bool requireJsonPost()
{
    if (server.header("Content-Type").startsWith("application/json"))
        return true;
    sendError(415, "send application/json");
    return false;
}

static void handleGetAlarm()
{
    if (!authorized())
        return;
    AlarmState st = alarm_get_state();
    JsonDocument d;
    d["state"] = st == AlarmState::RINGING ? "ringing" : (st == AlarmState::SNOOZED ? "snoozed" : "idle");
    d["snoozeLeftS"] = alarm_snooze_left_s();
    sendJson(200, d);
}

// Rings the real alarm now: saved alarm sound + volume on repeat, servo, red eyes
// (whatever the alarm days / enabled setting say).
static void handleAlarmTest()
{
    if (!authorized() || !requireJsonPost())
        return;
    if (alarm_get_state() != AlarmState::IDLE)
    {
        sendError(409, "alarm is already active");
        return;
    }
    alarm_request_test();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleAlarmSnooze()
{
    if (!authorized() || !requireJsonPost())
        return;
    if (alarm_get_state() != AlarmState::RINGING)
    {
        sendError(409, "alarm is not ringing");
        return;
    }
    alarm_request_snooze();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleAlarmDismiss()
{
    if (!authorized() || !requireJsonPost())
        return;
    if (alarm_get_state() == AlarmState::IDLE)
    {
        sendError(409, "alarm is not active");
        return;
    }
    alarm_request_dismiss();
    server.send(200, "application/json", "{\"ok\":true}");
}

// Boot counter + the reasons of the last boots, newest first (kept in flash).
static void handleGetBootLog()
{
    if (!authorized())
        return;
    JsonDocument d;
    d["boots"] = boot_log_count();
    d["brownouts"] = boot_log_brownouts();
    d["uptimeS"] = millis() / 1000;
    JsonArray h = d["history"].to<JsonArray>();
    BootRecord rec[BOOT_LOG_HISTORY];
    int n = boot_log_history(rec, BOOT_LOG_HISTORY);
    for (int i = 0; i < n; i++)
    {
        JsonObject o = h.add<JsonObject>();
        o["boot"] = rec[i].bootNo;
        o["reason"] = boot_log_reason_name(rec[i].reason);
        o["bad"] = boot_log_reason_abnormal(rec[i].reason);
    }
    sendJson(200, d);
}

// Pet button statistics (taps today / best streak / total).
static void handleGetPet()
{
    if (!authorized())
        return;
    PetStats ps = pet_stats();
    JsonDocument d;
    d["today"] = ps.today;
    d["bestStreak"] = ps.bestStreak;
    d["streak"] = ps.streak;
    d["total"] = ps.total;
    PetBond pb = pet_bond();
    d["bond"] = pb.value / 10;      // percent, 0-100
    d["bondLevel"] = (int)pb.level; // 0 lonely, 1 friendly, 2 fond, 3 devoted
    d["bondOn"] = pb.enabled;
    sendJson(200, d);
}

// ---- Reminders (reminders.cpp): list / add / edit / delete from the settings page ----
static void sendReminders(int code)
{
    Reminder list[REMINDER_MAX];
    int n = reminders_list(list, REMINDER_MAX);
    JsonDocument d;
    d["max"] = REMINDER_MAX;
    d["textMax"] = REMINDER_TEXT_MAX - 1; // bytes of UTF-8 (a Persian letter is 2)
    d["sound"] = reminders_get_sound();
    ReminderView v;
    d["showing"] = reminder_get_active(v);
    JsonArray arr = d["items"].to<JsonArray>();
    for (int i = 0; i < n; i++)
    {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = list[i].id;
        o["enabled"] = list[i].enabled;
        o["hour"] = list[i].hour;
        o["minute"] = list[i].minute;
        o["days"] = list[i].days;
        char date[12] = "";
        if (list[i].date)
            snprintf(date, sizeof(date), "%04u-%02u-%02u", (unsigned)(list[i].date / 10000),
                     (unsigned)((list[i].date / 100) % 100), (unsigned)(list[i].date % 100));
        o["date"] = date;
        o["text"] = list[i].text;
    }
    sendJson(code, d);
}

static void handleGetReminders()
{
    if (!authorized())
        return;
    sendReminders(200);
}

// Reads the JSON body of a POST; false = an error was already sent.
static bool readJsonBody(JsonDocument &doc)
{
    if (!authorized() || !requireJsonPost())
        return false;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok)
    {
        sendError(400, "bad JSON");
        return false;
    }
    return true;
}

// Text typed on the page -> a cleaned UTF-8 string that fits Reminder::text. false = error sent.
static bool readReminderText(JsonVariantConst v, char *out, size_t cap)
{
    if (!v.is<const char *>())
    {
        sendError(400, "text missing");
        return false;
    }
    char tmp[256];
    int n = fa::sanitize(v.as<const char *>(), tmp, sizeof(tmp));
    if (n == -1)
        sendError(400, "text is not valid UTF-8");
    else if (n == -2 || n >= (int)cap)
        sendError(400, "text too long");
    else if (n == 0)
        sendError(400, "text is empty");
    else
    {
        memcpy(out, tmp, (size_t)n + 1);
        return true;
    }
    return false;
}

// POST {id?, enabled, hour, minute, days, date:"YYYY-MM-DD"|"", text}: id missing / 0 = add, else edit.
static void handlePostReminder()
{
    JsonDocument doc;
    if (!readJsonBody(doc))
        return;
    Reminder r = {};
    r.id = doc["id"] | 0;
    r.enabled = doc["enabled"] | true;
    r.hour = doc["hour"] | 0;
    r.minute = doc["minute"] | 0;
    r.days = doc["days"] | 0;
    const char *ds = doc["date"] | "";
    if (*ds)
    {
        unsigned y = 0, m = 0, dd = 0;
        if (sscanf(ds, "%4u-%2u-%2u", &y, &m, &dd) != 3)
        {
            sendError(400, "bad date");
            return;
        }
        r.date = y * 10000 + m * 100 + dd;
    }
    if (!readReminderText(doc["text"], r.text, sizeof(r.text)))
        return;
    const char *err = "invalid reminder";
    bool ok = r.id == 0 ? reminders_add(r, &err) : reminders_update(r, &err);
    if (!ok)
    {
        sendError(400, err);
        return;
    }
    sendReminders(200);
}

static void handleDeleteReminder()
{
    JsonDocument doc;
    if (!readJsonBody(doc))
        return;
    if (!reminders_remove(doc["id"] | 0))
    {
        sendError(404, "no such reminder");
        return;
    }
    sendReminders(200);
}

static void handleReminderSound()
{
    JsonDocument doc;
    if (!readJsonBody(doc))
        return;
    reminders_set_sound(doc["on"] | true);
    sendReminders(200);
}

// Shows the typed text on the clock right now, so Persian text can be checked on the real display.
static void handleReminderTest()
{
    JsonDocument doc;
    if (!readJsonBody(doc))
        return;
    char text[REMINDER_TEXT_MAX];
    if (!readReminderText(doc["text"], text, sizeof(text)))
        return;
    if (alarm_get_state() == AlarmState::RINGING)
    {
        sendError(409, "alarm is ringing");
        return;
    }
    reminder_request_test(text);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleReminderDismiss()
{
    if (!authorized() || !requireJsonPost())
        return;
    reminder_dismiss();
    server.send(200, "application/json", "{\"ok\":true}");
}

// ---- Firmware update (update_check.cpp): status / check / install ----
// The settings page polls GET /api/update; the two POSTs only start work (a check or the
// install of the version the last check found) and the page watches the status afterwards.
static const char *updateStateName(UpdateState s)
{
    switch (s)
    {
    case UpdateState::CHECKING:
        return "checking";
    case UpdateState::UP_TO_DATE:
        return "uptodate";
    case UpdateState::AVAILABLE:
        return "available";
    case UpdateState::DOWNLOADING:
        return "downloading";
    case UpdateState::DONE:
        return "done";
    case UpdateState::FAILED:
        return "failed";
    default:
        return "idle";
    }
}

static void handleGetUpdate()
{
    if (!authorized())
        return;
    JsonDocument d;
#if defined(USE_WOKWI_WIFI) || !UPDATE_ENABLED
    d["enabled"] = false; // same condition as update_check.cpp: the page hides its update card
#else
    UpdateStatus st = update_status();
    d["enabled"] = true;
    d["state"] = updateStateName(st.state);
    d["current"] = FW_VERSION;
    d["variant"] = FW_VARIANT;
#if UPDATE_AUTO_CHECK
    d["autoCheck"] = true;
#else
    d["autoCheck"] = false;
#endif
    d["latest"] = st.latest;
    d["notes"] = st.notesUrl;
    d["error"] = st.error;
    d["sizeKb"] = (st.sizeBytes + 1023) / 1024;
    d["percent"] = st.percent;
    d["checkedAgoS"] = st.checkedMs ? (int32_t)((millis() - st.checkedMs) / 1000) : -1;
#endif
    sendJson(200, d);
}

static void handleUpdateCheck()
{
    if (!authorized() || !requireJsonPost())
        return;
    if (!update_request_check())
    {
        sendError(409, "busy");
        return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleUpdateInstall()
{
    JsonDocument doc;
    if (!readJsonBody(doc))
        return;
    String err;
    if (!update_request_install(String(doc["version"] | ""), err))
    {
        sendError(409, err.length() ? err.c_str() : "busy");
        return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

// The page only changes with the firmware, so the browser may keep its copy: on a repeat visit
// it asks "still this version?" (If-None-Match) and gets an empty 304 instead of the whole page.
// __DATE__/__TIME__ are those of this file's last build, which also reruns when web_page.h changes.
static const char PAGE_ETAG[] = "\"" __DATE__ " " __TIME__ "\"";

static void handleRoot()
{
    if (!authorized())
        return;
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("ETag", PAGE_ETAG);
    if (server.header("If-None-Match") == PAGE_ETAG)
    {
        server.send(304);
        return;
    }
    server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

static void startServer()
{
    // needed so the POST handlers can read Content-Type and handleRoot() can read If-None-Match
    static const char *keys[] = {"Content-Type", "If-None-Match"};
    server.collectHeaders(keys, 2);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/test-sound", HTTP_POST, handleTestSound);
    server.on("/api/test-occasion", HTTP_POST, handleTestOccasion);
    server.on("/api/alarm", HTTP_GET, handleGetAlarm);
    server.on("/api/bootlog", HTTP_GET, handleGetBootLog);
    server.on("/api/pet", HTTP_GET, handleGetPet);
    server.on("/api/alarm/test", HTTP_POST, handleAlarmTest);
    server.on("/api/alarm/snooze", HTTP_POST, handleAlarmSnooze);
    server.on("/api/alarm/dismiss", HTTP_POST, handleAlarmDismiss);
    server.on("/api/reminders", HTTP_GET, handleGetReminders);
    server.on("/api/reminders", HTTP_POST, handlePostReminder);
    server.on("/api/reminders/delete", HTTP_POST, handleDeleteReminder);
    server.on("/api/reminders/sound", HTTP_POST, handleReminderSound);
    server.on("/api/reminders/test", HTTP_POST, handleReminderTest);
    server.on("/api/reminders/dismiss", HTTP_POST, handleReminderDismiss);
    server.on("/api/update", HTTP_GET, handleGetUpdate);
    server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
    server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
    server.onNotFound([]()
                      { server.send(404, "text/plain", "not found"); });
    server.begin();
    s_serverStarted = true;
}

static void startMdns()
{
    if (s_mdnsStarted)
        MDNS.end();
    s_mdnsStarted = MDNS.begin(WEB_HOSTNAME);
    if (s_mdnsStarted)
        MDNS.addService("http", "tcp", 80);
}

void web_ui_tick()
{
    bool connected = WiFi.status() == WL_CONNECTED;

    if (connected && !s_wasConnected)
    {
        // First connection, or WiFi came back: (re)announce the .local name.
        startMdns();
        if (!s_serverStarted)
            startServer();
        Serial.printf("Settings page: http://%s.local/  (http://%s/)\n",
                      WEB_HOSTNAME, WiFi.localIP().toString().c_str());
    }
    s_wasConnected = connected;

    if (s_serverStarted)
        server.handleClient();

    if (s_restoreVolume && (int32_t)(millis() - s_restoreAtMs) >= 0)
    {
        s_restoreVolume = false;
        // If the alarm started meanwhile it owns the module: leave it alone.
        if (alarm_get_state() == AlarmState::IDLE)
        {
            audio_stop(); // end of the preview (alarm clips can be long)
            audio_set_volume((uint8_t)settings_get().volume);
        }
    }
}