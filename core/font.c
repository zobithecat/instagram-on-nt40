/* core/font.c — 5x7 Latin bitmap font data + renderer, plus a 14x14 Hangul
 * bitmap font (KS X 1001 subset, see font_hangul_data.h) for real-feed
 * captions that mix English and Korean. Each Latin glyph is 7 rows; the low
 * 5 bits of each row are the pixels, MSB = left (same convention for the
 * Hangul glyphs' low 14 bits). */
#include "font.h"
#include "font_hangul_data.h"

#define HANGUL_ADV (HANGUL_W + 1) /* 1px gap, same spirit as FONT_ADV */

/* Binary search HANGUL_CODEPOINTS (sorted ascending) for a decoded UTF-8
 * codepoint. Returns the glyph index, or -1 if this codepoint isn't in the
 * KS X 1001 subset (rare modern Hangul syllable, or a non-Hangul script). */
static int hangul_lookup(unsigned cp) {
    int lo = 0, hi = HANGUL_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        unsigned v = HANGUL_CODEPOINTS[mid];
        if (v == cp) return mid;
        if (v < cp) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* How many bytes the UTF-8 sequence starting at `c` should occupy, per the
 * leading byte's high bits. Returns 1 for an invalid lead byte (treat as a
 * single stray byte rather than desyncing the rest of the string). */
