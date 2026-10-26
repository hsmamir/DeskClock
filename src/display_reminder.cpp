// display_reminder.cpp - the full-screen reminder: a bell, the time it was set for and the text. Everything the clock itself
// writes here is English ("Reminder", the digits); the only Persian (Vazirmatn) or other-language text on this screen is the
// text the user typed for the reminder.
//
// It is drawn into the two existing cards (no extra RAM) but looks like ONE page: the strip between the cards gets the page
// background (no frame colour, no divider line) and only the outer corners are rounded, like the Date / Weather pages.
// Two layouts, picked by the
// length of the text:
//   hero     (short text)  upper card: bell, label, big time.   lower card: the text, centred.
//   compact  (long text)   upper card: one header row, then the first lines.   lower card: the rest of the lines.
// Text lines are placed in "slots" that lie completely inside one card, so nothing is ever cut by the strip between them.
// Persian shaping, right-to-left order and word wrap are in persian_text.cpp.
#include "display_internal.h"
#include "display_style.h"
#include "display_vazir.h"
#include "persian_text.h"
#include "reminders.h"

namespace disp
{
    static const int NFONTS = 3;
    static const uint8_t *const FONTS[NFONTS] = {Vazir22, Vazir18, Vazir14}; // largest first
    static const int16_t FONT_PITCH[NFONTS] = {VAZIR22_PITCH, VAZIR18_PITCH, VAZIR14_PITCH};
    static const int MAX_LINES = 6;
    static const int LINE_BYTES = 3 * 40 + 8;

    struct RLayout
    {
        bool valid = false;
        uint32_t seq = 0;
        int16_t faceH = 0, infoH = 0;
        int font = 0;
        bool compact = false;
        int lines = 0;
        int topLines = 0;                // how many of the lines are in the upper card (compact layout)
        char vis[MAX_LINES][LINE_BYTES]; // ready to draw, left to right
        int16_t baseline[MAX_LINES];     // canvas y of the baseline, in the card the line belongs to
    };
    static RLayout L;

    // ---- geometry shared by layout and drawing ----
    static int16_t compactHeaderH() { return u(32); }
    static int16_t botTextTop() { return u(3); }
    static int16_t botTextBottom() { return infoH - u(9); } // the countdown bar sits below
    static int16_t textWidthMax() { return W - 2 * u(7); }

    // How many lines of font f fit in the lower card (hero layout) / in each card (compact layout).
    // In the compact layout the text is ONE block of equally spaced lines that continues across the strip between the
    // cards, so each card may use its full height: the upper one from under the header to its bottom edge, the lower
    // one from its top edge down to the countdown bar.
    static int slotsBottom(int f) { return (botTextBottom() - botTextTop()) / FONT_PITCH[f]; }
    static int16_t topCapC() { return faceH - (compactHeaderH() + u(3)); }
    static int16_t botCapC() { return botTextBottom(); }
    static int slotsTopC(int f) { return topCapC() / FONT_PITCH[f]; }
    static int slotsBottomC(int f) { return botCapC() / FONT_PITCH[f]; }

    // ---- measuring: one font is loaded on tft at a time while the layout is worked out ----
    static int s_loaded = -1;
    static int measureCb(int font, const char *v, void *)
    {
        if (s_loaded != font)
        {
            if (s_loaded >= 0)
                tft.unloadFont();
            tft.loadFont(FONTS[font]);
            s_loaded = font;
        }
        return tft.textWidth(v);
    }

