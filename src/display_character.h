#pragma once
// display_character.h - the face of the desk clock, drawn procedurally (no bitmaps).
//
// The look follows the "emoji tile" design sheet: big round eyes (white of the eye + a dark iris with two
// glossy highlights) inside a soft blue neon glow, lilac blush pills under the eyes, a tiny mouth, thin
// neon brows. Every shape is a signed-distance function that is anti-aliased against whatever is already on
// the canvas, so the glow stays translucent and nothing needs a picture in flash.
//
//   CHARACTER_STYLE      1 = this character (default), 0 = the old glowing LED eyes
//   CHARACTER_THEME_TINT 1 = the glow follows the time-of-day / feeling colour (default),
//                        0 = it always stays the blue of the design
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
    // How the round eyes are drawn.
    enum class CharEyeStyle : uint8_t
    {
        NORMAL, // white + iris + highlights
        STAR    // "excited": a glowing ring with a star inside
    };
    void charSetEyeStyle(CharEyeStyle st); // sticks until changed

    // One eye of the character (white, iris, highlights, glow, blush, brow). Same arguments as drawEyeShape(); the lids,
    // slant, size, brow and gaze of the EyeShape are honoured (lids cut the eye, the iris follows the gaze).
    void drawCharacterEye(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                          uint16_t cTop, uint16_t cBot, bool shine);

    // The mouth shapes of the design sheet. All are small and thin.
    enum class CharMouth : uint8_t
    {
        SMILE,     // the resting mouth: a tiny smile
        SMILE_BIG, // a happier, wider smile (pet reactions)
        DASH,      // sleepy / suspicious / confused: a short dash
        FROWN,     // sad / worried / angry: a small downturned arc
        OPEN_O,    // surprised / hot: a small ring
        WAVE,      // cold: a shivering wave
        CUTE,      // "w" of two small bowls
        THINK      // thinking: a small downturned arc a little to the left
    };
    // The mouth centred on (cx, cy); alpha 0..1.
    void drawCharMouthShape(TFT_eSPI &g, int16_t cx, int16_t cy, CharMouth m, float alpha);
    // The resting mouth (a tiny smile) - kept for the callers that only want "the" mouth.
    void drawCharMouth(TFT_eSPI &g, int16_t cx, int16_t cy, float alpha);
    // Distance from the eye line down to the middle of the mouth, px.
    int16_t charMouthDy();

    // Other faces of the character. R is the glyph radius the old code used (W / 8 = full size), so the
    // "pop" animations (R * pop) keep working. cx / cy = where the old eye would be.
    void charHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R);   // "^" arch (the wink) + blush
    void charContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R); // closed, curving down + blush
    void charHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float pulse); // "Love": a glowing heart outline
    void charSpiralEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase); // phase in rad, may be negative
    // "Cold": the squeezed eye ">" (left) / "<" (right) + blush.
    void charColdEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R);
    // The two wavy shiver marks at the outer sides of the eyes (cold); phase = animation phase in rad.
    void charShiverMarks(TFT_eSPI &g, int16_t cxLeft, int16_t cxRight, int16_t cy, int16_t R, float phase);

    // Small one-colour extras (the firmware colours them): heart, drop, "!", anger mark, sparkle, "?", snowflake, star.
    enum class CharMono : uint8_t
    {
        HEART,
        DROP,
        BANG,
        ANGER,
        SPARK,
        QUESTION,
        SNOW,
        STAR
    };
    // Centred on (cx, cy); k = scale (1 = the base size in px of the design).
    void drawCharMono(TFT_eSPI &g, CharMono kind, int16_t cx, int16_t cy, float k, uint16_t col);
} // namespace disp
