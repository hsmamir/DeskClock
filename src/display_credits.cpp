// display_credits.cpp - hidden credits (easter egg).
// Five quick taps on the touch pad while the diagnostics page is open start them (touch.cpp counts the taps and bumps
// g_state.creditsSeq). The text scrolls up through both cards like film credits. The two small eyes at the top read
// it: the gaze sweeps along the line that is passing the middle of the screen, then jumps back for the next line.
// On the last line the eyes turn into happy "^ ^" eyes. A tap on the pad skips the credits (touch.cpp clears
// g_state.creditsActive), and so does leaving the page. Drawn like the diagnostics page: two cards, one seamless page.
#include "display_internal.h"
#include "version.h"
#include <string.h>

namespace disp
{
#if EGG_CREDITS_ENABLED

    enum : uint8_t
    {
        CL_GAP,   // empty space
        CL_TITLE, // accent colour
        CL_SOFT,  // small heading, dim
        CL_TEXT,  // normal line, white
        CL_SMALL, // tiny dim line (built-in font 1)
        CL_OWNER, // "owner" part of UPDATE_REPO
        CL_REPO   // "repository" part of UPDATE_REPO
    };

    struct CreditLine
    {
        uint8_t style;
        const char *text;
    };

    // About 14 characters fit on a line in font 2, 18 in font 1 (a line that is too wide falls back to font 1).
    static const CreditLine kLines[] = {
        {CL_TITLE, "DeskClock"},
        {CL_SMALL, "firmware " FW_VERSION},
        {CL_GAP, nullptr},
        {CL_SOFT, "made by"},
        {CL_TEXT, EGG_CREDITS_AUTHOR},
        {CL_GAP, nullptr},
        {CL_SOFT, "starring"},
        {CL_TEXT, "two curious eyes"},
        {CL_GAP, nullptr},
        {CL_SOFT, "supporting role"},
        {CL_TEXT, "the little servo"},
        {CL_GAP, nullptr},
        {CL_SOFT, "special thanks to"},
        {CL_TEXT, "everyone who pets"},
        {CL_TEXT, "the clock"},
        {CL_GAP, nullptr},
        {CL_TEXT, "no pixels were"},
        {CL_TEXT, "harmed making"},
        {CL_TEXT, "this clock"},
        {CL_GAP, nullptr},
        {CL_SOFT, "open source (MIT)"},
        {CL_SMALL, "github.com"},
        {CL_OWNER, nullptr},
        {CL_REPO, nullptr},
        {CL_GAP, nullptr},
        {CL_TITLE, "you found it!"},
    };
    static constexpr uint8_t N_LINES = sizeof(kLines) / sizeof(kLines[0]);
    static const uint32_t CREDITS_HOLD_MS = 2200; // the last line stays (happy eyes) this long

    // ---- State (display task only, except g_state.creditsSeq / creditsActive) ----
    static bool sRun = false;
    static uint8_t sSeen = 0; // creditsSeq already answered
    static uint32_t sStartMs = 0;
    static float sGazeX = 0, sGazeY = 0;
    static char sOwner[24] = "", sRepo[28] = "";
    static int16_t sCum[N_LINES], sPitch[N_LINES]; // top of each line below the first one, height of each line
    static int16_t sBandTop = 0, sBandBot = 0, sFocusY = 0;
    static float sSpeed = 1, sScrollMax = 0;
    static uint32_t sScrollMs = 0;

    static const char *lineText(uint8_t i)
    {
        switch (kLines[i].style)
        {
        case CL_OWNER:
            return sOwner;
        case CL_REPO:
            return sRepo;
        default:
            return kLines[i].text;
        }
    }

    // font 2 unless the line is a tiny one, or too wide for the card
    static uint8_t lineFont(uint8_t i, int16_t maxW)
    {
        if (kLines[i].style == CL_SMALL)
            return 1;
        return tft.textWidth(lineText(i), 2) > maxW ? 1 : 2;
    }

    // screen y of the middle of line i after the credits scrolled by `scroll` px
    static float lineCy(uint8_t i, float scroll)
    {
        return (float)sBandBot - scroll + sCum[i] + sPitch[i] * 0.5f;
    }

    static void startCredits(uint32_t now)
    {
        const char *slash = strchr(UPDATE_REPO, '/'); // "owner/repo" -> two lines
        int on = slash ? (int)(slash - UPDATE_REPO) : (int)strlen(UPDATE_REPO);
        snprintf(sOwner, sizeof(sOwner), "%.*s", on, UPDATE_REPO);
        snprintf(sRepo, sizeof(sRepo), "%s", slash ? slash + 1 : "");

        int16_t y = 0;
        for (uint8_t i = 0; i < N_LINES; i++)
        {
            sPitch[i] = imax(1, kLines[i].style == CL_GAP ? u(8) : u(15));
            sCum[i] = y;
            y += sPitch[i];
        }
        sBandTop = u(42); // text fades out here, under the eyes
        sBandBot = H - u(6);
        sFocusY = u(86); // the row the eyes read; the last line stops here
        sSpeed = (float)imax(1, u(EGG_CREDITS_SPEED));
        sScrollMax = (float)(sBandBot - sFocusY) + sCum[N_LINES - 1] + sPitch[N_LINES - 1] * 0.5f;
        sScrollMs = (uint32_t)(sScrollMax / sSpeed * 1000.0f);

        sGazeX = sGazeY = 0;
        sStartMs = now;
        sRun = true;
        g_state.creditsActive = true;
    }