    static void layoutFor(const ReminderView &rv)
    {
        const char *text = rv.text;
        size_t len = strlen(text);
        int slotsTotal[NFONTS];
        for (int f = 0; f < NFONTS; f++)
            slotsTotal[f] = slotsBottomC(f) + slotsTopC(f);

        fa::Span spans[MAX_LINES];
        fa::FitResult fr = fa::fit(text, len, textWidthMax(), NFONTS, slotsTotal, measureCb, nullptr, spans, MAX_LINES);
        if (s_loaded >= 0)
        {
            tft.unloadFont();
            s_loaded = -1;
        }

        L.valid = true;
        L.seq = rv.seq;
        L.faceH = faceH;
        L.infoH = infoH;
        L.font = fr.font;
        L.lines = fr.lines;
        const int16_t pitch = FONT_PITCH[fr.font];
        const int dir = fa::hasRtl(text) ? fa::DIR_RTL : fa::DIR_LTR;

        for (int i = 0; i < fr.lines; i++)
        {
            char buf[LINE_BYTES];
            size_t n = spans[i].len;
            if (n > sizeof(buf) - 8)
                n = sizeof(buf) - 8;
            memcpy(buf, text + spans[i].start, n);
            if (i == fr.lines - 1 && fr.truncated)
            {
                memcpy(buf + n, fa::kEllipsis, 4); // includes the NUL
                n += 3;
            }
            else
                buf[n] = '\0';
            fa::visual(buf, n, L.vis[i], LINE_BYTES, dir);
        }

        // hero when every line fits in the lower card, else compact: the block is split between the two cards
        const int n = fr.lines;
        const int16_t ascent = (int16_t)(pitch * 0.74f); // baseline below the top of a line slot
        L.compact = n > slotsBottom(fr.font);
        L.topLines = 0;
        if (!L.compact)
        {
            // hero: the block is centred in the lower card
            int16_t areaH = botTextBottom() - botTextTop();
            int16_t y0 = botTextTop() + (areaH - n * pitch) / 2;
            for (int i = 0; i < n; i++)
                L.baseline[i] = y0 + i * pitch + ascent;
            return;
        }

        // compact: every line has the same pitch, also across the join of the two cards (there is no strip, see geometrySet). Line k-1 is
        // the last one in the upper card (its slot ends at the card's bottom edge), line k the first one in the lower
        // card (its slot starts at the card's top edge). k is the split that keeps the block closest to the centre.
        const int16_t topCap = topCapC(), botCap = botCapC();
        const int sTop = slotsTopC(fr.font), sBot = slotsBottomC(fr.font);
        const int kMin = imax(0, n - sBot), kMax = imin(n, sTop);
        const int16_t wantStart = (topCap + botCap - n * pitch) / 2; // block start if both cards were one tall card
        int bestK = kMin;
        int bestCost = 32000;
        for (int k = kMin; k <= kMax; k++)
        {
            int cost = abs(wantStart + k * pitch - topCap); // where the split would fall vs where the strip is
            if (cost < bestCost)
            {
                bestCost = cost;
                bestK = k;
            }
        }
        L.topLines = bestK;
        for (int i = 0; i < bestK; i++) // upper card: counted back from its bottom edge
            L.baseline[i] = faceH - (bestK - i) * pitch + ascent;
        for (int j = 0; bestK + j < n; j++) // lower card: counted down from its top edge
            L.baseline[bestK + j] = j * pitch + ascent;
    }

    // ---- drawing ----
    static void drawText(TFT_eSPI &g, const uint8_t *font, const char *vis, int16_t x, int16_t baseline, uint8_t datum, uint16_t fg)
    {
        g.loadFont(font);
        g.setTextColor(fg, C_BG);
        g.setTextDatum(datum);
        g.drawString(vis, x, baseline);
        g.unloadFont();
    }

    // Bell with a swinging clapper. (cx, cy) = middle of the body, r = dome radius.
    static void drawBigBell(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, int16_t dx, int16_t swing, uint16_t c)
    {
        cx += dx;
        g.fillCircle(cx, cy - 1, r, c);
        g.fillRect(cx - r, cy - 1, 2 * r + 1, r - 1, c);
        g.fillRect(cx - r - 2, cy + r - 2, 2 * r + 5, 2, c);
        g.fillCircle(cx, cy - r - 1, imax(1, r / 4), c);
        g.fillCircle(cx + swing, cy + r + 2, imax(1, r / 4), c);
        // two small "ring" arcs on each side while it shakes
        if (swing != 0)
        {
            int16_t s = swing > 0 ? 1 : -1;
            g.drawFastVLine(cx - r - 4 * s, cy - r / 2, r, tint(c, 0.6f));
            g.drawFastVLine(cx + r + 4 * s, cy - r / 2, r, tint(c, 0.6f));
        }
    }

