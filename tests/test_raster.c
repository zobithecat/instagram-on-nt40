/* tests/test_raster.c — native (Mac clang + ASan/UBSan) unit tests for core/raster. */
#include "../core/raster.h"
#include "../core/font.h"
#include "../core/json.h"
#include "../core/http.h"
#include "../core/model.h"
#include "../core/model_private.h"
#include "../img/qoi.h"
#include "../img/jpeg.h"
#include "../ui/feed.h"
#include "fixtures/jpeg_test_images.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    g_checks++; \
    uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
    if (_a != _b) { g_fail++; \
        fprintf(stderr, "FAIL %s:%d: %s == %s (0x%08X != 0x%08X)\n", \
            __FILE__, __LINE__, #a, #b, _a, _b); } \
} while (0)

static uint32_t at(const Surface *s, int x, int y) {
    return s->pixels[(size_t)y * s->stride + x];
}

static void test_alloc_free(void) {
    Surface s;
    CHECK(surface_alloc(&s, 4, 3) == 0);
    CHECK(s.w == 4 && s.h == 3 && s.stride == 4 && s.owns == 1);
    /* calloc-zeroed */
    CHECK_EQ(at(&s, 0, 0), 0);
    CHECK_EQ(at(&s, 3, 2), 0);
    surface_free(&s);
    CHECK(s.pixels == NULL);
    /* bad dims rejected */
    CHECK(surface_alloc(&s, 0, 5) == -1);
    CHECK(surface_alloc(&s, -1, 5) == -1);
}

static void test_fill_and_rect(void) {
    Surface s;
    surface_alloc(&s, 5, 5);
    surface_fill(&s, ras_rgb(10, 20, 30));
    CHECK_EQ(at(&s, 2, 2), ras_rgb(10, 20, 30));

    /* rect fully inside */
    surface_fill_rect(&s, (Rect){1, 1, 2, 2}, ras_rgb(200, 0, 0));
    CHECK_EQ(at(&s, 1, 1), ras_rgb(200, 0, 0));
    CHECK_EQ(at(&s, 2, 2), ras_rgb(200, 0, 0));
    CHECK_EQ(at(&s, 0, 0), ras_rgb(10, 20, 30)); /* untouched */
    CHECK_EQ(at(&s, 3, 3), ras_rgb(10, 20, 30));
    surface_free(&s);
}

static void test_clipping(void) {
    Surface s;
    surface_alloc(&s, 4, 4);
    surface_fill(&s, 0);
    /* rect straddling the top-left corner (negative origin) */
    surface_fill_rect(&s, (Rect){-2, -2, 4, 4}, ras_rgb(1, 2, 3));
    CHECK_EQ(at(&s, 0, 0), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&s, 1, 1), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&s, 2, 2), 0); /* outside the clipped region */
    /* rect entirely off-surface is a no-op, must not crash (ASan catches OOB) */
    surface_fill_rect(&s, (Rect){100, 100, 10, 10}, ras_rgb(9, 9, 9));
    surface_fill_rect(&s, (Rect){-50, -50, 10, 10}, ras_rgb(9, 9, 9));
    CHECK_EQ(at(&s, 3, 3), 0);
    surface_free(&s);
}

static void test_bevel(void) {
    Surface s;
    surface_alloc(&s, 6, 6);
    surface_fill(&s, ras_rgb(128, 128, 128));
    uint32_t light = ras_rgb(255, 255, 255), dark = ras_rgb(64, 64, 64);
    surface_bevel(&s, (Rect){0, 0, 6, 6}, light, dark);
    CHECK_EQ(at(&s, 0, 0), light); /* top-left corner is light */
    CHECK_EQ(at(&s, 3, 0), light); /* top edge */
    CHECK_EQ(at(&s, 0, 3), light); /* left edge */
    CHECK_EQ(at(&s, 5, 5), dark);  /* bottom-right corner */
    CHECK_EQ(at(&s, 3, 5), dark);  /* bottom edge */
    CHECK_EQ(at(&s, 5, 3), dark);  /* right edge */
    surface_free(&s);
}

