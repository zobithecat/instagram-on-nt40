/* core/raster.h — OS-independent DIB pixel engine.
 *
 * Pixels are uint32_t in 0xAARRGGBB (host order). On a little-endian machine
 * (x86 NT4 target AND the arm64 macOS test host are both little-endian) the
 * in-memory byte order is B,G,R,A, which is exactly what a Win32 32bpp
 * BI_RGB DIB section expects. So the UI layer can blit a Surface straight to
 * an HDC with no per-pixel conversion, while this file stays OS-independent
 * and is unit-tested natively on the Mac with ASan/UBSan.
 */
#ifndef CORE_RASTER_H
#define CORE_RASTER_H

#include <stdint.h>
#include <stddef.h> /* NULL, size_t (freestanding-safe) */

typedef struct {
    uint32_t *pixels; /* row-major, top-down, `stride` uint32_t per row */
    int w, h;         /* logical dimensions in pixels */
    int stride;       /* pixels per row; >= w (lets a Surface view a sub-rect) */
    int owns;         /* 1 if pixels was malloc'd by surface_alloc */
} Surface;

typedef struct { int x, y, w, h; } Rect;

/* ---- pixel helpers ---- */
/* Compile-time constant variants (usable in static initializers). */
#define RAS_ARGB(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define RAS_RGB(r, g, b) RAS_ARGB(0xFF, (r), (g), (b))

static inline uint32_t ras_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static inline uint32_t ras_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ras_argb(0xFF, r, g, b);
}
static inline uint8_t ras_a(uint32_t p) { return (uint8_t)(p >> 24); }
static inline uint8_t ras_r(uint32_t p) { return (uint8_t)(p >> 16); }
static inline uint8_t ras_g(uint32_t p) { return (uint8_t)(p >> 8); }
static inline uint8_t ras_b(uint32_t p) { return (uint8_t)(p); }

/* ---- lifecycle ---- */
/* Allocate a top-down Surface with its own pixel buffer (stride == w).
 * Returns 0 on success, -1 on allocation failure or bad dimensions. */
int  surface_alloc(Surface *s, int w, int h);
void surface_free(Surface *s);
/* Wrap an existing pixel buffer (e.g. a GDI DIB section) without owning it. */
void surface_wrap(Surface *s, uint32_t *pixels, int w, int h, int stride);

/* ---- drawing (all clip to the surface; out-of-range rects are safe) ---- */
void surface_fill(Surface *s, uint32_t color);                 /* whole surface */
void surface_fill_rect(Surface *s, Rect r, uint32_t color);    /* opaque */
void surface_blend_rect(Surface *s, Rect r, uint32_t color);   /* src-over, uses color's alpha */
void surface_hline(Surface *s, int x, int y, int w, uint32_t color);
void surface_vline(Surface *s, int x, int y, int h, uint32_t color);
void surface_frame(Surface *s, Rect r, uint32_t color);        /* 1px outline */

/* Classic Win32 "raised bevel": light top/left, dark bottom/right. */
void surface_bevel(Surface *s, Rect r, uint32_t light, uint32_t dark);

/* ---- blitting ---- */
/* Opaque copy of src (or src's `srcrect`, NULL = all of src) to (dx,dy). Clipped. */
void surface_blit(Surface *dst, int dx, int dy, const Surface *src, const Rect *srcrect);
/* Per-pixel straight-alpha src-over blit of all of src to (dx,dy). Clipped. */
void surface_blit_alpha(Surface *dst, int dx, int dy, const Surface *src);

/* Area-average downscale of src into dst (dst smaller-or-equal). Both must be
 * allocated. Straight RGBA averaging — good for opaque photos. */
void surface_downscale(Surface *dst, const Surface *src);

#endif /* CORE_RASTER_H */