    // Time as big seven-segment digits, centred on cx.
    static void drawBigTime(TFT_eSPI &g, int16_t cx, int16_t y, int16_t dh, uint8_t hour, uint8_t minute, uint16_t col)
    {
        const int16_t dw = (int16_t)(dh * 0.56f);
        const int16_t gap = imax(2, u(3));
        const int16_t cw = imax(4, u(8));
        const int16_t total = 4 * dw + 2 * gap + 2 * gap + cw;
        const int16_t x0 = cx - total / 2;
        const int8_t d[4] = {(int8_t)(hour / 10), (int8_t)(hour % 10), (int8_t)(minute / 10), (int8_t)(minute % 10)};
        const int16_t xs[4] = {x0, (int16_t)(x0 + dw + gap), (int16_t)(x0 + 2 * dw + 3 * gap + cw),
                               (int16_t)(x0 + 3 * dw + 4 * gap + cw)};
        for (int i = 0; i < 4; i++)
            drawSegDigit(g, xs[i], y, dw, dh, d[i], col, C_GHOST);
        drawColonGlow(g, x0 + 2 * dw + 2 * gap + cw / 2, y, dh, imax(2, dw / 4), col, 15);
    }

    static void drawTopCard(TFT_eSPI &g, const ReminderView &rv, uint32_t now)
    {
        g.fillRect(0, 0, W, faceH, C_BG);
        drawPageAccent(g, C_AMBER);

        // the bell rings for 1.3 s, rests for 1.2 s
        const uint32_t t = now - rv.shownMs;
        const bool ringing = (t % 2500) < 1300;
        int16_t swing = 0, dx = 0;
        if (ringing)
        {
            float ph = t / 75.0f;
            swing = (int16_t)(sinf(ph) * imax(2, u(3.5f)));
            dx = (int16_t)(sinf(ph * 0.5f) * 1.0f);
        }
        const uint16_t bell = C_AMBER;
        const uint16_t labelCol = mixColor(C_SOFT, C_WHITE, 0.5f);
        g.setTextColor(labelCol, C_BG);

        if (!L.compact)
        {
            drawBigBell(g, W / 2, u(20), u(7), dx, swing, bell);
            g.setTextDatum(MC_DATUM);
            g.drawString("Reminder", W / 2, u(41), 2);
            drawBigTime(g, W / 2, u(51), u(26), rv.hour, rv.minute, C_AMBER);
        }
        else
        {
            // Header: bell on the left, then a small "Reminder" label with the time under it (two rows). They used to sit
            // side by side in one row and the label (right-aligned) ran into the time digits on the 128 px display.
            const int16_t cy = u(16);
            drawBigBell(g, u(13), cy, u(5), dx, swing / 2, bell);
            char tm[8];
            snprintf(tm, sizeof(tm), "%02u:%02u", (unsigned)rv.hour, (unsigned)rv.minute);
            const int16_t tx = u(28);
            g.setTextDatum(TL_DATUM);
            g.drawString("Reminder", tx, u(4), 1); // built-in 6x8 font: stays clear of the "+N" counter (top right)
            smoothText(g, ClockSm18, CLOCKSM18_CAPH, tm, tx, u(22), L_BASELINE, C_AMBER, C_BG);
            drawDivider(g, u(6), W - u(6), compactHeaderH());
            for (int i = 0; i < L.topLines; i++)
                drawText(g, FONTS[L.font], L.vis[i], W / 2, L.baseline[i], C_BASELINE, C_WHITE);
        }

        if (rv.waiting > 0) // more reminders are queued behind this one
        {
            char more[8];
            snprintf(more, sizeof(more), "+%u", (unsigned)rv.waiting);
            g.setTextDatum(TR_DATUM);
            g.setTextColor(C_AMBER, C_BG);
            g.drawString(more, W - u(6), u(6), 2);
        }
    }

