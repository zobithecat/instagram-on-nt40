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

/* Shared per-card view, filled in by either the mock feed (render_posts) or
 * the real feed (render_posts_real) and drawn by the one draw_post. `loc` and
 * `comments` may be NULL to omit that line (the real feed doesn't have a
 * location or a comment count parsed out of the private-API response). */
typedef struct {
    uint32_t grad_top, grad_bot; /* fallback-gradient colors (used if photo == NULL) */
    const char *user, *loc, *likes, *caption, *comments;
    int liked;                   /* heart filled? */
    const Surface *photo;        /* decoded cover image, or NULL for the gradient */
} PostView;

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

/* layout constants */
#define BAR_H     30   /* fixed top app bar */
#define NAV_H     28   /* fixed bottom nav bar */
#define POST_PAD  8
#define POST_GAP  6    /* vertical gap between cards (and above the first) */

/* Height of one post card at content width w (matches draw_post's card.h). */
static int post_card_height(int w) {
    int photo_h = w - 2 * POST_PAD;
    return 40 /*hdr*/ + photo_h + 26 /*act*/ + 36 /*cap*/ + POST_PAD;
}

/* Format "N likes" (no thousands separators -- keeps this a plain digit loop,
 * no snprintf needed in the freestanding build) into buf (cap bytes). */
static void format_likes(char *buf, int cap, long n) {
    char digits[24];
    int nd = 0;
    if (n <= 0) {
        digits[nd++] = '0';
    } else {
        while (n > 0 && nd < (int)sizeof(digits)) { digits[nd++] = (char)('0' + (n % 10)); n /= 10; }
    }
    int pos = 0;
    for (int i = nd - 1; i >= 0 && pos < cap - 1; i--) buf[pos++] = digits[i];
    static const char suffix[] = " likes";
    for (int i = 0; suffix[i] && pos < cap - 1; i++) buf[pos++] = suffix[i];
    buf[pos] = 0;
}

/* Derive a stable placeholder gradient from a username, so posts without a
 * decoded photo (fetch/decode failed, or no image_url) still look distinct
 * rather than all-identical. Not cryptographic -- just a visual seed. */
static void gradient_from_name(const char *name, uint32_t *top, uint32_t *bot) {
    unsigned h = 5381;
    for (const unsigned char *p = (const unsigned char *)name; p && *p; p++) h = h * 33 + *p;
    uint8_t r = (uint8_t)(80 + (h & 0x7F));
    uint8_t g = (uint8_t)(80 + ((h >> 7) & 0x7F));
    uint8_t b = (uint8_t)(80 + ((h >> 14) & 0x7F));
    *top = ras_rgb(r, g, b);
    *bot = ras_rgb((uint8_t)(r / 2), (uint8_t)(g / 2), (uint8_t)(b / 2));
}

/* Draw one feed post starting at top y; returns y after the post. `p->loc`
 * and `p->comments` may be NULL to omit that line (real-feed posts don't
 * have them). `p->photo` (or NULL) is the decoded post image; NULL uses the
 * gradient. */
static int draw_post(Surface *s, int x, int y, int w, const PostView *p) {
    int pad = POST_PAD;
    Rect card = { x, y, w, post_card_height(w) };

    int hdr_h   = 40;
    int photo_h = w - 2 * pad;      /* square-ish photo */
    int act_h   = 26;

    panel_raised(s, card);

    int cx = x + pad, cw = w - 2 * pad;
    int cy = y + pad;

    /* header: avatar + username + "..." */
    Rect avatar = { cx, cy, 24, 24 };
    panel_sunken(s, avatar);
    gradient_v(s, (Rect){ avatar.x + 2, avatar.y + 2, 20, 20 }, p->grad_top, p->grad_bot);
    font_draw(s, cx + 32, cy + 3, p->user, C_TEXT);          /* username */
    if (p->loc) font_draw(s, cx + 32, cy + 13, p->loc, C_SHADOW); /* location */
    /* three-dot menu */
    for (int i = 0; i < 3; i++)
        surface_fill_rect(s, (Rect){ cx + cw - 4 - i * 6, cy + 10, 3, 3 }, C_INK);

    cy = y + hdr_h;

    /* photo well: decoded photo (downscaled to fit) or gradient fallback */
    Rect well = { cx, cy, cw, photo_h };
    panel_sunken(s, well);
    if (p->photo && p->photo->w > 0 && p->photo->h > 0) {
        Surface tmp;
        if (surface_alloc(&tmp, well.w - 4, well.h - 4) == 0) {
            surface_downscale(&tmp, p->photo);
            surface_blit(s, well.x + 2, well.y + 2, &tmp, NULL); /* clips to fb */
            surface_free(&tmp);
        }
    } else {
        gradient_v(s, (Rect){ well.x + 2, well.y + 2, well.w - 4, well.h - 4 },
                   p->grad_top, p->grad_bot);
    }

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

    /* caption: likes, "user caption", comments link. `likes`/`comments` are
     * always our own ASCII (formatted "N likes" or a hardcoded mock string)
     * so they keep the tight FONT_LINE pitch; the caption itself is real,
     * externally-sourced text that may contain an 18px-tall Hangul glyph
     * (see core/font.h's FONT_WIDE_LINE), so it gets a full wide-line's
     * worth of clearance both above and below regardless of what it
     * actually contains -- otherwise a Hangul caption visually overlaps the
     * likes/comments lines instead of just taking up its reserved space. */
    int caption_y = cy + FONT_LINE + 1;
    font_draw(s, cx, cy, p->likes, C_TEXT);
    font_draw(s, cx, caption_y, p->user, C_TEXT);
    if (p->caption)
        font_draw(s, cx + font_text_width(p->user, 1) + 4, caption_y, p->caption, C_INK);
    if (p->comments) font_draw(s, cx, caption_y + FONT_WIDE_LINE, p->comments, C_SHADOW);

    return y + card.h + 6;
}

