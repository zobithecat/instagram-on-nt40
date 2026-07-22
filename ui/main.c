/* ui/main.c — GUI entry point for the NT4 Instagram client. */
#include <windows.h>
#include <stdlib.h> /* free — provided by pal/nt4_crt.c */
#include "pal.h"
#include "feed.h"
#include "qoi.h"

static Surface    g_photos[4];
static FeedImages g_imgs;

/* Load the sample post photos off the CD (D:\), decode QOI -> Surface. */
static void load_photos(void) {
    static const char *names[4] = {
        "D:\\sunset.qoi", "D:\\ocean.qoi", "D:\\forest.qoi", "D:\\city.qoi"
    };
    for (int i = 0; i < 4; i++) {
        int len = 0;
        void *buf = pal_read_file(names[i], &len);
        if (!buf) { pal_log("photo %d missing: %s", i, names[i]); continue; }
        if (qoi_decode(buf, len, &g_photos[i]) == 0) {
            g_imgs.photos[i] = &g_photos[i];
            pal_log("photo %d: %s %dx%d", i, names[i], g_photos[i].w, g_photos[i].h);
        } else {
            pal_log("photo %d: decode failed %s", i, names[i]);
        }
        free(buf);
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== instagram-on-nt40 boot ===");
    load_photos();
    int rc = pal_run_window("Instagram", 340, 600, ui_feed_chrome_height(),
                            ui_feed_render, ui_feed_content_height, &g_imgs);
    pal_log("=== exit %d ===", rc);
    return rc;
}
