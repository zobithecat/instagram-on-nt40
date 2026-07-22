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

/* Draw the feed: a fixed top app bar, the scrollable posts region offset by
 * `scroll_y` (clamped internally), and a fixed bottom nav bar. `user` is a
 * FeedImages* or NULL (all-gradient). pal_render_fn-compatible. */
void ui_feed_render(Surface *fb, int scroll_y, void *user);

/* Total pixel height of the scrollable posts content at the given window width
 * (excludes the fixed app/nav bars). pal_height_fn-compatible; `user` unused. */
int ui_feed_content_height(int width, void *user);

/* Combined height of the fixed, non-scrolling chrome (top app bar + bottom nav);
 * pal subtracts this from the client height to size the scroll viewport. */
int ui_feed_chrome_height(void);

#endif /* UI_FEED_H */
