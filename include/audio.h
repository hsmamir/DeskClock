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

// Plays a file on repeat (gapless, no need to know the clip length) until
// audio_stop() is called. Blocks ~50 ms between the two serial commands, so
// call it from a task, not from an ISR.
void audio_play_loop(uint16_t fileIndex);

// Stops whatever is playing and puts the module back into "play once"
// mode, so audio_play() behaves normally again.
void audio_stop();

// Like audio_play(), but for non-alarm sounds (pet reaction): at night
// (NIGHT_START_HOUR..NIGHT_END_HOUR) it plays at AUDIO_NIGHT_PET_VOLUME, or
// not at all when that is 0. Outside the night window it plays at the saved
// volume. Call from a task (may block ~30 ms).
void audio_play_gentle(uint16_t fileIndex);