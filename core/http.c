/* core/http.c — see http.h. Pure parsing/formatting; no socket I/O. */
#include "http.h"
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ---- */

static int ci_eq(const char *a, const char *b) {
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

/* Parse a leading (optionally signed) decimal integer; stops at first non-digit. */
static long parse_long(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    long n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return neg ? -n : n;
}

static int put_uint(char *b, unsigned v) {
    char t[12];
    int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) b[i] = t[n - 1 - i];
    return n;
}

/* ---- request builder ---- */

char *http_build_request(const char *method, const char *host, const char *path,
                         const HttpHeader *headers, int nheaders,
                         const void *body, int body_len, int *out_len) {
    if (!method || !host || !path) return NULL;
    if (body_len < 0) body_len = 0;

    /* generous upper bound on the serialized size */
    size_t cap = strlen(method) + 1 + strlen(path) + 16
               + 6 + strlen(host) + 2
               + 32 /* Content-Length line */
               + (size_t)body_len + 4;
    for (int i = 0; i < nheaders; i++)
        cap += strlen(headers[i].name) + 2 + strlen(headers[i].value) + 2;

    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    int p = 0;
#define PUT(s) do { size_t _l = strlen(s); memcpy(buf + p, (s), _l); p += (int)_l; } while (0)
    PUT(method); buf[p++] = ' ';
    PUT(path);   buf[p++] = ' ';
    PUT("HTTP/1.1\r\n");
    PUT("Host: "); PUT(host); PUT("\r\n");
    for (int i = 0; i < nheaders; i++) {
        PUT(headers[i].name); PUT(": "); PUT(headers[i].value); PUT("\r\n");
    }
    if (body_len > 0) {
        PUT("Content-Length: ");
        p += put_uint(buf + p, (unsigned)body_len);
        PUT("\r\n");
    }
    PUT("\r\n");
    if (body_len > 0) { memcpy(buf + p, body, (size_t)body_len); p += body_len; }
#undef PUT
    if (out_len) *out_len = p;
    return buf;
}

/* ---- response parser ---- */

/* Decode a chunked-transfer body using explicit lengths (bytes may be binary). */
static char *dechunk(const char *p, int rem, int *out_len) {
    char *out = (char *)malloc((size_t)rem + 1);
    if (!out) return NULL;
    int o = 0;
    while (rem > 0) {
        long clen = 0;
        int i = 0;
        for (; i < rem; i++) {
            char c = p[i], d;
            if (c >= '0' && c <= '9') d = (char)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (char)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (char)(c - 'A' + 10);
            else break;
            clen = clen * 16 + d;
        }
        while (i < rem && p[i] != '\n') i++;   /* skip ext + CR to LF */
        if (i < rem) i++;                       /* past LF */
        p += i; rem -= i;
        if (clen <= 0) break;                   /* last chunk */
        if (clen > rem) clen = rem;             /* truncated safety */
        memcpy(out + o, p, (size_t)clen); o += (int)clen;
        p += clen; rem -= (int)clen;
        while (rem > 0 && (*p == '\r' || *p == '\n')) { p++; rem--; }
    }
    out[o] = 0;
    if (out_len) *out_len = o;
    return out;
}

int http_parse_response(const void *data, int len, HttpResponse *out) {
    if (!data || len < 12 || !out) return -1;
    memset(out, 0, sizeof(*out));

    char *raw = (char *)malloc((size_t)len + 1);
    if (!raw) return -1;
    memcpy(raw, data, (size_t)len);
    raw[len] = 0;
    out->raw = raw;

    /* status line: HTTP/x.y SP code SP reason CRLF */
    char *p = raw;
    char *eol = strstr(raw, "\r\n");
    if (!eol) { http_response_free(out); return -1; }
    *eol = 0;
    char *sp1 = strchr(p, ' ');
    if (!sp1) { http_response_free(out); return -1; }
    out->status = (int)parse_long(sp1 + 1);
    char *sp2 = strchr(sp1 + 1, ' ');
    out->reason = sp2 ? sp2 + 1 : "";
    p = eol + 2;

    /* headers until a blank line */
    for (;;) {
        char *line_end = strstr(p, "\r\n");
        if (!line_end) { http_response_free(out); return -1; }
        if (line_end == p) { p += 2; break; } /* blank line -> end of headers */
        *line_end = 0;
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = 0;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            HttpHeader *nh = (HttpHeader *)realloc(out->headers,
                                sizeof(HttpHeader) * (size_t)(out->nheaders + 1));
            if (!nh) { http_response_free(out); return -1; }
            out->headers = nh;
            out->headers[out->nheaders].name = p;
            out->headers[out->nheaders].value = val;
            out->nheaders++;
        }
        p = line_end + 2;
    }

    /* body framing */
    int body_off = (int)(p - raw);
    int remaining = len - body_off;
    if (remaining < 0) remaining = 0;

    const char *te = http_header(out, "transfer-encoding");
    const char *cl = http_header(out, "content-length");
    if (te && ci_eq(te, "chunked")) {
        int bl = 0;
        char *decoded = dechunk(p, remaining, &bl);
        if (!decoded) { http_response_free(out); return -1; }
        out->body_owned = decoded;
        out->body = decoded;
        out->body_len = bl;
    } else if (cl) {
        int n = (int)parse_long(cl);
        if (n < 0) n = 0;
        if (n > remaining) n = remaining;
        out->body = p;
        out->body_len = n;
    } else {
        out->body = p;             /* read-until-close */
        out->body_len = remaining;
    }
    return 0;
}

void http_response_free(HttpResponse *r) {
    if (!r) return;
    if (r->headers) free(r->headers);
    if (r->body_owned) free(r->body_owned);
    if (r->raw) free(r->raw);
    r->headers = NULL; r->body_owned = NULL; r->raw = NULL;
    r->nheaders = 0; r->body = NULL; r->body_len = 0;
}

const char *http_header(const HttpResponse *r, const char *name) {
    if (!r || !name) return NULL;
    for (int i = 0; i < r->nheaders; i++)
        if (ci_eq(r->headers[i].name, name)) return r->headers[i].value;
    return NULL;
}
