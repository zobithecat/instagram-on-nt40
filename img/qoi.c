/* img/qoi.c — QOI codec. See qoi.h. Pure C, no floating point. */
#include "qoi.h"
#include <stdlib.h>
#include <string.h>

#define QOI_OP_INDEX 0x00 /* 00xxxxxx */
#define QOI_OP_DIFF  0x40 /* 01xxxxxx */
#define QOI_OP_LUMA  0x80 /* 10xxxxxx */
#define QOI_OP_RUN   0xC0 /* 11xxxxxx */
#define QOI_OP_RGB   0xFE
#define QOI_OP_RGBA  0xFF
#define QOI_MASK2    0xC0

#define QOI_HASH(r, g, b, a) (((r) * 3 + (g) * 5 + (b) * 7 + (a) * 11) & 63)

int qoi_decode(const void *data, int len, Surface *out) {
    const unsigned char *p = (const unsigned char *)data;
    if (!p || len < 14 + 8) return -1;
    if (p[0] != 'q' || p[1] != 'o' || p[2] != 'i' || p[3] != 'f') return -1;

    unsigned w = ((unsigned)p[4] << 24) | ((unsigned)p[5] << 16) | ((unsigned)p[6] << 8) | p[7];
    unsigned h = ((unsigned)p[8] << 24) | ((unsigned)p[9] << 16) | ((unsigned)p[10] << 8) | p[11];
    if (w == 0 || h == 0 || w > 20000 || h > 20000) return -1;
    if (surface_alloc(out, (int)w, (int)h) != 0) return -1;

    unsigned char index[64][4];
    memset(index, 0, sizeof(index));
    unsigned char r = 0, g = 0, b = 0, a = 255;
    int pos = 14;
    long n = (long)w * (long)h;
    int run = 0;

    for (long i = 0; i < n; i++) {
        if (run > 0) {
            run--;
        } else if (pos < len) {
            int op = p[pos++];
            if (op == QOI_OP_RGB) {
                if (pos + 3 > len) break;
                r = p[pos++]; g = p[pos++]; b = p[pos++];
            } else if (op == QOI_OP_RGBA) {
                if (pos + 4 > len) break;
                r = p[pos++]; g = p[pos++]; b = p[pos++]; a = p[pos++];
            } else if ((op & QOI_MASK2) == QOI_OP_INDEX) {
                int ix = op & 63;
                r = index[ix][0]; g = index[ix][1]; b = index[ix][2]; a = index[ix][3];
            } else if ((op & QOI_MASK2) == QOI_OP_DIFF) {
                r += ((op >> 4) & 3) - 2;
                g += ((op >> 2) & 3) - 2;
                b += (op & 3) - 2;
            } else if ((op & QOI_MASK2) == QOI_OP_LUMA) {
                if (pos >= len) break;
                int b2 = p[pos++];
                int vg = (op & 63) - 32;
                r += vg + ((b2 >> 4) & 15) - 8;
                g += vg;
                b += vg + (b2 & 15) - 8;
            } else { /* QOI_OP_RUN */
                run = op & 63;
            }
            int hh = QOI_HASH(r, g, b, a);
            index[hh][0] = r; index[hh][1] = g; index[hh][2] = b; index[hh][3] = a;
        }
        out->pixels[i] = ras_argb(a, r, g, b);
    }
    return 0;
}

static void put32(unsigned char *b, int *p, unsigned v) {
    b[(*p)++] = (unsigned char)(v >> 24);
    b[(*p)++] = (unsigned char)(v >> 16);
    b[(*p)++] = (unsigned char)(v >> 8);
    b[(*p)++] = (unsigned char)(v);
}

unsigned char *qoi_encode(const Surface *s, int *out_len) {
    if (!s || s->w <= 0 || s->h <= 0) return NULL;
    long n = (long)s->w * (long)s->h;
    long cap = n * 5 + 14 + 8;
    unsigned char *out = (unsigned char *)malloc((size_t)cap);
    if (!out) return NULL;

    int p = 0;
    out[p++] = 'q'; out[p++] = 'o'; out[p++] = 'i'; out[p++] = 'f';
    put32(out, &p, (unsigned)s->w);
    put32(out, &p, (unsigned)s->h);
    out[p++] = 4; /* channels: RGBA */
    out[p++] = 0; /* colorspace: sRGB with linear alpha */

    unsigned char index[64][4];
    memset(index, 0, sizeof(index));
    unsigned char pr = 0, pg = 0, pb = 0, pa = 255;
    int run = 0;

    for (int y = 0; y < s->h; y++) {
        const uint32_t *row = s->pixels + (size_t)y * s->stride;
        for (int x = 0; x < s->w; x++) {
            uint32_t px = row[x];
            unsigned char r = ras_r(px), g = ras_g(px), b = ras_b(px), a = ras_a(px);

            if (r == pr && g == pg && b == pb && a == pa) {
                run++;
                if (run == 62 || (y == s->h - 1 && x == s->w - 1)) {
                    out[p++] = (unsigned char)(QOI_OP_RUN | (run - 1));
                    run = 0;
                }
            } else {
                if (run > 0) {
                    out[p++] = (unsigned char)(QOI_OP_RUN | (run - 1));
                    run = 0;
                }
                int hh = QOI_HASH(r, g, b, a);
                if (index[hh][0] == r && index[hh][1] == g && index[hh][2] == b && index[hh][3] == a) {
                    out[p++] = (unsigned char)(QOI_OP_INDEX | hh);
                } else {
                    index[hh][0] = r; index[hh][1] = g; index[hh][2] = b; index[hh][3] = a;
                    if (a == pa) {
                        signed char vr = (signed char)(r - pr);
                        signed char vg = (signed char)(g - pg);
                        signed char vb = (signed char)(b - pb);
                        signed char vgr = (signed char)(vr - vg);
                        signed char vgb = (signed char)(vb - vg);
                        if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2) {
                            out[p++] = (unsigned char)(QOI_OP_DIFF | ((vr + 2) << 4) | ((vg + 2) << 2) | (vb + 2));
                        } else if (vgr > -9 && vgr < 8 && vg > -33 && vg < 32 && vgb > -9 && vgb < 8) {
                            out[p++] = (unsigned char)(QOI_OP_LUMA | (vg + 32));
                            out[p++] = (unsigned char)(((vgr + 8) << 4) | (vgb + 8));
                        } else {
                            out[p++] = QOI_OP_RGB;
                            out[p++] = r; out[p++] = g; out[p++] = b;
                        }
                    } else {
                        out[p++] = QOI_OP_RGBA;
                        out[p++] = r; out[p++] = g; out[p++] = b; out[p++] = a;
                    }
                }
            }
            pr = r; pg = g; pb = b; pa = a;
        }
    }

    /* 8-byte end marker */
    for (int i = 0; i < 7; i++) out[p++] = 0;
    out[p++] = 1;

    if (out_len) *out_len = p;
    return out;
}

void qoi_free(void *ptr) { free(ptr); }
