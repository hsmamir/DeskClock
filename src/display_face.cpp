// display_face.cpp - the face: pet reaction expressions and drawFace() (eyes, brows, sleepy Zs ...).
#include "display_internal.h"
#include "display_character.h"
#include "weather_mood.h"
#include "display_wx.h"

namespace disp
{

    // ---- Petted state: the servo only wiggles for a moment, so the display keeps the
    // reaction face on its own for pet_hold_ms(kind). pet.cpp decides WHICH reaction
    // (petEv.kind); a new tap (petEv.seq changes) restarts the animation (see display_tick). ----
    PetEvent petEv = {PetKind::HAPPY, 0, 0, 0};
    uint32_t happyStartMs = 0;

    // Distance from the eye line to the middle of the mouth. `oldHalf` is what the old LED eyes used:
    // the old mouth sat at cy + oldHalf + u(10); the clock character has its own mouth line.
    static int16_t mouthDyFor(int16_t oldHalf)
    {
#if CHARACTER_STYLE
        (void)oldHalf;
        return charMouthDy();
#else
        return oldHalf + u(10);
#endif
    }

    // =====================================================================
    //  Pet reactions (what pet.cpp picked; see PetKind)
    // =====================================================================

    // Small hearts rising from above the eyes. `age` = ms since they started; heart i starts
    // i*stepMs later and lives lifeMs.
    static void drawRisingHearts(TFT_eSPI &g, uint32_t age, int16_t cy, int16_t R, uint8_t n,
                                 uint16_t stepMs, uint16_t lifeMs, int16_t size, uint16_t col = C_HEART)
    {
        for (uint8_t i = 0; i < n; i++)
        {
            int32_t a = (int32_t)age - (int32_t)i * stepMs;
            if (a < 0 || a > lifeMs)
                continue;
            float p = a / (float)lifeMs;
            float frac = fmodf(0.30f + i * 0.382f, 1.0f); // spread the hearts without a pattern
            int16_t hx = (int16_t)(W * (0.20f + 0.60f * frac)) + (int16_t)(sinf(p * 6.0f + i) * u(4));
            int16_t hy = (int16_t)(cy - R - u(2) - p * (cy - R - u(8)));
            drawHeart(g, hx, hy, size, tint(col, (255 - p * 140) / 255.0f));
        }
    }

    // A heart of total width 2*s centred on (cx, cyc) (drawHeart() takes the top-lobe line).
    static void drawHeartCentered(TFT_eSPI &g, int16_t cx, int16_t cyc, int16_t s, uint16_t c)
    {
        drawHeart(g, cx, cyc - imax(1, s / 2), s, c);
    }

    // ---- "!" and the anger mark, drawn as shapes (no font glyphs) ----
    // The anti-aliased shapes blend toward the pixels already in the face sprite: fillSmooth* reads them per
    // pixel (bg = 0x00FFFFFF), the arcs take one colour sampled at the middle of the mark. Without a sprite
    // (viewport fallback on the real TFT, which has no MISO to read back) they blend toward the plain background.
    static uint32_t aaBgPerPixel() { return faceCv.ok ? 0x00FFFFFFu : (uint32_t)C_BG; }
    static uint32_t aaBgAt(TFT_eSPI &g, int16_t x, int16_t y) { return faceCv.ok ? (uint32_t)g.readPixel(x, y) : (uint32_t)C_BG; }

    // "!" of total height h centred on (cx, cyc): a rounded bar and a dot.
    static void drawBang(TFT_eSPI &g, int16_t cx, int16_t cyc, int16_t h, uint16_t c)
    {
#if CHARACTER_STYLE
        drawCharMono(g, CharMono::BANG, cx, cyc, h / 17.0f, c); // the sprite is 17 px tall
        return;
#endif
        int16_t bw = imax(3, (int16_t)(h * 0.26f + 0.5f)); // bar width
        int16_t dotR = imax(2, (int16_t)((bw + 1) / 2));   // dot radius
        int16_t barH = imax((int16_t)(bw + 1), (int16_t)(h * 0.55f + 0.5f));
        int16_t top = cyc - h / 2;
        uint32_t bg = aaBgPerPixel();
        g.fillSmoothRoundRect(cx - bw / 2, top, bw, barH, bw / 2, c, bg);
        g.fillSmoothCircle(cx, top + h - dotR, dotR, c, bg);
    }

    // Anger mark (manga style): four small arcs, one per corner of an imaginary square around (cx, cy), each
    // bending away from the middle. drawSmoothArc angles: 0 = 6 o'clock, clockwise (90 = 9, 180 = 12, 270 = 3).
    static void drawAngerMark(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t c)
    {
#if CHARACTER_STYLE
        drawCharMono(g, CharMono::ANGER, cx, cy, sc, c);
        return;
#endif
        int16_t d = u(3);             // corner centres sit at (+-d, +-d) from the middle
        int16_t R = imax(3, u(4.5f)); // outer radius
        int16_t ir = imax(1, R - 2);  // inner radius (stroke = R - ir + 1 px)
        uint32_t bg = aaBgAt(g, cx, cy);
        g.drawSmoothArc(cx - d, cy - d, R, ir, 90, 180, c, bg);  // top-left
        g.drawSmoothArc(cx + d, cy - d, R, ir, 180, 270, c, bg); // top-right
        g.drawSmoothArc(cx + d, cy + d, R, ir, 270, 360, c, bg); // bottom-right
        g.drawSmoothArc(cx - d, cy + d, R, ir, 0, 90, c, bg);    // bottom-left
    }

