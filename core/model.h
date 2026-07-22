/* core/model.h — the feed data model + mapping from an Instagram Graph API
 * media response into it. OS-independent; owns its own copies of the strings so
 * the parsed JSON tree can be freed. This is what makes the feed data-driven
 * instead of hardcoded. */
#ifndef CORE_MODEL_H
#define CORE_MODEL_H

typedef struct {
    char *id;
    char *username;
    char *caption;
    char *media_url;   /* full-size photo URL (or thumbnail_url for video) */
    char *media_type;  /* IMAGE / VIDEO / CAROUSEL_ALBUM */
    char *permalink;
    char *timestamp;
    long  like_count;  /* 0 if the response doesn't include it */
} Post;

typedef struct {
    Post *posts;
    int   count;
    char *next_cursor; /* paging.cursors.after, or NULL — for pagination */
} Feed;

/* Parse a Graph API `/me/media` (or similar) JSON response into a Feed.
 * Returns NULL on parse/alloc failure. Caller feed_free()s the result. */
Feed *feed_from_graph_json(const char *json, int len);
void  feed_free(Feed *f);

#endif /* CORE_MODEL_H */
