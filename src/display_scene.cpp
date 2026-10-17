// display_scene.cpp - weather glyphs (sun, moon, clouds ...), the face background scene and the weather icon.
#include "display_internal.h"
#include "weather_mood.h"

namespace disp
{

    void drawSpinner(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
    {
        uint8_t head = (millis() / 90) % 8;
        for (uint8_t i = 0; i < 8; i++)
        {
            float a = i * ((float)M_PI / 4.0f);
            uint8_t age = (uint8_t)((head - i) & 7);
            g.fillCircle(cx + (int16_t)(r * cosf(a)), cy + (int16_t)(r * sinf(a)),
                         imax(1, u(2)), tint(c, (255 - age * 30) / 255.0f));
        }
    }

    void drawCloud(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t c)
    {
        g.fillCircle(cx - r * 0.6f, cy + r * 0.2f, r * 0.40f, c);
        g.fillCircle(cx - r * 0.1f, cy - r * 0.15f, r * 0.55f, c);
        g.fillCircle(cx + r * 0.55f, cy + r * 0.15f, r * 0.42f, c);
        g.fillRect(cx - r * 0.6f, cy + r * 0.2f, r * 1.15f, r * 0.4f, c);
    }

    static void drawSun(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint32_t now, bool withRays)
    {
        uint16_t sun = rgb(255, 200, 40);
        g.fillCircle(cx, cy, r * 0.62f, dimColor(sun, 90));
        g.fillCircle(cx, cy, r * 0.5f, sun);
        if (!withRays)
            return;
        float rot = now / 900.0f;
        for (uint8_t i = 0; i < 8; i++)
        {
            float a = rot + i * ((float)M_PI / 4.0f);
            int16_t x0 = cx + (int16_t)(r * 0.72f * cosf(a)), y0 = cy + (int16_t)(r * 0.72f * sinf(a));
            int16_t x1 = cx + (int16_t)(r * 1.0f * cosf(a)), y1 = cy + (int16_t)(r * 1.0f * sinf(a));
            g.drawLine(x0, y0, x1, y1, sun);
            g.drawLine(x0 + 1, y0, x1 + 1, y1, sun);
        }
    }

    // =====================================================================
    //  Face background scene (weather + time of day)
    // =====================================================================
    // All pieces are dim, procedural (derived from `now`, no per-drop state) and
    // drawn BEFORE the eyes, so the eyes always stay on top and in front.

    static void drawDimSun(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint32_t now, uint16_t sun)
    {
        g.fillCircle(cx, cy, r + u(3), tint(sun, 0.14f));
        g.fillCircle(cx, cy, r, dimColor(sun, 200));
        float rot = now / 1400.0f;
        for (uint8_t i = 0; i < 8; i++)
        {
            float a = rot + i * ((float)M_PI / 4.0f);
            g.drawLine(cx + (int16_t)(r * 1.35f * cosf(a)), cy + (int16_t)(r * 1.35f * sinf(a)),
                       cx + (int16_t)(r * 1.8f * cosf(a)), cy + (int16_t)(r * 1.8f * sinf(a)),
                       dimColor(sun, 150));
        }
    }

    // Moon with its real phase. phase: 0 new, 0.25 first quarter (right half lit),
    // 0.5 full, 0.75 last quarter (left half lit). The terminator is an ellipse, drawn
    // row by row: lit part of each row runs from the terminator to the limb.
    static void drawMoon(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, float phase, float bright)
    {
        uint16_t lit = dimColor(rgb(225, 230, 255), (uint8_t)(235.0f * bright));
#if !MOON_PHASE_ENABLED
        (void)phase;
        g.fillCircle(cx, cy, r, lit);
        g.fillCircle(cx + (int16_t)(r * 0.55f), cy - (int16_t)(r * 0.25f), (int16_t)(r * 0.85f), C_BG);
#else
        // the dark side is still faintly there (earthshine)
        g.fillCircle(cx, cy, r, tint(lit, 0.14f));

        float k = cosf(2.0f * (float)M_PI * phase); // 1 new .. 0 quarter .. -1 full
        bool waxing = phase < 0.5f;
        if ((1.0f - k) * 0.5f < 0.02f)
            return; // new moon: only the faint disc
        for (int16_t dy = -r; dy <= r; dy++)
        {
            float w = sqrtf((float)(r * r - dy * dy) + 0.25f);
            float t = k * w; // terminator x offset on this row
            int16_t x0, x1;
            if (waxing)
            {
                x0 = (int16_t)lroundf(t);
                x1 = (int16_t)lroundf(w);
            }
            else
            {
                x0 = (int16_t)lroundf(-w);
                x1 = (int16_t)lroundf(-t);
            }
            if (x1 >= x0)
                g.drawFastHLine(cx + x0, cy + dy, x1 - x0 + 1, lit);
        }
#endif
    }

    static void drawShootingStar(TFT_eSPI &g, uint32_t now)
    {
        uint32_t ph = now % 11000;
        if (ph >= 700)
            return;
        float p = ph / 700.0f;
        int16_t hx = (int16_t)(W * 0.88f - p * W * 0.45f);
        int16_t hy = (int16_t)(faceH * 0.06f + p * faceH * 0.30f);
        uint8_t v = (uint8_t)(255.0f * (1.0f - p));
        g.drawLine(hx, hy, hx + u(14), hy - u(6), dimColor(rgb(200, 215, 255), v / 3));
        g.drawLine(hx, hy, hx + u(7), hy - u(3), dimColor(rgb(235, 240, 255), v / 2));
        g.drawPixel(hx, hy, dimColor(C_WHITE, v));
    }

    // ---- Real wind ----
    // One smoothed wind state shared by clouds, fog, rain and snow. The scene is drawn without
    // per-object state, so the things that must keep their place when the wind changes (clouds, fog)
    // keep a position that is advanced every frame by the current wind speed instead of being
    // derived from the clock; a change of wind then only changes their speed, they never jump.
    static const uint8_t WIND_CLOUDS = 4;
    static const float CLOUD_SPEED_MULT[WIND_CLOUDS] = {1.0f, 0.8f, 0.65f, 0.55f}; // a little parallax
    static const float FOG_SPEED_MULT = 0.7f;

    struct WindView
    {
        bool init = false;
        uint32_t lastMs = 0;
        float kmh = 0.0f;   // smoothed wind speed
        float vx = 1.0f;    // smoothed sideways part of where the wind blows to: +1 right (east) .. -1 left (west)
        float slant = 0.0f; // same, but 0 when the direction is unknown (rain / snow then fall straight)
        float cloudPos[WIND_CLOUDS] = {};
        float fogPos = 0.0f;
    };
    static WindView wind;

    static int16_t cloudRadius(uint8_t i) { return u(9 + (i % 2) * 3); }
    static int16_t fogWidth() { return u(70); }

    static float wrapPos(float v, float span)
    {
        v = fmodf(v, span);
        return v < 0.0f ? v + span : v;
    }

    static int16_t wrapX(int v)
    {
        v %= W;
        return (int16_t)(v < 0 ? v + W : v);
    }

    // Blend the wind of the weather reading into the smoothed state and move clouds / fog on.
    static void windStep(uint32_t now, const SceneInfo &scn)
    {
        // target: what the weather reading says; gentle drift to the right while it is unknown
        float kmh = SCENE_WIND_FALLBACK_KMH, vx = 1.0f, slant = 0.0f;
#if SCENE_WIND_ENABLED
        if (scn.weatherKnown && scn.windKmh >= 0.0f)
        {
            kmh = scn.windKmh;
            if (scn.windDirDeg >= 0 && kmh >= 1.0f) // a direction means nothing in calm air
            {
                // windDirDeg is where the wind comes FROM; clouds go the opposite way. 0 = north,
                // clockwise, so the sideways part is sin(direction it blows to): east = right.
                vx = sinf((float)(scn.windDirDeg + 180) * (float)M_PI / 180.0f);
                slant = vx;
            }
        }
#else
        (void)scn;
#endif
        uint32_t dt = now - wind.lastMs;
        wind.lastMs = now;
        if (!wind.init)
        {
            wind.init = true;
            wind.kmh = kmh;
            wind.vx = vx;
            wind.slant = slant;
            dt = 0;
        }
        else
        {
            if (dt > 250) // the scene was not drawn for a while (diagnostics page): carry on from here
                dt = 33;
            float a = clamp01((float)dt / (float)SCENE_WIND_SMOOTH_MS);
            wind.kmh += (kmh - wind.kmh) * a;
            wind.vx += (vx - wind.vx) * a;
            wind.slant += (slant - wind.slant) * a;
        }

        // cloud speed in px/s. A north / south wind has no sideways part, so a small floor in the
        // direction of the (smoothed) sign keeps the sky from freezing.
        float spd = (SCENE_WIND_CLOUD_CALM_PXS + SCENE_WIND_CLOUD_PXS_PER_KMH * fminf(wind.kmh, SCENE_WIND_MAX_KMH)) * sc;
        float dir = wind.vx + 0.15f * fmaxf(-1.0f, fminf(1.0f, wind.vx * 10.0f));
        dir = fmaxf(-1.0f, fminf(1.0f, dir));
        float dx = spd * dir * (float)dt / 1000.0f;

        for (uint8_t i = 0; i < WIND_CLOUDS; i++)
            wind.cloudPos[i] = wrapPos(wind.cloudPos[i] + dx * CLOUD_SPEED_MULT[i], (float)(W + 4 * cloudRadius(i)));
        wind.fogPos = wrapPos(wind.fogPos + dx * FOG_SPEED_MULT, (float)(W + fogWidth()));
    }

    // Sideways pixels per pixel fallen for rain / snow: 0 = straight down, > 0 = blown to the right.
    static float windSlant(float maxSlant)
    {
        return wind.slant * clamp01(wind.kmh / SCENE_WIND_MAX_KMH) * maxSlant;
    }

    static void drawDriftClouds(TFT_eSPI &g, uint32_t now, uint8_t count, uint16_t color)
    {
        (void)now;
        for (uint8_t i = 0; i < count && i < WIND_CLOUDS; i++)
        {
            int16_t r = cloudRadius(i);
            float span = (float)(W + 4 * r);
            int16_t cx = (int16_t)wrapPos(wind.cloudPos[i] + i * (span / count), span) - 2 * r;
            drawCloud(g, cx, u(10 + i * 11), r, color);
        }
    }

    static void drawRainBg(TFT_eSPI &g, uint32_t now, uint8_t n)
    {
        uint16_t c = rgb(70, 140, 225);
        float k = windSlant(SCENE_WIND_RAIN_MAX_SLANT);
        int16_t len = imax(2, u(5));
        int16_t dxLine = (int16_t)lroundf(k * len);
        for (uint8_t i = 0; i < n; i++)
        {
            int16_t y = (int16_t)(((now / (4u + i % 3)) + i * 53u) % (uint32_t)(faceH + 12)) - 6;
            // a drop falls along the slanted line; centred on mid-height and wrapped at the sides,
            // so the rain stays evenly spread over the whole width however hard the wind blows
            int16_t x = wrapX((i * 37 + 11) % W + (int)lroundf(k * (y - faceH / 2)));
            g.drawLine(x, y, x + dxLine, y + len, c);
        }
    }

    static void drawSnowBg(TFT_eSPI &g, uint32_t now, uint8_t n)
    {
        uint16_t c = dimColor(C_WHITE, 230);
        float k = windSlant(SCENE_WIND_SNOW_MAX_SLANT);
        for (uint8_t i = 0; i < n; i++)
        {
            int16_t y = (int16_t)(((now / (24u + (i % 3) * 8u)) + i * 29u) % (uint32_t)faceH);
            int16_t x = wrapX((i * 41 + 7) % W + (int)lroundf(sinf(now / 700.0f + i) * u(3)) +
                              (int)lroundf(k * (y - faceH / 2)));
            if (i % 3 == 0)
                g.fillCircle(x, y, imax(1, u(1.2f)), c);
            else
                g.drawPixel(x, y, c);
        }
    }

    static void drawFogBg(TFT_eSPI &g, uint32_t now)
    {
        uint16_t c = tint(rgb(165, 180, 215), 0.33f);
        int16_t bw = fogWidth();
        (void)now;
        for (uint8_t i = 0; i < 4; i++)
        {
            int16_t x = (int16_t)wrapPos(wind.fogPos + i * 47.0f, (float)(W + bw)) - bw;
            int16_t y = (int16_t)(faceH * (0.18f + 0.20f * i));
            g.fillRoundRect(x, y, bw, imax(3, u(7)), imax(2, u(3)), c);
        }
    }

    // Dim flash of the whole sky plus a bolt in the right margin: two quick
    // flashes every ~6.5 s (never a rapid strobe).
    static void drawLightningBg(TFT_eSPI &g, uint32_t now)
    {
        uint32_t ph = now % 6500;
        if (!(ph < 60 || (ph > 150 && ph < 200)))
            return;
        g.fillRect(0, 0, W, faceH, tint(rgb(110, 130, 255), 0.18f));
        uint16_t bolt = dimColor(rgb(255, 230, 120), 200);
        int16_t x = (int16_t)(W * 0.88f);
        int16_t y1 = (int16_t)(faceH * 0.22f), y2 = (int16_t)(faceH * 0.50f);
        g.drawLine(x, 0, x - u(4), y1, bolt);
        g.drawLine(x - u(4), y1, x + u(1), y1, bolt);
        g.drawLine(x + u(1), y1, x - u(5), y2, bolt);
    }

    // ---- Sky gradient ----
    // Darker at the top, a soft glow of the accent colour toward the bottom edge (the horizon).
    // Drawn in horizontal bands; the band colours are cached and only recomputed when the themed
    // colours / glow change (about once a second at most), so a frame costs the same as the
    // flat fill it replaces.
    static void drawSkyGradient(TFT_eSPI &g, float glow, float flash)
    {
        const int MAXB = 64;
        static uint16_t col[MAXB];
        static uint16_t cBg = 0, cAcc = 0;
        static int cGlowQ = -1, cFaceH = -1;

        int16_t band = imax(u(2), (int16_t)((faceH + MAXB - 1) / MAXB));
        int n = (faceH + band - 1) / band;
        if (n > MAXB)
            n = MAXB;

        if (glow < 0)
            glow = 0;
        if (glow > 0.9f)
            glow = 0.9f;
        int glowQ = (int)(glow * 64.0f + 0.5f);
        if (cBg != C_BG || cAcc != C_ACCENT || cGlowQ != glowQ || cFaceH != faceH)
        {
            cBg = C_BG;
            cAcc = C_ACCENT;
            cGlowQ = glowQ;
            cFaceH = faceH;
            uint16_t top = mixColor(C_BG, 0x0000, SCENE_SKY_TOP_DARKEN);
            uint16_t horizon = mixColor(C_BG, C_ACCENT, glowQ / 64.0f);
            for (int i = 0; i < n; i++)
            {
                float t = ((i + 0.5f) * band) / (float)faceH; // 0 top .. 1 bottom
                float s = clamp01((t - 0.2f) / 0.8f);         // the upper fifth stays plain
                col[i] = mixColor(top, horizon, s * s);       // eased: the glow gathers at the bottom
            }
        }
        for (int i = 0; i < n; i++)
        {
            int16_t y = (int16_t)(i * band);
            int16_t h = imin(band, (int16_t)(faceH - y));
            if (h <= 0)
                continue;
            uint16_t c = col[i];
            if (flash > 0.01f) // sunrise: warm flash, strongest at the horizon (bottom)
            {
                float t = ((i + 0.5f) * band) / (float)faceH;
                c = mixColor(c, rgb(255, 196, 120), flash * AMBIENT_SUNRISE_PEAK * (0.15f + 0.85f * t * t));
            }
            g.fillRect(0, y, W, h, c);
        }
    }

    void drawSceneBackground(TFT_eSPI &g, uint32_t now, const SceneInfo &scn)
    {
        windStep(now, scn);
        int code = scn.weatherKnown ? scn.code : -1;
        bool partly = (code == 1 || code == 2);
        bool clearish = !scn.weatherKnown || code == 0 || partly; // sky is (mostly) visible
        bool fog = wxIsFog(code);
        bool rain = wxIsRain(code);
        bool snow = wxIsSnow(code);
        bool storm = wxIsStorm(code);
        bool overcast = scn.weatherKnown && !clearish && !fog && !rain && !snow && !storm; // code 3 etc.

        uint16_t cloudCol = tint(rgb(165, 180, 215), scn.day ? 0.55f : 0.41f);
        uint16_t darkCloud = tint(rgb(120, 130, 170), 0.59f);

        // sky gradient first (everything else is drawn over it); clouds / rain / fog hide the horizon glow
        float skyVis = clearish ? 1.0f : (fog ? 0.6f : 0.35f);
        if (SCENE_SKY_GRADIENT)
            drawSkyGradient(g, themeGlow * SCENE_SKY_GLOW_SCALE * skyVis, ambientSunriseFlash(now) * skyVis);

        if (storm)
            drawLightningBg(g, now);

        // sky: stars at night when it's clear enough to see them (also: no time yet = boot look)
        if (!scn.day && clearish)
            drawStars(g, now);
        if (!scn.day && scn.timeKnown && clearish)
            drawNightMotes(g, now); // a few faint specks drifting up

        // sun / moon, only once the time is known. With SCENE_SUN_ARC they travel along an arc
        // (left = rise, top = middle of the day / night, right = set); near the horizon the sun is
        // redder and both are dimmer. `lift` is 0 on the horizon .. 1 at the top of the arc.
        if (scn.timeKnown && clearish)
        {
            int16_t cx = u(16), cy = u(16);
            float lift = 1.0f;
            if (SCENE_SUN_ARC && scn.arc >= 0)
            {
                lift = sinf((float)M_PI * scn.arc);
                int16_t margin = u(14);
                cx = margin + (int16_t)((W - 2 * margin) * scn.arc);
                cy = (int16_t)(faceH * 0.42f - faceH * 0.30f * lift);
                cy = imax(cy, u(13)); // keep the top rays on the screen
            }
            if (scn.day)
                drawDimSun(g, cx, cy, u(7), now, mixColor(rgb(255, 110, 30), rgb(255, 205, 50), clamp01(lift * 1.6f)));
            else
            {
                drawMoon(g, cx, cy, u(8), scn.moonPhase, 0.55f + 0.45f * lift);
                if (!partly)
                    drawShootingStar(g, now);
            }
        }

        // weather layers
        if (clearish)
        {
            if (partly)
                drawDriftClouds(g, now, 2, cloudCol);
            else if (scn.day && scn.timeKnown)
                drawDriftClouds(g, now, 1, cloudCol); // a lone, faint cloud so a clear day isn't empty
        }
        else if (overcast)
            drawDriftClouds(g, now, 3, cloudCol);
        else if (fog)
            drawFogBg(g, now);
        else if (rain)
        {
            drawDriftClouds(g, now, 2, darkCloud);
            drawRainBg(g, now, 16);
        }
        else if (snow)
        {
            drawDriftClouds(g, now, 2, cloudCol);
            drawSnowBg(g, now, 18);
        }
        else if (storm)
        {
            drawDriftClouds(g, now, 3, darkCloud);
            drawRainBg(g, now, 20);
        }
    }

    void drawWeatherIcon(TFT_eSPI &g, int code, int16_t cx, int16_t cy, int16_t r, uint32_t now)
    {
        uint16_t lightCloud = rgb(215, 225, 245);
        uint16_t greyCloud = rgb(150, 165, 195);
        uint16_t darkCloud = rgb(95, 105, 140);
        uint16_t rain = rgb(80, 170, 255);

        if (code == 0)
        {
            drawSun(g, cx, cy, r, now, true);
        }
        else if (code == 1 || code == 2)
        {
            drawSun(g, cx - r * 0.35f, cy - r * 0.3f, r * 0.75f, now, false);
            drawCloud(g, cx + r * 0.15f, cy + r * 0.25f, r * 0.85f, lightCloud);
        }
        else if (wxIsFog(code))
        {
            for (int i = 0; i < 3; i++)
            {
                int16_t sway = (int16_t)(sinf(now / 600.0f + i * 1.3f) * r * 0.15f);
                g.fillRoundRect(cx - r * 0.8f + sway + i * r * 0.1f, cy - r * 0.5f + i * r * 0.5f,
                                r * 1.4f, imax(2, (int16_t)(r * 0.22f)), 2, greyCloud);
            }
        }
        else if (wxIsRain(code))
        {
            drawCloud(g, cx, cy - r * 0.3f, r * 0.9f, greyCloud);
            for (int i = 0; i < 3; i++)
            {
                int16_t dy = (int16_t)(((now / 90) + i * 3) % 6) * r * 0.1f;
                int16_t x = cx - r * 0.5f + i * r * 0.5f;
                int16_t y = cy + r * 0.45f + dy;
                g.drawLine(x, y, x - 2, y + r * 0.3f, rain);
            }
        }
        else if (wxIsSnow(code))
        {
            drawCloud(g, cx, cy - r * 0.3f, r * 0.9f, greyCloud);
            for (int i = 0; i < 3; i++)
            {
                int16_t dy = (int16_t)(((now / 140) + i * 2) % 5) * r * 0.1f;
                int16_t x = cx - r * 0.5f + i * r * 0.5f + (int16_t)(sinf(now / 300.0f + i) * 2);
                g.fillCircle(x, cy + r * 0.5f + dy, imax(1, r / 9), C_WHITE);
            }
        }
        else if (wxIsStorm(code))
        {
            drawCloud(g, cx, cy - r * 0.3f, r * 0.9f, darkCloud);
            if ((now / 300) % 3 != 0)
            {
                uint16_t bolt = rgb(255, 220, 50);
                g.fillTriangle(cx + r * 0.2f, cy + r * 0.1f, cx - r * 0.2f, cy + r * 0.55f, cx + r * 0.05f, cy + r * 0.55f, bolt);
                g.fillTriangle(cx - r * 0.05f, cy + r * 0.5f, cx + r * 0.25f, cy + r * 0.5f, cx - r * 0.15f, cy + r * 1.0f, bolt);
            }
        }
        else
        {
            drawCloud(g, cx, cy, r, lightCloud);
        }
    }

    // Where the sun / moon is, as a gaze target for the eyes (-1..1 sideways, up is negative). false = not visible
    // (no time yet, or the sky is covered). Same arc as drawSceneBackground().
    bool celestialGaze(const SceneInfo &scn, float &gx, float &gy)
    {
        int code = scn.weatherKnown ? scn.code : -1;
        bool clearish = !scn.weatherKnown || code == 0 || code == 1 || code == 2;
        if (!scn.timeKnown || !clearish)
            return false;
        int16_t cx = u(16), cy = u(16);
        if (SCENE_SUN_ARC && scn.arc >= 0)
        {
            float lift = sinf((float)M_PI * scn.arc);
            int16_t margin = u(14);
            cx = margin + (int16_t)((W - 2 * margin) * scn.arc);
            cy = (int16_t)(faceH * 0.42f - faceH * 0.30f * lift);
            cy = imax(cy, u(13));
        }
        gx = (cx - W / 2.0f) / (W / 2.0f);
        gy = (cy - faceH * 0.55f) / (faceH * 0.40f);
        gx = gx < -1 ? -1 : (gx > 1 ? 1 : gx);
        gy = gy < -1 ? -1 : (gy > 0.2f ? 0.2f : gy);
        return true;
    }

} // namespace disp