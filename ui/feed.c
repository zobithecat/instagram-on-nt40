/* ui/feed.c — classic NT4/Win95 gray-bevel Instagram feed, drawn with core/raster.
 * Hardcoded mock posts for milestone 2's "money screenshot". Real data, decoded
 * photos, and text (bitmap font) arrive in later milestones. */
#include "feed.h"
#include "font.h"

/* Classic Win 3D system palette */
#define C_FACE       ras_rgb(192, 192, 192)
#define C_HILIGHT    ras_rgb(255, 255, 255)
#define C_LIGHT      ras_rgb(223, 223, 223)
#define C_SHADOW     ras_rgb(128, 128, 128)
#define C_DARK       ras_rgb(64, 64, 64)
#define C_WHITE      ras_rgb(255, 255, 255)
#define C_INK        ras_rgb(80, 80, 80)
#define C_TEXT       ras_rgb(20, 20, 20)   /* strong body text */
#define C_IG_BLUE    ras_rgb(0, 60, 128)

/* A 2px raised panel (Windows "button edge" look). */
static void panel_raised(Surface *s, Rect r) {
    surface_bevel(s, r, C_HILIGHT, C_DARK);
    Rect inner = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
    surface_bevel(s, inner, C_LIGHT, C_SHADOW);
}

/* A 2px sunken panel (for the photo well / text fields). */
static void panel_sunken(Surface *s, Rect r) {
    surface_bevel(s, r, C_SHADOW, C_HILIGHT);
    Rect inner = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
    surface_bevel(s, inner, C_DARK, C_LIGHT);
}

/* Vertical gradient fill (fake photo) from top color to bottom color. */
static void gradient_v(Surface *s, Rect r, uint32_t top, uint32_t bot) {
    if (r.h <= 0) return;
    for (int y = 0; y < r.h; y++) {
        int t = (r.h == 1) ? 0 : (y * 255) / (r.h - 1);
        uint8_t rr = (uint8_t)((ras_r(top) * (255 - t) + ras_r(bot) * t) / 255);
        uint8_t gg = (uint8_t)((ras_g(top) * (255 - t) + ras_g(bot) * t) / 255);
        uint8_t bb = (uint8_t)((ras_b(top) * (255 - t) + ras_b(bot) * t) / 255);
        surface_hline(s, r.x, r.y + y, r.w, ras_rgb(rr, gg, bb));
    }
}

typedef struct {
    uint32_t grad_top, grad_bot; /* fake-photo colors */
    const char *user, *loc, *likes, *caption, *comments;
    int liked;                   /* heart filled? */
} MockPost;

static const MockPost k_posts[] = {
    { RAS_RGB(255, 180, 90),  RAS_RGB(200, 60, 120),
      "jun.photo",  "Busan, Korea", "1,204 likes",
      "golden hour never gets old", "View all 48 comments", 1 },
    { RAS_RGB(120, 200, 255), RAS_RGB(20, 80, 160),
      "skywatch",   "Jeju Island",  "892 likes",
      "endless blue", "View all 12 comments", 0 },
    { RAS_RGB(160, 220, 160), RAS_RGB(30, 110, 60),
      "greentrail", "Seoraksan",    "2,317 likes",
      "morning hike before the crowds", "View all 73 comments", 1 },
    { RAS_RGB(230, 230, 235), RAS_RGB(120, 120, 140),
      "citypulse",  "Seoul",        "560 likes",
      "concrete jungle at night", "View all 9 comments", 0 },
};
#define N_POSTS ((int)(sizeof(k_posts) / sizeof(k_posts[0])))

