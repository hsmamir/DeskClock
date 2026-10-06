// display_face.cpp - the face: pet reaction expressions and drawFace() (eyes, brows, sleepy Zs ...).
#include "display_internal.h"

namespace disp
{

// ---- Petted state: the servo only wiggles for a moment, so the display keeps the
// reaction face on its own for pet_hold_ms(kind). pet.cpp decides WHICH reaction
// (petEv.kind); a new tap (petEv.seq changes) restarts the animation (see display_tick). ----
PetEvent petEv = {PetKind::HAPPY, 0, 0, 0};
uint32_t happyStartMs = 0;

// =====================================================================
//  Pet reactions (what pet.cpp picked; see PetKind)
// =====================================================================

// Small hearts rising from above the eyes. `age` = ms since they started; heart i starts
// i*stepMs later and lives lifeMs.
static void drawRisingHearts(TFT_eSPI &g, uint32_t age, int16_t cy, int16_t R, uint8_t n,
                             uint16_t stepMs, uint16_t lifeMs, int16_t size)
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
        drawHeart(g, hx, hy, size, tint(C_HEART, (255 - p * 140) / 255.0f));
    }
}

// A heart of total width 2*s centred on (cx, cyc) (drawHeart() takes the top-lobe line).
static void drawHeartCentered(TFT_eSPI &g, int16_t cx, int16_t cyc, int16_t s, uint16_t c)
{
    drawHeart(g, cx, cyc - imax(1, s / 2), s, c);
}

// Closed, contented eye "‿" (sleepy pet, purring): a thin arch curving downwards + blush.
static void drawContentEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R)
{
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
    g.fillEllipse(cx, cy + R + u(2), R / 2 + u(1), imax(2, R / 5), C_CHEEK);
}

// Heart used as an eye: pulsing, with a glow and a highlight.
static void drawHeartEye(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, uint32_t age)
{
    float pulse = 1.0f + 0.10f * sinf(age / 85.0f);
    int16_t s = (int16_t)(R * 1.55f * pulse);
    drawHeartCentered(g, cx, cy, s + u(5), tint(C_HEART, 0.28f)); // glow
    drawHeartCentered(g, cx, cy, s, C_HEART);
    int16_t r = imax(1, s / 2);
    g.fillCircle(cx - r, cy - r, imax(1, r / 3), C_WHITE); // highlight on the left lobe
}

