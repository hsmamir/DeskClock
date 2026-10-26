#!/usr/bin/env python3
"""
make_vlw.py - builds the three smooth (VLW) fonts of the desk clock and the C arrays for them.

Why a script and not the Processing sketch? Create_Smooth_Font (TFT_eSPI/Tools) is a Processing
sketch: it calls createFont(...) and PFont.save(...). This script writes exactly the same file
layout (the "VLW" format TFT_eSPI's loadFont() reads: 24-byte header, 28 bytes per glyph, 8-bit
alpha bitmaps, trailing name strings + smooth flag), but renders the glyphs with FreeType (Pillow)
so it runs headless. The .vlw files it writes load with TFT_eSPI::loadFont() like any other.

Usage:   python3 tools/make_vlw.py [font.otf|ttf]
Output:  tools/out/ClockSm12.vlw, ClockSm18.vlw, ClockSm26.vlw   (the real VLW files)
         src/display_fonts.cpp, include/display_fonts.h           (the same bytes as C arrays + metrics)
         tools/out/preview.png                                    (glyph sheet to eyeball)
"""
import struct, sys, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "out")
FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/crosextra/Carlito-Bold.ttf"
FONT_NAME = os.path.splitext(os.path.basename(FONT))[0]

DIGITS = "0123456789"
SYMBOLS = ":°%,-"                     # colon, degree sign, percent, comma ("Tue, Oct 21"), minus ("-2°")
# Day names (Sunday..Saturday, Sun..Sat), month abbreviations (Jan..Dec), alarm tail ("Tmrw", "6h12", "42m"):
MIXED = ("Sunday Monday Tuesday Wednesday Thursday Friday Saturday "
         "Sun Mon Tue Wed Thu Fri Sat "
         "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec Tmrw h m")
# Month names written in capitals ("OCT", "DEC") on the date page, plus the "C" of the temperature:
UPPER = "JAN FEB MAR APR MAY JUN JUL AUG SEP OCT NOV DEC C"


def charset(text):
    return sorted({ord(c) for c in text if c != " "})


# The big digits of the Simple clock face (rounded, medium-bold, like the design): only 0-9 and ':'.
# Manrope SemiBold, 48 px for the 160 x 128 display, 92 px for the 320 x 240 simulator.
BIG_FONT = os.path.join(HERE, "Manrope-Variable.ttf")   # Manrope (OFL), variable weight: 600 = SemiBold
BIG_WEIGHT = 600
BOLD_WEIGHT = 800   # the stacked hour / minute digits of the Minimal face
BIG_CODES = charset(DIGITS + ":-")
BOLD_CODES = charset(DIGITS + "-")

# key: (name, code points, font file, stroke px). The key is only a unique id (the size is read from the name).
SETS = {
    12: ("ClockSm12", charset(DIGITS + SYMBOLS + MIXED), FONT, 0, None),
    18: ("ClockSm18", charset(DIGITS + SYMBOLS + MIXED), FONT, 0, None),
    26: ("ClockSm26", charset(UPPER), FONT, 0, None),   # only what is drawn at this size: OCT, DEC ... and the "C"
    48: ("ClockBig48", BIG_CODES, BIG_FONT, 0, BIG_WEIGHT),
    92: ("ClockBig92", BIG_CODES, BIG_FONT, 0, BIG_WEIGHT),
    45: ("ClockBold45", BOLD_CODES, BIG_FONT, 0, BOLD_WEIGHT),
    90: ("ClockBold90", BOLD_CODES, BIG_FONT, 0, BOLD_WEIGHT),
}


def load_font(fontfile, size, weight=None):
    font = ImageFont.truetype(fontfile, size, layout_engine=ImageFont.Layout.BASIC)
    if weight:
        font.set_variation_by_axes([weight])   # variable font (Manrope): 600 = SemiBold, 800 = ExtraBold
    return font


def render_glyph(font, ch, stroke=0):
    """-> (width, height, advance, dY, dX, alpha bytes). dY = baseline to top of bitmap (+up)."""
    pad = 4
    asc, desc = font.getmetrics()
    W = int(font.getlength(ch)) + 2 * pad + font.size
    H = asc + desc + 2 * pad
    ox, oy = pad + font.size // 2, pad + asc
    img = Image.new("L", (W, H), 0)
    ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255, anchor="ls", stroke_width=stroke, stroke_fill=255)
    bb = img.getbbox()
    adv = int(round(font.getlength(ch))) + stroke   # the stroke fattens the glyph, so it gets a little more room
    if bb is None:
        return 0, 0, adv, 0, 0, b""
    img = img.crop(bb)
    return img.width, img.height, adv, oy - bb[1], bb[0] - ox, img.tobytes()


