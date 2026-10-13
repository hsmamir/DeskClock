// display_core.cpp - shared display state: the TFT object, palette, layout, colour helpers,
// canvases and the readers that copy values out of g_state (under its mutex).
#include "display_internal.h"

namespace disp
{

    TFT_eSPI tft = TFT_eSPI();

    // The five themed colours (values are the night look, also used until the clock knows the time).
    uint16_t C_BG = rgb(6, 9, 26);                // background
    uint16_t C_ACCENT = rgb(0, 225, 255);         // clock digits, accent lines, bars (was fixed cyan)
    uint16_t C_EYE_TOP = rgb(140, 245, 255);      // eye gradient, top    (drawn: base + feeling)
    uint16_t C_EYE_BOT = rgb(0, 140, 235);        // eye gradient, bottom
    uint16_t C_EYE_TOP_BASE = rgb(140, 245, 255); // time-of-day eye colours alone
    uint16_t C_EYE_BOT_BASE = rgb(0, 140, 235);
    uint16_t C_FRAME = rgb(1, 2, 6);    // frame around the cards (cardsInit / frameUpdate set the real value)
    uint16_t C_GHOST = rgb(16, 26, 52); // unlit 7-seg segments, empty bars

    // Layout - computed in cardsInit() from tft.width()/height()
    int16_t W, SW, H, faceH, infoH;
    int16_t cardX, cardGap, cardR, faceY, infoY;
    int16_t cardGapBase = 0; // the gap of the clock layout; cardGap is 0 on the one-piece pages (see geometrySet)
    float sc = 1.0f;         // scale relative to the 128 px wide real display
    bool frameDirty = false;
    bool fullLayout = false; // Date / Weather page: one full-screen page instead of face card + info card (display_pages.cpp)

