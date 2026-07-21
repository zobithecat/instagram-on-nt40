/* core/raster.c — see raster.h. Pure C, no OS calls. */
#include "raster.h"
#include <stdlib.h>
#include <string.h>

/* ---- lifecycle ---- */

int surface_alloc(Surface *s, int w, int h) {
    if (!s || w <= 0 || h <= 0) return -1;
    /* guard against overflow in w*h before allocating */
    if ((int64_t)w * (int64_t)h > (int64_t)(1 << 28)) return -1;
    uint32_t *px = (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!px) return -1;
    s->pixels = px;
    s->w = w;
    s->h = h;
    s->stride = w;
    s->owns = 1;
    return 0;
}

void surface_free(Surface *s) {
    if (!s) return;
    if (s->owns && s->pixels) free(s->pixels);
    s->pixels = NULL;
    s->w = s->h = s->stride = 0;
    s->owns = 0;
}

void surface_wrap(Surface *s, uint32_t *pixels, int w, int h, int stride) {
    s->pixels = pixels;
    s->w = w;
    s->h = h;
    s->stride = stride;
    s->owns = 0;
}

/* ---- helpers ---- */

/* Clip rect r to the surface bounds; returns 0 if nothing remains. */
static int clip_rect(const Surface *s, Rect *r) {
    int x0 = r->x, y0 = r->y;
    int x1 = r->x + r->w, y1 = r->y + r->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0) return 0;
    r->x = x0; r->y = y0; r->w = x1 - x0; r->h = y1 - y0;
    return 1;
}

