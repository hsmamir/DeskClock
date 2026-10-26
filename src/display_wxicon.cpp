// display_wxicon.cpp - one small animated icon for every weather state (Open-Meteo WMO codes), in the flat look of the
// design: peach sun, lavender clouds, blue drops, light-blue snowflake. Day and night have their own sky (sun / moon).
// Everything is drawn with circles, rounded rects and thick lines, so it scales with s (the half width of the icon in px).
#include "display_wxicon.h"

namespace disp
{
    namespace
    {
        constexpr uint16_t SUN = rgb(250, 200, 105);
        constexpr uint16_t MOON = rgb(246, 230, 160);
        constexpr uint16_t STAR = rgb(255, 244, 200);
        constexpr uint16_t CLOUD = rgb(178, 168, 245);      // the lavender cloud of the design
        constexpr uint16_t CLOUD_BACK = rgb(120, 112, 192);  // a second cloud behind it (overcast)
        constexpr uint16_t CLOUD_DARK = rgb(116, 108, 176);  // heavy rain / storm
        constexpr uint16_t DROP = rgb(110, 175, 255);
        constexpr uint16_t DROP_HVY = rgb(80, 150, 255);
        constexpr uint16_t ICE = rgb(190, 232, 255);
        constexpr uint16_t SNOWC = rgb(130, 205, 255);       // the snowflake of the design
        constexpr uint16_t FLAKE = rgb(228, 240, 255);
        constexpr uint16_t BOLT = rgb(255, 224, 80);
        constexpr uint16_t BOLT_HI = rgb(255, 250, 200);
        constexpr uint16_t HAIL = rgb(215, 238, 255);
        constexpr uint16_t FOG = rgb(160, 156, 214);

        inline int16_t R(float v) { return (int16_t)lroundf(v); }
        inline float fmaxf2(float a, float b) { return a > b ? a : b; }

        // A line with round ends, w px thick.
        void seg(TFT_eSPI &g, float x0, float y0, float x1, float y1, float w, uint16_t c)
        {
            float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
            if (len < 0.5f)
            {
                g.fillCircle(R(x0), R(y0), imax(1, R(w * 0.5f)), c);
                return;
            }
            float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
            g.fillTriangle(R(x0 + nx), R(y0 + ny), R(x1 + nx), R(y1 + ny), R(x1 - nx), R(y1 - ny), c);
            g.fillTriangle(R(x0 + nx), R(y0 + ny), R(x1 - nx), R(y1 - ny), R(x0 - nx), R(y0 - ny), c);
            if (w >= 2.0f)
            {
                g.fillCircle(R(x0), R(y0), R(w * 0.5f), c);
                g.fillCircle(R(x1), R(y1), R(w * 0.5f), c);
            }
        }

        // 0..1, repeating every period ms; off shifts the phase (so several drops do not fall in step).
        float cyc(uint32_t now, float period, float off)
        {
            float t = fmodf((float)now / period + off, 1.0f);
            return t < 0 ? t + 1.0f : t;
        }
        // 0..1..0: fade in at the start of a fall and out at its end.
        float edge(float t)
        {
            float a = t * 5.0f, b = (1.0f - t) * 5.0f, m = a < b ? a : b;
            return m > 1.0f ? 1.0f : m;
        }

        void cloud(TFT_eSPI &g, float cx, float cy, float s, uint16_t c)
        {
            g.fillRoundRect(R(cx - 0.85f * s), R(cy - 0.05f * s), R(1.7f * s), R(0.7f * s), R(0.35f * s), c);
            g.fillCircle(R(cx - 0.40f * s), R(cy - 0.05f * s), imax(1, R(0.38f * s)), c);
            g.fillCircle(R(cx + 0.12f * s), R(cy - 0.25f * s), imax(1, R(0.52f * s)), c);
        }

