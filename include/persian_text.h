#pragma once
// Persian / Arabic text for a display that knows nothing about scripts (TFT_eSPI draws one glyph per code
// point, left to right). Pure C++, no Arduino / TFT dependency, so it is unit-tested on a PC (tools/host).
//
//   1. normalise  Arabic yeh / kaf -> Persian ی ک, Arabic-Indic digits -> Persian ۰-۹, drop diacritics,
//                 tatweel, bidi control characters
//   2. shape      every letter gets its isolated / initial / medial / final shape (as the code point in the
//                 Arabic Presentation Forms blocks, which the Vazirmatn font in display_vazir.cpp has);
//                 lam + alef become one ligature glyph; ZWNJ (نیم‌فاصله) breaks a join and draws nothing
//   3. reorder    right-to-left paragraph, with Latin words and numbers (14:30) kept left-to-right inside it,
//                 brackets and « » mirrored (a simplified Unicode bidi algorithm)
//   4. wrap       greedy word wrap using the real pixel width of a line (the caller measures)
//
// Usage on the display:   fa::wrap(...) once per text, then for every line  fa::visual(...)  and
//                         drawString(visual) with the smooth font loaded.
#include <stddef.h>
#include <stdint.h>

namespace fa
{
    // Paragraph direction for visual() / wrap().
    enum : int
    {
        DIR_AUTO = -1, // right-to-left if the text has any Arabic-script letter, else left-to-right
        DIR_LTR = 0,
        DIR_RTL = 1
    };

    // True if the text has at least one Arabic-script letter.
    bool hasRtl(const char *utf8);

    // Cleans text typed by a person, for storing: invalid UTF-8 is refused, line breaks / tabs / no-break and
    // wide spaces become one space, runs of spaces collapse, the ends are trimmed, control characters, bidi
    // controls, variation selectors and characters outside the Basic Multilingual Plane (emoji, which the
    // font cannot draw anyway) are removed. out is NUL-terminated.
    // Returns the length in bytes, -1 = not valid UTF-8, -2 = result does not fit in cap bytes (incl. NUL).
    int sanitize(const char *in, char *out, size_t cap);

    // One logical line -> what to draw left to right: shaped, reordered, mirrored UTF-8 (NUL-terminated).
    // Returns the number of bytes written (not counting the NUL). Text past ~120 characters is cut.
    size_t visual(const char *utf8, size_t len, char *out, size_t cap, int paraDir = DIR_AUTO);

    // Pixel width of a ready-to-draw (visual) string with the font the caller has loaded.
    typedef int (*MeasureFn)(const char *visualUtf8, void *ctx);

    struct Span
    {
        uint16_t start, len; // byte range of one line in the text that was wrapped
    };

    struct WrapResult
    {
        int lines;      // lines written to out
        bool truncated; // more text was left over: the last line is shortened so that an ellipsis fits after it
    };

    // Breaks text (as returned by sanitize(), single spaces) into at most maxLines lines no wider than maxW
    // pixels. A word wider than a line is cut between letters. When text is left over, the last span is
    // shortened to leave room for "…" - append kEllipsis to that line before drawing it.
    WrapResult wrap(const char *text, size_t len, int maxW, MeasureFn measure, void *ctx, int paraDir,
                    Span *out, int maxLines);

    // ---- picking the font size ----
    // Measures a ready-to-draw string with font number `font` (0 = the largest).
    typedef int (*FontMeasureFn)(int font, const char *visualUtf8, void *ctx);

    struct FitResult
    {
        int font;       // index of the font used
        int lines;      // lines written to spans
        bool truncated; // even the smallest font does not hold it all: the last line needs an ellipsis
    };

    // Tries the fonts from the largest down and takes the first whose wrapped text fits slotsTotal[font] lines
    // (the smallest one is used regardless, truncated). The caller decides from lines vs slotsBottom[font]
    // which layout it needs.
    FitResult fit(const char *text, size_t len, int maxW, int nFonts, const int *slotsTotal, FontMeasureFn measure,
                  void *ctx, Span *spans, int maxSpans);

    extern const char kEllipsis[]; // "…" in UTF-8
} // namespace fa