/* src-over blend of a straight-alpha `src` pixel onto an opaque-ish `dst`. */
static inline uint32_t blend_over(uint32_t dst, uint32_t src) {
    uint32_t sa = ras_a(src);
    if (sa == 0) return dst;
    if (sa == 255) return src;
    uint32_t ia = 255 - sa;
    /* +127 for rounding */
    uint32_t r = (ras_r(src) * sa + ras_r(dst) * ia + 127) / 255;
    uint32_t g = (ras_g(src) * sa + ras_g(dst) * ia + 127) / 255;
    uint32_t b = (ras_b(src) * sa + ras_b(dst) * ia + 127) / 255;
    uint32_t da = ras_a(dst);
    uint32_t a = sa + (da * ia + 127) / 255;
    if (a > 255) a = 255;
    return ras_argb((uint8_t)a, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* ---- drawing ---- */

void surface_fill(Surface *s, uint32_t color) {
    for (int y = 0; y < s->h; y++) {
        uint32_t *row = s->pixels + (size_t)y * s->stride;
        for (int x = 0; x < s->w; x++) row[x] = color;
    }
}

void surface_fill_rect(Surface *s, Rect r, uint32_t color) {
    if (!clip_rect(s, &r)) return;
    for (int y = 0; y < r.h; y++) {
        uint32_t *row = s->pixels + (size_t)(r.y + y) * s->stride + r.x;
        for (int x = 0; x < r.w; x++) row[x] = color;
    }
}

void surface_blend_rect(Surface *s, Rect r, uint32_t color) {
    if (!clip_rect(s, &r)) return;
    for (int y = 0; y < r.h; y++) {
        uint32_t *row = s->pixels + (size_t)(r.y + y) * s->stride + r.x;
        for (int x = 0; x < r.w; x++) row[x] = blend_over(row[x], color);
    }
}

void surface_hline(Surface *s, int x, int y, int w, uint32_t color) {
    Rect r = { x, y, w, 1 };
    surface_fill_rect(s, r, color);
}

void surface_vline(Surface *s, int x, int y, int h, uint32_t color) {
    Rect r = { x, y, 1, h };
    surface_fill_rect(s, r, color);
}

void surface_frame(Surface *s, Rect r, uint32_t color) {
    if (r.w <= 0 || r.h <= 0) return;
    surface_hline(s, r.x, r.y, r.w, color);
    surface_hline(s, r.x, r.y + r.h - 1, r.w, color);
    surface_vline(s, r.x, r.y, r.h, color);
    surface_vline(s, r.x + r.w - 1, r.y, r.h, color);
}

void surface_bevel(Surface *s, Rect r, uint32_t light, uint32_t dark) {
    if (r.w <= 0 || r.h <= 0) return;
    surface_hline(s, r.x, r.y, r.w, light);              /* top */
    surface_vline(s, r.x, r.y, r.h, light);              /* left */
    surface_hline(s, r.x, r.y + r.h - 1, r.w, dark);     /* bottom */
    surface_vline(s, r.x + r.w - 1, r.y, r.h, dark);     /* right */
}

/* ---- blitting ---- */

void surface_blit(Surface *dst, int dx, int dy, const Surface *src, const Rect *srcrect) {
    Rect sr = srcrect ? *srcrect : (Rect){ 0, 0, src->w, src->h };
    /* clip source rect to src bounds */
    if (sr.x < 0) { dx -= sr.x; sr.w += sr.x; sr.x = 0; }
    if (sr.y < 0) { dy -= sr.y; sr.h += sr.y; sr.y = 0; }
    if (sr.x + sr.w > src->w) sr.w = src->w - sr.x;
    if (sr.y + sr.h > src->h) sr.h = src->h - sr.y;
    if (sr.w <= 0 || sr.h <= 0) return;
    /* clip against dst */
    if (dx < 0) { sr.x -= dx; sr.w += dx; dx = 0; }
    if (dy < 0) { sr.y -= dy; sr.h += dy; dy = 0; }
    if (dx + sr.w > dst->w) sr.w = dst->w - dx;
    if (dy + sr.h > dst->h) sr.h = dst->h - dy;
    if (sr.w <= 0 || sr.h <= 0) return;
    for (int y = 0; y < sr.h; y++) {
        const uint32_t *srow = src->pixels + (size_t)(sr.y + y) * src->stride + sr.x;
        uint32_t *drow = dst->pixels + (size_t)(dy + y) * dst->stride + dx;
        memcpy(drow, srow, (size_t)sr.w * sizeof(uint32_t));
    }
}

void surface_blit_alpha(Surface *dst, int dx, int dy, const Surface *src) {
    Rect sr = { 0, 0, src->w, src->h };
    if (dx < 0) { sr.x -= dx; sr.w += dx; dx = 0; }
    if (dy < 0) { sr.y -= dy; sr.h += dy; dy = 0; }
    if (dx + sr.w > dst->w) sr.w = dst->w - dx;
    if (dy + sr.h > dst->h) sr.h = dst->h - dy;
    if (sr.w <= 0 || sr.h <= 0) return;
    for (int y = 0; y < sr.h; y++) {
        const uint32_t *srow = src->pixels + (size_t)(sr.y + y) * src->stride + sr.x;
        uint32_t *drow = dst->pixels + (size_t)(dy + y) * dst->stride + dx;
        for (int x = 0; x < sr.w; x++) drow[x] = blend_over(drow[x], srow[x]);
    }
}

void surface_downscale(Surface *dst, const Surface *src) {
    if (dst->w <= 0 || dst->h <= 0 || src->w <= 0 || src->h <= 0) return;
    for (int dy = 0; dy < dst->h; dy++) {
        int sy0 = (int)((int64_t)dy * src->h / dst->h);
        int sy1 = (int)((int64_t)(dy + 1) * src->h / dst->h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src->h) sy1 = src->h;
        uint32_t *drow = dst->pixels + (size_t)dy * dst->stride;
        for (int dx = 0; dx < dst->w; dx++) {
            int sx0 = (int)((int64_t)dx * src->w / dst->w);
            int sx1 = (int)((int64_t)(dx + 1) * src->w / dst->w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src->w) sx1 = src->w;
            uint32_t ar = 0, ag = 0, ab = 0, aa = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint32_t *srow = src->pixels + (size_t)sy * src->stride;
                for (int sx = sx0; sx < sx1; sx++) {
                    uint32_t p = srow[sx];
                    ar += ras_r(p); ag += ras_g(p); ab += ras_b(p); aa += ras_a(p);
                    n++;
                }
            }
            if (n == 0) n = 1;
            drow[dx] = ras_argb((uint8_t)(aa / n), (uint8_t)(ar / n),
                                (uint8_t)(ag / n), (uint8_t)(ab / n));
        }
    }
}