    // Overshooting ease-out (t 0..1): reaches 1 with a ~10 % overshoot on the way.
    static float easeOutBack(float t)
    {
        const float c1 = 1.70158f, c3 = c1 + 1.0f;
        float q = t - 1.0f;
        return 1.0f + c3 * q * q * q + c1 * q * q;
    }

    // Closed, contented eye "‿" (sleepy pet, purring): a thin arch curving downwards + blush.
    void drawContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
    {
#if CHARACTER_STYLE
        charContentEye(g, cx, cy, R);
        return;
#endif
        float rx = R * 0.9f, ry = R * 0.5f;
        int16_t th = imax(2, (int16_t)(R * 0.22f));
        int16_t baseY = cy - R / 6;
        for (int a = 0; a <= 180; a += 10)
        {
            float t = a * (float)M_PI / 180.0f;
            int16_t x = cx + (int16_t)(rx * cosf(t));
            int16_t y = baseY + (int16_t)(ry * sinf(t));
            g.fillCircle(x, y, th, mixColor(C_EYE_TOP, C_EYE_BOT, 0.35f + 0.4f * sinf(t)));
        }
        drawCheek(g, cx, cy + R + u(2), R / 2 + u(1), imax(2, R / 5), 1.0f);
    }

    // Heart used as an eye: pulsing, with a glow and a highlight.
    static void drawHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, uint32_t age)
    {
        float pulse = 1.0f + 0.10f * sinf(age / 85.0f);
#if CHARACTER_STYLE
        charHeartEye(g, cx, cy, R, pulse);
        return;
#endif
        int16_t s = (int16_t)(R * 1.55f * pulse);
        drawHeartCentered(g, cx, cy, s + u(5), tint(C_HEART, 0.28f)); // glow
        drawHeartCentered(g, cx, cy, s, C_HEART);
        int16_t r = imax(1, s / 2);
        g.fillCircle(cx - r, cy - r, imax(1, r / 3), C_WHITE); // highlight on the left lobe
    }

    // Spiral for the dizzy eyes.
    static void drawSpiral(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase)
    {
#if CHARACTER_STYLE
        charSpiralEye(g, cx, cy, R, phase);
        return;
#endif
        const int steps = 36;
        int16_t dot = imax(1, (int16_t)(R * 0.16f));
        for (int i = 0; i <= steps; i++)
        {
            float f = (float)i / steps;
            float ang = f * 2.5f * 2.0f * (float)M_PI + phase;
            float rr = R * 1.05f * f;
            g.fillCircle(cx + (int16_t)(rr * cosf(ang)), cy + (int16_t)(rr * sinf(ang)), dot,
                         mixColor(C_EYE_TOP, C_EYE_BOT, f));
        }
    }

    // Raised eyebrow: an arch above the eye, `lift` px higher than its resting place.
    static void drawRaisedBrow(TFT_eSPI &g, int16_t cx, int16_t eyeTop, int16_t w, int16_t lift)
    {
        int16_t th = imax(2, u(3));
        uint16_t col = mixColor(C_EYE_TOP, C_EYE_BOT, 0.15f);
        for (int i = 0; i <= 10; i++)
        {
            float t = i / 10.0f;
            int16_t x = cx - w / 2 + (int16_t)(t * w);
            int16_t y = eyeTop - u(7) - lift - (int16_t)(sinf(t * (float)M_PI) * u(4));
            g.fillCircle(x, y, th / 2 + 1, col);
        }
    }

    // Pet kinds that start with normal eyes (and draw them themselves) before any decoration shows up.
    // All other kinds are decoration from the first frame: the eyes cross-fade into them (see drawFace).
    static bool petEyesFirst(PetKind k)
    {
        switch (k)
        {
        case PetKind::RAISED_BROW:
        case PetKind::ANNOYED:
        case PetKind::MISSED_YOU:
        case PetKind::MORNING:
        case PetKind::RARE_WINK:
        case PetKind::RARE_SURPRISE:
        case PetKind::SORRY:
            return true;
        default:
            return false;
        }
    }

    // How strong the temperature is for the pet reaction that is showing (0..1, weather_mood.cpp); drawFace() sets it
    // before drawPetFace(). The pet's hot / cold faces use the same grading and the same burst shiver as the idle ones.
    static float petTempK = 1.0f;
    static float coldShiverEnv(uint32_t now, bool active); // below, with the idle cold face

    // pop 0..1(+overshoot): how far the decorative glyphs (heart / spiral / arch eyes) have grown.
    static void drawPetFace(TFT_eSPI &g, uint32_t now, bool purr, bool sleepy, int16_t cy, int16_t lcx,
                            int16_t rcx, int16_t R, int16_t eyeW, int16_t eyeH, float pop)
    {
        uint32_t age = now - happyStartMs;
        int16_t Re = imax(1, (int16_t)(R * pop)); // glyph radius; positions below keep using the full R
        uint16_t dropCol = rgb(120, 200, 255);

        if (purr) // button held: purring
        {
            int16_t j = ((now / 45) & 1) ? 1 : -1; // tiny vibration
            drawContentEye(g, lcx + j, cy, Re);
            drawContentEye(g, rcx + j, cy, Re);
            drawRisingHearts(g, now % 1800, cy, R, 2, 700, 1100, u(3));
            if (sleepy)
                drawSleepZ(g, now, cy, R);
            return;
        }

        switch (petEv.kind)
        {
        case PetKind::HEART_EYES:
            drawHeartEye(g, lcx, cy, Re, age);
            drawHeartEye(g, rcx, cy, Re, age);
            drawRisingHearts(g, age, cy, R, 6, 160, 1100, u(4));
            break;

        case PetKind::SLEEPY:
            drawContentEye(g, lcx, cy, Re);
            drawContentEye(g, rcx, cy, Re);
            drawRisingHearts(g, age, cy, R, 1, 0, 1800, u(3));
            drawSleepZ(g, now, cy, R);
            break;

        case PetKind::RAISED_BROW:
            if (age < 1100) // "rain? really?": one wide eye with a raised brow, the other squints
            {
                float e = clamp01(age / 250.0f);
                int16_t ew = (int16_t)(eyeW * 1.1f), eh = (int16_t)(eyeH * 1.12f);
                drawEye(g, lcx, cy, ew, eh, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
                drawEye(g, rcx, cy + u(1), eyeW, eyeH, 1.0f, 0.38f, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
                drawRaisedBrow(g, lcx, cy - eh / 2, ew, (int16_t)(e * u(4)));
                float pd = (age % 700) / 700.0f; // a raindrop falls past
                drawDrop(g, (int16_t)(W * 0.84f), (int16_t)(u(6) + pd * cy), imax(2, u(2.5f)), tint(dropCol, 0.8f));
            }
            else // ... and then it smiles anyway
            {
                drawHappyEye(g, lcx, cy, Re);
                drawHappyEye(g, rcx, cy, Re);
                drawRisingHearts(g, age - 1100, cy, R, 1, 0, 1100, u(4));
            }
            break;

        case PetKind::DIZZY:
        {
            float ph = age / 90.0f;
            int16_t sway = (int16_t)(sinf(age / 140.0f) * u(3));
            drawSpiral(g, lcx + sway, cy, Re, ph);
            drawSpiral(g, rcx + sway, cy, Re, -ph);
            for (uint8_t k = 0; k < 3; k++) // little stars circling the head
            {
                float a = age / 200.0f + k * 2.094f;
                int16_t sx = W / 2 + (int16_t)(cosf(a) * R * 2.4f);
                int16_t sy = cy - R - u(8) + (int16_t)(sinf(a) * u(4));
#if CHARACTER_STYLE
                drawCharMono(g, CharMono::SPARK, sx, sy, 0.7f * sc, C_AMBER);
#else
                g.fillCircle(sx, sy, imax(1, u(2)), C_AMBER);
#endif
            }
            break;
        }

        case PetKind::ANNOYED:
        {
            int16_t shake = ((now / 50) & 1) ? u(1) : -u(1);
            uint16_t top = mixColor(C_EYE_TOP, C_ALARM_TOP, 0.5f), bot = mixColor(C_EYE_BOT, C_ALARM_BOT, 0.5f);
            drawEye(g, lcx + shake, cy, eyeW, eyeH, 0.7f, 0, BROW_ANGRY, -1, top, bot, false);
            drawEye(g, rcx + shake, cy, eyeW, eyeH, 0.7f, 0, BROW_ANGRY, 1, top, bot, false);
            drawAngerMark(g, W - u(22), cy - R - u(6), C_RED);
            break;
        }

        case PetKind::MISSED_YOU:
            if (age < 700) // wide eyes: "you're back!"
            {
                float s = 1.0f + 0.3f * clamp01(age / 250.0f);
                int16_t ew = (int16_t)(eyeW * s), eh = (int16_t)(eyeH * s);
                drawEye(g, lcx, cy, ew, eh, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
                drawEye(g, rcx, cy, ew, eh, 1.0f, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
            }
            else // happy hops + heart burst
            {
                float q = clamp01((age - 700) / 1500.0f);
                int16_t dy = -(int16_t)(fabsf(sinf((age - 700) / 130.0f)) * u(5) * (1.0f - q));
                drawHappyEye(g, lcx, cy + dy, Re);
                drawHappyEye(g, rcx, cy + dy, Re);
                drawRisingHearts(g, age - 700, cy, R, 5, 170, 1100, u(4));
            }
            break;

        case PetKind::SORRY:
            if (age < 1600) // a small apology: sad eyes, a sweat drop
            {
                drawEye(g, lcx, cy, eyeW, eyeH, 1.0f, 0, BROW_SAD, -1, C_EYE_TOP, C_EYE_BOT, true);
                drawEye(g, rcx, cy, eyeW, eyeH, 1.0f, 0, BROW_SAD, 1, C_EYE_TOP, C_EYE_BOT, true);
                float pd = (age % 1400) / 1400.0f;
                drawDrop(g, lcx - eyeW / 2 - u(9), (int16_t)(cy - R + pd * R * 2.2f), imax(2, u(3)),
                         tint(dropCol, (255 - pd * 140) / 255.0f));
            }
            else // ... forgiven: a shy smile and one small heart
            {
                drawHappyEye(g, lcx, cy, Re);
                drawHappyEye(g, rcx, cy, Re);
                drawRisingHearts(g, age - 1600, cy, R, 1, 0, 1100, u(3));
            }
            break;

        case PetKind::MORNING:
            if (age < 1500) // yawn
            {
                float env = sinf(clamp01(age / 1500.0f) * (float)M_PI);
                float open = 1.0f - 0.92f * env;
                drawEye(g, lcx, cy, eyeW, eyeH, open, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
                drawEye(g, rcx, cy, eyeW, eyeH, open, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
                int16_t my = cy + mouthDyFor(R);
                int16_t rx = u(2) + (int16_t)(u(7) * env), ry = u(2) + (int16_t)(u(6) * env);
                if (!faceMaskUp) // behind the mask there is no yawn to see
                {
                    g.fillEllipse(W / 2, my, rx, ry, dimColor(C_HEART, 200));
                    if (rx > 3 && ry > 3)
                        g.fillEllipse(W / 2, my, rx - 2, ry - 2, rgb(40, 6, 22));
                }
            }
            else // ... then a happy good morning
            {
                drawHappyEye(g, lcx, cy, Re);
                drawHappyEye(g, rcx, cy, Re);
                drawRisingHearts(g, age - 1500, cy, R, 2, 260, 1100, u(4));
            }
            break;

        case PetKind::RARE_WINK:
            drawEye(g, lcx, cy, eyeW, eyeH, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawHappyEye(g, rcx, cy, Re); // winking eye
            if (!faceMaskUp)              // (no tongue out through a mask)
            {
                // tongue hangs from the smile (drawn before the mouth arc, which then covers its top edge)
                int16_t my = cy + eyeH / 2 + u(10);
                uint16_t tc = tint(C_CHEEK, eyesMouthAlpha());
                g.fillRoundRect(W / 2 - u(3), my + u(1), u(6), u(7), u(3), tc);
                g.drawFastVLine(W / 2, my + u(2), u(4), dimColor(tc, 160));
            }
            drawRisingHearts(g, age, cy, R, 2, 250, 1100, u(4));
            break;

        case PetKind::RARE_SURPRISE:
            if (age < 800)
            {
                float s, lid = 0;
                if (age < ANTICIPATE_MS) // anticipation: the eyes squint a little first ...
                {
                    float p = age / (float)ANTICIPATE_MS;
                    s = 1.0f - 0.15f * p;
                    lid = 0.15f * p;
                }
                else // ... then pop wide open with an overshoot
                    s = 0.85f + 0.45f * easeOutBack(clamp01((age - ANTICIPATE_MS) / 220.0f));
                int16_t ew = (int16_t)(eyeW * s), eh = (int16_t)(eyeH * s);
                drawEye(g, lcx, cy, ew, eh, 1.0f, lid, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
                drawEye(g, rcx, cy, ew, eh, 1.0f, lid, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
                if (age >= ANTICIPATE_MS)
                {
                    drawBang(g, W / 2, cy - u(2), u(19), C_AMBER);
                }
            }
            else // a big heart pops up between the happy eyes
            {
                drawHappyEye(g, lcx, cy, Re);
                drawHappyEye(g, rcx, cy, Re);
                float pop = clamp01((age - 800) / 200.0f);
                float pulse = 1.0f + 0.12f * sinf(age / 90.0f);
                drawHeartCentered(g, W / 2, cy - R * 2, (int16_t)(u(8) * pop * pulse) + 1, C_HEART);
            }
            break;

        case PetKind::HOT:
        {
            // The same character as the idle hot face, only happy: relaxed arches, a WARM blush instead of the pink
            // one, the slow sweat drop at the right temple (when it is really hot), at most one small warm heart.
            cheekColor = rgb(255, 150, 70);
            drawHappyEye(g, lcx, cy, Re);
            drawHappyEye(g, rcx, cy, Re);
            cheekColor = C_CHEEK;
            if (petTempK >= 0.5f)
            {
                float pd = fminf(1.0f, age / 3400.0f);
                drawDrop(g, rcx + eyeW / 2 + u(9), (int16_t)(cy - R + pd * R * 2.2f), imax(2, u(3)),
                         tint(dropCol, (255 - pd * 140) / 255.0f));
            }
            if (PET_HOT_HEARTS > 0)
                drawRisingHearts(g, age, cy, R, PET_HOT_HEARTS, 0, 1100, u(3), rgb(255, 140, 60));
            break;
        }

        case PetKind::COLD:
        {
            // The same shiver as the idle cold face (bursts, up to 1-2 px, only when it is really cold), a pale blue
            // blush, snowflakes, one heart instead of two.
            int16_t sh = 0;
            const float env = coldShiverEnv(now, petTempK > 0.35f);
            if (env > 0)
                sh = (((now / 40) & 1) ? 1 : -1) * (int16_t)(u(1) * petTempK * 2.0f * env + 0.5f);
            cheekColor = rgb(130, 185, 255);
            drawHappyEye(g, lcx + sh, cy, Re);
            drawHappyEye(g, rcx + sh, cy, Re);
            cheekColor = C_CHEEK;
            for (uint8_t k = 0; k < 4; k++) // snowflakes drifting down
            {
                int16_t sx = (int16_t)(W * (0.15f + 0.2f * k)) + (int16_t)(sinf(age / 300.0f + k) * u(3));
                int16_t sy = (int16_t)((age / 9 + k * 23) % faceH);
                g.fillCircle(sx, sy, imax(1, u(1.5f)), C_ICE);
            }
            if (PET_COLD_HEARTS > 0)
                drawRisingHearts(g, age, cy, R, PET_COLD_HEARTS, 0, 1100, u(3), mixColor(C_HEART, C_ICE, 0.35f));
            break;
        }

        case PetKind::HAPPY:
        default:
            drawHappyEye(g, lcx, cy, Re);
            drawHappyEye(g, rcx, cy, Re);
            drawRisingHearts(g, age, cy, R, 3, 220, 1100, u(4));
            break;
        }
    }

    // Which mouth belongs to a pet reaction at this moment (age = ms since the tap).
    static MouthKind petMouth(bool purr, uint32_t age)
    {
        if (purr)
            return MouthKind::CUTE;
        switch (petEv.kind)
        {
        case PetKind::SLEEPY:
            return MouthKind::CUTE;
        case PetKind::HEART_EYES:
            return age >= ANTICIPATE_MS ? MouthKind::SMILE : MouthKind::NONE;
        case PetKind::RAISED_BROW:
            return age >= 1100 ? MouthKind::SMILE : MouthKind::NONE;
        case PetKind::MISSED_YOU:
            return age >= 700 ? MouthKind::SMILE : MouthKind::NONE;
        case PetKind::SORRY:
            return age >= 1600 ? MouthKind::SMILE : MouthKind::NONE;
        case PetKind::MORNING:
            return age >= 1500 ? MouthKind::SMILE : MouthKind::NONE; // the yawn draws its own mouth
        case PetKind::RARE_SURPRISE:
            if (age < ANTICIPATE_MS)
                return MouthKind::NONE;
            return age < 800 ? MouthKind::OPEN_O : MouthKind::SMILE;
        case PetKind::DIZZY:
        case PetKind::ANNOYED:
            return MouthKind::NONE;
        default: // HAPPY, RARE_WINK, HOT, COLD
            return MouthKind::SMILE;
        }
    }

    // The shiver of a cold day in bursts (SHIVER_*): 0 .. 1, how strong it is right now. `active` = the cold face is
    // showing; when it is not, the schedule is forgotten, so every cold moment starts with a burst after a short delay.
    static float coldShiverEnv(uint32_t now, bool active)
    {
        static bool on = false;
        static uint32_t start = 0, len = 0, next = 0;
        static float peak = 1.0f;
        if (!active)
        {
            on = false;
            next = 0;
            return 0;
        }
        if (next == 0)
            next = now + 250;
        if (!on)
        {
            if ((int32_t)(now - next) < 0)
                return 0;
            on = true;
            start = now;
            len = random(SHIVER_BURST_MIN_MS, SHIVER_BURST_MAX_MS);
            peak = 0.6f + 0.4f * (random(0, 101) / 100.0f); // every burst a little different
        }
        uint32_t el = now - start;
        if (el >= len)
        {
            on = false;
            next = now + random(SHIVER_GAP_MIN_MS, SHIVER_GAP_MAX_MS);
            return 0;
        }
        float e = sinf((float)M_PI * (float)el / (float)len); // swells and fades
        return sqrtf(e < 0 ? 0 : e) * peak;                   // sqrt: a longer strong middle
    }

    // Both eyes from the spring rig. squeeze 0..1 = extra lid closing (yawn), dx = shiver offset.
    static void drawRigEyes(TFT_eSPI &g, int16_t lcx, int16_t rcx, int16_t cy, float squeeze, int16_t dx)
    {
        EyeShape sl = eyeShapeNow(0), sr = eyeShapeNow(1);
        if (squeeze > 0)
        {
            eyeCloseLid(sl, squeeze);
            eyeCloseLid(sr, squeeze);
        }
        drawEyeShape(g, lcx + dx, cy, sl, 1.0f, -1, C_EYE_TOP, C_EYE_BOT, true);
        drawEyeShape(g, rcx + dx, cy, sr, 1.0f, 1, C_EYE_TOP, C_EYE_BOT, true);
    }

    // Gust streaks flying across the card in the direction the wind blows (the WIND reaction); k = 0..1 strength.
    static void drawWindStreaks(TFT_eSPI &g, uint32_t now, const SceneInfo &scn, float k)
    {
        const float vx = windBlowX(scn);
        const int16_t dir = vx < 0 ? -1 : 1; // unknown direction: to the right
        const uint16_t col = tint(C_ICE, 0.30f + 0.25f * k);
        for (uint8_t i = 0; i < 3; i++)
        {
            const uint32_t period = (uint32_t)((1100 + i * 280) / (0.6f + 0.8f * k));
            const float p = (float)((now + i * 410UL) % period) / (float)period;
            const int16_t len = u(12 + 5 * i);
            const int16_t y = (int16_t)(faceH * (0.20f + 0.27f * i)) + (int16_t)(sinf(now / 260.0f + i * 2.0f) * u(1));
            const int16_t x0 = dir > 0 ? (int16_t)(-len + p * (W + len)) : (int16_t)(W - p * (W + len));
            const int16_t xa = dir > 0 ? x0 : x0 - len;
            g.drawFastHLine(xa, y, len, col);
            g.drawFastHLine(xa + dir * u(3), y + 2, len / 2, dimColor(col, 150));
        }
    }

    void drawFace(TFT_eSPI &g, uint32_t now, bool alarm, bool happy, bool purr, bool sleepy,
                  const SceneInfo &scn, Reaction reaction, float exprP)
    {
        int16_t R = W / 8; // same base size as before
        int16_t eyeW = R * 2;
        int16_t eyeH = (int16_t)(R * 2.25f);
        int16_t cy = (int16_t)(faceH * FACE_CY_FRAC) + occasionEyeDy(now, alarm); // breathing is the eye height now (eyesSetBreath), not the position; the occasion may make the eyes hop
        int16_t lcx = W / 2 - R * 2;
        int16_t rcx = W / 2 + R * 2;

        const int16_t maskDy = cy - (int16_t)(faceH * FACE_CY_FRAC); // the occasion's hop of the eyes: the mask hops too
        faceMaskUp = false;
        // Wake-up at boot: a black card, then a thin line grows from the middle; the lids open after that.
        if (persona.stage != PStage::RUN)
        {
            g.fillRect(0, 0, W, faceH, 0x0000);
            if (persona.stage == PStage::BOOT_LINE)
                drawBootLine(g, cy, R);
            return;
        }

        g.fillRect(0, 0, W, faceH, C_BG);

        // Background: weather/time-of-day scene. The alarm keeps the plain starry
        // look so nothing distracts from the alert.
#if LANDSCAPE_UI && !LANDSCAPE_SCENE
        // the new design: plain navy behind the eyes (the weather / time-of-day scene is optional, LANDSCAPE_SCENE 1)
        (void)scn;
#else
        if (SCENE_ENABLED && !alarm)
            drawSceneBackground(g, now, scn);
        else
            drawStars(g, now);
#endif
        if (!alarm)
            drawOccasionBack(g, now); // bats, snow, fireworks ... behind the eyes
        if (!alarm && reaction == Reaction::WIND && !happy)
            drawWindStreaks(g, now, scn, wxWindIntensity(scn.windKmh));

        // ---- Eye pose: situation -> presets -> spring targets. The springs run every frame, also
        // while the alarm / a pet reaction is drawn, so the eyes never jump when they come back. ----
        Expr ex = exprCur;
        // anticipation: before a big reaction (hearts, surprise) the eyes squash for ANTICIPATE_MS, then burst
        bool antic = false;
        if (!alarm && happy && !purr && petEv.kind == PetKind::HEART_EYES && now - happyStartMs < ANTICIPATE_MS)
            antic = true;
        if (!alarm && !happy && exprAnticipating(now))
            antic = true;
        bool decor = !alarm && !antic && happy && (purr || !petEyesFirst(petEv.kind)); // heart / spiral / arches take over
        // Drowsy / waking: the lids come from persona.lidBias (a ramp), not from the TIRED preset.
        EyeMood mood = pickEyeMood((sleepy && !persona.lidRamp) || persona.snoozed, reaction, ex);
        EyeMood moodR = (ex == Expr::WINK) ? EyeMood::HAPPY : mood; // a wink is just one eye's preset
        if (persona.frown)                                          // woken by a touch: a small frown before the smile
            mood = moodR = EyeMood::ANGRY;
        if (persona.startled) // woken by thunder: wide eyes, worried brows
            mood = moodR = EyeMood::ALARMED;
        float scale = 1.0f;
        if (reaction == Reaction::STORM && lightningActive(now))
            scale = 1.25f;           // flinch at every flash
        scale *= persona.scalePulse; // the chime widens the eyes once
        if (antic)
        {
            mood = moodR = EyeMood::COLD; // lid 0.15
            scale = 0.85f;
        }
        if (decor)
            scale = 0.0f; // eyes shrink away while the decoration grows
        // breathing: +-2 % eye height, 4 s period (6 s at night)
        uint32_t period = sleepy ? 6000 : 4000;
        eyesSetBreath(1.0f + 0.02f * sinf(6.2831853f * (float)(now % period) / (float)period));
        // mouth: only in reactions / one-off expressions, idle = eyes only
        MouthKind wantMouth = MouthKind::NONE;
        if (!alarm)
        {
            if (happy)
                wantMouth = petMouth(purr, now - happyStartMs);
            else if (!antic && ex == Expr::SURPRISE)
                wantMouth = MouthKind::OPEN_O;
            else if (ex == Expr::WINK)
                wantMouth = MouthKind::SMILE;
            else if (persona.relief > 0.15f && persona.relief < 0.7f)
                wantMouth = MouthKind::OPEN_O; // the sigh
            else if (persona.sneezeMouth)
                wantMouth = MouthKind::OPEN_O; // "ah ... choo!"
            else if (persona.hiccupHop > 0.3f)
                wantMouth = MouthKind::OPEN_O; // the "hic"
        }
        eyesSetMouth(wantMouth);
        int16_t mouthY = cy + mouthDyFor(eyeH / 2); // ~y 76 on the 128x88 face (the clock character has its own mouth line)
        eyesSetFast(antic);
        eyesSetDecor(decor);
        eyesSetLidBias(persona.lidBias);
        // warm / hot, cool / cold: the face is graded by the temperature (weather_mood.cpp); everything else is full strength
        float tempK = 1.0f;
        if (reaction == Reaction::HOT)
            tempK = wxHotIntensity(scn.tempC);
        else if (reaction == Reaction::COLD)
            tempK = wxColdIntensity(scn.tempC);
        else if (reaction == Reaction::WIND)
            tempK = wxWindIntensity(scn.windKmh);
        eyesSetMoodK(tempK);
        eyesSetCross(persona.cross * R * IDLE_FLYLAND_CROSS);
        if (ex == Expr::WINK && exprWinkLeft) // the wink is the right eye's preset; this time it is the left one
        {
            EyeMood tmp = mood;
            mood = moodR;
            moodR = tmp;
        }
        eyesSetTargets(mood, moodR, scale, lookX * u(5) + lookTremX, lookY * u(4) + lookTremY, lookX, R);
        eyesStep(now);

        if (!alarm)
            faceMaskUpdate(now); // the air alone decides; every path below that draws a face draws the mask

        if (alarm)
        {
            int16_t shake = alarmFlashOn ? u(2) : -u(2);
            float open = alarmFlashOn ? 1.0f : 0.3f;
            drawEye(g, lcx + shake, cy, eyeW, eyeH, open, 0, BROW_ANGRY, -1, C_ALARM_TOP, C_ALARM_BOT, false);
            drawEye(g, rcx + shake, cy, eyeW, eyeH, open, 0, BROW_ANGRY, 1, C_ALARM_TOP, C_ALARM_BOT, false);
            if (alarmFlashOn)
            {
                g.drawRect(0, 0, W, faceH, C_RED);
                g.drawRect(1, 1, W - 2, faceH - 2, C_RED);
            }
            drawBang(g, W / 2, cy, u(19), C_RED);
            return;
        }

        // fast asleep: closed contented eyes, Zs, now and then a twitch or a dream bubble
        if (persona.asleep && !happy)
        {
            drawSleepFace(g, now, cy, lcx, rcx, R);
            if (faceMaskUp)
                drawFaceMask(g, now, 0, maskDy, false); // he sleeps with it on
            return;
        }

        if (happy)
        {
            if (antic) // the squash before the hearts burst out
            {
                drawRigEyes(g, lcx, rcx, cy, 0, 0);
                if (faceMaskUp)
                    drawFaceMask(g, now, 0, maskDy, false);
                else
                    drawMouth(g, W / 2, mouthY);
                return;
            }
            // Cross-fade: the (shrinking) eyes stay underneath until they are gone, the glyphs grow on top.
            if (decor)
                drawRigEyes(g, lcx, rcx, cy, 0, 0);
            petTempK = petEv.kind == PetKind::HOT ? wxHotIntensity(scn.tempC) : (petEv.kind == PetKind::COLD ? wxColdIntensity(scn.tempC) : 1.0f);
            drawPetFace(g, now, purr, sleepy, cy, lcx, rcx, R, eyeW, eyeH, decor ? eyesDecorPop() : 1.0f);
            if (faceMaskUp) // the smile and the tongue are behind the mask
                drawFaceMask(g, now, 0, maskDy, false);
            else
                drawMouth(g, W / 2, mouthY);
            return;
        }

        // normal / sleepy eyes (the blink is the lid spring inside the rig)
        int16_t shiver = 0;
        // a cool day squints, only a cold one shivers, and in bursts (the pre-alarm quiver of the persona stays steady)
        const float shvEnv = coldShiverEnv(now, reaction == Reaction::COLD && tempK > 0.35f && !persona.shiver);
        if (persona.shiver)
            shiver = (((now / 40) & 1) ? 1 : -1) * u(1);
        else if (shvEnv > 0)
        {
            float amp = tempK * 2.0f * shvEnv; // up to 1 px when mildly cold, up to 2 px at full strength; the edges of a burst round to 0
            shiver = (((now / 40) & 1) ? 1 : -1) * (int16_t)(u(1) * amp + 0.5f);
        }
        // peek-a-boo: the eyes rise from below the card's bottom edge (the sprite clips them)
        int16_t cyE = cy + (int16_t)(persona.peek * ((faceH - cy) + eyeH / 2));
        if (persona.sneezeJerk > 0)
        {
            cyE += (int16_t)(persona.sneezeJerk * u(5)); // the sneeze kicks the eyes down and to one side
            shiver += (int16_t)(-persona.sneezeJerk * u(3));
        }
        if (persona.hiccupHop > 0)
            cyE -= (int16_t)(persona.hiccupHop * u(3)); // the hiccup lifts the eyes in a quick little jump

        // one-off idle expressions
        float squeeze = 0;
        if (ex == Expr::YAWN)
        {
            float edge = (exprP < 1.0f - exprP ? exprP : 1.0f - exprP) / 0.15f; // ease in/out
            if (edge > 1.0f)
                edge = 1.0f;
            squeeze = (0.62f + 0.30f * exprYawnSize) * edge; // eyes squeeze shut mid-yawn (less in a small yawn)
        }

        if (shvEnv > 0) // the eyes scrunch up a little during a burst
            squeeze = fmaxf(squeeze, 0.18f * shvEnv);

        // Very cold: the eyes are squeezed into "> <" (the design sheet), with the shiver marks at the sides and a snowflake
        // above; a cool day keeps the squinting round eyes of the rig. Snow is an "excited" moment: a ring with a star.
#if CHARACTER_STYLE
        const bool coldFace = reaction == Reaction::COLD && tempK >= 0.6f && ex == Expr::NONE && !persona.frown && !persona.startled &&
                              !persona.snoozed && !persona.lidRamp;
        if (coldFace)
        {
            charColdEye(g, lcx + shiver, cyE, R);
            charColdEye(g, rcx + shiver, cyE, R);
            charShiverMarks(g, lcx + shiver, rcx + shiver, cyE, R, now / 55.0f);
            drawCharMono(g, CharMono::SNOW, W / 2 + 2 * R + u(12), cyE - R - u(10), 1.2f * sc, rgb(130, 190, 255));
        }
        else
        {
            const bool excited = reaction == Reaction::SNOW && ex == Expr::NONE && !persona.frown && !persona.startled;
            charSetEyeStyle(excited ? CharEyeStyle::STAR : CharEyeStyle::NORMAL);
            drawRigEyes(g, lcx, rcx, cyE, squeeze, shiver);
            charSetEyeStyle(CharEyeStyle::NORMAL);
        }
#else
        drawRigEyes(g, lcx, rcx, cyE, squeeze, shiver);
#endif

        // the mask goes with the head (peek-a-boo, sneeze kick, hiccup hop, shiver) and shakes in a sneeze; it is drawn
        // before the extras (fly, spray, dots), which stay in front of it
        if (faceMaskUp)
            drawFaceMask(g, now, shiver, maskDy + (cyE - cy), persona.sneezeJerk > 0 || persona.sneezeMouth);

        // extras
        uint16_t dropCol = rgb(120, 200, 255);
        if (reaction == Reaction::WORRIED || (reaction == Reaction::HOT && tempK >= 0.5f)) // sweat only when really hot
        {
            // sweat drop sliding down the face: worried = left side, hot = right temple and slower, so the two differ
            const bool hot = reaction == Reaction::HOT;
            float pd = (now % (hot ? 3400 : 2600)) / (hot ? 3400.0f : 2600.0f);
            int16_t sx = hot ? rcx + eyeW / 2 + u(9) : lcx - eyeW / 2 - u(9);
            int16_t sy = (int16_t)(cy - R + pd * R * 2.2f);
            drawDrop(g, sx, sy, imax(2, u(3)), tint(dropCol, (255 - pd * 140) / 255.0f));
        }
        if (reaction == Reaction::SAD)
        {
            // a tear rolls from the lower edge of the left eye now and then
            float pt = (now % 3600) / 3600.0f;
            if (pt < 0.6f)
            {
                float q = pt / 0.6f;
                int16_t tx = lcx - eyeW / 4;
                int16_t ty = (int16_t)(cy + eyeH / 2 + u(2) + q * R * 1.3f);
                drawDrop(g, tx, ty, imax(2, u(2.5f)), tint(dropCol, (255 - q * 150) / 255.0f));
            }
        }
        if (ex == Expr::SURPRISE && !antic)
        {
            drawBang(g, W / 2, cy - u(2), u(19), C_AMBER);
        }
        if (ex == Expr::YAWN && !faceMaskUp) // (behind the mask the eyes squeeze, that is all)
        {
            float s = sinf(exprP * (float)M_PI); // mouth opens, then closes
            int16_t my = cy + mouthDyFor(R);
            int16_t rx = u(2) + (int16_t)(u(7) * s * exprYawnSize), ry = u(2) + (int16_t)(u(6) * s * exprYawnSize);
            g.fillEllipse(W / 2, my, rx, ry, dimColor(C_HEART, 200));
            if (rx > 3 && ry > 3)
                g.fillEllipse(W / 2, my, rx - 2, ry - 2, rgb(40, 6, 22));
        }

        if (!faceMaskUp)
            drawMouth(g, W / 2, mouthY);

        if (sleepy && !persona.snoozed && !persona.startled)
            drawSleepZ(g, now, cy, R);
        drawPersonaExtras(g, now, cy, R, eyeH);
    }

} // namespace disp