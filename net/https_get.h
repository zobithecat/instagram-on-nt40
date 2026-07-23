/* net/https_get.h — generic HTTPS request over our own mbedTLS/pal-socket
 * stack, verified against the embedded DigiCert Global Root G2 CA. That root
 * covers not just i.instagram.com/graph.instagram.com but also the
 * *.cdninstagram.com photo CDN (same DigiCert Global G2 TLS RSA SHA256 2020
 * CA1 intermediate — confirmed via `openssl s_client -showcerts` against a
 * real cdninstagram.com host), so this one helper serves both the timeline
 * API call and the per-post photo downloads. Refactored out of the
 * proven-on-real-NT4 TLS boilerplate in net/ig_feed_main.c. */
#ifndef NET_HTTPS_GET_H
#define NET_HTTPS_GET_H

#include "http.h"

/* Perform an HTTPS request against host:443. `headers`/`nheaders` and
 * `body`/`body_len` may be 0/NULL for a plain GET. On success returns a
 * malloc'd response body (caller frees) with *out_len set and *out_status set
 * to the HTTP status code. Returns NULL on any connect/TLS/HTTP failure
 * (status is left unset in that case) — callers should treat NULL as "could
 * not fetch this", not necessarily fatal to the whole feed.
 *
 * Caller must have already called pal_net_init() (once at boot, not once per
 * request — this fetches a whole feed's worth of timeline + per-post photos
 * over separate connections, and Winsock startup/cleanup isn't meant to be
 * cycled per-socket). */
void *https_request(const char *host, const char *method, const char *path,
                    const HttpHeader *headers, int nheaders,
                    const void *body, int body_len,
                    int *out_status, int *out_len);

/* Split a "https://host/path?query" URL into a NUL-terminated host (written
 * into `host`, capacity `host_cap`) and a path pointer into the *same*
 * original `url` string (borrowed, not copied — `url` must outlive it).
 * Returns 0 on success, -1 if the host wouldn't fit or the url is malformed. */
int https_split_url(const char *url, char *host, int host_cap, const char **path_out);

#endif /* NET_HTTPS_GET_H */
