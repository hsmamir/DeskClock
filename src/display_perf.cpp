// display_perf.cpp - frame-time measurement of the face page (see display_perf.h).
#include <Arduino.h>
#include "display_perf.h"

#if DISPLAY_PERF_ENABLED

// Written by the display task only, read by the main loop: plain 32-bit values, so a read is never torn.
static volatile uint32_t s_drawUs = 0, s_pushUs = 0, s_periodUs = 0;
static volatile uint32_t s_worstUs = 0, s_frames = 0, s_slow = 0;
static uint32_t s_lastFrameUs = 0;

// Moving average with weight 1/16 for the new value (the first value starts it).
static inline uint32_t ema(uint32_t avg, uint32_t v)
{
    if (avg == 0)
        return v;
    return (uint32_t)((int32_t)avg + ((int32_t)v - (int32_t)avg) / 16);
}

void display_perf_record(uint32_t drawUs, uint32_t pushUs)
{
    const uint32_t now = micros();
    if (s_lastFrameUs != 0)
    {
        const uint32_t period = now - s_lastFrameUs;
        // a gap of half a second or more = the face was not shown for a while (another page, a TLS pause): not a
        // frame time. The average starts again from the next frame instead of keeping an old, misleading value.
        if (period < 500000UL)
            s_periodUs = ema(s_periodUs, period);
        else
            s_periodUs = 0;
    }
    s_lastFrameUs = now;

    s_drawUs = ema(s_drawUs, drawUs);
    s_pushUs = ema(s_pushUs, pushUs);
    const uint32_t total = drawUs + pushUs;
    if (total > s_worstUs)
        s_worstUs = total;
    if (total > PERF_SLOW_FRAME_US)
        s_slow = s_slow + 1;
    s_frames = s_frames + 1;
}

void display_perf_get(DisplayPerf &out, bool reset)
{
    out.drawUs = s_drawUs;
    out.pushUs = s_pushUs;
    out.periodUs = s_periodUs;
    out.worstUs = s_worstUs;
    out.frames = s_frames;
    out.slowFrames = s_slow;
    if (reset)
    {
        s_worstUs = 0;
        s_frames = 0;
        s_slow = 0;
    }
}

#endif // DISPLAY_PERF_ENABLED