/* core/model_private.c — see model_private.h. */
#include "model_private.h"
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

/* First image_versions2.candidates[0].url under `node`, or NULL. */
static const char *first_image_url(const JsonValue *node) {
    const JsonValue *iv2 = json_get(node, "image_versions2");
    const JsonValue *cands = json_get(iv2, "candidates");
    const JsonValue *first = json_at(cands, 0);
    return json_get_str(first, "url");
}

PrivateFeed *feed_from_private_json(const char *json, int len) {
    JsonValue *root = json_parse(json, len);
    if (!root) return NULL;

    PrivateFeed *f = (PrivateFeed *)calloc(1, sizeof(PrivateFeed));
    if (!f) { json_free(root); return NULL; }

    const JsonValue *items = json_get(root, "feed_items");
    int n = json_count(items);
    if (n > 0) {
        f->posts = (PrivatePost *)calloc((size_t)n, sizeof(PrivatePost));
        if (!f->posts) { json_free(root); free(f); return NULL; }

        for (int i = 0; i < n; i++) {
            const JsonValue *item = json_at(items, i);
            const JsonValue *m = json_get(item, "media_or_ad");
            if (json_type(m) != JSON_OBJ) continue; /* ads-with-no-media, explore
                                                       * injections, etc. — skip */

            PrivatePost *p = &f->posts[f->count++];
            p->username    = dup_str(json_get_str(json_get(m, "user"), "username"));
            p->caption     = dup_str(json_get_str(json_get(m, "caption"), "text"));
            p->like_count  = json_get_int(m, "like_count", 0);
            p->media_type  = (int)json_get_int(m, "media_type", 0);

            const JsonValue *carousel = json_get(m, "carousel_media");
            int nslides = json_count(carousel);
            if (nslides > 0) {
                p->num_slides = nslides;
                p->image_url  = dup_str(first_image_url(json_at(carousel, 0)));
            } else {
                p->image_url  = dup_str(first_image_url(m));
            }
        }
    }

    f->next_max_id = dup_str(json_get_str(root, "next_max_id"));

    json_free(root);
    return f;
}

void private_feed_free(PrivateFeed *f) {
    if (!f) return;
    for (int i = 0; i < f->count; i++) {
        free(f->posts[i].username);
        free(f->posts[i].caption);
        free(f->posts[i].image_url);
    }
    free(f->posts);
    free(f->next_max_id);
    free(f);
}
