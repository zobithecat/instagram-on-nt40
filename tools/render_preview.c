/* tools/render_preview.c — host-side preview of the feed render.
 *
 * ui/feed.c draws with pure core/raster (no Win32), so we can render the exact
 * same framebuffer the NT4 app produces, offscreen on the Mac, and dump it to a
 * PPM. Lets us iterate on the "gray bevel feed" look without the QEMU loop.
 *
 *   build/preview <out.ppm> [w] [h]
 */
#include "../core/raster.h"
#include "../img/qoi.h"
#include "../ui/feed.h"
#include <stdio.h>
#include <stdlib.h>

/* Load a .qoi file from disk into `out`. Returns 0 on success. */
static int load_qoi(const char *path, Surface *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return -1; }
    fclose(f);
    int rc = qoi_decode(buf, (int)len, out);
    free(buf);
    return rc;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "feed.ppm";
    int w = argc > 2 ? atoi(argv[2]) : 340;
    int h = argc > 3 ? atoi(argv[3]) : 600;

    /* load the sample photos (same files that ship on the NT4 CD) */
    static const char *paths[] = {
        "assets/photos/sunset.qoi", "assets/photos/ocean.qoi",
        "assets/photos/forest.qoi", "assets/photos/city.qoi",
    };
    static Surface photos[4];
    FeedImages imgs = { { 0 } };
    for (int i = 0; i < 4; i++)
        if (load_qoi(paths[i], &photos[i]) == 0) imgs.photos[i] = &photos[i];

    Surface fb;
    if (surface_alloc(&fb, w, h) != 0) { fprintf(stderr, "alloc failed\n"); return 1; }
    ui_feed_render(&fb, &imgs);

    FILE *f = fopen(out, "wb");
    if (!f) { perror("fopen"); surface_free(&fb); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t p = fb.pixels[(size_t)y * fb.stride + x];
            unsigned char rgb[3] = { ras_r(p), ras_g(p), ras_b(p) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    surface_free(&fb);
    fprintf(stderr, "wrote %s (%dx%d)\n", out, w, h);
    return 0;
}
