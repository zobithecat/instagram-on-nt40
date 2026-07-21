/* ui/feed.h — renders the mock Instagram feed into a framebuffer Surface.
 * Pure core/raster drawing (no GDI), so it can be dumped to BMP and diffed
 * off-VM too. Text is added later via a bitmap font in core/. */
#ifndef UI_FEED_H
#define UI_FEED_H

#include "raster.h"

/* pal_render_fn-compatible: draws the whole feed. `user` unused for now. */
void ui_feed_render(Surface *fb, void *user);

#endif /* UI_FEED_H */