        // The sun: a disc with eight rays that turn slowly and "breathe".
        void sun(TFT_eSPI &g, float x, float y, float r, uint32_t now, bool rays)
        {
            g.fillCircle(R(x), R(y), imax(2, R(r)), SUN);
            if (!rays)
                return;
            const float a0 = now * 0.0005f, w = fmaxf2(1.0f, 0.42f * r);
            for (int i = 0; i < 8; i++)
            {
                const float a = a0 + i * 0.7853982f, sx = sinf(a), sy = -cosf(a);
                const float pulse = 1.0f + 0.16f * sinf(now / 380.0f + i * 0.8f);
                const float r0 = 1.62f * r, r1 = r0 + 0.62f * r * pulse;
                seg(g, x + sx * r0, y + sy * r0, x + sx * r1, y + sy * r1, w, SUN);
            }
        }

        void star(TFT_eSPI &g, int16_t x, int16_t y, int16_t k, uint16_t c)
        {
            g.drawFastHLine(x - k, y, 2 * k + 1, c);
            g.drawFastVLine(x, y - k, 2 * k + 1, c);
        }

        // The moon: a crescent (a disc with a bite out of it) and two stars that twinkle.
        void moon(TFT_eSPI &g, float x, float y, float r, uint32_t now)
        {
            g.fillCircle(R(x), R(y), imax(2, R(r)), MOON);
            g.fillCircle(R(x + 0.5f * r), R(y - 0.35f * r), imax(2, R(0.82f * r)), C_BG);
            const float k1 = 0.5f + 0.5f * sinf(now / 330.0f), k2 = 0.5f + 0.5f * sinf(now / 270.0f + 2.0f);
            star(g, R(x + 0.5f * r), R(y - 0.35f * r), 1, tint(STAR, 0.3f + 0.7f * k1));
            g.drawPixel(R(x + 1.25f * r), R(y + 0.7f * r), tint(STAR, 0.2f + 0.8f * k2));
        }

        // The body of the sky: sun by day, moon by night. r = the sun's disc radius.
        void body(TFT_eSPI &g, bool day, float x, float y, float r, uint32_t now, bool rays = true)
        {
            if (day)
                sun(g, x, y, r, now, rays);
            else
                moon(g, x, y, r * 1.6f, now);
        }

        // n drops falling from yTop to yBot, centred on cx.
        void drops(TFT_eSPI &g, float cx, float yTop, float yBot, float spacing, int n, uint32_t now, float period,
                   float len, float slant, float w, uint16_t col)
        {
            for (int i = 0; i < n; i++)
            {
                const float x = cx + (i - (n - 1) * 0.5f) * spacing, t = cyc(now, period, i * 0.382f + (i & 1) * 0.17f);
                const float y = yTop + (yBot - yTop) * t;
                seg(g, x, y, x - slant, y + len, w, tint(col, edge(t)));
            }
        }

        // n flakes (or grains / hail stones when sway is 0) falling from yTop to yBot.
        void flakes(TFT_eSPI &g, float cx, float yTop, float yBot, float spacing, int n, uint32_t now, float period,
                    float r, float sway, uint16_t col)
        {
            for (int i = 0; i < n; i++)
            {
                const float t = cyc(now, period, i * 0.382f + (i & 1) * 0.23f);
                const float x = cx + (i - (n - 1) * 0.5f) * spacing + sinf(now / 330.0f + i * 1.7f) * sway;
                const float y = yTop + (yBot - yTop) * t;
                const uint16_t c = tint(col, edge(t));
                if (r < 1.0f)
                    g.drawPixel(R(x), R(y), c);
                else
                    g.fillCircle(R(x), R(y), R(r), c);
            }
        }

        // A little ice crystal (a diamond) drifting down: freezing rain / drizzle.
        void crystals(TFT_eSPI &g, float cx, float yTop, float yBot, float spacing, int n, uint32_t now, float r)
        {
            for (int i = 0; i < n; i++)
            {
                const float t = cyc(now, 1500.0f, i * 0.5f + 0.25f);
                const float x = cx + (i - (n - 1) * 0.5f) * spacing + sinf(now / 400.0f + i * 2.3f) * r * 0.6f;
                const float y = yTop + (yBot - yTop) * t;
                const uint16_t c = tint(ICE, edge(t));
                g.fillTriangle(R(x), R(y - r), R(x + 0.7f * r), R(y), R(x), R(y + r), c);
                g.fillTriangle(R(x), R(y - r), R(x - 0.7f * r), R(y), R(x), R(y + r), c);
            }
        }

