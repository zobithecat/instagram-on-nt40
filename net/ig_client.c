/* net/ig_client.c — see ig_client.h. Thin wrapper: https_request (generic TLS
 * POST) + ig_build_headers/ig_build_timeline_body (request assembly, from
 * net/ig_private.h) + feed_from_private_json (core/model_private.h). Same
 * pipeline net/ig_feed_main.c proved out headlessly on real NT4, refactored
 * onto the shared https_request helper so the GUI app doesn't duplicate the
 * TLS boilerplate. */
#include "ig_client.h"
#include "ig_private.h"
#include "https_get.h"
#include "pal.h"
#include "../vm/ig_session_private.h" /* IG_PRIVATE_HOST/PORT/PATH -- gitignored, real session */

#include <stdlib.h> /* free -- provided by pal/nt4_crt.c */

PrivateFeed *ig_fetch_timeline(void) {
    HttpHeader headers[IG_STATIC_HEADERS_COUNT + IG_FRESH_HEADERS_COUNT];
    char scratch[IG_FRESH_SCRATCH_BYTES];
    int nheaders = ig_build_headers(headers, IG_STATIC_HEADERS_COUNT + IG_FRESH_HEADERS_COUNT,
                                    scratch, sizeof(scratch));

    int body_len = 0;
    char *body = ig_build_timeline_body(&body_len);
    if (!body) { pal_log("ig_client: body build failed"); return NULL; }

    int status = 0, resp_len = 0;
    void *resp = https_request(IG_PRIVATE_HOST, "POST", IG_PRIVATE_PATH,
                               headers, nheaders, body, body_len, &status, &resp_len);
    free(body);

    if (!resp) { pal_log("ig_client: timeline request failed"); return NULL; }

    PrivateFeed *feed = NULL;
    if (status == 200) {
        feed = feed_from_private_json((const char *)resp, resp_len);
        if (!feed) pal_log("ig_client: feed_from_private_json failed");
    } else {
        pal_log("ig_client: timeline HTTP status=%d", status);
    }
    free(resp);
    return feed;
}
