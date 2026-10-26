// display_character.cpp - draws the face of the desk clock procedurally (see display_character.h).
//
// Nothing here is a bitmap. Every part is a shape described by a signed distance (negative inside, positive
// outside, in px); the distance gives the anti-aliased edge AND the soft glow around it. Each pixel is
// composed in floating point from what is already on the canvas (readPixel of the face sprite; on the plain
// TFT, which cannot be read back, the background colour), so the glow stays translucent over the background.
//
// The lids of the eye are the same maths as always (EyeShape: lidTop / slant / lidBot), so blink, yawn, sad,
// angry, sleepy and all the other presets still work; they now cut the white of the eye with a straight lid
// line, as on the design sheet.
//
// C++11 only (the ESP32 Arduino core compiles with gnu++11): no generic lambdas, no std::clamp.
#include "display_character.h"

namespace disp
{
    namespace
    {
        // ---------------------------------------------------------------------------------------------
        //  Design constants (px at the 128 px display, scaled by sc)
        // ---------------------------------------------------------------------------------------------
        const float CH_DX = 28.5f;       // the eye centres are this far from the middle of the screen
        const float CH_MOUTH_DY = 23.0f; // eye line -> middle of the mouth
        const float GLOW_R = 8.0f;       // reach of the glow around the eyes
        const float BAND_W = 2.2f;       // thickness of the lid line

        struct Col
        {
            float r, g, b;
        };
        struct Tint
        {
            int r, g, b;
        };
        struct Pt
        {
            float x, y;
        };

        const Col C_WHITE_EYE = {246.0f, 249.0f, 255.0f};
        const Col C_LINE = {236.0f, 243.0f, 255.0f}; // thin neon strokes (mouth, brows, arches)
        const Col C_NAVY = {9.0f, 11.0f, 52.0f};
        const Col C_ROYAL = {50.0f, 88.0f, 232.0f};
        const Col C_BLUE = {55.0f, 100.0f, 255.0f}; // the glow of the design at night
        const Col C_BLUSHC = {229.0f, 140.0f, 238.0f};

        CharEyeStyle gStyle = CharEyeStyle::NORMAL;

        inline float sqr(float v) { return v * v; }
        inline float c01(float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); }
        inline float cov(float d) { return c01(0.5f - d); } // coverage of a pixel whose centre is d px inside the edge

