// display_character.cpp - draws the clock character from the layers in include/character_art.h.
//
// Every layer is a small bitmap with per-pixel alpha, so the soft glow stays translucent over the sky
// behind the face. The bitmap is scaled with nearest-neighbour (pixel art stays crisp; at the real
// 128 px display the scale is exactly 1). The eye is cut by the same lid maths as the old eyes
// (eyeSpan), so blink, yawn, sad, angry, happy "^ ^" and the other presets all still work.
#include "display_character.h"
#include "character_art.h"

namespace disp
{
    namespace
    {
        struct Tint
        {
            int r, g, b;
        };

        inline void unpack565(uint16_t c, int &r, int &g, int &b)
        {
            r = ((c >> 11) & 0x1F) * 255 / 31;
            g = ((c >> 5) & 0x3F) * 255 / 63;
            b = (c & 0x1F) * 255 / 31;
        }

        // How far the theme moves the blue of the artwork: (current eye colour) - (night eye colour).
        // At night this is exactly zero, so the character looks like the picture.
        Tint themeShift(uint16_t cBot)
        {
#if CHARACTER_THEME_TINT
            const uint16_t night = rgb(0, 140, 235); // TH_NIGHT.eyeBot (display_theme.cpp)
            int r, g, b, nr, ng, nb;
            unpack565(cBot, r, g, b);
            unpack565(night, nr, ng, nb);
            return {r - nr, g - ng, b - nb};
#else
            (void)cBot;
            return {0, 0, 0};
#endif
        }

        TFT_eSprite *readable(TFT_eSPI &g)
        {
            if (faceCv.ok && &g == static_cast<TFT_eSPI *>(&faceCv.spr))
                return &faceCv.spr;
            return nullptr;
        }

        struct Clip
        {
            const EyeShape *s; // lid shape of the ring (w/h = ring size as drawn, centred on 0,0)
            int8_t side;
            float cx, cy; // ring centre on screen
            float grow;   // the layer is a bit bigger than the ring (glow)
        };

        // Draws layer L with its top-left corner at (posX, posY), scaled by kx / ky.
        //  clip      : lids of the eye (nullptr = none)
        //  shearAmp  : brow tilt: the column at the inner end moves down by shearAmp px, the outer end up
        //  shearSign : +1 / -1, which end of the layer is the inner one
        //  mono      : >= 0: draw every pixel in this RGB565 colour (only the alpha of the layer is used)
        void blit(TFT_eSPI &g, TFT_eSprite *rd, const CharLayer &L, float posX, float posY, float kx, float ky,
                  const Tint &sh, float alphaMul, const Clip *clip, float shearAmp, int8_t shearSign, int32_t mono = -1)
        {
            if (alphaMul < 0.02f || kx <= 0.05f || ky <= 0.05f)
                return;
            const float ikx = 1.0f / kx, iky = 1.0f / ky;
            const float pad = fabsf(shearAmp);
            const int16_t x0 = (int16_t)floorf(posX), x1 = (int16_t)ceilf(posX + L.w * kx);
            const int16_t y0 = (int16_t)floorf(posY - pad), y1 = (int16_t)ceilf(posY + L.h * ky + pad);
            const float midX = posX + L.w * kx * 0.5f, halfW = fmaxf(1.0f, L.w * kx * 0.5f);
            for (int16_t y = y0; y < y1; y++)
            {
                float xa = -1e9f, xb = 1e9f, hole = 0;
                if (clip && !eyeSpan(*clip->s, clip->side, y + 0.5f - clip->cy, clip->grow, xa, xb, hole))
                    continue;
                for (int16_t x = x0; x < x1; x++)
                {
                    if (clip)
                    {
                        float rx = x + 0.5f - clip->cx;
                        if (rx < xa || rx > xb || (hole > 0 && fabsf(rx) < hole))
                            continue;
                    }
                    float dy = 0;
                    if (pad > 0)
                        dy = shearAmp * clamp01(0.5f + 0.5f * shearSign * (x + 0.5f - midX) / halfW) * 2.0f - shearAmp;
                    int sx = (int)floorf((x + 0.5f - posX) * ikx);
                    int sy = (int)floorf((y + 0.5f - dy - posY) * iky);
                    if (sx < 0 || sy < 0 || sx >= L.w || sy >= L.h)
                        continue;
                    uint32_t v = L.px[sy * L.w + sx];
                    uint8_t a8 = v >> 24;
                    if (!a8)
                        continue;
                    int r, gg, b;
                    unpack565((uint16_t)v, r, gg, b);
                    int wt = (v >> 16) & 0xFF;
                    if (wt && (sh.r | sh.g | sh.b))
                    {
                        r += sh.r * wt / 255;
                        gg += sh.g * wt / 255;
                        b += sh.b * wt / 255;
                        r = r < 0 ? 0 : (r > 255 ? 255 : r);
                        gg = gg < 0 ? 0 : (gg > 255 ? 255 : gg);
                        b = b < 0 ? 0 : (b > 255 ? 255 : b);
                    }
                    uint16_t col = mono >= 0 ? (uint16_t)mono : rgb((uint8_t)r, (uint8_t)gg, (uint8_t)b);
                    float a = a8 * (1.0f / 255.0f) * alphaMul;
                    if (a < 0.985f)
                    {
                        uint16_t under = rd ? rd->readPixel(x, y) : C_BG;
                        col = mixColor(under, col, a);
                    }
                    g.drawPixel(x, y, col);
                }
            }
        }

