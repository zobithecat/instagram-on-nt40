/* tools/mkqoi.c — host tool: draw synthetic "photos" with core/raster and
 * encode them to .qoi. The NT4 client then loads + decodes these, proving the
 * whole image pipeline (load -> QOI decode -> downscale -> blit) end to end
 * without embedding a JPEG decoder. Real photos can replace these later. */
#include "../core/raster.h"
#include "../img/qoi.h"
#include <stdio.h>
#include <stdlib.h>

#define DIM 256

static void vgrad(Surface *s, int x0, int y0, int w, int h, uint32_t top, uint32_t bot) {
    for (int y = 0; y < h; y++) {
        int t = (h <= 1) ? 0 : (y * 255) / (h - 1);
        uint8_t r = (uint8_t)((ras_r(top) * (255 - t) + ras_r(bot) * t) / 255);
        uint8_t g = (uint8_t)((ras_g(top) * (255 - t) + ras_g(bot) * t) / 255);
        uint8_t b = (uint8_t)((ras_b(top) * (255 - t) + ras_b(bot) * t) / 255);
        surface_hline(s, x0, y0 + y, w, ras_rgb(r, g, b));
    }
}

static void disc(Surface *s, int cx, int cy, int rad, uint32_t c) {
    for (int y = -rad; y <= rad; y++)
        for (int x = -rad; x <= rad; x++)
            if (x * x + y * y <= rad * rad)
                surface_fill_rect(s, (Rect){ cx + x, cy + y, 1, 1 }, c);
}

/* a downward-pointing peak filled from its silhouette to the bottom */
static void peak(Surface *s, int px, int base_y, int half_w, int height, uint32_t c) {
    for (int dx = -half_w; dx <= half_w; dx++) {
        int col = px + dx;
        int top = base_y - height + (abs(dx) * height) / half_w;
        surface_fill_rect(s, (Rect){ col, top, 1, base_y - top }, c);
    }
}

static void write_ppm(const Surface *s, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint32_t p = s->pixels[(size_t)y * s->stride + x];
            unsigned char rgb[3] = { ras_r(p), ras_g(p), ras_b(p) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void save(Surface *s, const char *path) {
    int len = 0;
    unsigned char *q = qoi_encode(s, &len);
    if (!q) { fprintf(stderr, "encode failed %s\n", path); return; }
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(q, 1, (size_t)len, f); fclose(f); }
    /* decode it right back to validate + emit a .ppm preview alongside */
    Surface d;
    int ok = qoi_decode(q, len, &d);
    char ppm[512];
    snprintf(ppm, sizeof(ppm), "%s.ppm", path);
    if (ok == 0) { write_ppm(&d, ppm); surface_free(&d); }
    fprintf(stderr, "wrote %s (%d bytes, decode %s)\n", path, len, ok == 0 ? "ok" : "FAIL");
    qoi_free(q);
}

static void scene_sunset(Surface *s) {
    vgrad(s, 0, 0, DIM, 160, ras_rgb(255, 170, 70), ras_rgb(220, 70, 120));
    vgrad(s, 0, 160, DIM, DIM - 160, ras_rgb(180, 55, 110), ras_rgb(60, 25, 60));
    disc(s, 128, 120, 34, ras_rgb(255, 240, 180));
    disc(s, 128, 120, 44, ras_argb(70, 255, 230, 160)); /* soft glow (blended) */
    /* city silhouette on the horizon */
    int hy = 190;
    for (int i = 0; i < DIM; i += 22) {
        int bh = 18 + ((i * 37) % 40);
        surface_fill_rect(s, (Rect){ i + 2, hy - bh, 16, bh + (DIM - hy) }, ras_rgb(25, 15, 35));
    }
}

static void scene_ocean(Surface *s) {
    vgrad(s, 0, 0, DIM, 150, ras_rgb(150, 205, 255), ras_rgb(70, 140, 225));
    vgrad(s, 0, 150, DIM, DIM - 150, ras_rgb(40, 120, 195), ras_rgb(15, 60, 120));
    disc(s, 190, 60, 20, ras_rgb(255, 250, 220));
    /* sun glint on the water */
    for (int y = 150; y < DIM; y += 3)
        surface_fill_rect(s, (Rect){ 188 - (y - 150) / 6, y, 6 + (y - 150) / 4, 1 },
                          ras_argb(120, 255, 250, 220));
}

static void scene_forest(Surface *s) {
    vgrad(s, 0, 0, DIM, DIM, ras_rgb(200, 225, 210), ras_rgb(120, 175, 140));
    /* layered ridgelines */
    peak(s, 70,  DIM, 90, 150, ras_rgb(80, 130, 95));
    peak(s, 180, DIM, 100, 180, ras_rgb(55, 105, 75));
    peak(s, 128, DIM, 130, 210, ras_rgb(35, 80, 55));
    disc(s, 205, 45, 16, ras_rgb(255, 250, 235));
}

static void scene_city(Surface *s) {
    vgrad(s, 0, 0, DIM, DIM, ras_rgb(25, 25, 55), ras_rgb(80, 45, 90));
    disc(s, 210, 40, 12, ras_rgb(240, 240, 210)); /* moon */
    for (int i = 0; i < DIM; i += 30) {
        int bh = 70 + ((i * 53) % 110);
        int bx = i + 4, bw = 24;
        surface_fill_rect(s, (Rect){ bx, DIM - bh, bw, bh }, ras_rgb(15, 15, 30));
        for (int wy = DIM - bh + 6; wy < DIM - 6; wy += 12)
            for (int wx = bx + 4; wx < bx + bw - 4; wx += 8)
                if (((wx * 7 + wy * 3) % 5) < 3)
                    surface_fill_rect(s, (Rect){ wx, wy, 4, 6 }, ras_rgb(255, 220, 120));
    }
}

int main(void) {
    struct { const char *name; void (*fn)(Surface *); } scenes[] = {
        { "assets/photos/sunset.qoi", scene_sunset },
        { "assets/photos/ocean.qoi",  scene_ocean },
        { "assets/photos/forest.qoi", scene_forest },
        { "assets/photos/city.qoi",   scene_city },
    };
    for (int i = 0; i < 4; i++) {
        Surface s;
        if (surface_alloc(&s, DIM, DIM) != 0) return 1;
        surface_fill(&s, ras_rgb(0, 0, 0));
        scenes[i].fn(&s);
        save(&s, scenes[i].name);
        surface_free(&s);
    }
    return 0;
}
