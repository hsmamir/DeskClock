// persian_text.cpp - Persian / Arabic text for a plain left-to-right glyph renderer. See persian_text.h.
#include "persian_text.h"
#include "persian_forms.h"
#include <string.h>

namespace fa
{
    const char kEllipsis[] = "\xE2\x80\xA6";

    static const int MAXCP = 128; // code points handled per line (a reminder is far shorter)

    // ------------------------------------------------------------------ UTF-8

    // Decodes one character at s. Returns the bytes used, 0 = invalid sequence (strict: no overlongs,
    // no surrogates). cp is set for valid sequences, including characters outside the BMP.
    static int decodeOne(const unsigned char *s, size_t n, uint32_t &cp)
    {
        if (n == 0)
            return 0;
        unsigned char c = s[0];
        if (c < 0x80)
        {
            cp = c;
            return 1;
        }
        if ((c & 0xE0) == 0xC0 && n >= 2 && (s[1] & 0xC0) == 0x80)
        {
            cp = ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
            return cp >= 0x80 ? 2 : 0;
        }
        if ((c & 0xF0) == 0xE0 && n >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
        {
            cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            return (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) ? 3 : 0;
        }
        if ((c & 0xF8) == 0xF0 && n >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
        {
            cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) |
                 (s[3] & 0x3F);
            return (cp >= 0x10000 && cp <= 0x10FFFF) ? 4 : 0;
        }
        return 0;
    }

    static int encodeOne(uint16_t cp, char *out)
    {
        if (cp < 0x80)
        {
            out[0] = (char)cp;
            return 1;
        }
        if (cp < 0x800)
        {
            out[0] = (char)(0xC0 | (cp >> 6));
            out[1] = (char)(0x80 | (cp & 0x3F));
            return 2;
        }
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }

    // Lenient decode for drawing: invalid bytes and characters outside the BMP are skipped.
    static int decode(const char *s, size_t len, uint16_t *out, int cap)
    {
        int n = 0;
        size_t i = 0;
        while (i < len && n < cap)
        {
            uint32_t cp;
            int k = decodeOne((const unsigned char *)s + i, len - i, cp);
            if (k == 0)
            {
                i++;
                continue;
            }
            i += k;
            if (cp <= 0xFFFF)
                out[n++] = (uint16_t)cp;
        }
        return n;
    }

    // ------------------------------------------------------------------ character classes

    static bool isSpaceLike(uint32_t c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x00A0 || (c >= 0x2000 && c <= 0x200A) ||
               c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
    }

    // Characters that never reach the screen: controls, joiners / bidi marks, variation selectors.
    static bool isInvisible(uint32_t c)
    {
        return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x200B || c == 0x200D || c == 0x200E || c == 0x200F ||
               (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) || c == 0xFEFF ||
               (c >= 0xFE00 && c <= 0xFE0F) || c == 0x061C;
    }

    // Persian-friendly normalisation of one character. Returns 0 = drop it.
    static uint16_t normalise(uint16_t c)
    {
        if (isSpaceLike(c))
            return ' ';
        if (c == 0x200C)
            return c; // ZWNJ stays: it breaks a join
        if (isInvisible(c))
            return 0;
        if (c == 0x0640 || (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06ED))
            return 0; // tatweel, vowel marks, Quranic marks
        if (c == 0x064A)
            return 0x06CC; // Arabic yeh -> Persian yeh
        if (c == 0x0643)
            return 0x06A9; // Arabic kaf -> Persian kaf
        if (c >= 0x0660 && c <= 0x0669)
            return (uint16_t)(0x06F0 + (c - 0x0660)); // Arabic-Indic -> Persian digits
        if (c == 0x06D2)
            return 0x06CC;
        if (c == 0x06BE || c == 0x06C1)
            return 0x0647; // heh variants
        return c;
    }

    enum : uint8_t
    {
        C_R,  // strong right-to-left (letters)
        C_L,  // strong left-to-right (Latin ...)
        C_EN, // digit
        C_AN, // digit that follows an Arabic-script letter (UBA W2): behaves like a number, but a % next to it does not stick
        C_CS, // separator that stays inside a number: , . : / + -
        C_ET, // terminator that sticks to a number: % $ #
        C_NEU // space, punctuation, brackets
    };

    static uint8_t classify(uint16_t c)
    {
        if ((c >= '0' && c <= '9') || (c >= 0x06F0 && c <= 0x06F9))
            return C_EN;
        if (c == ',' || c == '.' || c == ':' || c == '/' || c == '+' || c == '-' || c == 0x060C || c == 0x066B ||
            c == 0x066C)
            return C_CS;
        if (c == '%' || c == '$' || c == '#' || c == 0x00B0 || c == 0x066A)
            return C_ET;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            return C_L;
        if ((c >= 0x00C0 && c < 0x0590 && c != 0x00D7 && c != 0x00F7))
            return C_L;
        if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF))
            return C_R;
        return C_NEU;
    }

    static uint16_t mirror(uint16_t c)
    {
        switch (c)
        {
        case '(':
            return ')';
        case ')':
            return '(';
        case '[':
            return ']';
        case ']':
            return '[';
        case '{':
            return '}';
        case '}':
            return '{';
        case '<':
            return '>';
        case '>':
            return '<';
        case 0x00AB:
            return 0x00BB;
        case 0x00BB:
            return 0x00AB;
        }
        return c;
    }

    // ------------------------------------------------------------------ shaping

    static const Forms *findForms(uint16_t cp)
    {
        if (cp < FORMS[0].base || cp > FORMS[FORMS_N - 1].base)
            return nullptr;
        unsigned lo = 0, hi = FORMS_N;
        while (lo < hi)
        {
            unsigned mid = (lo + hi) / 2;
            if (FORMS[mid].base == cp)
                return &FORMS[mid];
            if (FORMS[mid].base < cp)
                lo = mid + 1;
            else
                hi = mid;
        }
        return nullptr;
    }

    static const Lig *findLig(uint16_t alef)
    {
        for (unsigned i = 0; i < LIGS_N; i++)
            if (LIGS[i].alef == alef)
                return &LIGS[i];
        return nullptr;
    }

    // Logical letters -> glyph code points (still in logical order). ZWNJ is consumed here.
    static int shape(const uint16_t *in, int n, uint16_t *out)
    {
        int m = 0;
        bool prevJoins = false; // the previous letter reaches out to this one
        for (int i = 0; i < n; i++)
        {
            uint16_t c = in[i];
            if (c == 0x200C)
            {
                prevJoins = false;
                continue;
            }
            const Forms *f = findForms(c);
            if (!f)
            {
                out[m++] = c;
                prevJoins = false;
                continue;
            }
            if (c == 0x0644 && i + 1 < n) // lam + alef = one glyph
            {
                const Lig *lg = findLig(in[i + 1]);
                if (lg)
                {
                    out[m++] = prevJoins ? lg->fin : lg->iso;
                    prevJoins = false; // the alef does not join forward
                    i++;
                    continue;
                }
            }
            bool nextAccepts = false; // this letter reaches out to the next one, and the next one takes it
            if (f->ini && i + 1 < n)
            {
                const Forms *g = findForms(in[i + 1]);
                nextAccepts = g && g->fin;
            }
            bool joinPrev = prevJoins && f->fin;
            uint16_t glyph;
            if (joinPrev && nextAccepts)
                glyph = f->med ? f->med : f->fin;
            else if (joinPrev)
                glyph = f->fin;
            else if (nextAccepts)
                glyph = f->ini;
            else
                glyph = f->iso;
            if (!glyph)
                glyph = f->iso ? f->iso : c;
            out[m++] = glyph;
            prevJoins = nextAccepts;
        }
        return m;
    }

    // ------------------------------------------------------------------ bidi (simplified UBA)

    // Embedding level of every glyph: 0 / 2 = left-to-right, 1 = right-to-left. Follows the Unicode bidi
    // algorithm (rules W2, W4-W7, N1-N2, I1-I2) for the one case we have: Arabic-script letters (all of them
    // class AL) mixed with Latin text and numbers.
    static void resolveLevels(const uint8_t *cin, int n, bool rtl, uint8_t *lvl)
    {
        uint8_t c[MAXCP];
        memcpy(c, cin, n);

        // W2: a digit after an Arabic letter is an Arabic number (the sos of a right-to-left paragraph is no letter)
        uint8_t strong = 0xFF; // 0xFF = none yet
        for (int i = 0; i < n; i++)
        {
            if (c[i] == C_R || c[i] == C_L)
                strong = c[i];
            else if (c[i] == C_EN && strong == C_R)
                c[i] = C_AN;
        }
        // W4: one separator between two numbers of the same kind is part of the number (14:30, 3.5)
        for (int i = 1; i + 1 < n; i++)
            if (c[i] == C_CS && (c[i - 1] == C_EN || c[i - 1] == C_AN) && c[i + 1] == c[i - 1])
                c[i] = c[i - 1];
        // W5: % $ # next to a European number are part of it (not next to an Arabic number)
        for (int i = 1; i < n; i++)
            if (c[i] == C_ET && c[i - 1] == C_EN)
                c[i] = C_EN;
        for (int i = n - 2; i >= 0; i--)
            if (c[i] == C_ET && c[i + 1] == C_EN)
                c[i] = C_EN;
        // W6: the rest is neutral
        for (int i = 0; i < n; i++)
            if (c[i] == C_CS || c[i] == C_ET)
                c[i] = C_NEU;
        // W7: a European number after Latin text is Latin text
        uint8_t last = rtl ? C_R : C_L;
        for (int i = 0; i < n; i++)
        {
            if (c[i] == C_R || c[i] == C_L)
                last = c[i];
            else if (c[i] == C_EN && last == C_L)
                c[i] = C_L;
        }

        // N1 / N2: neutrals between the same direction take it (a number counts as right-to-left), else the paragraph's
        const uint8_t para = rtl ? C_R : C_L;
        for (int i = 0; i < n; i++)
        {
            if (c[i] != C_NEU)
                continue;
            int j = i;
            while (j < n && c[j] == C_NEU)
                j++;
            uint8_t left = i == 0 ? para : (uint8_t)(c[i - 1] == C_L ? C_L : C_R);
            uint8_t right = j == n ? para : (uint8_t)(c[j] == C_L ? C_L : C_R);
            uint8_t res = left == right ? left : para;
            for (int k = i; k < j; k++)
                c[k] = res;
            i = j - 1;
        }

        // I1 / I2
        for (int i = 0; i < n; i++)
        {
            if (c[i] == C_R)
                lvl[i] = 1;
            else if (c[i] == C_L)
                lvl[i] = rtl ? 2 : 0;
            else
                lvl[i] = 2; // numbers
        }
    }

    // Visual order: ord[k] = which logical glyph is drawn k-th from the left.
    static void reorder(const uint8_t *lvl, int n, uint8_t *ord)
    {
        int maxL = 0, minOdd = 99;
        for (int i = 0; i < n; i++)
        {
            ord[i] = (uint8_t)i;
            if (lvl[i] > maxL)
                maxL = lvl[i];
            if ((lvl[i] & 1) && lvl[i] < minOdd)
                minOdd = lvl[i];
        }
        if (minOdd == 99)
            return; // nothing right-to-left
        for (int L = maxL; L >= minOdd; L--)
        {
            int i = 0;
            while (i < n)
            {
                if (lvl[ord[i]] < L)
                {
                    i++;
                    continue;
                }
                int j = i;
                while (j < n && lvl[ord[j]] >= L)
                    j++;
                for (int a = i, b = j - 1; a < b; a++, b--)
                {
                    uint8_t t = ord[a];
                    ord[a] = ord[b];
                    ord[b] = t;
                }
                i = j;
            }
        }
    }

    // ------------------------------------------------------------------ public

    bool hasRtl(const char *utf8)
    {
        uint16_t cps[MAXCP];
        int n = decode(utf8, strlen(utf8), cps, MAXCP);
        for (int i = 0; i < n; i++)
            if (classify(normalise(cps[i])) == C_R)
                return true;
        return false;
    }

    int sanitize(const char *in, char *out, size_t cap)
    {
        if (cap == 0)
            return -2;
        size_t len = strlen(in), w = 0;
        bool pendingSpace = false;
        for (size_t i = 0; i < len;)
        {
            uint32_t cp;
            int k = decodeOne((const unsigned char *)in + i, len - i, cp);
            if (k == 0)
                return -1;
            i += k;
            if (isSpaceLike(cp))
            {
                pendingSpace = w > 0; // leading spaces are dropped
                continue;
            }
            if (cp > 0xFFFF || (isInvisible(cp) && cp != 0x200C))
                continue; // emoji, controls, bidi marks
            char enc[4];
            int e = encodeOne((uint16_t)cp, enc);
            size_t need = (size_t)e + (pendingSpace ? 1 : 0);
            if (w + need + 1 > cap)
                return -2;
            if (pendingSpace)
                out[w++] = ' ';
            pendingSpace = false;
            memcpy(out + w, enc, e);
            w += e;
        }
        out[w] = '\0';
        return (int)w;
    }

    size_t visual(const char *utf8, size_t len, char *out, size_t cap, int paraDir)
    {
        if (cap == 0)
            return 0;
        uint16_t cps[MAXCP], norm[MAXCP], glyph[MAXCP];
        uint8_t cls[MAXCP], lvl[MAXCP], ord[MAXCP];

        int n = decode(utf8, len, cps, MAXCP);
        int nn = 0;
        for (int i = 0; i < n; i++)
        {
            uint16_t c = normalise(cps[i]);
            if (c)
                norm[nn++] = c;
        }
        int m = shape(norm, nn, glyph);

        bool anyR = false;
        for (int i = 0; i < m; i++)
        {
            cls[i] = classify(glyph[i]);
            anyR |= cls[i] == C_R;
        }
        bool rtl = paraDir == DIR_AUTO ? anyR : paraDir == DIR_RTL;
        resolveLevels(cls, m, rtl, lvl);
        reorder(lvl, m, ord);

        size_t w = 0;
        for (int k = 0; k < m; k++)
        {
            uint16_t g = glyph[ord[k]];
            if (lvl[ord[k]] & 1)
                g = mirror(g);
            char enc[4];
            int e = encodeOne(g, enc);
            if (w + e + 1 > cap)
                break;
            memcpy(out + w, enc, e);
            w += e;
        }
        out[w] = '\0';
        return w;
    }

    // ------------------------------------------------------------------ wrapping

    namespace
    {
        struct Meter
        {
            const char *text;
            MeasureFn fn;
            void *ctx;
            int dir;
            int width(size_t from, size_t to) const
            {
                char buf[3 * MAXCP + 4];
                visual(text + from, to - from, buf, sizeof(buf), dir);
                return fn(buf, ctx);
            }
        };
    } // namespace

    // The last full character that starts at or before p (p itself if it is a start byte).
    static size_t charStart(const char *t, size_t p)
    {
        while (p > 0 && ((unsigned char)t[p] & 0xC0) == 0x80)
            p--;
        return p;
    }

    // Shortens [from, *end) until the line plus an ellipsis fits maxW.
    static void shrinkForEllipsis(const char *text, size_t from, size_t *end, int maxW, MeasureFn fn, void *ctx, int dir)
    {
        char tmp[3 * MAXCP + 8];
        while (*end > from)
        {
            size_t n = *end - from;
            if (n + 3 + 1 <= sizeof(tmp))
            {
                memcpy(tmp, text + from, n);
                memcpy(tmp + n, kEllipsis, 4);
                char vis[3 * MAXCP + 4];
                visual(tmp, n + 3, vis, sizeof(vis), dir);
                if (fn(vis, ctx) <= maxW)
                    return;
            }
            size_t p = *end; // drop the last word (or, in a single long word, the last character)
            while (p > from && text[p - 1] != ' ')
                p--;
            if (p > from)
            {
                *end = p - 1; // before the space
                while (*end > from && text[*end - 1] == ' ')
                    (*end)--;
            }
            else
            {
                size_t q = charStart(text, *end - 1);
                *end = q;
            }
        }
    }

    WrapResult wrap(const char *text, size_t len, int maxW, MeasureFn measure, void *ctx, int paraDir, Span *out,
                    int maxLines)
    {
        WrapResult r = {0, false};
        if (maxLines <= 0)
            return r;
        int dir = paraDir == DIR_AUTO ? (hasRtl(text) ? DIR_RTL : DIR_LTR) : paraDir; // one direction for every line
        Meter mt = {text, measure, ctx, dir};

        auto skipSpaces = [&](size_t p)
        {
            while (p < len && text[p] == ' ')
                p++;
            return p;
        };
        auto wordEnd = [&](size_t p)
        {
            while (p < len && text[p] != ' ')
                p++;
            return p;
        };

        size_t pos = skipSpaces(0);
        while (pos < len)
        {
            if (r.lines == maxLines)
            {
                r.truncated = true;
                break;
            }
            size_t end = pos, next = pos, p = pos;
            bool any = false;
            while (p < len)
            {
                size_t we = wordEnd(p);
                if (mt.width(pos, we) > maxW)
                    break;
                end = we;
                any = true;
                p = skipSpaces(we);
                next = p;
            }
            if (!any) // one word wider than the line: cut it between characters
            {
                size_t we = wordEnd(pos);
                size_t e = pos;
                size_t best = 0;
                while (e < we)
                {
                    size_t nx = e + 1;
                    while (nx < we && ((unsigned char)text[nx] & 0xC0) == 0x80)
                        nx++;
                    if (mt.width(pos, nx) > maxW)
                        break;
                    best = nx;
                    e = nx;
                }
                if (best == 0) // not even one character fits: take one anyway, never loop forever
                {
                    best = pos + 1;
                    while (best < we && ((unsigned char)text[best] & 0xC0) == 0x80)
                        best++;
                }
                end = best;
                next = best;
            }
            out[r.lines].start = (uint16_t)pos;
            out[r.lines].len = (uint16_t)(end - pos);
            r.lines++;
            pos = next;
        }

        if (r.truncated && r.lines > 0)
        {
            Span &s = out[r.lines - 1];
            size_t e = (size_t)s.start + s.len;
            shrinkForEllipsis(text, s.start, &e, maxW, measure, ctx, dir);
            s.len = (uint16_t)(e - s.start);
        }
        return r;
    }

    namespace
    {
        struct FontCtx
        {
            FontMeasureFn fn;
            void *ctx;
            int font;
        };
        int fontTrampoline(const char *v, void *c)
        {
            FontCtx *fc = (FontCtx *)c;
            return fc->fn(fc->font, v, fc->ctx);
        }
    } // namespace

    FitResult fit(const char *text, size_t len, int maxW, int nFonts, const int *slotsTotal, FontMeasureFn measure,
                  void *ctx, Span *spans, int maxSpans)
    {
        FitResult res = {0, 0, false};
        for (int f = 0; f < nFonts; f++)
        {
            bool last = f == nFonts - 1;
            int slots = slotsTotal[f] < maxSpans ? slotsTotal[f] : maxSpans;
            if (slots < 1)
            {
                if (!last)
                    continue;
                slots = 1;
            }
            FontCtx fc = {measure, ctx, f};
            WrapResult w = wrap(text, len, maxW, fontTrampoline, &fc, DIR_AUTO, spans, slots);
            res.font = f;
            res.lines = w.lines;
            res.truncated = w.truncated;
            if (!w.truncated)
                break;
        }
        return res;
    }

} // namespace fa