    static void drawBottomCard(TFT_eSPI &g, const ReminderView &rv, uint32_t now)
    {
        g.fillRect(0, 0, W, infoH, C_BG);
        const int botLines = L.lines - L.topLines;
        g.loadFont(FONTS[L.font]);
        g.setTextColor(C_WHITE, C_BG);
        g.setTextDatum(C_BASELINE);
        for (int i = 0; i < botLines; i++)
            g.drawString(L.vis[L.topLines + i], W / 2, L.baseline[L.topLines + i]);
        g.unloadFont();

        // time left before it closes by itself
        if (rv.timeoutMs)
        {
            uint32_t el = now - rv.shownMs;
            float left = el >= rv.timeoutMs ? 0.0f : 1.0f - (float)el / rv.timeoutMs;
            int16_t bx = u(10), bw = W - 2 * u(10), by = infoH - u(6), bh = imax(2, u(2));
#if UI_STYLE
            g.fillRoundRect(bx, by, bw, bh + 1, (bh + 1) / 2, C_GHOST);
            int16_t fw = (int16_t)(bw * left);
            if (fw > 0) // shrinks toward the middle
                g.fillRoundRect(bx + (bw - fw) / 2, by, fw, bh + 1, (bh + 1) / 2, tint(C_AMBER, 0.8f));
#else
            g.fillRect(bx, by, bw, bh, C_GHOST);
            int16_t fw = (int16_t)(bw * left);
            if (fw > 0)
                g.fillRect(bx + (bw - fw) / 2, by, fw, bh, tint(C_AMBER, 0.8f));
#endif
        }
    }

    void drawReminderScreen(const ReminderView &rv, uint32_t now)
    {
        static uint32_t lastCallMs = 0;
        static uint32_t lastTopMs = 0, botSig = 0xFFFFFFFFu;
        const bool reentered = lastCallMs == 0 || now - lastCallMs > 250; // other pages used the cards meanwhile
        lastCallMs = now ? now : 1;

        if (!L.valid || L.seq != rv.seq || L.faceH != faceH || L.infoH != infoH)
        {
            layoutFor(rv);
            botSig = 0xFFFFFFFFu;
        }

        const uint32_t sec = rv.timeoutMs ? (now - rv.shownMs) / 1000 : 0;
        const uint32_t sig = (rv.seq << 12) ^ (sec & 0xFFF) ^ ((uint32_t)rv.waiting << 24);
        bool drew = false;
        if (reentered || sig != botSig)
        {
            botSig = sig;
            drawBottomCard(infoCv.begin(), rv, now);
            infoCv.endAt(0, CORNERS_BOT); // outer corners only: one page, not two cards
            drew = true;
        }
        if (reentered || now - lastTopMs >= 80)
        {
            lastTopMs = now;
            drawTopCard(faceCv.begin(), rv, now);
            faceCv.endAt(0, CORNERS_TOP);
            drew = true;
        }
        if (drew)
        {
            gapInvalidate();
            gapIsFull = true; // one-piece page: the clock page then paints the strip between its cards again (see display.cpp)
        }
    }

} // namespace disp
// =====================================================================
//  Landscape UI: the reminder as one full-screen page (160 x 128): header (bell, label, time), the text, the countdown
// =====================================================================
#if LANDSCAPE_UI
#include "display_landscape.h"

namespace disp
{
    static RLayout LL; // layout of the landscape page (baselines are canvas y)

    static int16_t headerBottom() { return u(30); }
    static int16_t textBottom() { return H - u(10); } // the countdown bar sits below