    // yOff = screen y of this canvas' top edge, ch = its height. Everything is in screen coordinates, shifted into the
    // canvas (like the diagnostics page); the eyes live in the upper canvas only.
    static void drawCreditsContent(TFT_eSPI &g, int16_t yOff, int16_t ch, float scroll, bool finale, uint32_t now)
    {
        uint16_t accent = C_ACCENT;
        g.fillRect(0, -yOff, W, imax(2, u(2)), accent);
        g.fillRect(0, imax(2, u(2)) - yOff, W, imax(1, u(1)), tint(accent, 0.235f));

        // ---- the eyes (upper canvas) ----
        if (yOff == 0)
        {
            int16_t R = imax(4, (int16_t)(W / 14));
            int16_t dx = (int16_t)(R * 2.2f);
            int16_t cy = u(22);
            if (finale)
            {
                drawHappyEye(g, W / 2 - dx, cy, R);
                drawHappyEye(g, W / 2 + dx, cy, R);
            }
            else
            {
                float open = 1.0f;
                uint32_t t = now % 3400;
                if (t < 120)
                    open = 1.0f - 0.92f * sinf(t * (float)M_PI / 120.0f); // a quick blink every few seconds
                EyeShape s = {};
                s.w = R * 2.0f;
                s.h = R * 2.25f;
                s.radius = s.w * 0.36f;
                s.x = sGazeX;
                s.y = sGazeY;
                drawEyeShape(g, W / 2 - dx, cy, s, open, -1, C_EYE_TOP, C_EYE_BOT, true);
                drawEyeShape(g, W / 2 + dx, cy, s, open, 1, C_EYE_TOP, C_EYE_BOT, true);
            }
        }

        // ---- the text ----
        const float fadeH = (float)imax(1, u(18));
        g.setTextDatum(MC_DATUM);
        for (uint8_t i = 0; i < N_LINES; i++)
        {
            if (kLines[i].style == CL_GAP)
                continue;
            float cyL = lineCy(i, scroll);
            if (cyL < yOff - sPitch[i] || cyL > yOff + ch + sPitch[i])
                continue; // not in this canvas
            float k = fminf(clamp01((cyL - sBandTop) / fadeH), clamp01(((float)sBandBot + u(6) - cyL) / fadeH));
            if (k < 0.03f)
                continue;

            uint16_t col;
            switch (kLines[i].style)
            {
            case CL_TITLE:
                col = accent;
                break;
            case CL_TEXT:
            case CL_OWNER:
            case CL_REPO:
            {
                float f = clamp01(1.0f - fabsf(cyL - sFocusY) / (sPitch[i] * 1.6f)); // brighter near the reading row
                col = mixColor(C_SOFT, C_WHITE, 0.55f + 0.45f * f);
                break;
            }
            default: // CL_SOFT, CL_SMALL
                col = C_SOFT;
                break;
            }
            g.setTextColor(tint(col, k));
            g.drawString(lineText(i), W / 2, (int16_t)lroundf(cyL) - yOff, lineFont(i, W - u(8)));
        }
    }

    bool drawCreditsScreen(uint32_t now)
    {
        uint8_t seq = g_state.creditsSeq;
        if (seq != sSeen)
        {
            sSeen = seq;
            startCredits(now);
        }
        if (!sRun)
            return false;
        if (!g_state.creditsActive) // skipped by a tap
        {
            sRun = false;
            return false;
        }
        uint32_t el = now - sStartMs;
        if (el >= sScrollMs + CREDITS_HOLD_MS)
        {
            sRun = false;
            g_state.creditsActive = false;
            return false; // back to the diagnostics page
        }

        float scroll = fminf((float)el * sSpeed / 1000.0f, sScrollMax);
        bool finale = scroll >= sScrollMax - 0.5f;

        // Gaze: the line nearest to the reading row is read from its left end to its right end while it passes the
        // row, then the eyes jump back to the left for the next line. Always a little downward, towards the text.
        float tx = 0, ty = (float)u(2.5f);
        int8_t best = -1;
        float bestD = 1e9f, bestRel = 0;
        for (uint8_t i = 0; i < N_LINES; i++)
        {
            if (kLines[i].style == CL_GAP)
                continue;
            float rel = lineCy(i, scroll) - sFocusY;
            if (fabsf(rel) < bestD)
            {
                bestD = fabsf(rel);
                bestRel = rel;
                best = (int8_t)i;
            }
        }
        if (!finale && best >= 0 && bestD < sPitch[best])
        {
            float phase = clamp01(0.5f - bestRel / (float)sPitch[best]); // 0 = line just arrived below the row .. 1 = just left above it
            float wFrac = clamp01((float)tft.textWidth(lineText(best), lineFont(best, W - u(8))) / (float)(W - u(8)));
            tx = (phase * 2.0f - 1.0f) * (float)u(5.0f) * wFrac;
        }
        sGazeX += (tx - sGazeX) * 0.35f;
        sGazeY += (ty - sGazeY) * 0.2f;

        TFT_eSPI &gt = faceCv.begin();
        gt.fillRect(0, 0, W, faceH, C_BG);
        drawCreditsContent(gt, 0, faceH, scroll, finale, now);
        faceCv.endAt(0, CORNERS_TOP); // outer corners only: one page, not two cards

        TFT_eSPI &gb = infoCv.begin();
        gb.fillRect(0, 0, W, infoH, C_BG);
        drawCreditsContent(gb, faceH, infoH, scroll, finale, now);
        infoCv.endAt(0, CORNERS_BOT);

        gapInvalidate();
        gapIsFull = true; // one-piece page, like the diagnostics page
        return true;
    }

    void creditsStop()
    {
        sSeen = g_state.creditsSeq; // a request that came in while another page was up must not start them later
        if (sRun)
        {
            sRun = false;
            g_state.creditsActive = false;
        }
    }

#else

    bool drawCreditsScreen(uint32_t) { return false; }
    void creditsStop() {}

#endif

} // namespace disp