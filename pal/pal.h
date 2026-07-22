/* pal/pal.h — Platform Abstraction Layer (thin Win32 wrapper).
 *
 * Keeps NT4-specific goo (window, DIB-section double buffer, COM1 logging) in
 * one place so core/ and ui/ stay OS-independent where possible. The compositor
 * hands ui/ a top-down 32bpp Surface that aliases the DIB section's pixels, so
 * ui/ draws with core/raster and pal blits the result to the window.
 */
#ifndef PAL_H
#define PAL_H

#include "raster.h" /* Surface — resolved via -Icore */

/* Called every frame with the framebuffer Surface aliasing the DIB section.
 * `user` is passed through from pal_run_window. Draw with core/raster. */
typedef void (*pal_render_fn)(Surface *fb, void *user);

/* Create the main window, run the message loop, return the exit code.
 * The window starts at w x h client pixels and recomposites on resize/paint. */
int pal_run_window(const char *title, int w, int h, pal_render_fn render, void *user);

/* Append a line to COM1 (QEMU -serial file:debug.log) for the Claude debug loop.
 * No-op-safe if COM1 can't be opened. printf-style. */
void pal_log(const char *fmt, ...);

/* Read an entire file into a freshly malloc'd buffer. On success returns the
 * buffer and sets *len; caller frees with free(). Returns NULL on any error. */
void *pal_read_file(const char *path, int *len);

#endif /* PAL_H */
