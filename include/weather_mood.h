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
inline bool wxIsStorm(int c) { return c >= 95; }
inline bool wxIsPrecip(int c) { return wxIsRain(c) || wxIsSnow(c); } // something falls from the sky (storm not counted)
inline bool wxIsWet(int c) { return wxIsPrecip(c) || wxIsStorm(c); } // ... or it thunders

inline bool wxIsHot(float tempC) { return tempC >= REACT_HOT_C; }   // false for NaN
inline bool wxIsCold(float tempC) { return tempC <= REACT_COLD_C; } // false for NaN

// What the situation is "about", one winner. Order = priority: a storm beats rain, rain beats the temperature
// (a rainy day at 36 deg is about the rain), the temperature beats snow / fog.
// SNOW and FOG have a kind of their own now, but no face of their own yet: the callers map them to "nothing special".
enum class WxMood : uint8_t
{
    NONE,
    STORM,
    RAIN, // drizzle, rain, rain showers
    HOT,
    COLD,
    SNOW, // snow above REACT_COLD_C (colder than that it is COLD, as before)
    FOG
};

inline WxMood weatherMood(bool known, int code, float tempC)
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
    return WxMood::NONE;
}