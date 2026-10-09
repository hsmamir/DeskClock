#!/usr/bin/env python3
"""vlw_check.py - reads the .vlw files the way TFT_eSPI::loadFont()/drawGlyph() do and renders sample
strings (alpha blend toward the background colour, like setTextColor(fg, bg)). Also prints the pixel
width of every string the clock draws, so you can check it fits the 128 px display."""
import struct, sys, os
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

class Vlw:
    def __init__(self, path):
        d = open(path, "rb").read(); self.d = d
        n, _, size, _, asc, desc = struct.unpack(">6i", d[:24])
        self.asc, self.desc, self.size = asc, desc, size
        self.g, ptr = {}, 24 + 28 * n
        self.maxdesc = desc
        for i in range(n):
            u, h, w, adv, dy, dx, _ = struct.unpack(">7i", d[24 + 28 * i: 52 + 28 * i])
            self.g[u] = (h, w, adv, dy, dx, ptr)
            if u > 0x20 and h - dy > self.maxdesc: self.maxdesc = h - dy
            ptr += w * h
        self.yadv = asc + self.maxdesc
        self.space = (asc + desc) * 2 // 7
        self.missing = set()
    def width(self, s):
        w = 0
        for i, ch in enumerate(s):
            if ch == " ": w += self.space; continue
            g = self.g.get(ord(ch))
            if not g: self.missing.add(ch); w += self.space + 1; continue
            h, gw, adv, dy, dx, _ = g
            if w == 0 and dx < 0: w -= dx
            w += adv if i < len(s) - 1 else dx + gw
        return w
    def draw(self, img, s, x, y, fg, bg):          # (x, y) = top-left of the text box
        cx = x
        for ch in s:
            if ch == " ": cx += self.space; continue
            g = self.g.get(ord(ch))
            if not g: cx += self.space + 1; continue
            h, w, adv, dy, dx, p = g
            for yy in range(h):
                for xx in range(w):
                    a = self.d[p + yy * w + xx]
                    if a:
                        px, py = cx + dx + xx, y + self.asc - dy + yy
                        if 0 <= px < img.width and 0 <= py < img.height:
                            img.putpixel((px, py), tuple((f * a + b * (255 - a)) // 255 for f, b in zip(fg, bg)))
            cx += adv

fonts = {s: Vlw(os.path.join(OUT, "ClockSm%d.vlw" % s)) for s in (12, 18, 26)}
SAMPLES = [(12, "Wednesday"), (12, "Wed 09 Oct"), (12, "Sat 31 Dec"), (12, "07:30"), (12, "6h12"), (12, "42m"),
           (12, "Tmrw"), (12, "100%"), (18, "Wednesday"), (18, "Wed 09 Oct"), (26, "OCT"), (26, "DEC"), (26, "C")]
BG, FG = (6, 9, 26), (0, 225, 255)
Y = 0; rows = []
for size, s in SAMPLES:
    f = fonts[size]; w = f.width(s)
    print("size %2d %-12s width %3d px  height %2d %s" % (size, repr(s), w, f.yadv, "MISSING " + "".join(f.missing) if f.missing else ""))
    rows.append((size, s))
sc = 3
img = Image.new("RGB", (136, sum(fonts[s].yadv + 4 for s, _ in rows)), BG)
y = 2
for size, s in rows:
    fonts[size].draw(img, s, 4, y, FG, BG); y += fonts[size].yadv + 4
img.resize((img.width * sc, img.height * sc), Image.NEAREST).save(os.path.join(OUT, "preview.png"))
