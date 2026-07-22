/* img/jpeg.h — JPEG decode (real downloaded photos + video cover frames),
 * via a vendored stb_image (third_party/stb, public domain/MIT, gitignored).
 * Same API shape as img/qoi.h so callers don't care which codec produced a
 * Surface. */
#ifndef IMG_JPEG_H
#define IMG_JPEG_H

#include "raster.h"

/* Decode a JPEG buffer into `out` (allocates out->pixels; caller surface_free's).
 * Returns 0 on success, -1 on bad/unsupported data or allocation failure. */
int jpeg_decode(const void *data, int len, Surface *out);

#endif /* IMG_JPEG_H */
