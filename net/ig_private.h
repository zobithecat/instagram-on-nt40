/* net/ig_private.h — builds requests for Instagram's private mobile API
 * (feed/timeline/) from a captured, real instagrapi session template
 * (vm/ig_session_private.h — gitignored, generated, never committed).
 *
 * Most of a real request is either permanently static (device identity,
 * Authorization) or a long-lived session hint (the IG-U-* routing tokens,
 * empirically confirmed to carry a rolling ~365-day window — see
 * docs/vm-notes.md). Only a handful of fields are genuinely per-request:
 * this module regenerates exactly those, matching instagrapi's own
 * randomization ranges (read from its source, not guessed), and reuses
 * core/http.h for everything else.
 */
#ifndef NET_IG_PRIVATE_H
#define NET_IG_PRIVATE_H

#include "http.h"

/* Fill `out` with the static session headers plus freshly generated
 * per-request ones (timestamps, telemetry, a fresh X-Pigeon-Session-Id).
 * `out` must have room for IG_STATIC_HEADERS_COUNT + IG_FRESH_HEADERS_COUNT
 * entries (see vm/ig_session_private.h and the .c file). The header *values*
 * for the fresh ones are written into `scratch` (caller-owned buffer, must
 * outlive the headers array) since HttpHeader stores pointers, not copies.
 * Returns the number of headers written. */
#define IG_FRESH_HEADERS_COUNT 7
#define IG_FRESH_SCRATCH_BYTES 256 /* enough for all 7 fresh values, NUL-separated */

int ig_build_headers(HttpHeader *out, int out_cap, char *scratch, int scratch_cap);

/* Build the feed/timeline/ POST body (static fields from the session
 * template + 4 freshly computed ones). Returns a malloc'd NUL-terminated
 * JSON string (caller frees) and sets *out_len, or NULL on OOM. */
char *ig_build_timeline_body(int *out_len);

#endif /* NET_IG_PRIVATE_H */
