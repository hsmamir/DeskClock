#pragma once
#include <stdint.h>
#include "config.h"

// ONE place that decides what the weather "is", for everyone who cares: the idle eyes (pickReaction), the pet
// reactions (pet.cpp), the persona's little acts (the "oh!" at the first rain, the sneeze), the scene background
// and the weather icon, the labels. Before, five files each had their own copy of the WMO code ranges, and
// they did not agree (snow counted as precipitation in one place and not in another).
//
// Codes are the Open-Meteo WMO codes: 0 clear, 1-2 partly cloudy, 3 overcast, 45/48 fog, 51-57 drizzle,
// 61-67 rain, 71-77 snow, 80-82 rain showers, 85/86 snow showers, 95-99 thunderstorm.

inline bool wxIsFog(int c) { return c == 45 || c == 48; }
inline bool wxIsRain(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82); } // drizzle, rain, rain showers
inline bool wxIsSnow(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }   // snow, snow showers
inline bool wxIsHeavyRain(int c) { return c == 65 || c == 67 || c == 82; }           // heavy rain, freezing rain, violent showers
inline bool wxIsStorm(int c) { return c >= 95; }
inline bool wxIsPrecip(int c) { return wxIsRain(c) || wxIsSnow(c); } // something falls from the sky (storm not counted)
inline bool wxIsWet(int c) { return wxIsPrecip(c) || wxIsStorm(c); } // ... or it thunders

// Hot / cold with a hysteresis (weather_mood.cpp): true from REACT_HOT_C / REACT_COLD_C on, and it stays true until
// the temperature is REACT_TEMP_HYST_C degrees back. NaN = no reading = the answer stays as it was.
bool wxIsHot(float tempC);
bool wxIsCold(float tempC);
// How strong the face should be, REACT_TEMP_MIN_K (just started) .. 1 (REACT_*_FULL_C or beyond). Only meaningful
// while wxIsHot / wxIsCold is true.
float wxHotIntensity(float tempC);
float wxColdIntensity(float tempC);

// Strong wind, same idea (REACT_WIND_KMH / REACT_WIND_FULL_KMH / REACT_WIND_HYST_KMH). windKmh < 0 = unknown = the
// answer stays as it was. wxWindIntensity is REACT_TEMP_MIN_K .. 1, only meaningful while wxIsWindy is true.
bool wxIsWindy(float windKmh);
float wxWindIntensity(float windKmh);

// The extras (display_wx.cpp and the persona ask these, so the mask, the cough, the sunglasses and the sneezes all
// agree with the faces above):
//   wxIsBadAir     US AQI at or above WX_AIRQ_MASK_AQI (hysteresis WX_AIRQ_HYST_AQI). aqi < 0 = unknown = unchanged.
//   wxAirIntensity 0..1: 0 at the mask limit, 1 at WX_AIRQ_FULL_AQI (the cough gets more frequent)
//   wxIsHighUv     UV index at or above WX_UV_SHADES_MIN (hysteresis WX_UV_HYST). uv < 0 = unknown = unchanged.
//   wxIsBright     the sky the display shows is bright enough for sunglasses / a squint: anything but rain, a
//                  thunderstorm or fog (snow counts: it glares)
bool wxIsBadAir(int usAqi);
float wxAirIntensity(int usAqi);
bool wxIsHighUv(float uvIndex);
inline bool wxIsBright(int c) { return !wxIsRain(c) && !wxIsStorm(c) && !wxIsFog(c); }

// What the situation is "about", one winner. Order = priority: a storm beats rain, rain beats the temperature
// (a rainy day at 36 deg is about the rain), the temperature beats snow / fog, and a strong wind is the last one:
// a windy day that is also hot, cold, snowy or foggy is about that, a plain windy day is about the wind.
enum class WxMood : uint8_t
{
    NONE,
    STORM,
    RAIN, // drizzle, rain, rain showers
    HOT,
    COLD,
    SNOW, // snow while it is not cold (when it is, the cold wins)
    FOG,
    WIND // strong wind and nothing above it
};

// windKmh < 0 (the default) = no wind reading: the wind never wins.
inline WxMood weatherMood(bool known, int code, float tempC, float windKmh = -1.0f)
{
    if (!known)
        return WxMood::NONE;
    if (wxIsStorm(code))
        return WxMood::STORM;
    if (wxIsRain(code))
        return WxMood::RAIN;
    if (wxIsHot(tempC))
        return WxMood::HOT;
    if (wxIsCold(tempC))
        return WxMood::COLD;
    if (wxIsSnow(code))
        return WxMood::SNOW;
    if (wxIsFog(code))
        return WxMood::FOG;
    if (REACT_WIND_KMH > 0 && windKmh >= 0 && wxIsWindy(windKmh))
        return WxMood::WIND;
    return WxMood::NONE;
}