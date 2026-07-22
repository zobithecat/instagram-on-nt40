/* core/model.c — see model.h. Maps a Graph API media response to a Feed. */
#include "model.h"
#include "json.h"
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

Feed *feed_from_graph_json(const char *json, int len) {
    JsonValue *root = json_parse(json, len);
    if (!root) return NULL;

    Feed *f = (Feed *)calloc(1, sizeof(Feed));
    if (!f) { json_free(root); return NULL; }

    const JsonValue *data = json_get(root, "data");
    int n = json_count(data);
    if (n > 0) {
        f->posts = (Post *)calloc((size_t)n, sizeof(Post));
        if (!f->posts) { json_free(root); free(f); return NULL; }
        for (int i = 0; i < n; i++) {
            const JsonValue *o = json_at(data, i);
            if (json_type(o) != JSON_OBJ) continue;
            Post *p = &f->posts[f->count++];
            p->id         = dup_str(json_get_str(o, "id"));
            p->username   = dup_str(json_get_str(o, "username"));
            p->caption    = dup_str(json_get_str(o, "caption"));
            p->media_type = dup_str(json_get_str(o, "media_type"));
            /* videos expose only thumbnail_url; fall back to it */
            p->media_url  = dup_str(json_get_str(o, "media_url"));
            if (!p->media_url) p->media_url = dup_str(json_get_str(o, "thumbnail_url"));
            p->permalink  = dup_str(json_get_str(o, "permalink"));
            p->timestamp  = dup_str(json_get_str(o, "timestamp"));
            p->like_count = json_get_int(o, "like_count", 0);
        }
    }

    /* paging.cursors.after (for the next page) */
    const JsonValue *cursors = json_get(json_get(root, "paging"), "cursors");
    f->next_cursor = dup_str(json_get_str(cursors, "after"));

    json_free(root);
    return f;
}

void feed_free(Feed *f) {
    if (!f) return;
    for (int i = 0; i < f->count; i++) {
        Post *p = &f->posts[i];
        free(p->id); free(p->username); free(p->caption);
        free(p->media_url); free(p->media_type);
        free(p->permalink); free(p->timestamp);
    }
    free(f->posts);
    free(f->next_cursor);
    free(f);
}