int ui_feed_content_height(int width, void *user) {
    (void)user;
    int w = width - 12; /* content column width (6px margin each side) */
    return POST_GAP + N_POSTS * (post_card_height(w) + POST_GAP);
}

int ui_feed_content_height_real(int width, void *user) {
    const FeedData *data = (const FeedData *)user;
    int count = data ? data->count : 0;
    int w = width - 12;
    return POST_GAP + count * (post_card_height(w) + POST_GAP);
}

int ui_feed_chrome_height(void) { return BAR_H + NAV_H; }

/* fixed top app bar + bottom nav bar, shared by both render paths */
static void draw_chrome(Surface *fb) {
    surface_fill_rect(fb, (Rect){ 0, 0, fb->w, BAR_H }, C_IG_BLUE);
    surface_hline(fb, 0, BAR_H, fb->w, C_DARK);
    font_draw_scaled(fb, 10, 8, "Instagram", C_WHITE, 2);
    surface_frame(fb, (Rect){ fb->w - 26, 8, 16, 14 }, C_WHITE); /* DM icon */

    Rect nav = { 0, fb->h - NAV_H, fb->w, NAV_H };
    surface_fill_rect(fb, nav, C_FACE);
    surface_hline(fb, 0, nav.y, fb->w, C_HILIGHT);
    surface_hline(fb, 0, nav.y + 1, fb->w, C_SHADOW);
    int n = 5;
    for (int i = 0; i < n; i++) {
        int ix = (fb->w / n) * i + (fb->w / n) / 2 - 8;
        surface_frame(fb, (Rect){ ix, nav.y + 8, 16, 14 }, i == 0 ? C_IG_BLUE : C_INK);
    }
}

/* Fill `fb` with C_FACE, render `content_h` px of scrollable content (via
 * `fill_content`) clipped/scrolled into the viewport between the fixed bars,
 * then draw the chrome over it. Shared scroll-clamp/blit logic for both the
 * mock and real render paths. */
static void draw_scrollable(Surface *fb, int scroll_y, int content_h,
                            void (*fill_content)(Surface *, const void *), const void *ctx) {
    surface_fill(fb, C_FACE);

    int view_h = fb->h - BAR_H - NAV_H;
    if (view_h > 0) {
        int maxscroll = content_h - view_h;
        if (maxscroll < 0) maxscroll = 0;
        if (scroll_y < 0) scroll_y = 0;
        if (scroll_y > maxscroll) scroll_y = maxscroll;

        Surface content;
        if (surface_alloc(&content, fb->w, content_h > 0 ? content_h : 1) == 0) {
            fill_content(&content, ctx);
            Rect src = { 0, scroll_y, fb->w, view_h };
            surface_blit(fb, 0, BAR_H, &content, &src); /* clips to the viewport */
            surface_free(&content);
        }
    }

    draw_chrome(fb);
}

static void fill_mock_posts(Surface *content, const void *ctx) {
    const FeedImages *imgs = (const FeedImages *)ctx;
    surface_fill(content, C_FACE);
    int x = 6, w = content->w - 12, y = POST_GAP;
    for (int i = 0; i < N_POSTS; i++) {
        const MockPost *mp = &k_posts[i];
        PostView pv = {
            mp->grad_top, mp->grad_bot, mp->user, mp->loc, mp->likes,
            mp->caption, mp->comments, mp->liked,
            (imgs && i < 8) ? imgs->photos[i] : NULL,
        };
        y = draw_post(content, x, y, w, &pv);
    }
}

void ui_feed_render(Surface *fb, int scroll_y, void *user) {
    draw_scrollable(fb, scroll_y, ui_feed_content_height(fb->w, NULL), fill_mock_posts, user);
}

static void fill_real_posts(Surface *content, const void *ctx) {
    const FeedData *data = (const FeedData *)ctx;
    surface_fill(content, C_FACE);
    int x = 6, w = content->w - 12, y = POST_GAP;
    int count = data ? data->count : 0;
    for (int i = 0; i < count; i++) {
        const FeedPost *fp = &data->posts[i];
        uint32_t top, bot;
        gradient_from_name(fp->username, &top, &bot);
        char likes_buf[24];
        format_likes(likes_buf, sizeof(likes_buf), fp->like_count);
        PostView pv = {
            top, bot, fp->username, NULL, likes_buf,
            fp->caption, NULL, 0, fp->photo,
        };
        y = draw_post(content, x, y, w, &pv);
    }
}

void ui_feed_render_real(Surface *fb, int scroll_y, void *user) {
    draw_scrollable(fb, scroll_y, ui_feed_content_height_real(fb->w, user), fill_real_posts, user);
}