static void test_blend(void) {
    Surface s;
    surface_alloc(&s, 2, 1);
    surface_fill(&s, ras_rgb(0, 0, 0)); /* black opaque bg */
    /* 50% white over black -> ~128 grey */
    surface_blend_rect(&s, (Rect){0, 0, 1, 1}, ras_argb(128, 255, 255, 255));
    uint32_t p = at(&s, 0, 0);
    CHECK(ras_r(p) >= 127 && ras_r(p) <= 129);
    CHECK(ras_a(p) == 255);
    /* fully transparent is a no-op */
    surface_fill_rect(&s, (Rect){1, 0, 1, 1}, ras_rgb(10, 20, 30));
    surface_blend_rect(&s, (Rect){1, 0, 1, 1}, ras_argb(0, 255, 0, 0));
    CHECK_EQ(at(&s, 1, 0), ras_rgb(10, 20, 30));
    /* fully opaque overwrites exactly */
    surface_blend_rect(&s, (Rect){0, 0, 1, 1}, ras_argb(255, 7, 8, 9));
    CHECK_EQ(at(&s, 0, 0), ras_rgb(7, 8, 9));
    surface_free(&s);
}

static void test_blit(void) {
    Surface dst, src;
    surface_alloc(&dst, 5, 5);
    surface_alloc(&src, 2, 2);
    surface_fill(&dst, 0);
    surface_fill(&src, ras_rgb(1, 2, 3));
    /* opaque blit inside */
    surface_blit(&dst, 1, 1, &src, NULL);
    CHECK_EQ(at(&dst, 1, 1), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&dst, 2, 2), ras_rgb(1, 2, 3));
    CHECK_EQ(at(&dst, 0, 0), 0);
    CHECK_EQ(at(&dst, 3, 3), 0);
    /* blit partially off the bottom-right edge — clipped, no OOB */
    surface_blit(&dst, 4, 4, &src, NULL);
    CHECK_EQ(at(&dst, 4, 4), ras_rgb(1, 2, 3));
    /* blit partially off the top-left (negative dest) */
    surface_fill(&dst, 0);
    surface_blit(&dst, -1, -1, &src, NULL);
    CHECK_EQ(at(&dst, 0, 0), ras_rgb(1, 2, 3));
    surface_free(&dst);
    surface_free(&src);
}

static void test_blit_alpha(void) {
    Surface dst, src;
    surface_alloc(&dst, 3, 3);
    surface_alloc(&src, 3, 3);
    surface_fill(&dst, ras_rgb(0, 0, 0));
    surface_fill(&src, ras_argb(128, 255, 255, 255));
    surface_blit_alpha(&dst, 0, 0, &src);
    uint32_t p = at(&dst, 1, 1);
    CHECK(ras_r(p) >= 127 && ras_r(p) <= 129);
    surface_free(&dst);
    surface_free(&src);
}

static void test_downscale(void) {
    Surface dst, src;
    surface_alloc(&src, 4, 4);
    surface_alloc(&dst, 2, 2);
    /* left half red, right half blue -> each dst column averages one color */
    surface_fill(&src, ras_rgb(200, 0, 0));
    surface_fill_rect(&src, (Rect){2, 0, 2, 4}, ras_rgb(0, 0, 200));
    surface_downscale(&dst, &src);
    CHECK_EQ(at(&dst, 0, 0), ras_rgb(200, 0, 0));
    CHECK_EQ(at(&dst, 1, 0), ras_rgb(0, 0, 200));
    /* 2x2 uniform -> 1x1 exact average */
    Surface s1, s2;
    surface_alloc(&s2, 2, 2);
    surface_alloc(&s1, 1, 1);
    surface_fill(&s2, ras_rgb(100, 100, 100));
    surface_downscale(&s1, &s2);
    CHECK_EQ(at(&s1, 0, 0), ras_rgb(100, 100, 100));
    surface_free(&dst); surface_free(&src); surface_free(&s1); surface_free(&s2);
}

