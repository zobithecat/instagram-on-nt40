/* img/qoi.h — from-scratch QOI (Quite OK Image) codec, OS-independent.
 *
 * QOI is a dead-simple lossless format (qoiformat.org): a 14-byte header then
 * byte-oriented chunks (index / diff / luma / run / raw RGB(A)). Decoding needs
 * no floating point and no libc beyond malloc/mem*, so it drops straight into
 * the freestanding NT4 build — unlike a JPEG library. Pixels use our Surface
 * format (0xAARRGGBB); a real JPEG decoder can come later behind the same API. */
#ifndef IMG_QOI_H
#define IMG_QOI_H

#include "raster.h"

/* Decode a QOI buffer into `out` (allocates out->pixels; caller surface_free's).
 * Returns 0 on success, -1 on bad magic / bad dims / alloc failure. */
int qoi_decode(const void *data, int len, Surface *out);

/* Encode `s` to a newly malloc'd QOI buffer (RGBA). Caller frees via qoi_free.
 * *out_len receives the byte length. Returns NULL on failure. (Host tooling;
 * the NT4 client only decodes.) */
unsigned char *qoi_encode(const Surface *s, int *out_len);
void qoi_free(void *p);

#endif /* IMG_QOI_H */