        // The snowflake of the design: three arms with branches, turning slowly.
        void snowflake(TFT_eSPI &g, float cx, float cy, float s, uint32_t now)
        {
            const float w = fmaxf2(1.0f, 0.2f * s), a0 = now * 0.0004f;
            for (int i = 0; i < 3; i++)
            {
                const float a = a0 + i * 1.0471976f, sx = sinf(a), sy = -cosf(a);
                seg(g, cx - sx * 0.95f * s, cy - sy * 0.95f * s, cx + sx * 0.95f * s, cy + sy * 0.95f * s, w, SNOWC);
                if (s >= 6.0f)
                    for (int e = -1; e <= 1; e += 2)
                    {
                        const float tx = cx + e * sx * 0.95f * s, ty = cy + e * sy * 0.95f * s;
                        for (int d = -1; d <= 1; d += 2)
                        {
                            const float b = a + (e > 0 ? 0.0f : 3.1415927f) + d * 0.7f;
                            seg(g, tx, ty, tx - sinf(b) * 0.38f * s, ty + cosf(b) * 0.38f * s, w, SNOWC);
                        }
                    }
            }
        }

        // The lightning bolt (a zigzag). It flickers: mostly dim, with two quick bright flashes every couple of seconds.
        // Returns true during a flash (the cloud lights up with it).
        bool bolt(TFT_eSPI &g, float cx, float cy, float s, uint32_t now)
        {
            const float t = cyc(now, 2200.0f, 0.0f);
            const bool flash = t < 0.07f || (t > 0.14f && t < 0.22f);
            const float w = fmaxf2(2.0f, 0.3f * s);
            const float x0 = cx + 0.20f * s, y0 = cy, x1 = cx - 0.12f * s, y1 = cy + 0.50f * s, x2 = cx + 0.14f * s,
                        y2 = cy + 0.45f * s, x3 = cx - 0.16f * s, y3 = cy + 0.98f * s;
            const uint16_t c = flash ? BOLT : tint(BOLT, 0.82f);
            seg(g, x0, y0, x1, y1, w, c);
            seg(g, x1, y1, x2, y2, w, c);
            seg(g, x2, y2, x3, y3, w, c);
            if (flash)
            {
                seg(g, x0, y0, x1, y1, fmaxf2(1.0f, w * 0.4f), BOLT_HI);
                seg(g, x2, y2, x3, y3, fmaxf2(1.0f, w * 0.4f), BOLT_HI);
            }
            return flash;
        }

        // Fog: a cloud and bars that slide against each other. ice = rime fog (45 -> 48): a few glints between the bars.
        void fog(TFT_eSPI &g, float cx, float cy, float s, uint32_t now, bool ice)
        {
            cloud(g, cx, cy - 0.38f * s, 0.85f * s, CLOUD);
            const float h = fmaxf2(2.0f, 0.26f * s);
            for (int i = 0; i < 2; i++)
            {
                const float y = cy + (0.5f + 0.42f * i) * s, len = (1.5f - 0.35f * i) * s;
                const float sway = sinf(now / 900.0f + i * 2.2f) * 0.22f * s;
                g.fillRoundRect(R(cx - len * 0.5f + sway), R(y - h * 0.5f), R(len), R(h), R(h * 0.5f), FOG);
            }
            if (ice)
            {
                const float k = 0.5f + 0.5f * sinf(now / 260.0f);
                g.drawPixel(R(cx - 0.8f * s), R(cy + 0.5f * s - 0.55f * s), tint(ICE, 0.3f + 0.7f * k));
                g.drawPixel(R(cx + 0.75f * s), R(cy + 0.5f * s + 0.55f * s), tint(ICE, 1.0f - 0.7f * k));
            }
        }