    // ---- Colour helpers ----
    uint16_t dimColor(uint16_t c, uint8_t v)
    {
        uint16_t r = ((c >> 11) & 0x1F) * v / 255;
        uint16_t g = ((c >> 5) & 0x3F) * v / 255;
        uint16_t b = (c & 0x1F) * v / 255;
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    uint16_t mixColor(uint16_t a, uint16_t b, float t)
    {
        int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
        int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
        int r = ar + (int)((br - ar) * t);
        int g = ag + (int)((bg - ag) * t);
        int bl = ab + (int)((bb - ab) * t);
        return (uint16_t)((r << 11) | (g << 5) | bl);
    }

    // ---- Off-screen canvases (see Canvas in display_internal.h) ----
    Canvas faceCv, infoCv;

    // ---- Cards: layout, frame, rounded corners ----
    // The Date page gives the calendar more room: its upper card (weekday, day, month, year) is shorter and the
    // lower card (the calendar) taller. DATE_FACE_FRAC = share of the height (after margins and gap) of the upper
    // card on that page; the clock and the Weather page keep CARD_FACE_FRAC. Override it with -D if you like.
#ifndef DATE_FACE_FRAC
#define DATE_FACE_FRAC 0.44f
#endif
    int16_t faceHMax = 0, infoHMax = 0;
    static int16_t clockFaceH = 0, clockInfoH = 0, dateFaceH = 0, dateInfoH = 0;

    static const int MAX_R = 32;
    static float cornerOut[MAX_R]; // per corner row (0 = outermost): how many px of the row lie outside the arc

    static uint16_t frameColor() { return mixColor(C_BG, 0x0000, CARD_FRAME_DARKEN); }

    void cardsInit()
    {
        SW = tft.width();
        H = tft.height();
        sc = SW / 128.0f;
        cardX = imax(1, u(CARD_MARGIN));
        cardGapBase = cardGap = imax(1, u(CARD_GAP));
        cardR = imin(u(CARD_RADIUS), (int16_t)(MAX_R - 1));
        W = SW - 2 * cardX;
        int16_t avail = H - 2 * cardX - cardGap; // height left for the two cards
        faceH = (int16_t)(avail * CARD_FACE_FRAC + 0.5f);
        infoH = avail - faceH;
        faceY = cardX;
        infoY = faceY + faceH + cardGap;
        clockFaceH = faceH;
        clockInfoH = infoH;
        dateFaceH = (int16_t)(avail * DATE_FACE_FRAC + 0.5f);
        if (dateFaceH > clockFaceH)
            dateFaceH = clockFaceH; // never taller than on the clock page
        dateInfoH = avail - dateFaceH;
        faceHMax = imax(clockFaceH, dateFaceH);
        infoHMax = imax(clockInfoH, dateInfoH) + cardGapBase; // a one-piece page gives the strip to the lower card
        if (cardR > dateFaceH / 2)
            cardR = dateFaceH / 2;
        if (cardR > dateInfoH / 2)
            cardR = dateInfoH / 2;
        if (cardR > faceH / 2)
            cardR = faceH / 2;
        if (cardR > infoH / 2)
            cardR = infoH / 2;
        for (int i = 0; i < MAX_R; i++)
            cornerOut[i] = 0;
        for (int dy = 0; dy < cardR; dy++)
        {
            float yc = cardR - (dy + 0.5f); // distance of the row's middle from the arc centre's row
            cornerOut[dy] = cardR - sqrtf((float)cardR * cardR - yc * yc);
        }
        C_FRAME = frameColor();
        tft.fillScreen(C_FRAME);
        frameDirty = false;
    }

    // Switches the card split (faceH / infoH / infoY and the two canvases) between the clock layout and the Date
    // page, and between two cards with a frame strip between them (the clock page) and ONE page (seamless: the
    // strip is given to the lower card, cardGap = 0, so lines of text can be spaced evenly right across the
    // join). Call it every tick, before anything is drawn. All splits add up to the same height, so the frame
    // and the bottom edge never move.
    void geometrySet(bool dateLayout, bool seamless)
    {
        int16_t fh = dateLayout ? dateFaceH : clockFaceH;
        int16_t ih = dateLayout ? dateInfoH : clockInfoH;
        int16_t gp = cardGapBase;
        if (seamless)
        {
            ih += gp;
            gp = 0;
        }
        if (fh == faceH && gp == cardGap)
            return;
        faceH = fh;
        infoH = ih;
        cardGap = gp;
        infoY = faceY + faceH + cardGap;
        faceCv.h = faceH;
        infoCv.y0 = infoY;
        infoCv.h = infoH;
        gapInvalidate();           // the strip between the cards is somewhere else now (or gone)
        lastInfoSig = 0xFFFFFFFFu; // and the lower card has to be redrawn
    }

    static void frameFill()
    {
        tft.fillRect(0, 0, SW, faceY, C_FRAME);                           // top
        tft.fillRect(0, infoY + infoH, SW, H - (infoY + infoH), C_FRAME); // bottom
        tft.fillRect(0, faceY, cardX, H - 2 * faceY, C_FRAME);            // left
        tft.fillRect(SW - cardX, faceY, cardX, H - 2 * faceY, C_FRAME);   // right
        // Between the cards (the left / right fills above already cover the side margins of those rows). The strip
        // is painted by gapPaint(): frame colour on the clock page, no strip at all on a one-piece page.
        gapInvalidate();
        gapPaint(0);
    }

    // Called every tick after the theme moved. The frame follows the card background; when it changes (or a
    // full-screen page wiped it) it is repainted and the info card is redrawn, so its corners get the new colour.
    void frameUpdate()
    {
        uint16_t f = frameColor();
        if (f == C_FRAME && !frameDirty)
            return;
        C_FRAME = f;
        frameDirty = false;
        frameFill();
        lastInfoSig = 0xFFFFFFFFu;
    }

    // Cuts the four corners of a card that was just pushed, in the frame colour. The outermost pixel of each
    // row is blended with the card's own pixel (read back from the sprite) for a softer edge; while the card
    // slides (aa = false) the sprite pixel is not where the screen shows it, so only whole pixels are cut.
    // corners: which of the card's corners are cut (CORNERS_TOP / CORNERS_BOT); a full-screen page keeps the middle ones square.
    static void maskCorners(Canvas &c, bool aa, uint8_t corners)
    {
        if (cardR < 2)
            return;
        aa = aa && c.ok;
        tft.startWrite();
        for (int16_t dy = 0; dy < cardR; dy++)
        {
            float xo = cornerOut[dy];
            int16_t n = aa ? (int16_t)xo : (int16_t)(xo + 0.5f);
            float frac = aa ? xo - n : 0.0f;
            int16_t yT = c.y0 + dy, yB = c.y0 + c.h - 1 - dy;
            if (n > 0)
            {
                if (corners & CORNERS_TOP)
                {
                    tft.drawFastHLine(c.x0, yT, n, C_FRAME);
                    tft.drawFastHLine(c.x0 + c.w - n, yT, n, C_FRAME);
                }
                if (corners & CORNERS_BOT)
                {
                    tft.drawFastHLine(c.x0, yB, n, C_FRAME);
                    tft.drawFastHLine(c.x0 + c.w - n, yB, n, C_FRAME);
                }
            }
            if (frac > 0.08f)
            {
                int16_t xl = n, xr = c.w - 1 - n;
                int16_t ly = dy, lyb = c.h - 1 - dy;
                if (corners & CORNERS_TOP)
                {
                    tft.drawPixel(c.x0 + xl, yT, mixColor(c.spr.readPixel(xl, ly), C_FRAME, frac));
                    tft.drawPixel(c.x0 + xr, yT, mixColor(c.spr.readPixel(xr, ly), C_FRAME, frac));
                }
                if (corners & CORNERS_BOT)
                {
                    tft.drawPixel(c.x0 + xl, yB, mixColor(c.spr.readPixel(xl, lyb), C_FRAME, frac));
                    tft.drawPixel(c.x0 + xr, yB, mixColor(c.spr.readPixel(xr, lyb), C_FRAME, frac));
                }
            }
        }
        tft.endWrite();
    }

    // Cuts the corners INSIDE the sprite, before it is pushed: the corner pixels go out to the screen already in the
    // frame colour (the outermost pixel of each row blended with the card's own pixel). The old way pushed the whole
    // rectangle first and cut the corners on the screen afterwards, so on every redraw the corners showed card
    // colour for a moment: that was the flicker at the two ends of the strip between the cards (and at the outer
    // corners). The sprite is redrawn from scratch every frame, so nothing is lost by painting into it.
    static void bakeCorners(Canvas &c, uint8_t corners)
    {
        if (cardR < 2)
            return;
        for (int16_t dy = 0; dy < cardR; dy++)
        {
            float xo = cornerOut[dy];
            int16_t n = (int16_t)xo;
            float frac = xo - n;
            int16_t yT = dy, yB = c.h - 1 - dy;
            if (n > 0)
            {
                if (corners & CORNERS_TOP)
                {
                    c.spr.drawFastHLine(0, yT, n, C_FRAME);
                    c.spr.drawFastHLine(c.w - n, yT, n, C_FRAME);
                }
                if (corners & CORNERS_BOT)
                {
                    c.spr.drawFastHLine(0, yB, n, C_FRAME);
                    c.spr.drawFastHLine(c.w - n, yB, n, C_FRAME);
                }
            }
            if (frac > 0.08f)
            {
                int16_t xl = n, xr = c.w - 1 - n;
                if (corners & CORNERS_TOP)
                {
                    c.spr.drawPixel(xl, yT, mixColor(c.spr.readPixel(xl, yT), C_FRAME, frac));
                    c.spr.drawPixel(xr, yT, mixColor(c.spr.readPixel(xr, yT), C_FRAME, frac));
                }
                if (corners & CORNERS_BOT)
                {
                    c.spr.drawPixel(xl, yB, mixColor(c.spr.readPixel(xl, yB), C_FRAME, frac));
                    c.spr.drawPixel(xr, yB, mixColor(c.spr.readPixel(xr, yB), C_FRAME, frac));
                }
            }
        }
    }

    void Canvas::endAt(int16_t dx, uint8_t corners)
    {
        if (ok)
        {
            bool baked = (dx == 0);
            if (baked)
                bakeCorners(*this, corners); // settled card: corners are cut in the sprite, nothing to fix on the screen
            if (dx != 0 || sh != h)
            {
                // clip the sprite to the card: when it slides, and when the sprite is taller than the card
                tft.setViewport(x0, y0, w, h, false); // absolute coords
                spr.pushSprite(x0 + dx, y0);
                tft.resetViewport();
            }
            else
                spr.pushSprite(x0, y0);
            if (!baked)
                maskCorners(*this, false, corners); // sliding card: whole pixels are cut on the screen
        }
        else
        {
            tft.resetViewport();
            maskCorners(*this, false, corners); // no sprite: drawn straight on the screen, cut afterwards
        }
    }

    // Small sprite for the clock-digit slide animation (display_digits.cpp). Defined here, right after
    // tft, so it is constructed after it.
    TFT_eSprite digSpr(&tft);

    // ---- Alarm flash state (toggled by display_tick) ----
    bool alarmFlashOn = false;

    // ---- State readers ----
    Mood readMood()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        Mood m = g_state.mood;
        xSemaphoreGive(g_stateMutex);
        return m;
    }

