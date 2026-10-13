#!/usr/bin/env python3
"""
make_vazir.py - builds the Persian (Vazirmatn) smooth fonts of the reminder screen.

What it writes (all generated, do not edit by hand):
    src/display_vazir.cpp      the fonts as C arrays (TFT_eSPI "VLW" format, same layout make_vlw.py writes)
    include/display_vazir.h    declarations + pixel sizes
    include/persian_forms.h    the joining table used by src/persian_text.cpp (initial / medial / final forms)
    tools/out/vazir_glyphs.png contact sheet of every glyph, to eyeball

Why presentation forms?  TFT_eSPI looks glyphs up by Unicode code point and has no text shaping. A Persian
letter changes shape depending on its neighbours (isolated / initial / medial / final), so persian_text.cpp
picks the right shape and asks for the matching code point in the Arabic Presentation Forms blocks
(U+FB50..FDFF, U+FE70..FEFF). Vazirmatn contains those code points, so the font needs no extra tricks.

Usage:   pip install pillow fonttools
         python3 tools/make_vazir.py /path/to/Vazirmatn-Regular.ttf [14 18 22]
Font:    https://github.com/rastikerdar/vazirmatn  (SIL Open Font License 1.1, free to embed)
"""
import os
import struct
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "out")

FONT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "Vazirmatn-Regular.ttf")
SIZES = [int(s) for s in sys.argv[2:]] or [14, 18, 22]

# Persian letters + the Arabic ones people also type. (U+06C0 = heh with yeh above, "خانۀ".)
LETTERS = "ءآأؤإئابةتثجحخدذرزسشصضطظعغفقكلمنهوىيپچژکگیۀ"

# Everything else the screen may need, as plain code points.
ASCII = [c for c in range(0x20, 0x7F)]
PERSIAN_DIGITS = [0x6F0 + i for i in range(10)]
PUNCT = [0x060C, 0x061B, 0x061F, 0x066A, 0x066B, 0x066C,     # ، ؛ ؟ ٪ ٫ ٬
         0x00AB, 0x00BB, 0x2026, 0x2013, 0x2014,              # « » … – —
         0x2018, 0x2019, 0x201C, 0x201D, 0x00B0, 0x00D7, 0x00B7, 0x2022]


def collect_forms():
    """base letter -> {'isolated','final','initial','medial'} from the Unicode data."""
    forms = {}
    for cp in list(range(0xFB50, 0xFE00)) + list(range(0xFE70, 0xFF00)):
        d = unicodedata.decomposition(chr(cp))
        if not d.startswith("<"):
            continue
        tag, rest = d.split(" ", 1)
        parts = rest.split()
        if len(parts) != 1:
            continue
        forms.setdefault(int(parts[0], 16), {})[tag.strip("<>")] = cp
    return forms


def collect_lam_alef():
    """alef variant -> (isolated, final) of the lam-alef ligature."""
    out = {}
    for cp in range(0xFEF5, 0xFEFD):
        d = unicodedata.decomposition(chr(cp))
        tag, rest = d.split(" ", 1)
        lam, alef = (int(x, 16) for x in rest.split())
        assert lam == 0x644
        out.setdefault(alef, {})[tag.strip("<>")] = cp
    return out


def render_glyph(font, ch):
    """-> (width, height, advance, dY, dX, alpha bytes). dY = baseline to top of bitmap (+up)."""
    pad = 4
    asc, desc = font.getmetrics()
    W = int(font.getlength(ch)) + 2 * pad + font.size
    H = asc + desc + 2 * pad + font.size // 2
    ox, oy = pad + font.size // 2, pad + asc + font.size // 4
    img = Image.new("L", (W, H), 0)
    ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255, anchor="ls")
    bb = img.getbbox()
    adv = int(round(font.getlength(ch)))
    if bb is None:
        return 0, 0, adv, 0, 0, b""
    img = img.crop(bb)
    return img.width, img.height, adv, oy - bb[1], bb[0] - ox, img.tobytes()


def build_vlw(font_path, size, codes, name):
    font = ImageFont.truetype(font_path, size, layout_engine=ImageFont.Layout.BASIC)
    _, _, _, d_top, _, _ = render_glyph(font, "d")
    pw, ph, _, p_top, _, _ = render_glyph(font, "p")
    ascent, descent = d_top, ph - p_top
    glyphs = [(c,) + render_glyph(font, chr(c)) for c in codes]
    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    for c, w, h, adv, dy, dx, _ in glyphs:
        out += struct.pack(">7i", c, h, w, adv, dy, dx, 0)
    for *_, bmp in glyphs:
        out += bmp
    for s in (name, name):
        b = s.encode()
        out += struct.pack(">H", len(b)) + b
    out += b"\x01"  # smooth (anti-aliased)
    return bytes(out), glyphs


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    return "const uint8_t %s[] PROGMEM = {\n%s\n};\n" % (name, "\n".join(lines))


