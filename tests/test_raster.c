/* tests/test_raster.c — native (Mac clang + ASan/UBSan) unit tests for core/raster. */
#include "../core/raster.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    g_checks++; \
    uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
    if (_a != _b) { g_fail++; \
        fprintf(stderr, "FAIL %s:%d: %s == %s (0x%08X != 0x%08X)\n", \
            __FILE__, __LINE__, #a, #b, _a, _b); } \
} while (0)

static uint32_t at(const Surface *s, int x, int y) {
    return s->pixels[(size_t)y * s->stride + x];
}

static void test_alloc_free(void) {
    Surface s;
    CHECK(surface_alloc(&s, 4, 3) == 0);
    CHECK(s.w == 4 && s.h == 3 && s.stride == 4 && s.owns == 1);
    /* calloc-zeroed */
    CHECK_EQ(at(&s, 0, 0), 0);
    CHECK_EQ(at(&s, 3, 2), 0);
    surface_free(&s);
    CHECK(s.pixels == NULL);
    /* bad dims rejected */
    CHECK(surface_alloc(&s, 0, 5) == -1);
    CHECK(surface_alloc(&s, -1, 5) == -1);
}

static void test_fill_and_rect(void) {
    Surface s;
    surface_alloc(&s, 5, 5);
    surface_fill(&s, ras_rgb(10, 20, 30));
    CHECK_EQ(at(&s, 2, 2), ras_rgb(10, 20, 30));

    /* rect fully inside */
    surface_fill_rect(&s, (Rect){1, 1, 2, 2}, ras_rgb(200, 0, 0));
    CHECK_EQ(at(&s, 1, 1), ras_rgb(200, 0, 0));
    CHECK_EQ(at(&s, 2, 2), ras_rgb(200, 0, 0));
    CHECK_EQ(at(&s, 0, 0), ras_rgb(10, 20, 30)); /* untouched */
    CHECK_EQ(at(&s, 3, 3), ras_rgb(10, 20, 30));
    surface_free(&s);
}

static void test_clipping(void) {
    Surface s;
    surface_alloc(&s, 4, 4);
    surface_fill(&s, 0);
    /* rect straddling the top-left corner (negative origin) */
    surface_fill_rect(&s, (Rect){-2, -2, 4, 4}, ras_rgb(1, 2, 3));
    CHECK_EQ(at(&s, 0, 0), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&s, 1, 1), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&s, 2, 2), 0); /* outside the clipped region */
    /* rect entirely off-surface is a no-op, must not crash (ASan catches OOB) */
    surface_fill_rect(&s, (Rect){100, 100, 10, 10}, ras_rgb(9, 9, 9));
    surface_fill_rect(&s, (Rect){-50, -50, 10, 10}, ras_rgb(9, 9, 9));
    CHECK_EQ(at(&s, 3, 3), 0);
    surface_free(&s);
}

static void test_bevel(void) {
    Surface s;
    surface_alloc(&s, 6, 6);
    surface_fill(&s, ras_rgb(128, 128, 128));
    uint32_t light = ras_rgb(255, 255, 255), dark = ras_rgb(64, 64, 64);
    surface_bevel(&s, (Rect){0, 0, 6, 6}, light, dark);
    CHECK_EQ(at(&s, 0, 0), light); /* top-left corner is light */
    CHECK_EQ(at(&s, 3, 0), light); /* top edge */
    CHECK_EQ(at(&s, 0, 3), light); /* left edge */
    CHECK_EQ(at(&s, 5, 5), dark);  /* bottom-right corner */
    CHECK_EQ(at(&s, 3, 5), dark);  /* bottom edge */
    CHECK_EQ(at(&s, 5, 3), dark);  /* right edge */
    surface_free(&s);
}

