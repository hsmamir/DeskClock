// weather_mood.cpp - the temperature half of weather_mood.h: hot / cold with a hysteresis, and how strong it is.
//
// The "is it hot / cold" answer is remembered (s_hotOn / s_coldOn) and only changes when the temperature leaves the
// range by REACT_TEMP_HYST_C, so a reading hovering around the limit does not flip the face on every refresh.
// The answer depends only on the temperature history, never on who asks or when, so the idle eyes, the pet and the
// persona can all ask and always agree. (A race between the display task and the touch task can at worst repeat the
// same update, which does nothing.)
#include <math.h>
#include "weather_mood.h"

static volatile bool s_hotOn = false;
static volatile bool s_coldOn = false;

static void tempUpdate(float t)
{
    if (isnan(t))
        return; // no reading: keep the answer we had
    if (!s_hotOn && t >= REACT_HOT_C)
        s_hotOn = true;
    else if (s_hotOn && t < REACT_HOT_C - REACT_TEMP_HYST_C)
        s_hotOn = false;
    if (!s_coldOn && t <= REACT_COLD_C)
        s_coldOn = true;
    else if (s_coldOn && t > REACT_COLD_C + REACT_TEMP_HYST_C)
        s_coldOn = false;
}

bool wxIsHot(float tempC)
{
    tempUpdate(tempC);
    return s_hotOn;
}

bool wxIsCold(float tempC)
{
    tempUpdate(tempC);
    return s_coldOn;
}

static float ramp(float t, float start, float full) // 0 at start .. 1 at full (either direction), REACT_TEMP_MIN_K..1
{
    float r = (t - start) / (full - start);
    float k = REACT_TEMP_MIN_K + (1.0f - REACT_TEMP_MIN_K) * r;
    return k < REACT_TEMP_MIN_K ? REACT_TEMP_MIN_K : (k > 1.0f ? 1.0f : k);
}

float wxHotIntensity(float tempC)
{
    return isnan(tempC) ? REACT_TEMP_MIN_K : ramp(tempC, REACT_HOT_C, REACT_HOT_FULL_C);
}

float wxColdIntensity(float tempC)
{
    return isnan(tempC) ? REACT_TEMP_MIN_K : ramp(tempC, REACT_COLD_C, REACT_COLD_FULL_C);
}