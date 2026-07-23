/* ui/main.c — GUI entry point for the NT4 Instagram client.
 *
 * Boot sequence: try the real private-API home feed (net/ig_client.h) and
 * download+decode each post's cover photo (JPEG, via img/jpeg.h — video posts
 * show their cover frame only, no motion decode per the project's NT4-486
 * scope). If the fetch fails for any reason (no session, network down,
 * Instagram-side error) fall back to the built-in 4-post demo feed with its
 * bundled QOI sample photos, so the app is never left with a blank screen. */
#include <windows.h>
#include <stdlib.h> /* free — provided by pal/nt4_crt.c */
#include "pal.h"
#include "net.h"
#include "feed.h"
#include "qoi.h"
#include "jpeg.h"
#include "ig_client.h"
#include "https_get.h"

#define MAX_REAL_POSTS 8

static Surface    g_mock_photos[4];
static FeedImages  g_mock_imgs;

static Surface    g_real_photos[MAX_REAL_POSTS];
static FeedPost    g_real_posts[MAX_REAL_POSTS];
static FeedData    g_real_feed;

/* Load the bundled demo photos off the CD (D:\), decode QOI -> Surface. */
static void load_mock_photos(void) {
    static const char *names[4] = {
        "D:\\sunset.qoi", "D:\\ocean.qoi", "D:\\forest.qoi", "D:\\city.qoi"
    };
    for (int i = 0; i < 4; i++) {
        int len = 0;
        void *buf = pal_read_file(names[i], &len);
        if (!buf) { pal_log("photo %d missing: %s", i, names[i]); continue; }
        if (qoi_decode(buf, len, &g_mock_photos[i]) == 0) {
            g_mock_imgs.photos[i] = &g_mock_photos[i];
            pal_log("photo %d: %s %dx%d", i, names[i], g_mock_photos[i].w, g_mock_photos[i].h);
        } else {
            pal_log("photo %d: decode failed %s", i, names[i]);
        }
        free(buf);
    }
}

/* Download `url` over HTTPS and JPEG-decode it into `out`. 0 on success, -1 on
 * any failure (no url, connect/TLS/HTTP error, non-200, bad JPEG). */
static int fetch_photo(const char *url, Surface *out) {
    if (!url) return -1;
    char host[128];
    const char *path;
    if (https_split_url(url, host, sizeof(host), &path) != 0) {
        pal_log("real feed: unparseable image url");
        return -1;
    }
    int status = 0, len = 0;
    void *body = https_request(host, "GET", path, NULL, 0, NULL, 0, &status, &len);
    if (!body) { pal_log("real feed: photo request to %s failed", host); return -1; }
    int rc = -1;
    if (status == 200) {
        rc = jpeg_decode(body, len, out);
        if (rc != 0) pal_log("real feed: jpeg_decode failed for %s (%d bytes)", host, len);
    } else {
        pal_log("real feed: photo HTTP status=%d from %s", status, host);
    }
    free(body);
    return rc;
}

/* Fetch the real home feed and its cover photos into g_real_feed. Returns 1
 * if it's ready to show, 0 to fall back to the demo feed. Deliberately never
 * frees the PrivateFeed on success: g_real_posts borrows its username/caption
 * strings for the rest of the process's lifetime, and there's no refresh/
 * next-page flow yet that would need them released. */
static int load_real_feed(void) {
    PrivateFeed *feed = ig_fetch_timeline();
    if (!feed) { pal_log("real feed: unavailable, showing demo feed instead"); return 0; }
    if (feed->count == 0) {
        pal_log("real feed: empty, showing demo feed instead");
        private_feed_free(feed);
        return 0;
    }

    int n = feed->count;
    if (n > MAX_REAL_POSTS) n = MAX_REAL_POSTS;
    for (int i = 0; i < n; i++) {
        PrivatePost *p = &feed->posts[i];
        g_real_posts[i].username   = p->username ? p->username : "?";
        g_real_posts[i].caption    = p->caption;
        g_real_posts[i].like_count = p->like_count;
        g_real_posts[i].photo      = NULL;
        if (fetch_photo(p->image_url, &g_real_photos[i]) == 0) {
            g_real_posts[i].photo = &g_real_photos[i];
            pal_log("real feed: [%d] @%s photo %dx%d", i, g_real_posts[i].username,
                    g_real_photos[i].w, g_real_photos[i].h);
        } else {
            pal_log("real feed: [%d] @%s photo unavailable, using gradient", i, g_real_posts[i].username);
        }
    }
    g_real_feed.posts = g_real_posts;
    g_real_feed.count = n;
    pal_log("real feed: showing %d posts (of %d fetched)", n, feed->count);
    return 1;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== instagram-on-nt40 boot ===");

    int have_real = 0;
    if (pal_net_init() == 0) {
        have_real = load_real_feed();
    } else {
        pal_log("net init failed, showing demo feed instead");
    }

    int rc;
    if (have_real) {
        rc = pal_run_window("Instagram", 340, 600, ui_feed_chrome_height(),
                            ui_feed_render_real, ui_feed_content_height_real, &g_real_feed);
    } else {
        load_mock_photos();
        rc = pal_run_window("Instagram", 340, 600, ui_feed_chrome_height(),
                            ui_feed_render, ui_feed_content_height, &g_mock_imgs);
    }

    pal_log("=== exit %d ===", rc);
    return rc;
}