static void test_blend(void) {
    Surface s;
    surface_alloc(&s, 2, 1);
    surface_fill(&s, ras_rgb(0, 0, 0)); /* black opaque bg */
    /* 50% white over black -> ~128 grey */
    surface_blend_rect(&s, (Rect){0, 0, 1, 1}, ras_argb(128, 255, 255, 255));
    uint32_t p = at(&s, 0, 0);
    CHECK(ras_r(p) >= 127 && ras_r(p) <= 129);
    CHECK(ras_a(p) == 255);
    /* fully transparent is a no-op */
    surface_fill_rect(&s, (Rect){1, 0, 1, 1}, ras_rgb(10, 20, 30));
    surface_blend_rect(&s, (Rect){1, 0, 1, 1}, ras_argb(0, 255, 0, 0));
    CHECK_EQ(at(&s, 1, 0), ras_rgb(10, 20, 30));
    /* fully opaque overwrites exactly */
    surface_blend_rect(&s, (Rect){0, 0, 1, 1}, ras_argb(255, 7, 8, 9));
    CHECK_EQ(at(&s, 0, 0), ras_rgb(7, 8, 9));
    surface_free(&s);
}

static void test_blit(void) {
    Surface dst, src;
    surface_alloc(&dst, 5, 5);
    surface_alloc(&src, 2, 2);
    surface_fill(&dst, 0);
    surface_fill(&src, ras_rgb(1, 2, 3));
    /* opaque blit inside */
    surface_blit(&dst, 1, 1, &src, NULL);
    CHECK_EQ(at(&dst, 1, 1), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&dst, 2, 2), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&dst, 0, 0), 0);
    CHECK_EQ(at(&dst, 3, 3), 0);
    /* blit partially off the bottom-right edge — clipped, no OOB */
    surface_blit(&dst, 4, 4, &src, NULL);
    CHECK_EQ(at(&dst, 4, 4), ras_rgb(1, 2, 3));
    /* blit partially off the top-left (negative dest) */
    surface_fill(&dst, 0);
    surface_blit(&dst, -1, -1, &src, NULL);
    CHECK_EQ(at(&dst, 0, 0), ras_rgb(1, 2, 3));
    surface_free(&dst);
    surface_free(&src);
}

static void test_blit_alpha(void) {
    Surface dst, src;
    surface_alloc(&dst, 3, 3);
    surface_alloc(&src, 3, 3);
    surface_fill(&dst, ras_rgb(0, 0, 0));
    surface_fill(&src, ras_argb(128, 255, 255, 255));
    surface_blit_alpha(&dst, 0, 0, &src);
    uint32_t p = at(&dst, 1, 1);
    CHECK(ras_r(p) >= 127 && ras_r(p) <= 129);
    surface_free(&dst);
    surface_free(&src);
}

static void test_downscale(void) {
    Surface dst, src;
    surface_alloc(&src, 4, 4);
    surface_alloc(&dst, 2, 2);
    /* left half red, right half blue -> each dst column averages one color */
    surface_fill(&src, ras_rgb(200, 0, 0));
    surface_fill_rect(&src, (Rect){2, 0, 2, 4}, ras_rgb(0, 0, 200));
    surface_downscale(&dst, &src);
    CHECK_EQ(at(&dst, 0, 0), ras_rgb(200, 0, 0));
    CHECK_EQ(at(&dst, 1, 0), ras_rgb(0, 0, 200));
    /* 2x2 uniform -> 1x1 exact average */
    Surface s1, s2;
    surface_alloc(&s2, 2, 2);
    surface_alloc(&s1, 1, 1);
    surface_fill(&s2, ras_rgb(100, 100, 100));
    surface_downscale(&s1, &s2);
    CHECK_EQ(at(&s1, 0, 0), ras_rgb(100, 100, 100));
    surface_free(&dst); surface_free(&src); surface_free(&s1); surface_free(&s2);
}

int main(void) {
    test_alloc_free();
    test_fill_and_rect();
    test_clipping();
    test_bevel();
    test_blend();
    test_blit();
    test_blit_alpha();
    test_downscale();
    printf("raster tests: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