        // Scale factors of a shape relative to the neutral preset (w = 2 R, h = 2.25 R). Small wobbles
        // (breathing, parallax, the TIRED / HAPPY presets) are snapped away: pixel art must not shimmer.
        float snapScale(float f, float tol) { return fabsf(f - 1.0f) < tol ? 1.0f : f; }
    } // namespace

    namespace
    {
        // The art's eye centre for a caller that thinks the eyes sit at W/2 -+ 2R.
        int8_t sideOf(int16_t cx) { return cx < W / 2 ? -1 : 1; }
        float eyeX(int16_t cx, int8_t side)
        {
            const float artDx = side < 0 ? -CH_EYE_X_L : CH_EYE_X_R;
            return cx - side * (W * 0.25f - artDx * sc);
        }

        // glyph + blush + brow, all scaled by k. browDy / cheekDy = where they sit for this glyph (art px).
        void faceWith(TFT_eSPI &g, const CharLayer &glyph, float cxe, float cy, float k, int8_t side, float browDy,
                      float cheekDy, float tilt)
        {
            const CharEye &E = side < 0 ? CH_EYE_L : CH_EYE_R;
            Tint sh = themeShift(C_EYE_BOT);
            TFT_eSprite *rd = readable(g);
            blit(g, rd, E.cheek, cxe + E.cheek.ox * k, cy + (E.cheek.oy + cheekDy) * k, k, k, sh, 1.0f, nullptr, 0, 1);
            blit(g, rd, glyph, cxe + glyph.ox * k, cy + glyph.oy * k, k, k, sh, 1.0f, nullptr, 0, 1);
            blit(g, rd, E.brow, cxe + E.brow.ox * k, cy + (E.brow.oy + browDy) * k, k, k, sh, 1.0f, nullptr, tilt * k,
                 (int8_t)-side);
        }
        float glyphK(int16_t R) { return sc * (float)R / (W / 8.0f); }
    } // namespace

