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

/* Called every frame with the framebuffer Surface aliasing the DIB section and
 * the current vertical scroll offset. `user` is passed through. Draw with
 * core/raster. */
typedef void (*pal_render_fn)(Surface *fb, int scroll_y, void *user);

/* Returns the total scrollable content height (px) for a client width, so pal
 * can size the scrollbar and clamp scrolling. `user` is passed through. */
typedef int (*pal_height_fn)(int width, void *user);

/* Called on a left mouse click, with the RAW window-client (x_window,
 * y_window) -- pal has no idea how the UI layer splits its chrome into top/
 * bottom bars, so it doesn't attempt any content-space translation itself;
 * that's left entirely to the callback (see ui_feed_click_real). `scroll_y`
 * is pal's current scroll offset and `width` the client width, passed
 * through for the same reason `pal_render_fn`/`pal_height_fn` get them.
 * Return 1 if something changed such that content_height should be
 * re-queried and the window repainted (e.g. a card expanded/collapsed), 0
 * if the click didn't hit anything interactive. */
typedef int (*pal_click_fn)(int x_window, int y_window, int scroll_y, int width, void *user);

/* Create the main window (with a vertical scrollbar), run the message loop,
 * return the exit code. Recomposites on resize/paint/scroll. `content_height`
 * may be NULL for a non-scrolling window. `chrome_h` is the fixed, non-scrolling
 * UI height (top+bottom bars) subtracted from the client to size the viewport.
 * `click` may be NULL if the render target has no interactive elements. */
int pal_run_window(const char *title, int w, int h, int chrome_h,
                   pal_render_fn render, pal_height_fn content_height,
                   pal_click_fn click, void *user);

/* Append a line to COM1 (QEMU -serial file:debug.log) for the Claude debug loop.
 * No-op-safe if COM1 can't be opened. printf-style. */
void pal_log(const char *fmt, ...);

/* Read an entire file into a freshly malloc'd buffer. On success returns the
 * buffer and sets *len; caller frees with free(). Returns NULL on any error. */
void *pal_read_file(const char *path, int *len);

#endif /* PAL_H */
