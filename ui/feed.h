/* ui/feed.h — renders the Instagram feed into a framebuffer Surface. Pure
 * core/raster drawing (no GDI), so it renders identically in the host preview
 * and on NT4. Two data sources share the same card layout (draw_post in
 * feed.c):
 *   - the built-in 4-post mock/demo feed (ui_feed_render + FeedImages), used
 *     by `make preview` and as the offline/no-session fallback;
 *   - a real fetched-and-decoded feed (ui_feed_render_real + FeedData), used
 *     by ui/main.c once net/ig_client.h's timeline fetch succeeds. */
#ifndef UI_FEED_H
#define UI_FEED_H

#include "raster.h"

/* Decoded photos for the mock feed, one per post; a NULL slot falls back to
 * the built-in gradient. Passed to ui_feed_render as `user` (may itself be
 * NULL for an all-gradient render). */
typedef struct {
    const Surface *photos[8];
} FeedImages;

/* Draw the built-in mock feed: a fixed top app bar, the scrollable posts
 * region offset by `scroll_y` (clamped internally), and a fixed bottom nav
 * bar. `user` is a FeedImages* or NULL (all-gradient). pal_render_fn-compatible. */
void ui_feed_render(Surface *fb, int scroll_y, void *user);

/* Total pixel height of the built-in mock feed's scrollable content at the
 * given window width (excludes the fixed app/nav bars). pal_height_fn-
 * compatible; `user` unused. */
int ui_feed_content_height(int width, void *user);

/* One real, already-fetched post (see net/ig_client.h + img/jpeg.h). Text
 * fields are borrowed pointers that must outlive the render call. `photo`
 * NULL falls back to a placeholder gradient derived from the username (no
 * image_url, or the download/decode failed for this post — cover-frame-only
 * for videos, no motion decode). */
typedef struct {
    const char    *username;
    const char    *caption;    /* may be NULL: no caption */
    long           like_count;
    const Surface *photo;
} FeedPost;

/* A real feed: an array of FeedPost the caller owns (see net/ig_client.h). */
typedef struct {
    const FeedPost *posts;
    int             count;
} FeedData;

/* Same chrome/layout as ui_feed_render, but drawing real posts. `user` must
 * be a non-NULL `const FeedData*`. pal_render_fn-compatible. */
void ui_feed_render_real(Surface *fb, int scroll_y, void *user);

/* Real-feed counterpart to ui_feed_content_height; `user` must be a non-NULL
 * `const FeedData*`. pal_height_fn-compatible. */
int ui_feed_content_height_real(int width, void *user);

/* Combined height of the fixed, non-scrolling chrome (top app bar + bottom nav);
 * pal subtracts this from the client height to size the scroll viewport. Same
 * chrome for both the mock and real render paths. */
int ui_feed_chrome_height(void);

#endif /* UI_FEED_H */
