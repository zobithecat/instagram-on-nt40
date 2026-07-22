/* core/model_private.h — maps an Instagram *private* mobile-API
 * `feed/timeline/` response into a feed model. This is a different, much
 * richer/messier JSON shape than the official Graph API's `/me/media` (see
 * core/model.h) — deeply nested `feed_items[].media_or_ad`, mixed-in
 * non-post items (ads, explore injections) that must be skipped gracefully,
 * and per-item image lookup that differs for singles vs carousels. Schema
 * confirmed by capturing a real instagrapi response, not guessed. */
#ifndef CORE_MODEL_PRIVATE_H
#define CORE_MODEL_PRIVATE_H

typedef struct {
    char *username;
    char *caption;      /* NULL if the post has no caption */
    long  like_count;
    int   media_type;   /* 1=photo, 2=video, 8=carousel, other=ad/unrecognized */
    char *image_url;    /* cover/first-slide photo URL, or NULL if none found */
    int   num_slides;   /* carousel_media count; 0 for non-carousel posts */
} PrivatePost;

typedef struct {
    PrivatePost *posts;
    int          count;
    char        *next_max_id; /* pagination cursor (response "next_max_id"), or NULL */
} PrivateFeed;

/* Parse a feed/timeline/ response body. Non-post feed_items (ads with no
 * media, explore-tray injections, etc.) are silently skipped rather than
 * causing a failure. Returns NULL only on a JSON syntax error / OOM. */
PrivateFeed *feed_from_private_json(const char *json, int len);
void         private_feed_free(PrivateFeed *f);

#endif /* CORE_MODEL_PRIVATE_H */