// Spiral for the dizzy eyes.
static void drawSpiral(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t R, float phase)
{
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

static void drawPetFace(TFT_eSPI &g, uint32_t now, bool purr, bool sleepy, int16_t cy, int16_t lcx,
                        int16_t rcx, int16_t R, int16_t eyeW, int16_t eyeH)
{
    uint32_t age = now - happyStartMs;
    uint16_t dropCol = rgb(120, 200, 255);

    if (purr) // button held: purring
    {
        int16_t j = ((now / 45) & 1) ? 1 : -1; // tiny vibration
        drawContentEye(g, lcx + j, cy, R);
        drawContentEye(g, rcx + j, cy, R);
        drawRisingHearts(g, now % 1800, cy, R, 2, 700, 1100, u(3));
        if (sleepy)
            drawSleepZ(g, now, cy, R);
        return;
    }

    switch (petEv.kind)
    {
    case PetKind::HEART_EYES:
        drawHeartEye(g, lcx, cy, R, age);
        drawHeartEye(g, rcx, cy, R, age);
        drawRisingHearts(g, age, cy, R, 6, 160, 1100, u(4));
        break;

    case PetKind::SLEEPY:
        drawContentEye(g, lcx, cy, R);
        drawContentEye(g, rcx, cy, R);
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
            drawHappyEye(g, lcx, cy, R);
            drawHappyEye(g, rcx, cy, R);
            drawRisingHearts(g, age - 1100, cy, R, 1, 0, 1100, u(4));
        }
        break;

    case PetKind::DIZZY:
    {
        float ph = age / 90.0f;
        int16_t sway = (int16_t)(sinf(age / 140.0f) * u(3));
        drawSpiral(g, lcx + sway, cy, R, ph);
        drawSpiral(g, rcx + sway, cy, R, -ph);
        for (uint8_t k = 0; k < 3; k++) // little stars circling the head
        {
            float a = age / 200.0f + k * 2.094f;
            int16_t sx = W / 2 + (int16_t)(cosf(a) * R * 2.4f);
            int16_t sy = cy - R - u(8) + (int16_t)(sinf(a) * u(4));
            g.fillCircle(sx, sy, imax(1, u(2)), C_AMBER);
        }
        break;
    }

    case PetKind::ANNOYED:
    {
        int16_t shake = ((now / 50) & 1) ? u(1) : -u(1);
        uint16_t top = mixColor(C_EYE_TOP, C_ALARM_TOP, 0.5f), bot = mixColor(C_EYE_BOT, C_ALARM_BOT, 0.5f);
        drawEye(g, lcx + shake, cy, eyeW, eyeH, 0.7f, 0, BROW_ANGRY, -1, top, bot, false);
        drawEye(g, rcx + shake, cy, eyeW, eyeH, 0.7f, 0, BROW_ANGRY, 1, top, bot, false);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_RED);
        g.drawString("#", W - u(22), cy - R - u(6), 4); // anger mark
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
            drawHappyEye(g, lcx, cy + dy, R);
            drawHappyEye(g, rcx, cy + dy, R);
            drawRisingHearts(g, age - 700, cy, R, 5, 170, 1100, u(4));
        }
        break;

    case PetKind::MORNING:
        if (age < 1500) // yawn
        {
            float env = sinf(clamp01(age / 1500.0f) * (float)M_PI);
            float open = 1.0f - 0.92f * env;
            drawEye(g, lcx, cy, eyeW, eyeH, open, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawEye(g, rcx, cy, eyeW, eyeH, open, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
            int16_t my = cy + R + u(10);
            int16_t rx = u(2) + (int16_t)(u(7) * env), ry = u(2) + (int16_t)(u(6) * env);
            g.fillEllipse(W / 2, my, rx, ry, dimColor(C_HEART, 200));
            if (rx > 3 && ry > 3)
                g.fillEllipse(W / 2, my, rx - 2, ry - 2, rgb(40, 6, 22));
        }
        else // ... then a happy good morning
        {
            drawHappyEye(g, lcx, cy, R);
            drawHappyEye(g, rcx, cy, R);
            drawRisingHearts(g, age - 1500, cy, R, 2, 260, 1100, u(4));
        }
        break;

    case PetKind::RARE_WINK:
        drawEye(g, lcx, cy, eyeW, eyeH, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
        drawHappyEye(g, rcx, cy, R);                                              // winking eye
        g.fillRoundRect(W / 2 - u(5), cy + R + u(6), u(10), u(9), u(4), C_CHEEK); // tongue
        g.drawFastVLine(W / 2, cy + R + u(6), u(6), dimColor(C_CHEEK, 160));
        drawRisingHearts(g, age, cy, R, 2, 250, 1100, u(4));
        break;

    case PetKind::RARE_SURPRISE:
        if (age < 800)
        {
            float s = 1.0f + 0.3f * clamp01(age / 200.0f);
            int16_t ew = (int16_t)(eyeW * s), eh = (int16_t)(eyeH * s);
            drawEye(g, lcx, cy, ew, eh, 1.0f, 0, BROW_NONE, -1, C_EYE_TOP, C_EYE_BOT, true);
            drawEye(g, rcx, cy, ew, eh, 1.0f, 0, BROW_NONE, 1, C_EYE_TOP, C_EYE_BOT, true);
            g.setTextDatum(MC_DATUM);
            g.setTextColor(C_AMBER);
            g.drawString("!", W / 2, cy - u(2), 4);
        }
        else // a big heart pops up between the happy eyes
        {
            drawHappyEye(g, lcx, cy, R);
            drawHappyEye(g, rcx, cy, R);
            float pop = clamp01((age - 800) / 200.0f);
            float pulse = 1.0f + 0.12f * sinf(age / 90.0f);
            drawHeartCentered(g, W / 2, cy - R * 2, (int16_t)(u(8) * pop * pulse) + 1, C_HEART);
        }
        break;

    case PetKind::HOT:
    {
        drawHappyEye(g, lcx, cy, R);
        drawHappyEye(g, rcx, cy, R);
        float pd = (age % 1400) / 1400.0f; // sweat drop, but still happy
        drawDrop(g, lcx - eyeW / 2 - u(9), (int16_t)(cy - R + pd * R * 2.2f), imax(2, u(3)),
                 tint(dropCol, (255 - pd * 140) / 255.0f));
        drawRisingHearts(g, age, cy, R, 1, 0, 1100, u(4));
        break;
    }

    case PetKind::COLD:
    {
        int16_t sh = (((now / 40) & 1) ? 1 : -1) * u(1); // shiver
        drawHappyEye(g, lcx + sh, cy, R);
        drawHappyEye(g, rcx + sh, cy, R);
        for (uint8_t k = 0; k < 4; k++) // snowflakes drifting down
        {
            int16_t sx = (int16_t)(W * (0.15f + 0.2f * k)) + (int16_t)(sinf(age / 300.0f + k) * u(3));
            int16_t sy = (int16_t)((age / 9 + k * 23) % faceH);
            g.fillCircle(sx, sy, imax(1, u(1.5f)), C_ICE);
        }
        drawRisingHearts(g, age, cy, R, 2, 300, 1100, u(4));
        break;
    }

    case PetKind::HAPPY:
    default:
        drawHappyEye(g, lcx, cy, R);
        drawHappyEye(g, rcx, cy, R);
        drawRisingHearts(g, age, cy, R, 3, 220, 1100, u(4));
        break;
    }
}

void drawFace(TFT_eSPI &g, uint32_t now, bool alarm, bool happy, bool purr, bool sleepy, float blinkOpen,
                     const SceneInfo &scn, Reaction reaction, float exprP)
{
    g.fillRect(0, 0, W, faceH, C_BG);

    // Background: weather/time-of-day scene. The alarm keeps the plain starry
    // look so nothing distracts from the alert.
    if (SCENE_ENABLED && !alarm)
        drawSceneBackground(g, now, scn);
    else
        drawStars(g, now);

    int16_t R = W / 8; // same base size as before
    int16_t eyeW = R * 2;
    int16_t eyeH = (int16_t)(R * 2.25f);
    int16_t cy = (int16_t)(faceH * 0.55f) + (int16_t)(sinf(now / 850.0f) * u(1.5f)); // gentle "breathing"
    int16_t lcx = W / 2 - R * 2;
    int16_t rcx = W / 2 + R * 2;

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
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_RED);
        g.drawString("!", W / 2, cy, 4);
        return;
    }

    if (happy)
    {
        drawPetFace(g, now, purr, sleepy, cy, lcx, rcx, R, eyeW, eyeH);
        return;
    }

    // normal / sleepy eyes
    int16_t ox = (int16_t)(lookX * u(5));
    int16_t oy = (int16_t)(lookY * u(4));
    float open = blinkOpen;
    float lid = 0;
    float eScale = 1.0f; // >1: eyes widen (startled / surprised)
    uint8_t brow = BROW_NONE;
    if (sleepy)
    {
        lid = 0.5f;
        open = blinkOpen; // still blinks, slowly
    }

    // situation reactions (steady while the situation lasts)
    switch (reaction)
    {
    case Reaction::WORRIED:
        brow = BROW_SAD;
        break;
    case Reaction::STORM:
        brow = BROW_SAD;
        if (lightningActive(now))
            eScale = 1.25f; // flinch at every flash
        break;
    case Reaction::SAD:
        brow = BROW_SAD;
        break;
    case Reaction::HOT:
        lid = 0.28f;
        break;
    case Reaction::COLD:
        ox += (((now / 40) & 1) ? 1 : -1) * u(1); // shiver
        lid = 0.15f;
        break;
    default:
        break;
    }

    // one-off idle expressions
    Expr ex = exprCur;
    if (ex == Expr::YAWN)
    {
        float edge = (exprP < 1.0f - exprP ? exprP : 1.0f - exprP) / 0.15f; // ease in/out
        if (edge > 1.0f)
            edge = 1.0f;
        open *= 1.0f - 0.92f * edge; // eyes squeeze shut mid-yawn
    }
    else if (ex == Expr::SURPRISE)
    {
        float env = exprP < 0.15f ? exprP / 0.15f : (exprP > 0.8f ? (1.0f - exprP) / 0.2f : 1.0f);
        eScale = 1.0f + 0.28f * env;
    }

    int16_t ew = (int16_t)(eyeW * eScale), eh = (int16_t)(eyeH * eScale);
    drawEye(g, lcx + ox, cy + oy, ew, eh, open, lid, brow, -1, C_EYE_TOP, C_EYE_BOT, true);
    if (ex == Expr::WINK)
        drawHappyEye(g, rcx + ox, cy + oy, R); // winking eye = a happy arch
    else
        drawEye(g, rcx + ox, cy + oy, ew, eh, open, lid, brow, 1, C_EYE_TOP, C_EYE_BOT, true);

    // extras
    uint16_t dropCol = rgb(120, 200, 255);
    if (reaction == Reaction::WORRIED || reaction == Reaction::HOT)
    {
        // sweat drop sliding down the left side of the face
        float pd = (now % 2600) / 2600.0f;
        int16_t sx = lcx - eyeW / 2 - u(9);
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
    if (ex == Expr::SURPRISE)
    {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_AMBER);
        g.drawString("!", W / 2, cy - u(2), 4);
    }
    if (ex == Expr::YAWN)
    {
        float s = sinf(exprP * (float)M_PI); // mouth opens, then closes
        int16_t my = cy + R + u(10);
        int16_t rx = u(2) + (int16_t)(u(7) * s), ry = u(2) + (int16_t)(u(6) * s);
        g.fillEllipse(W / 2, my, rx, ry, dimColor(C_HEART, 200));
        if (rx > 3 && ry > 3)
            g.fillEllipse(W / 2, my, rx - 2, ry - 2, rgb(40, 6, 22));
    }

    if (sleepy)
        drawSleepZ(g, now, cy, R);
}

} // namespace disp
