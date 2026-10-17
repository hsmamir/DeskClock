// weather_mood.cpp - the temperature half of weather_mood.h: hot / cold with a hysteresis, and how strong it is.
//
// The "is it hot / cold" answer is remembered (s_hotOn / s_coldOn) and only changes when the temperature leaves the
// range by REACT_TEMP_HYST_C, so a reading hovering around the limit does not flip the face on every refresh.
// The answer depends only on the temperature history, never on who asks or when, so the idle eyes, the pet and the
// persona can all ask and always agree. (A race between the display task and the touch task can at worst repeat the
// same update, which does nothing.)
#include <math.h>
#include "weather_mood.h"
#include "weather_extras.h" // WX_AIRQ_MASK_AQI, WX_UV_SHADES_MIN

static volatile bool s_hotOn = false;
static volatile bool s_coldOn = false;
static volatile bool s_windOn = false;
static volatile bool s_airOn = false;
static volatile bool s_uvOn = false;

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

// ---- strong wind ----
bool wxIsWindy(float windKmh)
{
    if (REACT_WIND_KMH <= 0)
        return false;
    if (isnan(windKmh) || windKmh < 0)
        return s_windOn; // no reading: keep the answer we had
    if (!s_windOn && windKmh >= (float)REACT_WIND_KMH)
        s_windOn = true;
    else if (s_windOn && windKmh < (float)(REACT_WIND_KMH - REACT_WIND_HYST_KMH))
        s_windOn = false;
    return s_windOn;
}

float wxWindIntensity(float windKmh)
{
    return (isnan(windKmh) || windKmh < 0) ? REACT_TEMP_MIN_K : ramp(windKmh, (float)REACT_WIND_KMH, (float)REACT_WIND_FULL_KMH);
}

// ---- the extras ----
bool wxIsBadAir(int usAqi)
{
#if WX_AIRQ_ENABLED
    if (WX_AIRQ_MASK_AQI <= 0)
        return false;
    if (usAqi < 0)
        return s_airOn;
    if (!s_airOn && usAqi >= WX_AIRQ_MASK_AQI)
        s_airOn = true;
    else if (s_airOn && usAqi < WX_AIRQ_MASK_AQI - WX_AIRQ_HYST_AQI)
        s_airOn = false;
    return s_airOn;
#else
    (void)usAqi;
    return false;
#endif
}

float wxAirIntensity(int usAqi)
{
    if (usAqi < 0)
        return 0.0f;
    float r = (float)(usAqi - WX_AIRQ_MASK_AQI) / (float)(WX_AIRQ_FULL_AQI - WX_AIRQ_MASK_AQI);
    return r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
}

bool wxIsHighUv(float uvIndex)
{
#if WX_UV_SHADES_ENABLED
    if (isnan(uvIndex) || uvIndex < 0)
        return s_uvOn;
    if (!s_uvOn && uvIndex >= (float)WX_UV_SHADES_MIN)
        s_uvOn = true;
    else if (s_uvOn && uvIndex < (float)WX_UV_SHADES_MIN - WX_UV_HYST)
        s_uvOn = false;
    return s_uvOn;
#else
    (void)uvIndex;
    return false;
#endif
}