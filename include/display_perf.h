// display_perf.h - frame-time measurement of the face (eyes) page.
//
// display_tick() times the two big parts of every face frame:
//   draw = drawFace() + overlays into the sprite (CPU: the per-pixel eye shading)
//   push = Canvas::endAt() (SPI: the sprite going out to the panel)
// and the real time between two face frames. The values are smoothed (moving average over ~16 frames).
// main.cpp prints them every PERF_REPORT_INTERVAL_S seconds. Only the face page is measured
// (the clock / date / weather pages redraw only when something changes, so they are not a frame loop).
#pragma once
#include <stdint.h>

// 0 = no measuring and no report (the calls below then compile to nothing).
#ifndef DISPLAY_PERF_ENABLED
#define DISPLAY_PERF_ENABLED 1
#endif

// Seconds between two "Display perf" lines on Serial. 0 = no report.
#ifndef PERF_REPORT_INTERVAL_S
#define PERF_REPORT_INTERVAL_S 30
#endif

// A frame whose draw + push takes longer than this is counted as slow (the 30 fps budget is 33 ms).
#ifndef PERF_SLOW_FRAME_US
#define PERF_SLOW_FRAME_US 33000
#endif

struct DisplayPerf
{
    uint32_t drawUs;     // smoothed: drawing into the sprite
    uint32_t pushUs;     // smoothed: pushing the sprite to the panel
    uint32_t periodUs;   // smoothed: time between two face frames (1e6 / periodUs = real fps)
    uint32_t worstUs;    // slowest draw + push since the last reset
    uint32_t frames;     // face frames measured since the last reset
    uint32_t slowFrames; // of those, how many took longer than PERF_SLOW_FRAME_US
};

#if DISPLAY_PERF_ENABLED
// Called by display_tick() once per face frame with the two timings in microseconds.
void display_perf_record(uint32_t drawUs, uint32_t pushUs);
// Copies the current values. reset = true also clears worstUs / frames / slowFrames (not the averages).
void display_perf_get(DisplayPerf &out, bool reset);
#else
static inline void display_perf_record(uint32_t, uint32_t) {}
static inline void display_perf_get(DisplayPerf &out, bool) { out = DisplayPerf{}; }
#endif