def main():
    os.makedirs(OUT, exist_ok=True)
    cmap = TTFont(FONT).getBestCmap()
    forms = collect_forms()
    lam_alef = collect_lam_alef()

    # ---- the joining table + the glyph set it needs ----
    rows = []
    codes = set(ASCII + PERSIAN_DIGITS + PUNCT)
    for ch in LETTERS:
        b = ord(ch)
        f = forms.get(b, {})
        row = (b, f.get("isolated", 0), f.get("final", 0), f.get("initial", 0), f.get("medial", 0))
        if row[1] and row[1] not in cmap and row[2] in cmap:
            row = (b, row[2]) + row[2:]  # the font has no isolated shape (U+06C0): use the final one
        rows.append(row)
        codes.update(c for c in row[1:] if c)
    rows.sort()
    lig = []
    for alef in sorted(lam_alef):
        iso, fin = lam_alef[alef]["isolated"], lam_alef[alef]["final"]
        lig.append((alef, iso, fin))
        codes.update((iso, fin))
    missing = sorted(c for c in codes if c not in cmap)
    if missing:
        print("WARNING: not in the font, left out:", " ".join("U+%04X" % c for c in missing))
    codes = sorted(c for c in codes if c in cmap)

    h = ["// persian_forms.h - GENERATED by tools/make_vazir.py, do not edit by hand.",
         "// Joining table of the Persian / Arabic letters: base code point -> the code points of its four shapes",
         "// (0 = the letter has no such shape). Letters with an initial shape join to the NEXT letter (dual joining);",
         "// letters with only isolated / final shapes (ا د ذ ر ز ژ و ...) join to the PREVIOUS letter only.",
         "#pragma once",
         "#include <stdint.h>", "",
         "namespace fa",
         "{",
         "    struct Forms",
         "    {",
         "        uint16_t base, iso, fin, ini, med;",
         "    };",
         "    static const Forms FORMS[] = {"]
    for r in rows:
        h.append("        {0x%04X, 0x%04X, 0x%04X, 0x%04X, 0x%04X}, // %s" % (r + (chr(r[0]),)))
    h += ["    };",
          "    static const unsigned FORMS_N = sizeof(FORMS) / sizeof(FORMS[0]);",
          "",
          "    // lam + alef variant -> one ligature glyph (isolated / final)",
          "    struct Lig",
          "    {",
          "        uint16_t alef, iso, fin;",
          "    };",
          "    static const Lig LIGS[] = {"]
    for a, i, f in lig:
        h.append("        {0x%04X, 0x%04X, 0x%04X}, // lam + %s" % (a, i, f, chr(a)))
    h += ["    };",
          "    static const unsigned LIGS_N = sizeof(LIGS) / sizeof(LIGS[0]);",
          "} // namespace fa", ""]
    with open(os.path.join(ROOT, "include", "persian_forms.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(h))

    # ---- the fonts ----
    cpp = ["// display_vazir.cpp - GENERATED by tools/make_vazir.py, do not edit by hand.",
           "// Vazirmatn (SIL Open Font License 1.1, https://github.com/rastikerdar/vazirmatn) rendered to TFT_eSPI smooth",
           "// (VLW) fonts: ASCII, Persian digits, punctuation and every shape of the Persian / Arabic letters",
           "// (Arabic Presentation Forms, see persian_forms.h). Load with g.loadFont(Vazir14) ... g.unloadFont().",
           "#include <Arduino.h>", '#include "display_vazir.h"', ""]
    hdr = ["// display_vazir.h - GENERATED by tools/make_vazir.py, do not edit by hand.",
           "// Persian smooth (VLW) fonts: load with g.loadFont(VazirNN), draw, then g.unloadFont(). Needs -D SMOOTH_FONT=1.",
           "// Glyphs: %d (ASCII, Persian digits, punctuation, all shapes of the Persian letters). The *_PX number is the" % len(codes),
           "// font size in px; *_PITCH is a good distance between two text lines of that size.",
           "#pragma once", "#include <Arduino.h>", ""]
    sheet = []
    total = 0
    for size in SIZES:
        name = "Vazir%d" % size
        data, glyphs = build_vlw(FONT, size, codes, os.path.splitext(os.path.basename(FONT))[0])
        total += len(data)
        hdr.append("extern const uint8_t %s[]; // %d glyphs, %d bytes" % (name, len(glyphs), len(data)))
        hdr.append("constexpr int16_t %s_PX = %d;" % (name.upper(), size))
        hdr.append("constexpr int16_t %s_PITCH = %d;" % (name.upper(), int(round(size * 1.28))))
        with open(os.path.join(OUT, name + ".vlw"), "wb") as f:
            f.write(data)
        cpp.append("// %s: size %d, %d glyphs, %d bytes" % (name, size, len(glyphs), len(data)))
        cpp.append(c_array(name, data))
        print("%-8s size %2d  %3d glyphs  %6d bytes" % (name, size, len(glyphs), len(data)))
        sheet.append((size, glyphs))
    with open(os.path.join(ROOT, "src", "display_vazir.cpp"), "w") as f:
        f.write("\n".join(cpp))
    with open(os.path.join(ROOT, "include", "display_vazir.h"), "w") as f:
        f.write("\n".join(hdr) + "\n")
    print("flash for the fonts: %d bytes" % total)

    # ---- contact sheet of the middle size ----
    size, glyphs = sheet[len(sheet) // 2]
    cols, cell = 20, size * 2
    rows_n = (len(glyphs) + cols - 1) // cols
    img = Image.new("RGB", (cols * cell, rows_n * cell), (6, 9, 26))
    for i, (c, w, hh, adv, dy, dx, bmp) in enumerate(glyphs):
        if not w:
            continue
        g = Image.frombytes("L", (w, hh), bmp)
        x0 = (i % cols) * cell + (cell - w) // 2
        y0 = (i // cols) * cell + cell // 2 - dy + size // 3
        img.paste((255, 255, 255), (x0, y0), g)
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(OUT, "vazir_glyphs.png"))


if __name__ == "__main__":
    main()