    static void layoutForL(const ReminderView &rv)
    {
        const char *text = rv.text;
        const size_t len = strlen(text);
        const int16_t areaH = textBottom() - headerBottom() - u(3);
        int slots[NFONTS];
        for (int f = 0; f < NFONTS; f++)
            slots[f] = areaH / FONT_PITCH[f];

        fa::Span spans[MAX_LINES];
        fa::FitResult fr = fa::fit(text, len, W - 2 * u(8), NFONTS, slots, measureCb, nullptr, spans, MAX_LINES);
        if (s_loaded >= 0)
        {
            tft.unloadFont();
            s_loaded = -1;
        }
        LL.valid = true;
        LL.seq = rv.seq;
        LL.faceH = H;
        LL.font = fr.font;
        LL.lines = fr.lines;
        LL.compact = false;
        LL.topLines = 0;
        const int16_t pitch = FONT_PITCH[fr.font];
        const int dir = fa::hasRtl(text) ? fa::DIR_RTL : fa::DIR_LTR;
        for (int i = 0; i < fr.lines; i++)
        {
            char buf[LINE_BYTES];
            size_t n = spans[i].len;
            if (n > sizeof(buf) - 8)
                n = sizeof(buf) - 8;
            memcpy(buf, text + spans[i].start, n);
            if (i == fr.lines - 1 && fr.truncated)
            {
                memcpy(buf + n, fa::kEllipsis, 4);
                n += 3;
            }
            else
                buf[n] = '\0';
            fa::visual(buf, n, LL.vis[i], LINE_BYTES, dir);
        }
        const int16_t ascent = (int16_t)(pitch * 0.74f);
        const int16_t y0 = headerBottom() + u(3) + (areaH - fr.lines * pitch) / 2;
        for (int i = 0; i < fr.lines; i++)
            LL.baseline[i] = y0 + i * pitch + ascent;
    }

    void drawReminderScreenL(const ReminderView &rv, uint32_t now)
    {
        static uint32_t lastDrawMs = 0;
        if (!LL.valid || LL.seq != rv.seq || LL.faceH != H)
        {
            layoutForL(rv);
            lastDrawMs = 0;
        }
        if (lastDrawMs != 0 && now - lastDrawMs < 80) // the bell swings: ~12 fps is plenty
            return;
        lastDrawMs = now ? now : 1;

        TFT_eSPI &g = faceCv.begin();
        g.fillRect(0, 0, W, H, C_BG);
        drawTopBar(g, 0, C_AMBER);

        // header: bell, label, time
        const uint32_t t = now - rv.shownMs;
        int16_t swing = 0, dx = 0;
        if ((t % 2500) < 1300) // rings for 1.3 s, rests for 1.2 s
        {
            float ph = t / 75.0f;
            swing = (int16_t)(sinf(ph) * imax(2, u(3.0f)));
            dx = (int16_t)(sinf(ph * 0.5f) * 1.0f);
        }
        drawBigBell(g, u(16), u(16), u(5), dx, swing / 2, C_BLUSH);
        g.setTextColor(mixColor(C_SOFT, C_WHITE, 0.5f), C_BG);
        g.setTextDatum(TL_DATUM);
        g.drawString("Reminder", u(30), u(6), 1);
        char tm[8];
        snprintf(tm, sizeof(tm), "%02u:%02u", (unsigned)rv.hour, (unsigned)rv.minute);
        smoothText(g, ClockSm18, CLOCKSM18_CAPH, tm, u(30), u(22), L_BASELINE, C_AMBER, C_BG);
        if (rv.waiting > 0) // more reminders are queued behind this one
        {
            char more[8];
            snprintf(more, sizeof(more), "+%u", (unsigned)rv.waiting);
            g.setTextDatum(TR_DATUM);
            g.setTextColor(C_AMBER, C_BG);
            g.drawString(more, W - u(8), u(8), 2);
        }
        drawDivider(g, u(6), W - u(6), headerBottom());

        // the text
        g.loadFont(FONTS[LL.font]);
        g.setTextColor(C_WHITE, C_BG);
        g.setTextDatum(C_BASELINE);
        for (int i = 0; i < LL.lines; i++)
            g.drawString(LL.vis[i], W / 2, LL.baseline[i]);
        g.unloadFont();

        // time left before it closes by itself
        if (rv.timeoutMs)
        {
            const float left = t >= rv.timeoutMs ? 0.0f : 1.0f - (float)t / rv.timeoutMs;
            const int16_t bx = u(10), bw = W - 2 * u(10), bh = imax(3, u(3)), by = H - u(7);
            g.fillRoundRect(bx, by, bw, bh, bh / 2, C_GHOST);
            const int16_t fw = (int16_t)(bw * left);
            if (fw > 0) // shrinks toward the middle
                g.fillRoundRect(bx + (bw - fw) / 2, by, fw, bh, bh / 2, tint(C_AMBER, 0.8f));
        }
        faceCv.endAt(0);
    }
} // namespace disp
#endif