        // ---- the families -----------------------------------------------------------------------------------------

        // cloud over falling drops: n drops, heavy = darker cloud and bigger, faster drops
        void rainIcon(TFT_eSPI &g, float cx, float cy, float s, uint32_t now, int n, bool heavy, bool freezing, bool drizzle)
        {
            cloud(g, cx, cy - 0.32f * s, 0.85f * s, heavy ? CLOUD_DARK : CLOUD);
            const float w = fmaxf2(1.6f, (drizzle ? 0.2f : 0.26f) * s);
            const float len = (drizzle ? 0.28f : heavy ? 0.44f : 0.38f) * s;
            const float period = heavy ? 520.0f : (drizzle ? 1100.0f : 760.0f);
            const float sp = (n >= 4 ? 0.4f : 0.5f) * s;
            drops(g, cx + 0.05f * s, cy + 0.34f * s, cy + 0.78f * s, sp, n, now, period, len, 0.12f * s, w, heavy ? DROP_HVY : DROP);
            if (freezing)
                crystals(g, cx - 0.02f * s, cy + 0.36f * s, cy + 0.9f * s, 0.9f * s, 2, now, fmaxf2(1.2f, 0.17f * s));
        }

        void snowIcon(TFT_eSPI &g, float cx, float cy, float s, uint32_t now, int n, float r, float period)
        {
            cloud(g, cx, cy - 0.32f * s, 0.85f * s, CLOUD);
            flakes(g, cx, cy + 0.36f * s, cy + 0.95f * s, (n >= 5 ? 0.34f : 0.5f) * s, n, now, period, r * s, 0.1f * s, FLAKE);
        }

        // sun / moon peeking out behind a cloud, with whatever falls in front: kind 0 none, 1 rain, 2 snow
        void peekIcon(TFT_eSPI &g, bool day, float cx, float cy, float s, uint32_t now, int kind, int n, bool heavy)
        {
            body(g, day, cx - 0.42f * s, cy - 0.56f * s, 0.32f * s, now);
            cloud(g, cx + 0.1f * s, cy - 0.1f * s, 0.85f * s, heavy ? CLOUD_DARK : CLOUD);
            if (kind == 1)
                drops(g, cx + 0.15f * s, cy + 0.5f * s, cy + 0.9f * s, (n >= 4 ? 0.36f : 0.46f) * s, n, now, heavy ? 520.0f : 760.0f,
                      0.34f * s, 0.1f * s, fmaxf2(1.6f, 0.22f * s), heavy ? DROP_HVY : DROP);
            else if (kind == 2)
                flakes(g, cx + 0.15f * s, cy + 0.5f * s, cy + 1.0f * s, 0.42f * s, n, now, 1300.0f, 0.13f * s, 0.08f * s, FLAKE);
        }
    } // namespace