static void test_font(void) {
    /* width math: n glyphs -> (n-1)*ADV + W */
    CHECK(font_text_width("", 1) == 0);
    CHECK(font_text_width("A", 1) == FONT_W);
    CHECK(font_text_width("AB", 1) == FONT_ADV + FONT_W);
    CHECK(font_text_width("A", 2) == FONT_W * 2);

    Surface s;
    surface_alloc(&s, 12, 10);
    surface_fill(&s, 0); /* transparent bg so untouched pixels read back as 0 */
    /* '_' is a single filled bottom row (row 6, all 5 cols) */
    font_draw(&s, 0, 0, "_", ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 0, 6), ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 4, 6), ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 2, 0), 0); /* top row untouched */
    CHECK_EQ(at(&s, 5, 6), 0); /* past glyph width */

    /* '|' is column 2 for rows 0..6 */
    surface_fill(&s, 0);
    font_draw(&s, 0, 0, "|", ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 2, 0), ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 2, 6), ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 0, 0), 0);

    /* space draws nothing; out-of-range chars don't crash (ASan guards) */
    surface_fill(&s, 0);
    font_draw(&s, 0, 0, " ", ras_rgb(255, 255, 255));
    CHECK_EQ(at(&s, 2, 3), 0);
    font_draw(&s, 8, 3, "Xy!\x01\x80", ras_rgb(255, 255, 255)); /* clips at edge, skips bad */
    surface_free(&s);
}

static void test_qoi(void) {
    Surface s;
    surface_alloc(&s, 17, 13);
    /* varied pattern: per-pixel gradient with changing alpha exercises
     * RGB/RGBA/DIFF/LUMA/INDEX paths; a filled block exercises RUN. */
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            s.pixels[y * s.stride + x] =
                ras_argb((uint8_t)((x + y) * 9), (uint8_t)(x * 15), (uint8_t)(y * 20), (uint8_t)(x * 3 + 40));
    surface_fill_rect(&s, (Rect){ 2, 2, 8, 5 }, ras_argb(200, 10, 20, 30)); /* a run */

    int len = 0;
    unsigned char *enc = qoi_encode(&s, &len);
    CHECK(enc != NULL);
    CHECK(len > 14 + 8);
    CHECK(enc[0] == 'q' && enc[1] == 'o' && enc[2] == 'i' && enc[3] == 'f');

    Surface d;
    CHECK(qoi_decode(enc, len, &d) == 0);
    CHECK(d.w == s.w && d.h == s.h);
    int mism = 0;
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            if (s.pixels[y * s.stride + x] != d.pixels[y * d.stride + x]) mism++;
    CHECK(mism == 0); /* lossless round-trip */

    /* rejects: bad magic, truncated */
    Surface bad;
    unsigned char junk[32] = { 0 };
    CHECK(qoi_decode(junk, (int)sizeof(junk), &bad) == -1);
    CHECK(qoi_decode(enc, 5, &bad) == -1);

    qoi_free(enc);
    surface_free(&s);
    surface_free(&d);
}

static void test_json(void) {
    /* object: strings, ints, bool, null */
    const char *s1 = "{\"id\":\"1789\",\"count\":42,\"ok\":true,\"x\":null}";
    JsonValue *r = json_parse(s1, (int)strlen(s1));
    CHECK(r != NULL);
    CHECK(json_type(r) == JSON_OBJ);
    CHECK(json_count(r) == 4);
    CHECK(strcmp(json_get_str(r, "id"), "1789") == 0);
    CHECK(json_get_int(r, "count", -1) == 42);
    CHECK(json_bool(json_get(r, "ok"), 0) == 1);
    CHECK(json_type(json_get(r, "x")) == JSON_NULL);
    CHECK(json_get(r, "missing") == NULL);
    json_free(r);

    /* Graph-API-shaped: { data: [ {id, caption}, ... ] } */
    const char *s2 = "{ \"data\": [ {\"id\":\"a\",\"caption\":\"hi\"}, {\"id\":\"b\"} ], "
                     "\"paging\":{\"cursors\":{\"after\":\"XYZ\"}} }";
    JsonValue *g = json_parse(s2, (int)strlen(s2));
    CHECK(g != NULL);
    const JsonValue *data = json_get(g, "data");
    CHECK(json_type(data) == JSON_ARR);
    CHECK(json_count(data) == 2);
    CHECK(strcmp(json_get_str(json_at(data, 0), "id"), "a") == 0);
    CHECK(strcmp(json_get_str(json_at(data, 0), "caption"), "hi") == 0);
    CHECK(json_get_str(json_at(data, 1), "caption") == NULL); /* absent */
    CHECK(strcmp(json_get_str(json_get(json_get(g, "paging"), "cursors"), "after"), "XYZ") == 0);
    json_free(g);

    /* escapes + \u -> UTF-8, negative + fractional numbers */
    const char *s3 = "[\"a\\nb\\t\\\"\\u00e9\\uD83D\\uDE00\", -12, 3.5]";
    JsonValue *a = json_parse(s3, (int)strlen(s3));
    CHECK(a != NULL && json_count(a) == 3);
    int sl = 0; const char *str = json_string(json_at(a, 0), &sl);
    CHECK(str != NULL);
    CHECK(str[0] == 'a' && str[1] == '\n' && str[2] == 'b' && str[3] == '\t' && str[4] == '"');
    CHECK((unsigned char)str[5] == 0xC3 && (unsigned char)str[6] == 0xA9); /* é */
    CHECK((unsigned char)str[7] == 0xF0);                                  /* 😀 4-byte */
    CHECK(json_int(json_at(a, 1), 0) == -12);
    CHECK(json_int(json_at(a, 2), 0) == 3); /* truncates fraction */
    json_free(a);

    /* error cases return NULL (and don't leak — ASan) */
    CHECK(json_parse("{bad}", 5) == NULL);
    CHECK(json_parse("[1,2", 4) == NULL);       /* unterminated */
    CHECK(json_parse("{\"a\":1} junk", 12) == NULL); /* trailing garbage */
    CHECK(json_parse("", 0) == NULL);
    CHECK(json_parse("\"unterminated", 13) == NULL);
}

