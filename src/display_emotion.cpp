// display_emotion.cpp - emotional eye colour. The eyes' time-of-day colour (C_EYE_TOP/BOT_BASE, from
// display_theme.cpp) is mixed with the colour of the current feeling; the mix amount is a spring (emoTint),
// so a feeling fades in with a little overshoot and fades out softly.
#include "display_internal.h"

namespace disp
{

    struct Rgb
    {
        float r, g, b;
    };

    static Rgb toRgb(uint16_t c)
    {
        return {(float)(((c >> 11) & 0x1F) * 255 / 31), (float)(((c >> 5) & 0x3F) * 255 / 63), (float)((c & 0x1F) * 255 / 31)};
    }
    static uint16_t fromRgb(const Rgb &c)
    {
        auto cl = [](float v) -> uint8_t
        { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f)); };
        return rgb(cl(c.r), cl(c.g), cl(c.b));
    }
    static Rgb lerpRgb(const Rgb &a, const Rgb &b, float t)
    {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }
    // Less saturated (sat 0..1 of the colour kept) and darker (val 0..1 of the brightness kept).
    static Rgb washOut(const Rgb &c, float sat, float val)
    {
        float grey = 0.30f * c.r + 0.59f * c.g + 0.11f * c.b;
        Rgb m = lerpRgb({grey, grey, grey}, c, sat);
        return {m.r * val, m.g * val, m.b * val};
    }

    struct EmoLook
    {
        Rgb top, bot;
        float amount; // how far the eyes go toward it (1 = fully this colour)
    };

    static EmoLook emoLook(Emo e)
    {
        switch (e)
        {
        case Emo::LOVE:
            return {{255, 92, 147}, {214, 40, 104}, 0.85f}; // #FF5C93
        case Emo::ANGER:
            return {{255, 96, 72}, {205, 18, 24}, 0.80f};
        case Emo::COLD:
            return {{205, 235, 255}, {80, 165, 255}, 0.75f};
        case Emo::SLEEPY: // the time-of-day colour itself, washed out and dark
            return {washOut(toRgb(C_EYE_TOP_BASE), 0.35f, 0.58f), washOut(toRgb(C_EYE_BOT_BASE), 0.35f, 0.50f), 0.80f};
        case Emo::FOND: // the bond level as a faint, lasting colour (PET_BOND_TINT)
            return {{255, 140, 175}, {225, 80, 130}, 0.14f};
        case Emo::DEVOTED:
            return {{255, 110, 160}, {225, 55, 115}, 0.26f};
        case Emo::LONELY: // a bit duller than usual
            return {washOut(toRgb(C_EYE_TOP_BASE), 0.55f, 0.82f), washOut(toRgb(C_EYE_BOT_BASE), 0.55f, 0.78f), 0.60f};
        default:
            return {{0, 0, 0}, {0, 0, 0}, 0.0f};
        }
    }

    // 0 = plain time-of-day colour .. 1 = full feeling colour. The spring may overshoot; the mix is clamped.
    static Spring emoTint;
    static Rgb emoTop = {255, 92, 147}, emoBot = {214, 40, 104}; // colour the tint is moving toward (eased, so feeling -> feeling slides)
    static uint32_t emoLastMs = 0;

    Emo emoPick(bool alarm, bool happy, bool purr, bool sleepy, Reaction reaction, const PetBond &bond)
    {
        if (!EMO_TINT_ENABLED || alarm)
            return Emo::NONE;
        if (happy)
        {
            if (purr)
                return Emo::LOVE;
            switch (petEv.kind)
            {
            case PetKind::ANNOYED:
                return Emo::ANGER;
            case PetKind::SLEEPY:
                return Emo::SLEEPY;
            case PetKind::COLD:
                return Emo::COLD;
            case PetKind::DIZZY:
            case PetKind::RAISED_BROW:
            case PetKind::SORRY:
                break; // no feeling colour for these, fall through to the situation below
            default:
                return Emo::LOVE; // happy, hearts, missed you, morning, wink, surprise, hot
            }
        }
        if (reaction == Reaction::COLD)
            return Emo::COLD;
        if (sleepy)
            return Emo::SLEEPY;
        if (PET_BOND_TINT && bond.enabled) // nothing else going on: the colour of how much he likes you
        {
            switch (bond.level)
            {
            case BondLevel::DEVOTED:
                return Emo::DEVOTED;
            case BondLevel::FOND:
                return Emo::FOND;
            case BondLevel::LONELY:
                return Emo::LONELY;
            default:
                break;
            }
        }
        return Emo::NONE;
    }

    void emoUpdate(uint32_t now, Emo want)
    {
        float dt = emoLastMs ? (now - emoLastMs) / 1000.0f : 0.033f;
        emoLastMs = now;
        if (dt > 0.05f)
            dt = 0.05f;

        if (want != Emo::NONE)
        {
            EmoLook L = emoLook(want);
            if (emoTint.x < 0.03f)
            {
                emoTop = L.top; // nothing visible yet: start in the right colour
                emoBot = L.bot;
            }
            else
            {
                float k = 1.0f - expf(-dt * 9.0f); // ~0.1 s: one feeling slides into the next
                emoTop = lerpRgb(emoTop, L.top, k);
                emoBot = lerpRgb(emoBot, L.bot, k);
            }
            emoTint.target = L.amount;
        }
        else
            emoTint.target = 0.0f;

        uint8_t n = (uint8_t)(dt / 0.008f) + 1; // sub-steps like the eye springs (frame-rate independent)
        for (uint8_t j = 0; j < n; j++)
            springStep(emoTint, dt / n, 90.0f, 14.0f); // ratio ~0.74: a gentle overshoot

        float a = clamp01(emoTint.x);
        C_EYE_TOP = a > 0.004f ? mixColor(C_EYE_TOP_BASE, fromRgb(emoTop), a) : C_EYE_TOP_BASE;
        C_EYE_BOT = a > 0.004f ? mixColor(C_EYE_BOT_BASE, fromRgb(emoBot), a) : C_EYE_BOT_BASE;
    }

} // namespace disp