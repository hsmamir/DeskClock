#include <Arduino.h>
#include <time.h>
#include "audio.h"
#include "pins.h"
#include "config.h"
#include "settings.h"
#include "state.h"
#include "sun.h"

// Several tasks talk to the module (touch, alarm, web). Each public call is a
// multi-step sequence (volume -> delay -> play), so it is guarded by a mutex
// to keep two sequences from interleaving on the UART.
static SemaphoreHandle_t s_audioMutex = nullptr;

struct AudioLock
{
    AudioLock()
    {
        if (s_audioMutex)
            xSemaphoreTake(s_audioMutex, portMAX_DELAY);
    }
    ~AudioLock()
    {
        if (s_audioMutex)
            xSemaphoreGive(s_audioMutex);
    }
};

// Volume the module currently has (-1 = unknown). Lets us skip redundant
// volume commands and the settle delay that goes with them.
static int s_curVolume = -1;

// JQ6500 serial frame: 0x7E, LEN, CMD, [args...], 0xEF
// LEN counts itself + CMD + args (so a command with n args has LEN = n + 2).
static void sendCommand(uint8_t cmd, const uint8_t *args, uint8_t argCount)
{
    uint8_t buf[8];
    uint8_t n = 0;
    buf[n++] = 0x7E;
    buf[n++] = argCount + 2;
    buf[n++] = cmd;
    for (uint8_t i = 0; i < argCount; i++)
        buf[n++] = args[i];
    buf[n++] = 0xEF;
    Serial2.write(buf, n); // one write = one whole frame
}

// ---- Internal helpers (caller already holds the lock) ----

static void sendVolume(uint8_t volume)
{
    if (volume > 30)
        volume = 30;
    sendCommand(0x06, &volume, 1);
    s_curVolume = volume;
}

// Only touches the module when the level really changes; waits a moment so
// the following play command isn't swallowed.
static void ensureVolume(uint8_t volume)
{
    if (volume > 30)
        volume = 30;
    if (s_curVolume == (int)volume)
        return;
    sendVolume(volume);
    delay(30);
}

static void sendPlay(uint16_t fileIndex)
{
    uint8_t args[2] = {(uint8_t)(fileIndex >> 8), (uint8_t)(fileIndex & 0xFF)};
    sendCommand(0x03, args, 2);
}

static bool isNightNow()
{
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    bool synced = g_state.timeSynced;
    xSemaphoreGive(g_stateMutex);

    struct tm ti;
    if (!synced || !getLocalTime(&ti, 0))
        return false; // clock unknown -> behave like daytime
    return sun_is_night(ti);
}

// ---- Public API ----

static QueueHandle_t s_audioQueue = nullptr;

void audio_play_async(uint16_t fileIndex)
{
    if (fileIndex == 0 || !s_audioQueue)
        return;
    xQueueSend(s_audioQueue, &fileIndex, 0); // full queue: the sound is simply dropped
}

void audio_tick()
{
    uint16_t idx;
    while (s_audioQueue && xQueueReceive(s_audioQueue, &idx, 0) == pdTRUE)
        audio_play_gentle(idx);
}

uint16_t audio_next_pet_chirp()
{
    static const uint16_t all[3] = {SOUND_INDEX_PET, SOUND_INDEX_PET_B, SOUND_INDEX_PET_C};
    uint16_t v[3];
    uint8_t n = 0;
    for (uint8_t i = 0; i < 3; i++)
        if (all[i] != 0)
            v[n++] = all[i];
    if (n == 0)
        return SOUND_INDEX_PET;
    static int8_t last = -1;
    int8_t pick = (int8_t)(random(0, n));
    if (n > 1 && pick == last)
        pick = (int8_t)((pick + 1 + random(0, n - 1)) % n); // not the same chirp twice in a row
    last = pick;
    return v[pick];
}

void audio_set_volume(uint8_t volume)
{
    AudioLock lock;
    sendVolume(volume);
    delay(30); // same settle time as ensureVolume(): a play command right after must not be lost
}

void audio_play_preview(uint16_t fileIndex, uint8_t volume)
{
    AudioLock lock;
    ensureVolume(volume); // sends the volume + settle delay only when the level really changes
    sendPlay(fileIndex);
}

void audio_play(uint16_t fileIndex)
{
    AudioLock lock;
    sendPlay(fileIndex);
}

void audio_play_gentle(uint16_t fileIndex)
{
    uint8_t volume = (uint8_t)settings_get().volume;

    if (isNightNow())
    {
        if (AUDIO_NIGHT_PET_VOLUME <= 0)
            return; // night + muted: don't make a sound at all
        if (volume > AUDIO_NIGHT_PET_VOLUME)
            volume = AUDIO_NIGHT_PET_VOLUME;
    }

    AudioLock lock;
    ensureVolume(volume);
    sendPlay(fileIndex);
}

// The alarm always plays at the saved ALARM volume (separate from the pet
// volume), whatever the time of day and whatever level the module was left at
// by a night-time pet sound or a web page volume preview.
void audio_play_loop(uint16_t fileIndex)
{
    uint8_t full = (uint8_t)settings_get().alarmVolume;

    AudioLock lock;
    ensureVolume(full);
    uint8_t mode = 2; // loop mode 2 = repeat the current file forever
    sendCommand(0x11, &mode, 1);
    delay(50); // give the module time to apply the mode before the play command
    sendPlay(fileIndex);
}

void audio_stop()
{
    AudioLock lock;
    sendCommand(0x0E, nullptr, 0); // pause
    delay(50);
    uint8_t mode = 4; // back to "play once, then stop" for pet sounds etc.
    sendCommand(0x11, &mode, 1);
}

void audio_init()
{
    s_audioMutex = xSemaphoreCreateMutex();
    s_audioQueue = xQueueCreate(4, sizeof(uint16_t));

    Serial2.begin(AUDIO_BAUD, SERIAL_8N1, JQ6500_RX_PIN, JQ6500_TX_PIN);
    delay(300); // let the module finish booting before the first command

    audio_set_volume((uint8_t)settings_get().volume);

    // Loop mode 4 = play the selected file once and stop. Without this the
    // module carries on playing file 4, 5, ... after the chosen one ends.
    AudioLock lock;
    uint8_t mode = 4;
    sendCommand(0x11, &mode, 1);
}