static void test_http(void) {
    /* request builder */
    HttpHeader h[] = { { "Accept", "application/json" }, { "Connection", "close" } };
    int rl = 0;
    char *req = http_build_request("GET", "graph.instagram.com",
                                   "/me/media?fields=id", h, 2, NULL, 0, &rl);
    CHECK(req != NULL);
    const char *want =
        "GET /me/media?fields=id HTTP/1.1\r\n"
        "Host: graph.instagram.com\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n\r\n";
    CHECK(rl == (int)strlen(want));
    CHECK(memcmp(req, want, (size_t)rl) == 0);
    free(req);

    /* POST with body adds Content-Length */
    const char *bd = "x=1";
    char *preq = http_build_request("POST", "h", "/p", NULL, 0, bd, 3, &rl);
    CHECK(preq && strstr(preq, "Content-Length: 3\r\n") != NULL);
    CHECK(memcmp(preq + rl - 3, "x=1", 3) == 0);
    free(preq);

    /* Content-Length response */
    const char *r1 =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n\r\n"
        "{\"ok\":true}\r\n";
    HttpResponse resp;
    CHECK(http_parse_response(r1, (int)strlen(r1), &resp) == 0);
    CHECK(resp.status == 200);
    CHECK(strcmp(resp.reason, "OK") == 0);
    CHECK(resp.body_len == 13);
    CHECK(strcmp(http_header(&resp, "content-type"), "application/json") == 0); /* ci */
    CHECK(strcmp(http_header(&resp, "CONTENT-LENGTH"), "13") == 0);
    CHECK(http_header(&resp, "x-missing") == NULL);
    CHECK(memcmp(resp.body, "{\"ok\":true}\r\n", 13) == 0);
    http_response_free(&resp);

    /* chunked response decodes to a contiguous body */
    const char *r2 =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n";
    HttpResponse c;
    CHECK(http_parse_response(r2, (int)strlen(r2), &c) == 0);
    CHECK(c.body_len == 11);
    CHECK(memcmp(c.body, "hello world", 11) == 0);
    http_response_free(&c);

    /* malformed */
    HttpResponse bad;
    CHECK(http_parse_response("nonsense", 8, &bad) == -1);
}

static void test_model(void) {
    /* a realistic Graph API /me/media response */
    const char *j =
        "{\"data\":["
        "{\"id\":\"178\",\"username\":\"jun\",\"caption\":\"golden hour\","
        "\"media_type\":\"IMAGE\",\"media_url\":\"https://cdn/1.jpg\","
        "\"permalink\":\"https://ig/p/1\",\"timestamp\":\"2024-06-01T10:00:00+0000\"},"
        "{\"id\":\"179\",\"username\":\"sky\",\"media_type\":\"VIDEO\","
        "\"thumbnail_url\":\"https://cdn/2.jpg\"}"
        "],\"paging\":{\"cursors\":{\"after\":\"CURSOR2\"}}}";
    Feed *f = feed_from_graph_json(j, (int)strlen(j));
    CHECK(f != NULL);
    CHECK(f->count == 2);
    CHECK(strcmp(f->posts[0].id, "178") == 0);
    CHECK(strcmp(f->posts[0].username, "jun") == 0);
    CHECK(strcmp(f->posts[0].caption, "golden hour") == 0);
    CHECK(strcmp(f->posts[0].media_url, "https://cdn/1.jpg") == 0);
    CHECK(f->posts[0].caption != NULL);
    /* video: media_url absent -> falls back to thumbnail_url */
    CHECK(strcmp(f->posts[1].media_url, "https://cdn/2.jpg") == 0);
    CHECK(f->posts[1].caption == NULL); /* absent -> NULL */
    CHECK(strcmp(f->next_cursor, "CURSOR2") == 0);
    feed_free(f);

    /* empty data + no paging */
    Feed *e = feed_from_graph_json("{\"data\":[]}", 11);
    CHECK(e != NULL && e->count == 0 && e->next_cursor == NULL);
    feed_free(e);

    /* malformed JSON -> NULL */
    CHECK(feed_from_graph_json("{bad", 4) == NULL);
}

