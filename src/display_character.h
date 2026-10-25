#pragma once
// display_character.h - the "clock" character: round ring eyes with a sparkle, bell-like brows, blush
// and a small dash for a mouth (pixel art, see tools/make_character.py and include/character_art.h).
//
//   CHARACTER_STYLE      1 = the clock character (default), 0 = the old glowing LED eyes
//   CHARACTER_THEME_TINT 1 = the ring's blue glow follows the time-of-day / feeling colour (default),
//                        0 = it always stays the blue of the artwork
// Override with -D CHARACTER_STYLE=0 in platformio.ini build_flags.
#include "display_internal.h"

#ifndef CHARACTER_STYLE
#define CHARACTER_STYLE 1
#endif
#ifndef CHARACTER_THEME_TINT
#define CHARACTER_THEME_TINT 1
#endif

namespace disp
{
    // One eye of the character (ring, sparkle, brow, blush). Same arguments as drawEyeShape(); the lids,
    // slant, size and gaze of the EyeShape are honoured (lids cut the eye, the sparkle follows the gaze).
    void drawCharacterEye(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                          uint16_t cTop, uint16_t cBot, bool shine);
    // The neutral mouth (a small dash) centred on (cx, cy); alpha 0..1.
    void drawCharMouth(TFT_eSPI &g, int16_t cx, int16_t cy, float alpha);
    // Distance from the eye line down to the middle of the mouth, px.
    int16_t charMouthDy();

    // Other faces of the character. R is the glyph radius the old code used (W / 8 = full size), so the
    // "pop" animations (R * pop) keep working. cx / cy = where the old eye would be.
    void charHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R);   // "^" + blush + brow
    void charContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R); // closed, curving down + blush + brow
    void charHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float pulse);
    void charSpiralEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase); // phase in rad, may be negative

    // Small one-colour extras (the firmware colours them): heart, drop, "!", anger mark, sparkle.
    enum class CharMono : uint8_t
    {
        HEART,
        DROP,
        BANG,
        ANGER,
        SPARK
    };
    // Centred on (cx, cy); k = scale (1 = the base size in px of the artwork).
    void drawCharMono(TFT_eSPI &g, CharMono kind, int16_t cx, int16_t cy, float k, uint16_t col);
} // namespace disp