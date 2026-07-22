/* img/jpeg.c — see jpeg.h. Configures the vendored stb_image for a JPEG-only,
 * freestanding-friendly build:
 *   - STBI_ONLY_JPEG + STBI_NO_LINEAR: strips every other format AND the HDR/
 *     gamma-linear code paths, which is what pulls in <math.h> (ldexp/pow) —
 *     with both defined, stb_image needs no libm at all (confirmed by reading
 *     its source: the #include <math.h> itself is guarded by
 *     `!STBI_NO_LINEAR || !STBI_NO_HDR`, and STBI_ONLY_JPEG already implies
 *     STBI_NO_HDR).
 *   - STBI_NO_STDIO: we only ever decode from an in-memory buffer (the HTTPS
 *     response body), never a FILE*.
 *   - STBI_MALLOC/REALLOC/FREE default to plain malloc/realloc/free, which
 *     pal/nt4_crt.c already provides under those exact names — no override
 *     needed.
 *   - STBI_ASSERT -> no-op (no assert.h in the freestanding build).
 */
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_ASSERT(x) ((void)0)
/* Our NT4 app is single-threaded (one message loop, no threads at all), so
 * stb_image's __thread-qualified error-message storage is unneeded -- and on
 * our freestanding/-nostdlib build there's no emulated-TLS runtime to back
 * it (mingw's __emutls_get_address needs more of the CRT than we link). */
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
/* Vendored code isn't held to our own -Wall -Wextra -Werror bar (both clang
 * and gcc honor these GCC-style diagnostic pragmas). */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "../third_party/stb/stb_image.h"
#pragma GCC diagnostic pop

#include "jpeg.h"

int jpeg_decode(const void *data, int len, Surface *out) {
    int w = 0, h = 0, channels = 0;
    /* force 4 channels (RGBA) regardless of the source's channel count, so
     * the repack loop below is uniform for grayscale/RGB/CMYK-ish JPEGs too */
    unsigned char *pixels = stbi_load_from_memory((const unsigned char *)data, len,
                                                  &w, &h, &channels, 4);
    if (!pixels) return -1;

    if (surface_alloc(out, w, h) != 0) { stbi_image_free(pixels); return -1; }

    /* stb_image's output is row-major interleaved R,G,B,A bytes (top-down,
     * matching Surface); repack into our packed 0xAARRGGBB uint32. */
    const unsigned char *src = pixels;
    for (int y = 0; y < h; y++) {
        uint32_t *row = out->pixels + (size_t)y * out->stride;
        for (int x = 0; x < w; x++) {
            row[x] = ras_argb(src[3], src[0], src[1], src[2]);
            src += 4;
        }
    }
    stbi_image_free(pixels);
    return 0;
}