static void test_model_private(void) {
    /* Structurally mirrors a REAL captured instagrapi feed/timeline/ response
     * (schema confirmed empirically, not guessed) but with synthetic content —
     * we don't commit real third-party usernames/captions to the test suite.
     * Covers: single photo, carousel (album), video (cover-only), an ad item
     * with no renderable media, and a non-post item lacking media_or_ad
     * entirely (e.g. an explore-tray injection) that must be skipped. */
    const char *j =
        "{\"feed_items\":["
        /* [0] plain photo */
        "{\"media_or_ad\":{\"user\":{\"username\":\"jun.photo\"},"
        "\"caption\":{\"text\":\"golden hour\"},\"like_count\":1204,"
        "\"media_type\":1,\"image_versions2\":{\"candidates\":["
        "{\"url\":\"https://cdn/sunset.jpg\",\"width\":1080,\"height\":1350}]}}},"
        /* [1] carousel/album: 2 slides, each with its own image_versions2 */
        "{\"media_or_ad\":{\"user\":{\"username\":\"skywatch\"},"
        "\"caption\":null,\"like_count\":50,\"media_type\":8,"
        "\"image_versions2\":{\"candidates\":[{\"url\":\"https://cdn/cover.jpg\"}]},"
        "\"carousel_media\":["
        "{\"media_type\":1,\"image_versions2\":{\"candidates\":[{\"url\":\"https://cdn/slide1.jpg\"}]}},"
        "{\"media_type\":1,\"image_versions2\":{\"candidates\":[{\"url\":\"https://cdn/slide2.jpg\"}]}}"
        "]}},"
        /* [2] video: media_type=2, has a cover image but no carousel */
        "{\"media_or_ad\":{\"user\":{\"username\":\"greentrail\"},"
        "\"caption\":{\"text\":\"hike\"},\"like_count\":9,\"media_type\":2,"
        "\"image_versions2\":{\"candidates\":[{\"url\":\"https://cdn/vidcover.jpg\"}]}}},"
        /* [3] ad with no renderable media at all -- image_url must end up NULL */
        "{\"media_or_ad\":{\"user\":{\"username\":\"citypulse\"},"
        "\"product_type\":\"ad\",\"like_count\":0,\"media_type\":10}},"
        /* [4] non-post injection (no media_or_ad key) -- must be skipped entirely */
        "{\"explore_story\":{\"whatever\":1}}"
        "],\"next_max_id\":\"CURSOR123\"}";

    PrivateFeed *f = feed_from_private_json(j, (int)strlen(j));
    CHECK(f != NULL);
    CHECK(f->count == 4); /* the explore_story item is skipped */
    CHECK(strcmp(f->next_max_id, "CURSOR123") == 0);

    CHECK(strcmp(f->posts[0].username, "jun.photo") == 0);
    CHECK(strcmp(f->posts[0].caption, "golden hour") == 0);
    CHECK(f->posts[0].like_count == 1204);
    CHECK(f->posts[0].media_type == 1);
    CHECK(strcmp(f->posts[0].image_url, "https://cdn/sunset.jpg") == 0);
    CHECK(f->posts[0].num_slides == 0);

    CHECK(f->posts[1].caption == NULL); /* null caption -> NULL, not "null" */
    CHECK(f->posts[1].num_slides == 2);
    CHECK(strcmp(f->posts[1].image_url, "https://cdn/slide1.jpg") == 0); /* first SLIDE, not cover */

    CHECK(f->posts[2].media_type == 2);
    CHECK(strcmp(f->posts[2].image_url, "https://cdn/vidcover.jpg") == 0);

    CHECK(f->posts[3].image_url == NULL); /* no image_versions2 at all */
    CHECK(f->posts[3].media_type == 10);

    private_feed_free(f);

    /* malformed JSON -> NULL; empty feed_items -> count 0, no crash */
    CHECK(feed_from_private_json("{bad", 4) == NULL);
    PrivateFeed *empty = feed_from_private_json("{\"feed_items\":[]}", 17);
    CHECK(empty != NULL && empty->count == 0 && empty->next_max_id == NULL);
    private_feed_free(empty);
}

