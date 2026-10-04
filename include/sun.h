#pragma once
#include <time.h>

// Real sunrise / sunset from the saved coordinates (Settings.latitude/longitude)
// and the date — NOAA solar-position formulas, computed on the chip, no internet.
// All hours are LOCAL time in hours (e.g. 6.25 = 06:15) using the clock's
// timezone (DST included).
struct SunTimes
{
    bool valid;       // false: could not be computed -> callers use the fixed config.h hours
    bool polarNight;  // sun never rises today
    bool midnightSun; // sun never sets today
    float riseH;      // local sunrise, 0..24
    float setH;       // local sunset; always > riseH (may be > 24 when it falls after midnight)
};

// Pure calculation (no globals): year, day of year 0-based (tm_yday), latitude and
// longitude in degrees (east +), and the UTC offset of the local time in minutes.
SunTimes sun_compute(int year, int yday, double latDeg, double lonDeg, int utcOffsetMin);

// Today's sun times for the local date `local`. Cached per day / location, cheap
// enough to call every frame, safe from any task.
SunTimes sun_today(const struct tm &local);

// Sun above the horizon right now? (falls back to SCENE_DAY_START/END_HOUR)
bool sun_is_day(const struct tm &local);

// Night mode window (dim backlight, mute pet sound, sleepy eyes).
// With NIGHT_FOLLOWS_SUN: from sunset (+SUN_NIGHT_START_AFTER_SUNSET_H) to sunrise
// (+SUN_NIGHT_END_AFTER_SUNRISE_H). Otherwise / if the sun can't be computed: the
// fixed NIGHT_START_HOUR..NIGHT_END_HOUR window.
bool sun_is_night(const struct tm &local);

// ---- Moon phase ----
// Age of the moon as a fraction of the synodic month for a UTC time:
// 0 = new, 0.25 = first quarter, 0.5 = full, 0.75 = last quarter.
// Mean-lunation formula (no internet): accurate to about +-1 day, plenty for a picture.
float moon_phase(time_t utc);

// Phase as it LOOKS from the saved location: same as moon_phase(now), but mirrored in
// the southern hemisphere (the lit side is on the other side there), so 0.25 always
// means "right half lit" on screen.
float moon_phase_view();