    DisplayMode readDisplayMode()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        DisplayMode d = g_state.displayMode;
        xSemaphoreGive(g_stateMutex);
        return d;
    }

    bool readTimeSynced()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool t = g_state.timeSynced;
        xSemaphoreGive(g_stateMutex);
        return t;
    }

    bool readPortalActive()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool p = g_state.wifiPortalActive;
        xSemaphoreGive(g_stateMutex);
        return p;
    }

    int readOtaPercent()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        int p = g_state.otaPercent;
        xSemaphoreGive(g_stateMutex);
        return p;
    }

    bool readTlsActive()
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        bool t = g_state.tlsUsers > 0;
        xSemaphoreGive(g_stateMutex);
        return t;
    }

    void readWeather(bool &valid, float &tempC, int &code, uint32_t &updatedMs)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        valid = g_state.weatherValid;
        tempC = g_state.weatherTempC;
        code = g_state.weatherCode;
        updatedMs = g_state.weatherUpdatedMs;
        xSemaphoreGive(g_stateMutex);
    }

    void readWeatherDetail(WeatherDetail &d)
    {
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        d.valid = g_state.weatherValid && g_state.detailValid;
        d.maxC = g_state.tMaxC;
        d.minC = g_state.tMinC;
        d.humidityPct = g_state.humidityPct;
        d.windKmh = g_state.windKmh;
        d.windDirDeg = g_state.windDirDeg;
        d.feelsC = g_state.apparentC;
        d.pressureHpa = g_state.pressureHpa;
        d.uvMax = g_state.uvMax;
        d.barCount = g_state.barCount;
        for (uint8_t i = 0; i < d.barCount && i < AppState::WEATHER_MAX_BARS; i++)
        {
            d.hour[i] = g_state.barHour[i];
            d.tempC[i] = g_state.barTempC[i];
            d.rainPct[i] = g_state.barRainPct[i];
        }
        xSemaphoreGive(g_stateMutex);
    }

    // "45s", "12m", "3h", "2d" — compact age for the small screen.
    void fmtAge(char *out, size_t n, uint32_t ageMs)
    {
        uint32_t s = ageMs / 1000;
        if (s < 60)
            snprintf(out, n, "%us", (unsigned)s);
        else if (s < 3600)
            snprintf(out, n, "%um", (unsigned)(s / 60));
        else if (s < 48UL * 3600)
            snprintf(out, n, "%uh", (unsigned)(s / 3600));
        else
            snprintf(out, n, "%ud", (unsigned)(s / 86400));
    }

    // Age of a millis() timestamp. Signed so a timestamp written by another task
    // a moment after `now` was sampled reads as 0 instead of wrapping to ~49 days.
    uint32_t ageSince(uint32_t now, uint32_t stamp)
    {
        int32_t d = (int32_t)(now - stamp);
        return d < 0 ? 0 : (uint32_t)d;
    }

    uint16_t tempColor(float tempC)
    {
        if (tempC < 5)
            return rgb(100, 170, 255); // cold
        if (tempC < 18)
            return rgb(110, 225, 220); // cool
        if (tempC < 28)
            return C_AMBER;       // mild
        return rgb(255, 120, 60); // hot
    }

} // namespace disp