    void drawWxIcon(TFT_eSPI &g, int code, bool day, float cx, float cy, float s, uint32_t now)
    {
        switch (code)
        {
        case 0: // clear
            if (day)
                sun(g, cx, cy, 0.45f * s, now, true);
            else
                moon(g, cx, cy, 0.62f * s, now);
            return;
        case 1: // mainly clear: the sky with a little cloud in the corner
            body(g, day, cx - 0.15f * s, cy - 0.22f * s, 0.38f * s, now);
            cloud(g, cx + 0.42f * s, cy + 0.42f * s, 0.55f * s, CLOUD);
            return;
        case 2: // partly cloudy
            body(g, day, cx - 0.42f * s, cy - 0.5f * s, 0.34f * s, now);
            cloud(g, cx + 0.1f * s, cy + 0.12f * s, 0.95f * s, CLOUD);
            return;
        case 3: // overcast: two clouds drifting against each other
        {
            const float d = sinf(now / 1500.0f) * 0.12f * s;
            cloud(g, cx - 0.32f * s - d, cy - 0.38f * s, 0.75f * s, CLOUD_BACK);
            cloud(g, cx + 0.12f * s + d, cy + 0.1f * s, 0.95f * s, CLOUD);
            return;
        }
        case 45:
            fog(g, cx, cy, s, now, false);
            return;
        case 48:
            fog(g, cx, cy, s, now, true);
            return;
        // drizzle: small, slow drops (light 2, moderate 3, dense 4); freezing = a few ice crystals between them
        case 51:
            rainIcon(g, cx, cy, s, now, 2, false, false, true);
            return;
        case 53:
            rainIcon(g, cx, cy, s, now, 3, false, false, true);
            return;
        case 55:
            rainIcon(g, cx, cy, s, now, 4, false, false, true);
            return;
        case 56:
            rainIcon(g, cx, cy, s, now, 2, false, true, true);
            return;
        case 57:
            rainIcon(g, cx, cy, s, now, 3, false, true, true);
            return;
        // rain: light 2 drops, moderate 3, heavy 4 on a dark cloud
        case 61:
            rainIcon(g, cx, cy, s, now, 2, false, false, false);
            return;
        case 63:
            rainIcon(g, cx, cy, s, now, 3, false, false, false);
            return;
        case 65:
            rainIcon(g, cx, cy, s, now, 4, true, false, false);
            return;
        case 66:
            rainIcon(g, cx, cy, s, now, 3, false, true, false);
            return;
        case 67:
            rainIcon(g, cx, cy, s, now, 4, true, true, false);
            return;
        // snow
        case 71: // slight: a few big flakes
            snowIcon(g, cx, cy, s, now, 2, 0.14f, 1500.0f);
            return;
        case 73: // moderate: the snowflake of the design, turning
            snowflake(g, cx, cy, s, now);
            return;
        case 75: // heavy: many flakes, quick
            snowIcon(g, cx, cy, s, now, 5, 0.12f, 950.0f);
            return;
        case 77: // snow grains: lots of tiny dots
            snowIcon(g, cx, cy, s, now, 6, 0.0f, 800.0f);
            return;
        // rain showers: the sky peeks out behind the cloud
        case 80:
            peekIcon(g, day, cx, cy, s, now, 1, 2, false);
            return;
        case 81:
            peekIcon(g, day, cx, cy, s, now, 1, 3, false);
            return;
        case 82:
            peekIcon(g, day, cx, cy, s, now, 1, 4, true);
            return;
        case 85:
            peekIcon(g, day, cx, cy, s, now, 2, 3, false);
            return;
        case 86:
            peekIcon(g, day, cx, cy, s, now, 2, 4, false);
            return;
        case 95: // thunderstorm
        case 96: // ... with slight hail
        case 99: // ... with heavy hail
        {
            // the flash lights the cloud up; draw the cloud first, so find out about the flash from the same clock
            const float t = cyc(now, 2200.0f, 0.0f);
            const bool flash = t < 0.07f || (t > 0.14f && t < 0.22f);
            cloud(g, cx, cy - 0.34f * s, 0.85f * s, flash ? mixColor(CLOUD_DARK, C_WHITE, 0.35f) : CLOUD_DARK);
            bolt(g, cx, cy + 0.05f * s, s, now);
            if (code == 96) // slight hail: a stone on each side of the bolt
                flakes(g, cx, cy + 0.3f * s, cy + 0.95f * s, 1.3f * s, 2, now, 620.0f, 0.15f * s, 0.0f, HAIL);
            else if (code == 99) // heavy hail: two on each side
            {
                flakes(g, cx - 0.65f * s, cy + 0.3f * s, cy + 0.95f * s, 0.34f * s, 2, now, 560.0f, 0.15f * s, 0.0f, HAIL);
                flakes(g, cx + 0.65f * s, cy + 0.3f * s, cy + 0.95f * s, 0.34f * s, 2, now + 280, 560.0f, 0.15f * s, 0.0f, HAIL);
            }
            return;
        }
        default: // anything else: a plain cloud
            cloud(g, cx, cy + 0.05f * s, s, CLOUD);
            return;
        }
    }
} // namespace disp
