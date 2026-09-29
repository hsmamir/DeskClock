#pragma once
#include <stdint.h>

// Starts UART2 for the JQ6500-16P, sets the volume and "play one file, then
// stop" mode. Call once from setup().
void audio_init();

// Plays a file from the module's onboard flash by index (1 = first uploaded
// file, 3 = the third one, i.e. the sound the module's K3 pin triggers).
// Restarts the sound if it is already playing. Non-blocking.
void audio_play(uint16_t fileIndex);

// Volume 0-30.
void audio_set_volume(uint8_t volume);