static int utf8_seq_len(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Continuation bytes p[1..seqlen-1] must all be 10xxxxxx and, critically,
 * non-NUL -- a truncated sequence at the end of the string must not read
 * past its NUL terminator. */
static int utf8_seq_valid(const unsigned char *p, int seqlen) {
    for (int i = 1; i < seqlen; i++) {
        if (p[i] == 0 || (p[i] & 0xC0) != 0x80) return 0;
    }
    return 1;
}

/* Rows written as 0bXXXXX so the glyph shapes are legible in source. */
static const unsigned char FONT[95][7] = {
    { 0,0,0,0,0,0,0 },                                                  /* ' ' */
    { 0b00100,0b00100,0b00100,0b00100,0b00100,0b00000,0b00100 },        /* ! */
    { 0b01010,0b01010,0b01010,0b00000,0b00000,0b00000,0b00000 },        /* " */
    { 0b01010,0b01010,0b11111,0b01010,0b11111,0b01010,0b01010 },        /* # */
    { 0b00100,0b01111,0b10100,0b01110,0b00101,0b11110,0b00100 },        /* $ */
    { 0b11000,0b11001,0b00010,0b00100,0b01000,0b10011,0b00011 },        /* % */
    { 0b01100,0b10010,0b10100,0b01000,0b10101,0b10010,0b01101 },        /* & */
    { 0b00100,0b00100,0b01000,0b00000,0b00000,0b00000,0b00000 },        /* ' */
    { 0b00010,0b00100,0b01000,0b01000,0b01000,0b00100,0b00010 },        /* ( */
    { 0b01000,0b00100,0b00010,0b00010,0b00010,0b00100,0b01000 },        /* ) */
    { 0b00000,0b00100,0b10101,0b01110,0b10101,0b00100,0b00000 },        /* * */
    { 0b00000,0b00100,0b00100,0b11111,0b00100,0b00100,0b00000 },        /* + */
    { 0b00000,0b00000,0b00000,0b00000,0b00100,0b00100,0b01000 },        /* , */
    { 0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000 },        /* - */
    { 0b00000,0b00000,0b00000,0b00000,0b00000,0b00110,0b00110 },        /* . */
    { 0b00001,0b00010,0b00100,0b00100,0b00100,0b01000,0b10000 },        /* / */
    { 0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110 },        /* 0 */
    { 0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110 },        /* 1 */
    { 0b01110,0b10001,0b00001,0b00110,0b01000,0b10000,0b11111 },        /* 2 */
    { 0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110 },        /* 3 */
    { 0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010 },        /* 4 */
    { 0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110 },        /* 5 */
    { 0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110 },        /* 6 */
    { 0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000 },        /* 7 */
    { 0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110 },        /* 8 */
    { 0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100 },        /* 9 */
    { 0b00000,0b00110,0b00110,0b00000,0b00110,0b00110,0b00000 },        /* : */
    { 0b00000,0b00110,0b00110,0b00000,0b00110,0b00100,0b01000 },        /* ; */
    { 0b00010,0b00100,0b01000,0b10000,0b01000,0b00100,0b00010 },        /* < */
    { 0b00000,0b00000,0b11111,0b00000,0b11111,0b00000,0b00000 },        /* = */
    { 0b01000,0b00100,0b00010,0b00001,0b00010,0b00100,0b01000 },        /* > */
    { 0b01110,0b10001,0b00001,0b00010,0b00100,0b00000,0b00100 },        /* ? */
    { 0b01110,0b10001,0b10111,0b10101,0b10111,0b10000,0b01110 },        /* @ */
    { 0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001 },        /* A */
    { 0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110 },        /* B */
    { 0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110 },        /* C */
    { 0b11100,0b10010,0b10001,0b10001,0b10001,0b10010,0b11100 },        /* D */
    { 0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111 },        /* E */
    { 0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000 },        /* F */
    { 0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111 },        /* G */
    { 0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001 },        /* H */
    { 0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110 },        /* I */
    { 0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100 },        /* J */
    { 0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001 },        /* K */
    { 0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111 },        /* L */
    { 0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001 },        /* M */
    { 0b10001,0b10001,0b11001,0b10101,0b10011,0b10001,0b10001 },        /* N */
    { 0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110 },        /* O */
    { 0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000 },        /* P */
    { 0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101 },        /* Q */
    { 0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001 },        /* R */
    { 0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110 },        /* S */
    { 0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100 },        /* T */
    { 0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110 },        /* U */
    { 0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100 },        /* V */
    { 0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010 },        /* W */
    { 0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001 },        /* X */
    { 0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100 },        /* Y */
    { 0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111 },        /* Z */
    { 0b01110,0b01000,0b01000,0b01000,0b01000,0b01000,0b01110 },        /* [ */
    { 0b10000,0b01000,0b00100,0b00100,0b00100,0b00010,0b00001 },        /* \ */
    { 0b01110,0b00010,0b00010,0b00010,0b00010,0b00010,0b01110 },        /* ] */
    { 0b00100,0b01010,0b10001,0b00000,0b00000,0b00000,0b00000 },        /* ^ */
    { 0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b11111 },        /* _ */
    { 0b01000,0b00100,0b00010,0b00000,0b00000,0b00000,0b00000 },        /* ` */
    { 0b00000,0b00000,0b01110,0b00001,0b01111,0b10001,0b01111 },        /* a */
    { 0b10000,0b10000,0b10110,0b11001,0b10001,0b10001,0b11110 },        /* b */
    { 0b00000,0b00000,0b01110,0b10001,0b10000,0b10001,0b01110 },        /* c */
    { 0b00001,0b00001,0b01101,0b10011,0b10001,0b10001,0b01111 },        /* d */
    { 0b00000,0b00000,0b01110,0b10001,0b11111,0b10000,0b01110 },        /* e */
    { 0b00110,0b01001,0b01000,0b11100,0b01000,0b01000,0b01000 },        /* f */
    { 0b00000,0b00000,0b01111,0b10001,0b01111,0b00001,0b01110 },        /* g */
    { 0b10000,0b10000,0b10110,0b11001,0b10001,0b10001,0b10001 },        /* h */
    { 0b00100,0b00000,0b01100,0b00100,0b00100,0b00100,0b01110 },        /* i */
    { 0b00010,0b00000,0b00110,0b00010,0b00010,0b10010,0b01100 },        /* j */
    { 0b10000,0b10000,0b10010,0b10100,0b11000,0b10100,0b10010 },        /* k */
    { 0b01100,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110 },        /* l */
    { 0b00000,0b00000,0b11010,0b10101,0b10101,0b10001,0b10001 },        /* m */
    { 0b00000,0b00000,0b10110,0b11001,0b10001,0b10001,0b10001 },        /* n */
    { 0b00000,0b00000,0b01110,0b10001,0b10001,0b10001,0b01110 },        /* o */
    { 0b00000,0b00000,0b11110,0b10001,0b11110,0b10000,0b10000 },        /* p */
    { 0b00000,0b00000,0b01101,0b10011,0b01111,0b00001,0b00001 },        /* q */
    { 0b00000,0b00000,0b10110,0b11001,0b10000,0b10000,0b10000 },        /* r */
    { 0b00000,0b00000,0b01111,0b10000,0b01110,0b00001,0b11110 },        /* s */
    { 0b01000,0b01000,0b11100,0b01000,0b01000,0b01001,0b00110 },        /* t */
    { 0b00000,0b00000,0b10001,0b10001,0b10001,0b10011,0b01101 },        /* u */
    { 0b00000,0b00000,0b10001,0b10001,0b10001,0b01010,0b00100 },        /* v */
    { 0b00000,0b00000,0b10001,0b10001,0b10101,0b10101,0b01010 },        /* w */
    { 0b00000,0b00000,0b10001,0b01010,0b00100,0b01010,0b10001 },        /* x */
    { 0b00000,0b00000,0b10001,0b10001,0b01111,0b00001,0b01110 },        /* y */
    { 0b00000,0b00000,0b11111,0b00010,0b00100,0b01000,0b11111 },        /* z */
    { 0b00010,0b00100,0b00100,0b01000,0b00100,0b00100,0b00010 },        /* { */
    { 0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100 },        /* | */
    { 0b01000,0b00100,0b00100,0b00010,0b00100,0b00100,0b01000 },        /* } */
    { 0b00000,0b00000,0b01000,0b10101,0b00010,0b00000,0b00000 },        /* ~ */
};

void font_draw_scaled(Surface *s, int x, int y, const char *str, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    int cx = x;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        unsigned char c = *p;
        if (c == '\n') { cx = x; y += FONT_LINE * scale; p++; continue; }

        if (c >= 32 && c <= 126) {
            const unsigned char *g = FONT[c - 32];
            for (int row = 0; row < FONT_H; row++) {
                unsigned bits = g[row];
                for (int col = 0; col < FONT_W; col++) {
                    if (bits & (1u << (FONT_W - 1 - col))) {
                        Rect r = { cx + col * scale, y + row * scale, scale, scale };
                        surface_blend_rect(s, r, color); /* clips + honors alpha */
                    }
                }
            }
            cx += FONT_ADV * scale;
            p++;
            continue;
        }

        /* Non-ASCII: figure out how many bytes this codepoint occupies so we
         * advance the cursor once per *codepoint*, not once per raw byte. A
         * 3-byte sequence found in the KS X 1001 Hangul table gets a real
         * glyph; anything else (other scripts, emoji, malformed input)
         * advances blank as a single unit. */
        int seqlen = utf8_seq_len(c);
        if (seqlen > 1 && !utf8_seq_valid(p, seqlen)) seqlen = 1;

        int drew = 0;
        if (seqlen == 3) {
            unsigned cp = ((unsigned)(c & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (unsigned)(p[2] & 0x3F);
            int idx = hangul_lookup(cp);
            if (idx >= 0) {
                const unsigned int *g = HANGUL_GLYPHS[idx];
                for (int row = 0; row < HANGUL_H; row++) {
                    unsigned bits = g[row];
                    for (int col = 0; col < HANGUL_W; col++) {
                        if (bits & (1u << (HANGUL_W - 1 - col))) {
                            Rect r = { cx + col * scale, y + row * scale, scale, scale };
                            surface_blend_rect(s, r, color);
                        }
                    }
                }
                cx += HANGUL_ADV * scale;
                drew = 1;
            }
        }
        if (!drew) cx += FONT_ADV * scale;
        p += seqlen;
    }
}

void font_draw(Surface *s, int x, int y, const char *str, uint32_t color) {
    font_draw_scaled(s, x, y, str, color, 1);
}

int font_text_width(const char *str, int scale) {
    if (scale < 1) scale = 1;
    int width = 0, any = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        unsigned char c = *p;
        if (c >= 32 && c <= 126) { width += FONT_ADV; any = 1; p++; continue; }

        int seqlen = utf8_seq_len(c);
        if (seqlen > 1 && !utf8_seq_valid(p, seqlen)) seqlen = 1;
        if (seqlen == 3) {
            unsigned cp = ((unsigned)(c & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (unsigned)(p[2] & 0x3F);
            if (hangul_lookup(cp) >= 0) { width += HANGUL_ADV; any = 1; p += seqlen; continue; }
        }
        width += FONT_ADV; any = 1;
        p += seqlen;
    }
    if (!any) return 0;
    /* Every glyph's ADV includes a trailing 1px gap; the original (ASCII-
     * only) formula dropped just the last one. Same here, glyph-width-
     * agnostic since every ADV (Latin or Hangul) reserves exactly 1px for it. */
    return width * scale - scale;
}
