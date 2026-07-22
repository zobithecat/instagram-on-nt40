/* core/http.h — minimal HTTP/1.1 request builder + response parser.
 *
 * OS-independent: this module only turns requests into bytes and bytes into a
 * parsed response. The actual socket/TLS I/O lives in pal/. Handles the two
 * body framings the Instagram Graph API uses over Connection: close —
 * Content-Length and chunked transfer-encoding. Freestanding-friendly. */
#ifndef CORE_HTTP_H
#define CORE_HTTP_H

typedef struct { const char *name, *value; } HttpHeader;

/* Serialize an HTTP/1.1 request into a malloc'd buffer (caller free()s), with
 * *out_len set to its length. A Host header is emitted from `host`; `headers`
 * (nheaders) are extra headers; a Content-Length is added when body_len > 0.
 * Returns NULL on failure. */
char *http_build_request(const char *method, const char *host, const char *path,
                         const HttpHeader *headers, int nheaders,
                         const void *body, int body_len, int *out_len);

typedef struct {
    int          status;    /* status code, e.g. 200 */
    const char  *reason;    /* reason phrase (into `raw`) */
    HttpHeader  *headers;   /* owned array; name/value point into `raw` */
    int          nheaders;
    const char  *body;      /* response body (decoded if chunked) */
    int          body_len;
    /* internals */
    char        *raw;        /* owned copy of the response bytes */
    char        *body_owned; /* owned decoded body, if chunked (else NULL) */
} HttpResponse;

/* Parse a complete HTTP response. Returns 0 on success, -1 on malformed input.
 * On success the caller must http_response_free(out). */
int  http_parse_response(const void *data, int len, HttpResponse *out);
void http_response_free(HttpResponse *r);

/* Case-insensitive header lookup; returns the value or NULL. */
const char *http_header(const HttpResponse *r, const char *name);

#endif /* CORE_HTTP_H */