        inline Col mk(uint16_t c)
        {
            Col o;
            o.r = ((c >> 11) & 0x1F) * (255.0f / 31.0f);
            o.g = ((c >> 5) & 0x3F) * (255.0f / 63.0f);
            o.b = (c & 0x1F) * (255.0f / 31.0f);
            return o;
        }
        inline uint16_t packCol(const Col &c)
        {
            int r = (int)(c.r + 0.5f), g = (int)(c.g + 0.5f), b = (int)(c.b + 0.5f);
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        inline void over(Col &d, const Col &s, float a)
        {
            if (a <= 0.0f)
                return;
            if (a > 1.0f)
                a = 1.0f;
            d.r += (s.r - d.r) * a;
            d.g += (s.g - d.g) * a;
            d.b += (s.b - d.b) * a;
        }
        inline Col mix2(const Col &a, const Col &b, float t)
        {
            Col o = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
            return o;
        }

        // How far the theme moves the blue of the design: (current eye colour) - (night eye colour).
        // At night this is exactly zero, so the character looks like the design sheet.
        Tint themeShift(uint16_t cBot)
        {
#if CHARACTER_THEME_TINT
            const Col n = mk(rgb(0, 140, 235)); // TH_NIGHT.eyeBot (display_theme.cpp)
            const Col c = mk(cBot);
            Tint t = {(int)(c.r - n.r), (int)(c.g - n.g), (int)(c.b - n.b)};
            return t;
#else
            (void)cBot;
            Tint t = {0, 0, 0};
            return t;
#endif
        }
        // A colour of the design moved by the theme (w = how much of the shift it takes).
        inline Col shifted(const Col &c, const Tint &t, float w)
        {
            Col o = {c.r + t.r * w, c.g + t.g * w, c.b + t.b * w};
            o.r = o.r < 0 ? 0 : (o.r > 255 ? 255 : o.r);
            o.g = o.g < 0 ? 0 : (o.g > 255 ? 255 : o.g);
            o.b = o.b < 0 ? 0 : (o.b > 255 ? 255 : o.b);
            return o;
        }

        TFT_eSprite *readable(TFT_eSPI &g)
        {
            if (faceCv.ok && &g == static_cast<TFT_eSPI *>(&faceCv.spr))
                return &faceCv.spr;
            return nullptr;
        }

        // The surface we compose on.
        struct Surf
        {
            TFT_eSPI &g;
            TFT_eSprite *rd;
            Col at(int x, int y) const { return mk(rd ? rd->readPixel(x, y) : C_BG); }
            void put(int x, int y, const Col &c) const { g.drawPixel(x, y, packCol(c)); }
        };

        // ---------------------------------------------------------------------------------------------
        //  Signed distances
        // ---------------------------------------------------------------------------------------------
        // Ellipse, first-order approximation (exact on the axes, good everywhere else).
        inline float sdEll(float x, float y, float a, float b)
        {
            float ia = 1.0f / a, ib = 1.0f / b;
            float k0 = sqrtf(sqr(x * ia) + sqr(y * ib));
            float k1 = sqrtf(sqr(x * ia * ia) + sqr(y * ib * ib));
            if (k1 < 1e-6f)
                return -fminf(a, b);
            return k0 * (k0 - 1.0f) / k1;
        }
        inline float sdSeg(float px, float py, float ax, float ay, float bx, float by)
        {
            float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
            float l2 = bax * bax + bay * bay;
            float h = l2 > 0 ? c01((pax * bax + pay * bay) / l2) : 0.0f;
            return sqrtf(sqr(pax - bax * h) + sqr(pay - bay * h));
        }
        // Horizontal pill (capsule): half width hw, half height hh.
        inline float sdPill(float x, float y, float hw, float hh)
        {
            float dx = fabsf(x) - (hw - hh);
            if (dx < 0)
                dx = 0;
            return sqrtf(dx * dx + y * y) - hh;
        }
        // Heart (after Inigo Quilez): tip at (0,0), y up, lobes' outer edge at x = +-0.604, top at y = 1.104.
        inline float sdHeart(float x, float y)
        {
            x = fabsf(x);
            if (y + x > 1.0f)
                return sqrtf(sqr(x - 0.25f) + sqr(y - 0.75f)) - 0.35355339f;
            float a = sqr(x) + sqr(y - 1.0f);
            float m = 0.5f * fmaxf(x + y, 0.0f);
            float b = sqr(x - m) + sqr(y - m);
            return sqrtf(fminf(a, b)) * ((x - y) < 0 ? -1.0f : 1.0f);
        }
        // Regular 5-point star of radius r, inner ratio rf (after Inigo Quilez). x right, y up.
        inline float sdStar5(float x, float y, float r, float rf)
        {
            const float k1x = 0.809016994f, k1y = -0.587785252f, k2x = -0.809016994f, k2y = -0.587785252f;
            x = fabsf(x);
            float d1 = k1x * x + k1y * y;
            if (d1 > 0)
            {
                x -= 2.0f * d1 * k1x;
                y -= 2.0f * d1 * k1y;
            }
            float d2 = k2x * x + k2y * y;
            if (d2 > 0)
            {
                x -= 2.0f * d2 * k2x;
                y -= 2.0f * d2 * k2y;
            }
            x = fabsf(x);
            y -= r;
            float bax = rf * (-k1y) - 0.0f, bay = rf * k1x - 1.0f;
            float h = c01((x * bax + y * bay) / (bax * bax + bay * bay));
            h *= r;
            float dx = x - bax * h, dy = y - bay * h;
            float s = (y * bax - x * bay) < 0 ? -1.0f : 1.0f;
            return sqrtf(dx * dx + dy * dy) * s;
        }
        // Polygon (after Inigo Quilez), v = x0,y0,x1,y1 ...
        float sdPoly(float px, float py, const float *v, int n)
        {
            float d = sqr(px - v[0]) + sqr(py - v[1]);
            float s = 1.0f;
            for (int i = 0, j = n - 1; i < n; j = i, i++)
            {
                float ex = v[2 * j] - v[2 * i], ey = v[2 * j + 1] - v[2 * i + 1];
                float wx = px - v[2 * i], wy = py - v[2 * i + 1];
                float h = c01((wx * ex + wy * ey) / (ex * ex + ey * ey));
                float bx = wx - ex * h, by = wy - ey * h;
                float dd = bx * bx + by * by;
                if (dd < d)
                    d = dd;
                bool c1 = py >= v[2 * i + 1], c2 = py < v[2 * j + 1], c3 = ex * wy > ey * wx;
                if ((c1 && c2 && c3) || (!c1 && !c2 && !c3))
                    s = -s;
            }
            return s * sqrtf(d);
        }

        // ---------------------------------------------------------------------------------------------
        //  Painting helpers
        // ---------------------------------------------------------------------------------------------
        struct Paint
        {
            Col fill;
            float alpha;
            Col glow;
            float glowR, glowA; // glowR = 0: no glow
        };

        // Fills the shape sd(x, y) < 0 inside the box, with an optional glow outside. SD is a functor.
        template <class SD>
        void paintShape(const Surf &S, float bx0, float by0, float bx1, float by1, const SD &sd, const Paint &P)
        {
            const int x0 = (int)floorf(bx0 - P.glowR - 1), x1 = (int)ceilf(bx1 + P.glowR + 1);
            const int y0 = (int)floorf(by0 - P.glowR - 1), y1 = (int)ceilf(by1 + P.glowR + 1);
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    float d = sd(x + 0.5f, y + 0.5f);
                    float ga = 0, fa = cov(d) * P.alpha;
                    if (P.glowR > 0 && d > -0.5f && d < P.glowR)
                        ga = P.glowA * P.alpha * sqr(1.0f - c01(d / P.glowR));
                    if (ga < 0.01f && fa < 0.01f)
                        continue;
                    Col c = S.at(x, y);
                    over(c, P.glow, ga);
                    over(c, P.fill, fa);
                    S.put(x, y, c);
                }
        }

