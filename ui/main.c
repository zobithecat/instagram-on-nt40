/* ui/main.c — GUI entry point for the NT4 Instagram client. */
#include <windows.h>
#include "pal.h"
#include "feed.h"

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== instagram-on-nt40 boot ===");
    /* mobile-ish portrait window; client area 340x600 */
    int rc = pal_run_window("Instagram", 340, 600, ui_feed_render, NULL);
    pal_log("=== exit %d ===", rc);
    return rc;
}