/* JPEG is lossy, so we allow a small per-channel tolerance rather than
 * demanding an exact match -- a solid-color block should still decode very
 * close to the source since it has a zero AC / pure-DC DCT representation. */
static int close_enough(uint8_t a, uint8_t b, int tol) {
    int d = (int)a - (int)b;
    if (d < 0) d = -d;
    return d <= tol;
}

static void test_jpeg(void) {
    Surface s;
    CHECK(jpeg_decode(k_test_jpeg_red8, (int)k_test_jpeg_red8_len, &s) == 0);
    CHECK(s.w == 8 && s.h == 8);
    uint32_t p = at(&s, 3, 3);
    CHECK(ras_a(p) == 255); /* JPEG has no alpha; decoder must report opaque */
    CHECK(close_enough(ras_r(p), 200, 12));
    CHECK(close_enough(ras_g(p), 40, 12));
    CHECK(close_enough(ras_b(p), 40, 12));
    surface_free(&s);

    /* two-tone image: left half red, right half blue */
    Surface sp;
    CHECK(jpeg_decode(k_test_jpeg_split, (int)k_test_jpeg_split_len, &sp) == 0);
    CHECK(sp.w == 16 && sp.h == 8);
    uint32_t left = at(&sp, 2, 4), right = at(&sp, 13, 4);
    CHECK(close_enough(ras_r(left), 200, 20) && close_enough(ras_b(left), 40, 20));
    CHECK(close_enough(ras_r(right), 30, 20) && close_enough(ras_b(right), 200, 20));
    surface_free(&sp);

    /* malformed input -> -1, not a crash (ASan would catch OOB) */
    Surface bad;
    unsigned char junk[16] = { 0 };
    CHECK(jpeg_decode(junk, (int)sizeof(junk), &bad) == -1);
}

/* ui_feed_render_real / ui_feed_content_height_real: real-feed rendering
 * path (distinct from the built-in mock feed exercised implicitly by
 * `make preview`). Exercise both a populated feed (with and without a decoded
 * photo, and a NULL caption -- real posts can lack one) and an empty one, and
 * check it never touches out-of-bounds pixels (ASan would catch that). */
static void test_feed_real(void) {
    Surface photo;
    CHECK(surface_alloc(&photo, 32, 32) == 0);
    surface_fill(&photo, ras_rgb(10, 20, 30));

    FeedPost posts[2] = {
        { "realuser", "hello from the real feed", 1234, &photo },
        { "otheruser", NULL, 0, NULL }, /* no caption, no photo -> gradient fallback */
    };
    FeedData data = { posts, 2 };

    CHECK(ui_feed_content_height_real(340, &data) > 0);

    Surface fb;
    CHECK(surface_alloc(&fb, 340, 600) == 0);
    ui_feed_render_real(&fb, 0, &data);
    /* top app bar should be the Instagram-blue fill, not left at 0 */
    CHECK(at(&fb, 5, 5) != 0);
    surface_free(&fb);
    surface_free(&photo);

    /* empty feed: must not crash, content height still accounts for padding */
    FeedData empty = { NULL, 0 };
    CHECK(ui_feed_content_height_real(340, &empty) >= 0);
    Surface fb2;
    CHECK(surface_alloc(&fb2, 340, 600) == 0);
    ui_feed_render_real(&fb2, 0, &empty);
    surface_free(&fb2);
}

int main(void) {
    test_alloc_free();
    test_fill_and_rect();
    test_clipping();
    test_bevel();
    test_blend();
    test_blit();
    test_blit_alpha();
    test_downscale();
    test_font();
    test_qoi();
    test_json();
    test_http();
    test_model();
    test_model_private();
    test_jpeg();
    test_feed_real();
    printf("core tests: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