        // A stroke along a polyline (round caps), wd px wide, with a glow.
        void strokePoly(const Surf &S, const Pt *p, int n, float wd, const Col &core, const Col &glow, float glowR,
                        float glowA, float alpha)
        {
            if (n < 2 || alpha < 0.02f)
                return;
            float mnx = p[0].x, mxx = p[0].x, mny = p[0].y, mxy = p[0].y;
            for (int i = 1; i < n; i++)
            {
                mnx = fminf(mnx, p[i].x);
                mxx = fmaxf(mxx, p[i].x);
                mny = fminf(mny, p[i].y);
                mxy = fmaxf(mxy, p[i].y);
            }
            const float ext = wd * 0.5f + glowR + 1.0f;
            const int x0 = (int)floorf(mnx - ext), x1 = (int)ceilf(mxx + ext);
            const int y0 = (int)floorf(mny - ext), y1 = (int)ceilf(mxy + ext);
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    const float px = x + 0.5f, py = y + 0.5f;
                    float dm = 1e9f;
                    for (int i = 0; i + 1 < n; i++)
                    {
                        float ax = p[i].x, ay = p[i].y, bx = p[i + 1].x, by = p[i + 1].y;
                        float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
                        float l2 = bax * bax + bay * bay;
                        float h = l2 > 0 ? c01((pax * bax + pay * bay) / l2) : 0.0f;
                        float qx = pax - bax * h, qy = pay - bay * h;
                        float q2 = qx * qx + qy * qy;
                        if (q2 < dm)
                            dm = q2;
                    }
                    float d = sqrtf(dm) - wd * 0.5f;
                    if (d > glowR)
                        continue;
                    float fa = cov(d) * alpha;
                    float ga = (glowR > 0 && d > -0.5f) ? glowA * alpha * sqr(1.0f - c01(d / glowR)) : 0.0f;
                    if (fa < 0.01f && ga < 0.01f)
                        continue;
                    Col c = S.at(x, y);
                    over(c, glow, ga);
                    over(c, core, fa);
                    S.put(x, y, c);
                }
        }

        // ---------------------------------------------------------------------------------------------
        //  Shape functors
        // ---------------------------------------------------------------------------------------------
        struct PillSD
        {
            float cx, cy, hw, hh;
            float operator()(float x, float y) const { return sdPill(x - cx, y - cy, hw, hh); }
        };
        struct HeartSD
        {
            float cx, cy, S, inset;
            float operator()(float x, float y) const
            {
                return sdHeart((x - cx) / S, -(y - cy) / S + 0.552f) * S + inset;
            }
        };
        struct StarSD
        {
            float cx, cy, r, rf, inset;
            float operator()(float x, float y) const { return sdStar5(x - cx, -(y - cy), r, rf) + inset; }
        };
        struct EllSD
        {
            float cx, cy, a, b, inset;
            float operator()(float x, float y) const { return sdEll(x - cx, y - cy, a, b) + inset; }
        };
        struct RingSD // a ring: |distance to the ellipse edge| - half thickness
        {
            float cx, cy, a, b, half;
            float operator()(float x, float y) const { return fabsf(sdEll(x - cx, y - cy, a, b)) - half; }
        };
        struct CircleSD
        {
            float cx, cy, r;
            float operator()(float x, float y) const { return sqrtf(sqr(x - cx) + sqr(y - cy)) - r; }
        };
        struct PolyUnionCircleSD // drop: a circle (centre cy + k, radius 3k) and the triangle of its tangents up to the tip
        {
            float cx, cy, k;
            float operator()(float x, float y) const
            {
                const float ty = cy + k - 1.125f * k; // the tangent points
                float v[6] = {cx, cy - 7.0f * k, cx - 2.78f * k, ty, cx + 2.78f * k, ty};
                float dp = sdPoly(x, y, v, 3);
                float dc = sqrtf(sqr(x - cx) + sqr(y - (cy + k))) - 3.0f * k;
                return dp < dc ? dp : dc;
            }
        };
        struct BangSD
        {
            float cx, cy, k;
            float operator()(float x, float y) const
            {
                float db = sdSeg(x, y, cx, cy - 7.4f * k, cx, cy + 2.2f * k) - 1.9f * k;
                float dd = sqrtf(sqr(x - cx) + sqr(y - (cy + 6.4f * k))) - 2.0f * k;
                return db < dd ? db : dd;
            }
        };
        struct PolySD
        {
            const float *v;
            int n;
            float cx, cy;
            float operator()(float x, float y) const { return sdPoly(x - cx, y - cy, v, n); }
        };

        // ---------------------------------------------------------------------------------------------
        //  Small decorations
        // ---------------------------------------------------------------------------------------------
        // The lilac blush pill under an eye. (ex, ey) = eye centre, rx / ry = its half size.
        void drawBlush(const Surf &S, float ex, float ey, float rx, float ry, int8_t side, float k, float gazeX)
        {
            PillSD sd = {ex + side * 0.40f * rx + 0.12f * gazeX, ey + ry + 1.6f * sc * k, 8.6f * sc * k, 3.5f * sc * k};
            Paint P = {C_BLUSHC, 0.92f, C_BLUSHC, 0.0f, 0.0f};
            if (cheekColor != C_CHEEK) // a pet reaction asked for another blush colour (warm in heat, pale blue in cold)
                P.fill = mk(cheekColor);
            paintShape(S, sd.cx - sd.hw, sd.cy - sd.hh, sd.cx + sd.hw, sd.cy + sd.hh, sd, P);
        }

        // A neon brow: a gently curved stroke. (ex, ey) = the middle of its resting place. t = +1 is the outer end.
        // tilt = how much lower the outer end is than the inner one (px, negative = outer end up), bulge = arch height.
        void drawBrow(const Surf &S, const Tint &sh, float ex, float by, float halfLen, int8_t side, float tilt,
                      float bulge, float thick, float alpha)
        {
            Pt p[7];
            for (int i = 0; i < 7; i++)
            {
                float t = i / 3.0f - 1.0f;
                p[i].x = ex + side * t * halfLen;
                p[i].y = by + tilt * t * 0.5f - bulge * (1.0f - t * t);
            }
            strokePoly(S, p, 7, thick, C_LINE, shifted(C_BLUE, sh, 1.0f), 4.0f * sc, 0.30f, alpha);
        }

        // The arch "^" (wink / happy, bulging up) or the arc "u" (contented, bulging down) drawn as one neon stroke.
        void drawArcGlyph(const Surf &S, const Tint &sh, float ex, float ey, float Rg, bool up)
        {
            const float k = Rg / 20.0f;
            const float hw = 0.74f * Rg, hgt = 0.44f * Rg;
            Pt p[11];
            for (int i = 0; i < 11; i++)
            {
                float t = i / 5.0f - 1.0f;
                float arc = sqrtf(fmaxf(0.0f, 1.0f - t * t)); // half ellipse
                p[i].x = ex + hw * t;
                p[i].y = up ? ey + 0.36f * hgt - hgt * arc : ey - 0.36f * hgt + hgt * arc;
            }
            strokePoly(S, p, 11, 3.4f * sc * k, C_LINE, shifted(C_BLUE, sh, 1.0f), 5.0f * sc * k + 1.0f, 0.42f, 1.0f);
        }

        // ---------------------------------------------------------------------------------------------
        //  The eye
        // ---------------------------------------------------------------------------------------------
        // The "excited" eye: a glowing ring with a star in it.
        void drawStarEye(const Surf &S, const Tint &sh, float ex, float ey, float rx, float ry)
        {
            const Col glow = shifted(C_BLUE, sh, 1.0f);
            Paint ring = {C_WHITE_EYE, 1.0f, glow, GLOW_R * sc, 0.55f};
            EllSD outer = {ex, ey, rx, ry, 0};
            paintShape(S, ex - rx, ey - ry, ex + rx, ey + ry, outer, ring);
            Paint in = {C_NAVY, 1.0f, C_NAVY, 0.0f, 0.0f};
            EllSD inner = {ex, ey, rx, ry, 3.2f * sc}; // shrink by the ring thickness
            paintShape(S, ex - rx, ey - ry, ex + rx, ey + ry, inner, in);
            Paint star = {C_WHITE_EYE, 1.0f, glow, 0.0f, 0.0f};
            StarSD ssd = {ex, ey + 0.5f * sc, fminf(rx, ry) * 0.60f, 0.46f, 0.0f};
            paintShape(S, ex - rx, ey - ry, ex + rx, ey + ry, ssd, star);
        }

        // The round eye with the lids of the EyeShape.
        void drawRoundEye(const Surf &S, const Tint &sh, float ex, float ey, float rx, float ry, float ryFull,
                          const EyeShape &s, int8_t side)
        {
            const Col glow = shifted(C_BLUE, sh, 1.0f);
            const float gR = GLOW_R * sc;

            // lids: the same maths as eyeSpan(), on the ellipse's box
            const float lt = c01(s.lidTop), lb = c01(s.lidBot);
            const float dIn = c01(lt + s.slant * 0.5f), dOut = c01(lt - s.slant * 0.5f);
            const bool hasLid = dIn > 0.01f || dOut > 0.01f;
            float lidY0 = 0, lidM = 0, lidInv = 1;
            if (hasLid)
            {
                float yo = -ry + dOut * 2.0f * ry; // lid line at the outer edge x = side * rx
                float yi = -ry + dIn * 2.0f * ry;  // ... at the inner edge x = -side * rx
                lidM = (yi - yo) / (-side * 2.0f * rx);
                lidY0 = yo - lidM * (side * rx);
                lidInv = 1.0f / sqrtf(1.0f + lidM * lidM);
            }
            const bool hasHole = lb > 0.01f;
            const float holeA = rx + 3.0f * sc, holeB = fmaxf(1.0f, lb * 2.0f * ry);
            const float bandW = BAND_W * sc;

            // the iris
            const float irx = rx * 0.62f, iry = fminf(ry, ryFull) * 0.60f;
            float ix = 1.25f * s.x, iy = 1.25f * s.y;
            const float mx = fmaxf(0.0f, (rx - irx) * 0.85f), my = fmaxf(0.0f, (ry - iry) * 0.85f);
            ix = ix < -mx ? -mx : (ix > mx ? mx : ix);
            iy = iy < -my ? -my : (iy > my ? my : iy);
            iy += 0.05f * ry;
            const float h1x = ix - 0.32f * irx, h1y = iy - 0.36f * iry, h1r = fmaxf(1.2f, 0.29f * irx);
            const float h2x = ix + 0.36f * irx, h2y = iy + 0.40f * iry, h2r = fmaxf(0.8f, 0.13f * irx);
            const Col irisBot = shifted(C_ROYAL, sh, 0.8f);

            const int x0 = (int)floorf(ex - rx - gR - 1), x1 = (int)ceilf(ex + rx + gR + 1);
            const int y0 = (int)floorf(ey - ry - gR - 1), y1 = (int)ceilf(ey + ry + gR + 1);
            const float qx = rx + gR + 1.0f, qy = ry + gR + 1.0f;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    const float px = x + 0.5f - ex, py = y + 0.5f - ey;
                    if (sqr(px / qx) + sqr(py / qy) > 1.0f)
                        continue;
                    const float dE = sdEll(px, py, rx, ry);
                    if (dE > gR)
                        continue;
                    float dTop = -1e3f, dAll = dE;
                    if (hasLid)
                    {
                        dTop = (lidY0 + lidM * px - py) * lidInv; // > 0: above the lid line (cut)
                        float db = dTop - bandW;
                        if (db > dAll)
                            dAll = db;
                    }
                    if (hasHole)
                    {
                        float dh = -sdEll(px, py - ry, holeA, holeB);
                        if (dh > dAll)
                            dAll = dh;
                    }
                    if (dAll > gR)
                        continue;

                    Col c = S.at(x, y);
                    // glow
                    if (dAll > -0.5f)
                        over(c, glow, 0.62f * sqr(1.0f - c01(dAll / gR)));
                    // the white of the eye (and the lid line above the cut)
                    const float cs = cov(dAll);
                    if (cs > 0.0f)
                    {
                        float tb = c01((py / ry) * 0.5f + 0.5f); // 0 top .. 1 bottom: a touch cooler at the bottom
                        Col wcol = mix2(C_WHITE_EYE, mk(rgb(222, 236, 255)), tb * tb);
                        over(c, wcol, cs);
                        // the iris (not above the lid line, not outside the white)
                        float dI = sdEll(px - ix, py - iy, irx, iry);
                        float ci = cov(dI);
                        if (hasLid)
                            ci = fminf(ci, cov(dTop));
                        ci = fminf(ci, cs);
                        if (ci > 0.0f)
                        {
                            float tv = c01(((py - iy) / iry + 0.15f) * 0.5f + 0.5f);
                            Col icol = mix2(C_NAVY, irisBot, tv * tv * 0.92f);
                            over(c, icol, ci);
                            float a1 = cov(sqrtf(sqr(px - h1x) + sqr(py - h1y)) - h1r);
                            float a2 = cov(sqrtf(sqr(px - h2x) + sqr(py - h2y)) - h2r);
                            over(c, C_WHITE_EYE, fminf(ci, a1));
                            over(c, C_WHITE_EYE, fminf(ci, a2) * 0.9f);
                        }
                    }
                    S.put(x, y, c);
                }
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    //  Public: styles, geometry
    // ---------------------------------------------------------------------------------------------
    void charSetEyeStyle(CharEyeStyle st) { gStyle = st; }

    int16_t charMouthDy() { return (int16_t)(CH_MOUTH_DY * sc + 0.5f); }

    namespace
    {
        // Where the callers think an eye is (W/2 -+ 2R) -> where the design has it.
        float eyeX(int16_t cx, int8_t side)
        {
            const float inward = W * 0.25f - CH_DX * sc;
            return cx - side * inward;
        }
        int8_t sideOf(int16_t cx) { return cx < W / 2 ? -1 : 1; }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    //  Public: the eye
    // ---------------------------------------------------------------------------------------------
    void drawCharacterEye(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                          uint16_t cTop, uint16_t cBot, bool shine)
    {
        (void)cTop;
        if (s.w < 3 || s.h < 3) // shrunk away (cross-fade to a decoration)
            return;
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(cBot);
        const float R = W / 8.0f;
        const float fw = s.w / (2.0f * R);
        const float cxe = eyeX(cx, side);

        // Happy lids (a wink): the clean arch instead of the half-cut eye.
        if (s.lidBot > 0.25f && shine)
        {
            const float Rg = R * fmaxf(0.8f, fw);
            drawBlush(S, cxe + 0.3f * s.x, cy + 0.3f * s.y, 0.9f * Rg, 1.1f * Rg, side, 1.0f, s.x);
            drawArcGlyph(S, sh, cxe + 0.3f * s.x, cy + 0.3f * s.y, Rg, true);
            return;
        }

        const float rx = s.w * 0.5f;
        const float ryFull = s.h * 0.5f;
        const float ry = fmaxf(1.0f, s.h * openness * 0.5f);
        const float ecx = cxe + 0.35f * s.x, ecy = cy + 0.35f * s.y;

        if (shine)
            drawBlush(S, ecx, ecy, rx, ryFull, side, 1.0f, s.x);

        if (gStyle == CharEyeStyle::STAR)
        {
            const float sy = 1.0f - 0.95f * c01(s.lidTop); // a blink squashes the ring
            drawStarEye(S, sh, ecx, ecy + (1.0f - sy) * ryFull * 0.5f, rx, fmaxf(1.0f, ry * sy));
            return;
        }

        drawRoundEye(S, sh, ecx, ecy, rx, ry, ryFull, s, side);

        // brows: the sad / worried ones follow the slant of the lid, the raised / lowered ones come from s.brow
        if (shine)
        {
            const float lt = c01(s.lidTop);
            const float by = ecy - ryFull - 4.0f * sc + lt * ryFull * 0.55f;
            const float thick = 2.3f * sc;
            if (s.slant < -0.15f)
            {
                const float a = c01((-s.slant - 0.15f) / 0.45f);
                drawBrow(S, sh, ecx + side * 0.06f * rx, by - 1.0f * sc, 0.82f * rx, side, 6.2f * sc * a, 2.4f * sc, thick, 1.0f);
            }
            else if (s.brow > 0.05f)
            {
                const float b = c01(s.brow);
                drawBrow(S, sh, ecx, by - 4.0f * sc * b, 0.70f * rx, side, -6.0f * sc * b, 1.8f * sc, thick, 1.0f);
            }
            else if (s.brow < -0.05f)
            {
                const float b = c01(-s.brow);
                drawBrow(S, sh, ecx, by - 1.0f * sc, 0.62f * rx, side, -3.0f * sc * b, 0.0f, thick, 1.0f);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    //  Public: the other faces
    // ---------------------------------------------------------------------------------------------
    void charHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
        if (R < 2)
            return;
        const int8_t side = sideOf(cx);
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(C_EYE_BOT);
        const float ex = eyeX(cx, side), Rg = R * sc;
        drawBlush(S, ex, cy, 0.9f * Rg, 1.1f * Rg, side, fminf(1.0f, Rg / 20.0f), 0);
        drawArcGlyph(S, sh, ex, cy, Rg, true);
    }

    void charContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
        if (R < 2)
            return;
        const int8_t side = sideOf(cx);
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(C_EYE_BOT);
        const float ex = eyeX(cx, side), Rg = R * sc;
        drawBlush(S, ex, cy, 0.9f * Rg, 1.1f * Rg, side, fminf(1.0f, Rg / 20.0f), 0);
        drawArcGlyph(S, sh, ex, cy, Rg, false);
    }

    void charHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float pulse)
    {
        const float hw = 1.0f * R * sc * pulse;
        if (hw < 2.0f)
            return;
        const int8_t side = sideOf(cx);
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(C_EYE_BOT);
        const float ex = eyeX(cx, side);
        const float Sx = hw / 0.604f;
        const float k = fminf(1.0f, hw / 18.0f);
        // outline: a white heart with a navy heart inside, a glow around
        HeartSD outer = {ex, (float)cy, Sx, 0.0f};
        Paint ring = {C_WHITE_EYE, 1.0f, shifted(C_BLUE, sh, 1.0f), GLOW_R * sc * k, 0.55f};
        paintShape(S, ex - hw, cy - hw * 1.1f, ex + hw, cy + hw * 1.1f, outer, ring);
        HeartSD inner = {ex, (float)cy, Sx, 3.4f * sc * k};
        Paint in = {C_NAVY, 1.0f, C_NAVY, 0.0f, 0.0f};
        paintShape(S, ex - hw, cy - hw * 1.1f, ex + hw, cy + hw * 1.1f, inner, in);
        drawBlush(S, ex, cy, 0.9f * R * sc, 1.1f * R * sc, side, fminf(1.0f, R * sc / 20.0f), 0);
    }

    void charSpiralEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase)
    {
        const float Rg = R * sc;
        if (Rg < 3.0f)
            return;
        const int8_t side = sideOf(cx);
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(C_EYE_BOT);
        const float ex = eyeX(cx, side);
        const float turns = 2.4f, TWO_PI_F = 6.2831853f;
        const float b = Rg * 1.0f / (turns * TWO_PI_F); // radius gained per radian
        const float wd = 3.0f * sc * fminf(1.0f, Rg / 20.0f) + 0.6f;
        const float rMax = b * turns * TWO_PI_F;
        const Col glow = shifted(C_BLUE, sh, 1.0f);
        const float gR = 5.0f * sc;
        const int x0 = (int)floorf(ex - rMax - gR - 2), x1 = (int)ceilf(ex + rMax + gR + 2);
        const int y0 = (int)floorf(cy - rMax - gR - 2), y1 = (int)ceilf(cy + rMax + gR + 2);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
            {
                float px = x + 0.5f - ex, py = y + 0.5f - cy;
                float rho = sqrtf(px * px + py * py);
                if (rho > rMax + wd + gR)
                    continue;
                float th = atan2f(py, px) - phase;
                th = fmodf(th, TWO_PI_F);
                if (th < 0)
                    th += TWO_PI_F;
                float n = floorf((rho / b - th) / TWO_PI_F + 0.5f);
                if (n < 0)
                    n = 0;
                if (n > turns)
                    n = floorf(turns);
                float dr = fabsf(rho - b * (th + TWO_PI_F * n));
                float d = dr * rho / sqrtf(rho * rho + b * b + 1e-3f) - wd * 0.5f;
                if (rho < 1.0f)
                    d = rho - wd * 0.5f;
                if (d > gR)
                    continue;
                float fa = cov(d);
                float ga = d > -0.5f ? 0.45f * sqr(1.0f - c01(d / gR)) : 0.0f;
                if (fa < 0.01f && ga < 0.01f)
                    continue;
                Col c = S.at(x, y);
                over(c, glow, ga);
                over(c, C_LINE, fa);
                S.put(x, y, c);
            }
    }

    void charColdEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
        const float Rg = R * sc;
        if (Rg < 3.0f)
            return;
        const int8_t side = sideOf(cx);
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(C_EYE_BOT);
        const float ex = eyeX(cx, side), k = Rg / 20.0f;
        drawBlush(S, ex, cy, 0.9f * Rg, 1.1f * Rg, side, fminf(1.0f, k), 0);
        // ">" on the left eye, "<" on the right: the tip points at the middle of the face
        Pt p[3] = {{ex + side * 8.5f * k, cy - 10.0f * k}, {ex - side * 6.5f * k, (float)cy}, {ex + side * 8.5f * k, cy + 10.0f * k}};
        strokePoly(S, p, 3, 3.6f * sc * k, C_LINE, shifted(C_BLUE, sh, 1.0f), 5.0f * sc * k + 1.0f, 0.45f, 1.0f);
    }

    void charShiverMarks(TFT_eSPI &g, int16_t cxLeft, int16_t cxRight, int16_t cy, int16_t R, float phase)
    {
        const float k = R * sc / 20.0f;
        const Surf S = {g, readable(g)};
        const Col col = {120.0f, 165.0f, 255.0f};
        const Col glow = {60.0f, 100.0f, 255.0f};
        for (int sd = -1; sd <= 1; sd += 2)
        {
            const float x = eyeX(sd < 0 ? cxLeft : cxRight, (int8_t)sd) + sd * (24.0f * k);
            Pt p[9];
            for (int i = 0; i < 9; i++)
            {
                float t = i / 8.0f;
                p[i].y = cy - 8.0f * k + 16.0f * k * t;
                p[i].x = x + sd * 1.8f * sc * sinf(t * 3.0f * 3.1415926f + phase * sd);
            }
            strokePoly(S, p, 9, 1.8f * sc, col, glow, 3.0f * sc, 0.30f, 1.0f);
        }
    }

    // ---------------------------------------------------------------------------------------------
    //  Public: the mouth
    // ---------------------------------------------------------------------------------------------
    void drawCharMouthShape(TFT_eSPI &g, int16_t cx, int16_t cy, CharMouth m, float alpha)
    {
        if (alpha < 0.04f)
            return;
        const Surf S = {g, readable(g)};
        const Tint sh = themeShift(C_EYE_BOT);
        const Col glow = shifted(C_BLUE, sh, 1.0f);
        const float x = (float)cx, y = (float)cy;
        const float wd = 1.9f * sc;
        const float gR = 3.2f * sc, gA = 0.30f;
        Pt p[13];
        switch (m)
        {
        case CharMouth::SMILE:
        case CharMouth::SMILE_BIG:
        {
            const bool big = m == CharMouth::SMILE_BIG;
            const float hw = (big ? 8.5f : 6.4f) * sc, dp = (big ? 4.6f : 3.2f) * sc;
            for (int i = 0; i < 9; i++)
            {
                float t = i / 4.0f - 1.0f;
                p[i].x = x + hw * t;
                p[i].y = y - dp * 0.5f + dp * (1.0f - t * t);
            }
            strokePoly(S, p, 9, big ? wd * 1.15f : wd, C_LINE, glow, gR, gA, alpha);
            break;
        }
        case CharMouth::DASH:
            p[0] = {x - 3.4f * sc, y};
            p[1] = {x + 3.4f * sc, y};
            strokePoly(S, p, 2, wd, C_LINE, glow, gR, gA, alpha);
            break;
        case CharMouth::FROWN:
        case CharMouth::THINK:
        {
            const float ox = m == CharMouth::THINK ? -3.0f * sc : 0.0f;
            const float hw = 5.4f * sc, dp = 2.8f * sc;
            for (int i = 0; i < 9; i++)
            {
                float t = i / 4.0f - 1.0f;
                p[i].x = x + ox + hw * t;
                p[i].y = y + dp * 0.5f - dp * (1.0f - t * t);
            }
            strokePoly(S, p, 9, wd, C_LINE, glow, gR, gA, alpha);
            break;
        }
        case CharMouth::OPEN_O:
        {
            RingSD sd = {x, y + 0.5f * sc, 3.0f * sc, 3.5f * sc, wd * 0.5f};
            Paint P = {C_LINE, alpha, glow, gR, gA};
            paintShape(S, x - 4.0f * sc, y - 4.0f * sc, x + 4.0f * sc, y + 5.0f * sc, sd, P);
            break;
        }
        case CharMouth::WAVE:
        {
            const float hw = 7.5f * sc;
            for (int i = 0; i < 13; i++)
            {
                float t = i / 6.0f - 1.0f;
                p[i].x = x + hw * t;
                p[i].y = y + 1.5f * sc * sinf(t * 3.0f * 3.1415926f);
            }
            strokePoly(S, p, 13, wd * 0.9f, C_LINE, glow, gR, gA, alpha);
            break;
        }
        case CharMouth::CUTE:
        {
            const float hw = 5.6f * sc, dp = 2.8f * sc;
            for (int half = 0; half < 2; half++)
            {
                for (int i = 0; i < 7; i++)
                {
                    float t = i / 6.0f;
                    p[i].x = half == 0 ? x - hw + hw * t : x + hw * t;
                    p[i].y = y - dp * 0.5f + dp * sinf(t * 3.1415926f);
                }
                strokePoly(S, p, 7, wd, C_LINE, glow, gR, gA, alpha);
            }
            break;
        }
        }
    }

    void drawCharMouth(TFT_eSPI &g, int16_t cx, int16_t cy, float alpha)
    {
        drawCharMouthShape(g, cx, cy, CharMouth::SMILE, alpha);
    }

    // ---------------------------------------------------------------------------------------------
    //  Public: one-colour extras
    // ---------------------------------------------------------------------------------------------
    void drawCharMono(TFT_eSPI &g, CharMono kind, int16_t cx, int16_t cy, float k, uint16_t col)
    {
        if (k < 0.05f)
            return;
        const Surf S = {g, readable(g)};
        const Col C = mk(col);
        const float x = (float)cx, y = (float)cy;
        const Col none = C;
        switch (kind)
        {
        case CharMono::HEART:
        {
            const float hw = 5.2f * k, Sx = hw / 0.604f;
            HeartSD sd = {x, y, Sx, 0.0f};
            Paint P = {C, 1.0f, none, 0.0f, 0.0f};
            paintShape(S, x - hw, y - hw * 1.1f, x + hw, y + hw * 1.1f, sd, P);
            break;
        }
        case CharMono::DROP:
        {
            PolyUnionCircleSD sd = {x, y, k};
            Paint P = {C, 1.0f, none, 0.0f, 0.0f};
            paintShape(S, x - 3.2f * k, y - 7.2f * k, x + 3.2f * k, y + 4.2f * k, sd, P);
            // a small glint
            CircleSD gl = {x - 1.1f * k, y + 0.6f * k, 0.8f * k};
            Paint Pg = {mix2(C, C_WHITE_EYE, 0.75f), 0.9f, none, 0.0f, 0.0f};
            if (k >= 0.9f)
                paintShape(S, x - 2.0f * k, y - 0.4f * k, x - 0.2f * k, y + 1.4f * k, gl, Pg);
            break;
        }
        case CharMono::BANG:
        {
            BangSD sd = {x, y, k};
            Paint P = {C, 1.0f, none, 0.0f, 0.0f};
            paintShape(S, x - 2.5f * k, y - 9.5f * k, x + 2.5f * k, y + 9.0f * k, sd, P);
            break;
        }
        case CharMono::ANGER:
        {
            // four arcs, one per corner of an imaginary square, each bending away from the middle
            const float d = 3.0f * k, r = 4.8f * k;
            for (int qy = -1; qy <= 1; qy += 2)
                for (int qx = -1; qx <= 1; qx += 2)
                {
                    Pt p[7];
                    for (int i = 0; i < 7; i++)
                    {
                        float a = (i / 6.0f) * 1.5707963f;
                        p[i].x = x + qx * (d + r * cosf(a));
                        p[i].y = y + qy * (d + r * sinf(a));
                    }
                    strokePoly(S, p, 7, 2.0f * k + 0.4f, C, none, 0.0f, 0.0f, 1.0f);
                }
            break;
        }
        case CharMono::SPARK:
        {
            // a four-point sparkle
            float v[16];
            const float ro = 5.2f * k, ri = 1.3f * k;
            for (int i = 0; i < 8; i++)
            {
                float a = i * 0.7853982f;
                float rr = (i & 1) ? ri : ro;
                v[2 * i] = rr * sinf(a);
                v[2 * i + 1] = -rr * cosf(a);
            }
            PolySD sd = {v, 8, x, y};
            Paint P = {C, 1.0f, none, 0.0f, 0.0f};
            paintShape(S, x - ro, y - ro, x + ro, y + ro, sd, P);
            break;
        }
        case CharMono::QUESTION:
        {
            // the hook, the stem and the dot of a "?"
            Pt p[15];
            int n = 0;
            for (int i = 0; i <= 9; i++)
            {
                float a = 3.1415926f + (i / 9.0f) * (3.1415926f * 1.38f);
                p[n].x = x + 4.0f * k * cosf(a);
                p[n].y = y - 5.0f * k + 4.0f * k * sinf(a);
                n++;
            }
            p[n++] = {x + 0.4f * k, y + 0.2f * k};
            p[n++] = {x, y + 3.0f * k};
            strokePoly(S, p, n, 2.5f * k + 0.3f, C, mk(rgb(70, 110, 255)), 3.0f * k, 0.30f, 1.0f);
            CircleSD dot = {x, y + 7.2f * k, 1.4f * k + 0.2f};
            Paint P = {C, 1.0f, mk(rgb(70, 110, 255)), 3.0f * k, 0.30f};
            paintShape(S, x - 1.8f * k, y + 5.4f * k, x + 1.8f * k, y + 9.0f * k, dot, P);
            break;
        }
        case CharMono::SNOW:
        {
            const float r = 7.0f * k, w = 1.5f * k + 0.3f;
            for (int i = 0; i < 3; i++)
            {
                float a = i * 1.0471976f + 1.5707963f;
                float ux = cosf(a), uy = sinf(a);
                Pt arm[2] = {{x - ux * r, y - uy * r}, {x + ux * r, y + uy * r}};
                strokePoly(S, arm, 2, w, C, none, 0.0f, 0.0f, 1.0f);
                for (int e = -1; e <= 1; e += 2) // the little V at each end
                {
                    float tx = x + e * ux * r, ty = y + e * uy * r;
                    float bx = x + e * ux * r * 0.62f, by = y + e * uy * r * 0.62f;
                    for (int s2 = -1; s2 <= 1; s2 += 2)
                    {
                        float ca = cosf(s2 * 0.75f), sa = sinf(s2 * 0.75f);
                        float dx = -e * ux, dy = -e * uy;
                        Pt br[2] = {{bx, by}, {bx + (dx * ca - dy * sa) * r * 0.42f, by + (dx * sa + dy * ca) * r * 0.42f}};
                        (void)tx;
                        (void)ty;
                        strokePoly(S, br, 2, w * 0.9f, C, none, 0.0f, 0.0f, 1.0f);
                    }
                }
            }
            break;
        }
        case CharMono::STAR:
        {
            StarSD sd = {x, y, 5.0f * k, 0.46f, 0.0f};
            Paint P = {C, 1.0f, none, 0.0f, 0.0f};
            paintShape(S, x - 5.2f * k, y - 5.2f * k, x + 5.2f * k, y + 5.2f * k, sd, P);
            break;
        }
        }
    }
} // namespace disp