    void charHappyEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
        float k = glyphK(R);
        if (k < 0.12f)
            return;
        int8_t side = sideOf(cx);
        faceWith(g, CH_HAPPY, eyeX(cx, side), cy, k, side, 6.0f, -1.0f, 0);
    }

    void charContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
        float k = glyphK(R);
        if (k < 0.12f)
            return;
        int8_t side = sideOf(cx);
        faceWith(g, CH_CONTENT, eyeX(cx, side), cy, k, side, 9.0f, -1.0f, 0);
    }

    void charHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float pulse)
    {
        float k = glyphK(R) * pulse * 1.5f;
        if (k < 0.15f)
            return;
        int8_t side = sideOf(cx);
        float cxe = eyeX(cx, side);
        Tint none = {0, 0, 0};
        blit(g, readable(g), CH_HEART, cxe + CH_HEART.ox * k, cy + CH_HEART.oy * k, k, k, none, 1.0f, nullptr, 0, 1);
    }

    void charSpiralEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase)
    {
        float k = glyphK(R);
        if (k < 0.15f)
            return;
        int8_t side = sideOf(cx);
        float cxe = eyeX(cx, side);
        const float step = 6.2831853f / CH_SPIRAL_FRAMES;
        int f = (int)floorf(phase / step + 0.5f) % CH_SPIRAL_FRAMES;
        if (f < 0)
            f += CH_SPIRAL_FRAMES;
        const CharLayer &L = CH_SPIRAL[f];
        blit(g, readable(g), L, cxe + L.ox * k, cy + L.oy * k, k, k, themeShift(C_EYE_BOT), 1.0f, nullptr, 0, 1);
    }

    void drawCharMono(TFT_eSPI &g, CharMono kind, int16_t cx, int16_t cy, float k, uint16_t col)
    {
        const CharLayer *L = &CH_M_HEART;
        switch (kind)
        {
        case CharMono::DROP:
            L = &CH_M_DROP;
            break;
        case CharMono::BANG:
            L = &CH_M_BANG;
            break;
        case CharMono::ANGER:
            L = &CH_M_ANGER;
            break;
        case CharMono::SPARK:
            L = &CH_M_SPARK;
            break;
        default:
            break;
        }
        Tint none = {0, 0, 0};
        blit(g, readable(g), *L, cx + L->ox * k, cy + L->oy * k, k, k, none, 1.0f, nullptr, 0, 1, (int32_t)col);
    }

    int16_t charMouthDy() { return (int16_t)(CH_MOUTH_DY * sc + 0.5f); }

    void drawCharMouth(TFT_eSPI &g, int16_t cx, int16_t cy, float alpha)
    {
        if (alpha < 0.04f)
            return;
        Tint sh = themeShift(C_EYE_BOT);
        blit(g, readable(g), CH_MOUTH, cx + CH_MOUTH.ox * sc, cy + CH_MOUTH.oy * sc, sc, sc, sh, alpha, nullptr, 0, 1);
    }

    void drawCharacterEye(TFT_eSPI &g, int16_t cx, int16_t cy, const EyeShape &s, float openness, int8_t side,
                          uint16_t cTop, uint16_t cBot, bool shine)
    {
        (void)cTop;
        if (s.w < 3 || s.h < 3) // shrunk away (cross-fade to a decoration)
            return;
        const CharEye &E = side < 0 ? CH_EYE_L : CH_EYE_R;
        const float R = W / 8.0f;
        const float fw = snapScale(s.w / (2.0f * R), 0.10f);
        const float fh = snapScale(s.h * openness / (2.25f * R), 0.06f);
        const float kx = sc * fw, ky = sc * fh;
        const float kyPos = sc * fminf(1.25f, fmaxf(0.9f, fh)); // brow / blush distances: not squashed by a blink

        // The callers place the eyes at W/2 -+ 2R; the art has them a little closer together.
        const float artDx = side < 0 ? -CH_EYE_X_L : CH_EYE_X_R;
        const float inward = W * 0.25f - artDx * sc;
        const float cxe = cx - side * inward;

        // Happy lids (a wink): the clean "^" glyph instead of the half-cut ring.
        if (s.lidBot > 0.25f && shine)
        {
            faceWith(g, CH_HAPPY, cxe + 0.3f * s.x, cy + 0.3f * s.y, sc * fmaxf(0.8f, fw), side, 6.0f, -1.0f, 0);
            return;
        }

        // Gaze: the ring follows a little, the sparkle moves inside it (the rest of the gaze).
        const float rcx = cxe + 0.35f * s.x, rcy = cy + 0.35f * s.y;
        const float stx = fmaxf(-E.starRangeX * sc, fminf(E.starRangeX * sc, 0.65f * s.x));
        const float sty = fmaxf(-E.starRangeY * sc, fminf(E.starRangeY * sc, 0.65f * s.y));

        Tint sh = themeShift(cBot);
        TFT_eSprite *rd = readable(g);

        // lids: the same shape maths as before, on the ring's rectangle
        EyeShape d = s;
        d.w = E.ringW * kx;
        d.h = E.ringH * ky;
        d.radius = 0;
        d.x = d.y = 0;
        Clip clip = {&d, side, rcx, rcy, 4.0f * sc};

        // blush (under the eye); not when the eye is angry / alarmed (shine = false)
        if (shine)
            blit(g, rd, E.cheek, cxe + 0.10f * s.x + E.cheek.ox * kx, cy + 0.10f * s.y + E.cheek.oy * kyPos, sc, sc, sh,
                 1.0f, nullptr, 0, 1);

        // ring + glow + dark inside
        blit(g, rd, E.eye, rcx + E.eye.ox * kx, rcy + E.eye.oy * ky, kx, ky, sh, 1.0f, &clip, 0, 1);

        // sparkle
        if (shine)
            blit(g, rd, E.star, rcx + stx + E.star.ox * kx, rcy + sty + E.star.oy * ky, kx, ky, sh, 1.0f, &clip, 0, 1);

        // brow: follows the top of the eye, drops with the upper lid, tilts with the slant (angry / sad)
        const float lt = clamp01(s.lidTop);
        const float browDrop = lt * 0.16f * E.ringH * sc;
        blit(g, rd, E.brow, cxe + 0.15f * s.x + E.brow.ox * kx, cy + 0.15f * s.y + E.brow.oy * kyPos + browDrop, sc, sc,
             sh, 1.0f, nullptr, s.slant * 3.5f * sc, (int8_t)-side);
    }
} // namespace disp