def build_vlw(size, codes, fontfile, stroke, fname, weight=None):
    font = load_font(fontfile, size, weight)
    # ascent = top of "d", descent = bottom of "p" (what Processing's PFont stores)
    _, _, _, d_top, _, _ = render_glyph(font, "d")
    pw, ph, _, p_top, _, _ = render_glyph(font, "p")
    ascent, descent = d_top, ph - p_top
    glyphs = [(c,) + render_glyph(font, chr(c), stroke) for c in codes]
    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    for c, w, h, adv, dy, dx, _ in glyphs:
        out += struct.pack(">7i", c, h, w, adv, dy, dx, 0)
    for *_, bmp in glyphs:
        out += bmp
    for s in (fname, fname):                   # Java writeUTF: 2-byte length + bytes
        b = s.encode()
        out += struct.pack(">H", len(b)) + b
    out += b"\x01"                                      # smooth (anti-aliased)
    return bytes(out), glyphs, ascent, descent, render_glyph(font, "0", stroke)[3]   # last = digit/cap height


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    return "const uint8_t %s[] PROGMEM = {\n%s\n};\n" % (name, "\n".join(lines))


def main():
    os.makedirs(OUT, exist_ok=True)
    cpp = ["// display_fonts.cpp - GENERATED by tools/make_vlw.py, do not edit by hand.",
           "// Smooth (VLW) fonts for TFT_eSPI, fonts: %s (small sizes), Manrope SemiBold (ClockBig*). Load with g.loadFont(ClockSm12) ... g.unloadFont()." % FONT_NAME,
           "// Glyphs: digits, ':' '\\xB0' '%' ',', day / month letters (see display_fonts.h for what each size holds).",
           '#include <Arduino.h>', '#include "display_fonts.h"', ""]
    sheet_rows = []
    hdr = ["// display_fonts.h - GENERATED by tools/make_vlw.py, do not edit by hand.",
           "// Smooth (VLW) fonts for TFT_eSPI: load with g.loadFont(ClockSmNN), draw, then g.unloadFont().",
           "// *_CAPH = height of the digits / capital letters in px (to centre text on the baseline).",
           "// Needs -D SMOOTH_FONT=1. Glyphs per font:",
           "//   ClockSm12, ClockSm18: 0-9 : \xB0 % ,  and every letter of Sunday..Saturday, Sun..Sat, Jan..Dec, Tmrw, h, m",
           "//   ClockSm26:            the capitals of JAN..DEC and C (month name on the date page, the C of the temperature)",
           "//   ClockBold45, ClockBold90: only 0-9 and minus, ExtraBold - the stacked hour / minute of the Minimal face",
           "//   ClockBig48, ClockBig92: only 0-9, colon and minus - the big time of the Simple clock face (160 x 128 / 320 x 240 screens)",
           "#pragma once", "#include <Arduino.h>", ""]
    for size, (name, codes, fontfile, stroke, weight) in SETS.items():
        size = int("".join(ch for ch in name if ch.isdigit()))
        data, glyphs, asc, desc, caph = build_vlw(size, codes, fontfile, stroke, os.path.splitext(os.path.basename(fontfile))[0], weight)
        hdr.append("extern const uint8_t %s[]; // %d glyphs, %d bytes" % (name, len(glyphs), len(data)))
        hdr.append("constexpr int16_t %s_CAPH = %d;" % (name.upper(), caph))
        with open(os.path.join(OUT, name + ".vlw"), "wb") as f:
            f.write(data)
        cpp.append("// %s: size %d, %d glyphs, %d bytes" % (name, size, len(glyphs), len(data)))
        cpp.append(c_array(name, data))
        print("%-10s size %2d  %2d glyphs  %5d bytes  ascent %d descent %d" % (name, size, len(glyphs), len(data), asc, desc))
        sheet_rows.append((size, codes, fontfile, stroke, weight))
    with open(os.path.join(ROOT, "src", "display_fonts.cpp"), "w") as f:
        f.write("\n".join(cpp))
    with open(os.path.join(ROOT, "include", "display_fonts.h"), "w") as f:
        f.write("\n".join(hdr) + "\n")

    # preview: each size, 4x enlarged, on the clock's dark background
    rows = []
    for size, codes, fontfile, stroke, weight in sheet_rows:
        font = load_font(fontfile, size, weight)
        text = "".join(chr(c) for c in codes)
        img = Image.new("RGB", (len(text) * size + 20, size * 2), (6, 9, 26))
        ImageDraw.Draw(img).text((8, size // 2), text, font=font, fill=(0, 225, 255), stroke_width=stroke, stroke_fill=(0, 225, 255))
        rows.append(img)
    W = max(r.width for r in rows)
    sheet = Image.new("RGB", (W, sum(r.height for r in rows)), (6, 9, 26))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y)); y += r.height
    sheet.save(os.path.join(OUT, "preview_ref.png"))


if __name__ == "__main__":
    main()
