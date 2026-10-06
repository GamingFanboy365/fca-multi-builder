/*
 * text.c - title/text conversion (UTF-8, Shift-JIS, EUC-JP, ASCII).
 */
#include "text.h"
#include "common.h"
#include "sjis_table.h"

#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Decode one UTF-8 sequence; returns the code point and advances *s, or
 * returns -1 (and advances one byte) on malformed input. */
static long utf8_next(const unsigned char **s)
{
    const unsigned char *p = *s;
    long cp;
    int n, i;

    if (p[0] < 0x80) {
        *s = p + 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; n = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; n = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; n = 3; }
    else { *s = p + 1; return -1; }
    for (i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s = p + 1;
            return -1;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) ||
        (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
        *s = p + 1;
        return -1;
    }
    *s = p + n + 1;
    return cp;
}

static void utf8_put(buf_t *b, long cp)
{
    unsigned char t[4];
    size_t n;
    if (cp < 0x80) { t[0] = (unsigned char)cp; n = 1; }
    else if (cp < 0x800) {
        t[0] = (unsigned char)(0xC0 | (cp >> 6));
        t[1] = (unsigned char)(0x80 | (cp & 0x3F)); n = 2;
    } else if (cp < 0x10000) {
        t[0] = (unsigned char)(0xE0 | (cp >> 12));
        t[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (unsigned char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        t[0] = (unsigned char)(0xF0 | (cp >> 18));
        t[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        t[3] = (unsigned char)(0x80 | (cp & 0x3F)); n = 4;
    }
    buf_append(b, t, n);
}

static char *buf_to_str(buf_t *b)
{
    buf_append_zero(b, 1);
    return (char *)b->data;
}

int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p)
        if (utf8_next(&p) < 0)
            return 0;
    return 1;
}

/* ---- Shift-JIS / EUC-JP ------------------------------------------- */

static unsigned sjis_for_ucs(long cp)
{
    size_t i;
    for (i = 0; i < COUNT(fca_sjis_table); i++)
        if (fca_sjis_table[i].ucs == cp)
            return fca_sjis_table[i].sjis;
    return 0;
}

static long ucs_for_sjis(unsigned sj)
{
    size_t i;
    for (i = 0; i < COUNT(fca_sjis_table); i++)
        if (fca_sjis_table[i].sjis == sj)
            return fca_sjis_table[i].ucs;
    return -1;
}

static long fullwidth_for_halfwidth(long cp)
{
    size_t i;
    for (i = 0; i < COUNT(fca_halfwidth_table); i++)
        if (fca_halfwidth_table[i].half == cp)
            return fca_halfwidth_table[i].full;
    return -1;
}

/* Shift-JIS lead byte pair to EUC-JP (JIS X 0208 rows only). */
static void sjis_to_euc(unsigned sj, unsigned char out[2])
{
    unsigned s1 = sj >> 8, s2 = sj & 0xFF;
    unsigned row = (s1 < 0xA0 ? s1 - 0x70 : s1 - 0xB0) * 2;
    unsigned col;
    if (s2 < 0x9F) {
        row -= 1;
        col = s2 - (s2 >= 0x80 ? 0x20 : 0x1F);
    } else {
        col = s2 - 0x7E;
    }
    out[0] = (unsigned char)(row | 0x80);
    out[1] = (unsigned char)(col | 0x80);
}

static unsigned euc_to_sjis(unsigned char e1, unsigned char e2)
{
    unsigned j1 = e1 & 0x7F, j2 = e2 & 0x7F;
    unsigned s1 = ((j1 + 1) >> 1) + (j1 <= 0x5E ? 0x70 : 0xB0);
    unsigned s2;
    if (j1 & 1)
        s2 = j2 + (j2 < 0x60 ? 0x1F : 0x20);
    else
        s2 = j2 + 0x7E;
    return (s1 << 8) | s2;
}

static int is_sjis_lead(unsigned char c)
{
    return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

char *text_from_legacy(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    buf_t b = {0};

    if (utf8_valid(s))
        return xstrdup(s);
    while (*p) {
        if (*p < 0x80) {
            buf_append(&b, p, 1);
            p++;
        } else if (*p >= 0xA1 && *p <= 0xDF) {
            utf8_put(&b, 0xFF61 + (*p - 0xA1));
            p++;
        } else if (is_sjis_lead(*p) && p[1]) {
            long cp = ucs_for_sjis((unsigned)(p[0] << 8 | p[1]));
            utf8_put(&b, cp < 0 ? '?' : cp);
            p += 2;
        } else {
            buf_append(&b, "?", 1);
            p++;
        }
    }
    return buf_to_str(&b);
}

/* Combine a full-width katakana with a following (half-width) voicing mark,
 * the way NES2FCA turned e.g. "ｶﾞ" into "ガ". */
static long apply_voicing(long kana, long mark)
{
    int dakuten = (mark == 0xFF9E || mark == 0x309B || mark == 0x3099);
    int handakuten = (mark == 0xFF9F || mark == 0x309C || mark == 0x309A);
    if (dakuten) {
        if (kana == 0x30A6)
            return 0x30F4; /* ウ -> ヴ */
        if ((kana >= 0x30AB && kana <= 0x30C1 && (kana & 1)) ||
            (kana >= 0x30C4 && kana <= 0x30C8 && !(kana & 1)) ||
            (kana >= 0x30CF && kana <= 0x30DB && (kana - 0x30CF) % 3 == 0))
            return kana + 1;
    } else if (handakuten) {
        if (kana >= 0x30CF && kana <= 0x30DB && (kana - 0x30CF) % 3 == 0)
            return kana + 2;
    }
    return -1;
}

char *title_to_euc(const char *utf8, int max_chars, int max_bytes, int *lossy)
{
    const unsigned char *p = (const unsigned char *)utf8;
    buf_t b = {0};
    int chars = 0;

    if (lossy)
        *lossy = 0;
    while (*p && chars < max_chars) {
        long cp = utf8_next(&p);
        unsigned char enc[2];
        size_t n;

        if (cp >= 0xFF61 && cp <= 0xFF9F) {
            long full = fullwidth_for_halfwidth(cp);
            const unsigned char *q = p;
            long mark = *q ? utf8_next(&q) : 0;
            long voiced = full >= 0 ? apply_voicing(full, mark) : -1;
            if (voiced >= 0) {
                full = voiced;
                p = q;
            }
            cp = full;
        }
        if (cp >= 0x20 && cp < 0x7F) {
            enc[0] = (unsigned char)cp;
            n = 1;
        } else {
            unsigned sj = cp > 0 ? sjis_for_ucs(cp) : 0;
            if (sj) {
                sjis_to_euc(sj, enc);
                n = 2;
            } else {
                enc[0] = '?';
                n = 1;
                if (lossy)
                    *lossy = 1;
            }
        }
        if ((int)(b.len + n) > max_bytes)
            break;
        buf_append(&b, enc, n);
        chars++;
    }
    return buf_to_str(&b);
}

char *euc_to_utf8(const char *euc, size_t max_bytes)
{
    const unsigned char *p = (const unsigned char *)euc;
    const unsigned char *end = p + max_bytes;
    buf_t b = {0};

    while (p < end && *p) {
        if (*p < 0x80) {
            buf_append(&b, p, 1);
            p++;
        } else if (*p >= 0xA1 && *p <= 0xFE && p + 1 < end && p[1] >= 0xA1) {
            long cp = ucs_for_sjis(euc_to_sjis(p[0], p[1]));
            utf8_put(&b, cp < 0 ? '?' : cp);
            p += 2;
        } else {
            buf_append(&b, "?", 1);
            p++;
        }
    }
    return buf_to_str(&b);
}

/* Latin-1 Supplement (U+00C0..U+00FF) folded to ASCII. */
static const char latin1_fold[64] =
    "AAAAAAACEEEEIIII"
    "DNOOOOOxOUUUUYTs"
    "aaaaaaaceeeeiiii"
    "dnooooo/ouuuuyty";

char *title_to_ascii(const char *utf8, int max_bytes, int *lossy)
{
    const unsigned char *p = (const unsigned char *)utf8;
    buf_t b = {0};

    if (lossy)
        *lossy = 0;
    while (*p && (int)b.len < max_bytes) {
        long cp = utf8_next(&p);
        char c;
        if (cp >= 0x20 && cp < 0x7F)
            c = (char)cp;
        else if (cp >= 0xC0 && cp <= 0xFF)
            c = latin1_fold[cp - 0xC0];
        else if (cp >= 0xFF01 && cp <= 0xFF5E)
            c = (char)(cp - 0xFF01 + 0x21);
        else if (cp == 0x3000)
            c = ' ';
        else if (cp == 0x2018 || cp == 0x2019)
            c = '\'';
        else if (cp == 0x201C || cp == 0x201D)
            c = '"';
        else if (cp == 0x2013 || cp == 0x2014)
            c = '-';
        else {
            c = '?';
            if (lossy)
                *lossy = 1;
        }
        buf_append(&b, &c, 1);
    }
    return buf_to_str(&b);
}