/* Draw one feed post starting at top y; returns y after the post. */
static int draw_post(Surface *s, int x, int y, int w, const MockPost *p) {
    int pad = 8;
    Rect card = { x, y, w, 0 };

    int hdr_h   = 40;
    int photo_h = w - 2 * pad;      /* square-ish photo */
    int act_h   = 26;
    int cap_h   = 36;
    card.h = hdr_h + photo_h + act_h + cap_h + pad;

    panel_raised(s, card);

    int cx = x + pad, cw = w - 2 * pad;
    int cy = y + pad;

    /* header: avatar + username + "..." */
    Rect avatar = { cx, cy, 24, 24 };
    panel_sunken(s, avatar);
    gradient_v(s, (Rect){ avatar.x + 2, avatar.y + 2, 20, 20 }, p->grad_top, p->grad_bot);
    font_draw(s, cx + 32, cy + 3, p->user, C_TEXT);          /* username */
    font_draw(s, cx + 32, cy + 13, p->loc, C_SHADOW);        /* location */
    /* three-dot menu */
    for (int i = 0; i < 3; i++)
        surface_fill_rect(s, (Rect){ cx + cw - 4 - i * 6, cy + 10, 3, 3 }, C_INK);

    cy = y + hdr_h;

    /* photo well */
    Rect photo = { cx, cy, cw, photo_h };
    panel_sunken(s, photo);
    gradient_v(s, (Rect){ photo.x + 2, photo.y + 2, photo.w - 4, photo.h - 4 },
               p->grad_top, p->grad_bot);

    cy += photo_h;

    /* action bar: heart / comment / share as small icons */
    int iy = cy + 6;
    uint32_t heart = p->liked ? ras_rgb(220, 30, 60) : C_INK;
    surface_fill_rect(s, (Rect){ cx, iy, 14, 12 }, heart);          /* heart */
    surface_frame(s, (Rect){ cx + 22, iy, 14, 12 }, C_INK);        /* comment */
    surface_frame(s, (Rect){ cx + 44, iy, 16, 12 }, C_INK);        /* share  */
    /* bookmark on the right */
    surface_frame(s, (Rect){ cx + cw - 12, iy, 10, 12 }, C_INK);

    cy += act_h;

    /* caption: likes, "user caption", comments link */
    font_draw(s, cx, cy, p->likes, C_TEXT);
    font_draw(s, cx, cy + 11, p->user, C_TEXT);
    font_draw(s, cx + font_text_width(p->user, 1) + 4, cy + 11, p->caption, C_INK);
    font_draw(s, cx, cy + 22, p->comments, C_SHADOW);

    return y + card.h + 6;
}

void ui_feed_render(Surface *fb, void *user) {
    (void)user;
    surface_fill(fb, C_FACE);

    /* top app bar */
    int bar_h = 30;
    surface_fill_rect(fb, (Rect){ 0, 0, fb->w, bar_h }, C_IG_BLUE);
    surface_hline(fb, 0, bar_h, fb->w, C_DARK);
    /* wordmark */
    font_draw_scaled(fb, 10, 8, "Instagram", C_WHITE, 2);
    /* DM icon top-right */
    surface_frame(fb, (Rect){ fb->w - 26, 8, 16, 14 }, C_WHITE);

    /* posts, left margin */
    int x = 6, w = fb->w - 12;
    int y = bar_h + 6;
    for (int i = 0; i < N_POSTS; i++) {
        if (y > fb->h) break;
        y = draw_post(fb, x, y, w, &k_posts[i]);
    }

    /* bottom nav bar */
    int nav_h = 28;
    Rect nav = { 0, fb->h - nav_h, fb->w, nav_h };
    surface_fill_rect(fb, nav, C_FACE);
    surface_hline(fb, 0, nav.y, fb->w, C_HILIGHT);
    surface_hline(fb, 0, nav.y + 1, fb->w, C_SHADOW);
    int n = 5;
    for (int i = 0; i < n; i++) {
        int ix = (fb->w / n) * i + (fb->w / n) / 2 - 8;
        surface_frame(fb, (Rect){ ix, nav.y + 8, 16, 14 }, i == 0 ? C_IG_BLUE : C_INK);
    }
}
