#include <Arduino.h>
#include "audio.h"
#include "pins.h"
#include "config.h"

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

void audio_set_volume(uint8_t volume)
{
    if (volume > 30)
        volume = 30;
    sendCommand(0x06, &volume, 1);
}

void audio_play(uint16_t fileIndex)
{
    uint8_t args[2] = {(uint8_t)(fileIndex >> 8), (uint8_t)(fileIndex & 0xFF)};
    sendCommand(0x03, args, 2);
}

void audio_play_loop(uint16_t fileIndex)
{
    uint8_t mode = 2; // loop mode 2 = repeat the current file forever
    sendCommand(0x11, &mode, 1);
    delay(50); // give the module time to apply the mode before the play command
    audio_play(fileIndex);
}

void audio_stop()
{
    sendCommand(0x0E, nullptr, 0); // pause
    delay(50);
    uint8_t mode = 4; // back to "play once, then stop" for pet sounds etc.
    sendCommand(0x11, &mode, 1);
}

void audio_init()
{
    Serial2.begin(AUDIO_BAUD, SERIAL_8N1, JQ6500_RX_PIN, JQ6500_TX_PIN);
    delay(300); // let the module finish booting before the first command

    audio_set_volume(AUDIO_VOLUME);

    // Loop mode 4 = play the selected file once and stop. Without this the
    // module carries on playing file 4, 5, ... after the chosen one ends.
    uint8_t mode = 4;
    sendCommand(0x11, &mode, 1);
}