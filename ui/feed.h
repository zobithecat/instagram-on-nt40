/* ui/feed.h — renders the mock Instagram feed into a framebuffer Surface.
 * Pure core/raster drawing (no GDI), so it renders identically in the host
 * preview and on NT4. Decoded post photos are passed in via FeedImages. */
#ifndef UI_FEED_H
#define UI_FEED_H

#include "raster.h"

/* Decoded photos for the feed, one per post; a NULL slot falls back to the
 * built-in gradient. Passed to ui_feed_render as `user` (may itself be NULL). */
typedef struct {
    const Surface *photos[8];
} FeedImages;

/* pal_render_fn-compatible: draws the whole feed. `user` is a FeedImages* or
 * NULL (all-gradient). */
void ui_feed_render(Surface *fb, void *user);

#endif /* UI_FEED_H */
