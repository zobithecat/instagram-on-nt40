/* net/ig_private.c — see ig_private.h. Combines the baked session template
 * (vm/ig_session_private.h) with freshly generated per-request fields. */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "ig_private.h"
#include "../vm/ig_session_private.h" /* IG_STATIC_HEADERS[], IG_BODY_* -- gitignored */

/* mbedtls_nt4_snprintf lives in net/mbedtls_platform_nt4.c; it's a plain C
 * function (not mbedTLS-specific), safe to reuse here for integer formatting. */
extern int mbedtls_nt4_snprintf(char *buf, size_t n, const char *fmt, ...);

/* ---- randomness (advapi32 CryptGenRandom, same source mbedTLS's own
 * entropy_poll.c uses) ------------------------------------------------------*/

static void ig_random_bytes(unsigned char *buf, int len) {
    HCRYPTPROV prov;
    if (CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(prov, (DWORD)len, buf);
        CryptReleaseContext(prov, 0);
    } else {
        for (int i = 0; i < len; i++) buf[i] = (unsigned char)i; /* never hit in practice */
    }
}

static unsigned long ig_rand_range(unsigned long lo, unsigned long hi) {
    unsigned long v;
    ig_random_bytes((unsigned char *)&v, sizeof(v));
    return lo + (v % (hi - lo + 1));
}

/* Standard UUIDv4: 16 random bytes with the version/variant nibbles fixed,
 * formatted 8-4-4-4-12, matching Python's uuid.uuid4() (what instagrapi uses
 * to build X-Pigeon-Session-Id). */
static void ig_uuid4(char out[37]) {
    unsigned char b[16];
    ig_random_bytes(b, 16);
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40); /* version 4 */
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80); /* variant 10xx */
    static const char *hex = "0123456789abcdef";
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = hex[b[i] >> 4];
        out[p++] = hex[b[i] & 0xF];
    }
    out[p] = 0;
}

/* ---- clock (GetSystemTimeAsFileTime, same epoch math as
 * net/mbedtls_platform_nt4.c's time(), just kept at ms resolution here) ----*/

static unsigned long long ig_now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t100ns = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t100ns - 116444736000000000ULL) / 10000ULL; /* 100ns ticks -> ms */
}

/* ---- header assembly ------------------------------------------------------*/

static int ci_eq(const char *a, const char *b) {
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

int ig_build_headers(HttpHeader *out, int out_cap, char *scratch, int scratch_cap) {
    int n = 0;
    for (int i = 0; i < IG_STATIC_HEADERS_COUNT && n < out_cap; i++) {
        out[n].name  = IG_STATIC_HEADERS[i].name;
        out[n].value = IG_STATIC_HEADERS[i].value;
        /* The captured session advertised gzip/deflate support (a real
         * Android client always does), so the server compresses the body --
         * we have no gzip/deflate decoder, so ask for identity (uncompressed)
         * instead. The server honors this like any standard HTTP peer. */
        if (ci_eq(out[n].name, "Accept-Encoding")) out[n].value = "identity";
        n++;
    }

    /* Carve the 7 fresh header values out of `scratch`, one slice each. */
    int need = IG_FRESH_HEADERS_COUNT * 32; /* generous per-value slice */
    if (scratch_cap < need || out_cap < n + IG_FRESH_HEADERS_COUNT) return n;

    char uuid[37];
    ig_uuid4(uuid);
    unsigned long long now_ms = ig_now_ms();

    char *s = scratch;
#define SLICE(cap) (s += (cap))
    char *pigeon_id = s; SLICE(64);
    mbedtls_nt4_snprintf(pigeon_id, 64, "UFS-%s-1", uuid);

    char *rawtime = s; SLICE(32);
    mbedtls_nt4_snprintf(rawtime, 32, "%lu.%03lu",
                        (unsigned long)(now_ms / 1000), (unsigned long)(now_ms % 1000));

    char *speed = s; SLICE(16);
    mbedtls_nt4_snprintf(speed, 16, "%lu.%03lu",
                        (unsigned long)ig_rand_range(2500, 3000),
                        (unsigned long)ig_rand_range(0, 999));

    char *totalbytes = s; SLICE(16);
    mbedtls_nt4_snprintf(totalbytes, 16, "%lu", ig_rand_range(5000000, 90000000));

    char *totaltime = s; SLICE(16);
    mbedtls_nt4_snprintf(totaltime, 16, "%lu", ig_rand_range(2000, 9000));

    char *salt = s; SLICE(16);
    mbedtls_nt4_snprintf(salt, 16, "%lu", ig_rand_range(1061162222, 1061262222));

    char *latency = s; SLICE(8);
    mbedtls_nt4_snprintf(latency, 8, "%lu", ig_rand_range(1, 5));
#undef SLICE

    static const char *names[IG_FRESH_HEADERS_COUNT] = {
        "X-Pigeon-Session-Id", "X-Pigeon-Rawclienttime",
        "X-IG-Bandwidth-Speed-KBPS", "X-IG-Bandwidth-TotalBytes-B", "X-IG-Bandwidth-TotalTime-MS",
        "X-IG-SALT-IDS", "X-CM-Latency",
    };
    char *values[IG_FRESH_HEADERS_COUNT] = {
        pigeon_id, rawtime, speed, totalbytes, totaltime, salt, latency,
    };
    for (int i = 0; i < IG_FRESH_HEADERS_COUNT; i++) {
        out[n].name = names[i];
        out[n].value = values[i];
        n++;
    }
    return n;
}

/* ---- body assembly ---------------------------------------------------- */

static int append(char *buf, int cap, int pos, const char *s) {
    int len = (int)strlen(s);
    if (pos + len < cap) memcpy(buf + pos, s, (size_t)len);
    return pos + len;
}

char *ig_build_timeline_body(int *out_len) {
    unsigned long long now_ms = ig_now_ms();
    char ts[24];
    mbedtls_nt4_snprintf(ts, sizeof(ts), "%llu", now_ms);
    int will_sound_on = (int)ig_rand_range(0, 1);

    char buf[4096];
    int p = 0;
#define S(str) (p = append(buf, sizeof(buf), p, (str)))
#define STRFIELD(key, val) (S("\"" key "\":\"" val "\","))
#define NUMFIELD(key, val)                                                     \
    do {                                                                       \
        char numbuf[16];                                                       \
        mbedtls_nt4_snprintf(numbuf, sizeof(numbuf), "%d", (val));             \
        S("\"" key "\":"); S(numbuf); S(",");                                  \
    } while (0)

    S("{");
    S("\"app_start_time\":\""); S(ts); S("\",");
    STRFIELD("has_camera_permission", IG_BODY_HAS_CAMERA_PERMISSION);
    STRFIELD("feed_view_info", IG_BODY_FEED_VIEW_INFO);
    S("\"client_recorded_request_time_ms\":\""); S(ts); S("\",");
    STRFIELD("client_seen_store_media_list", IG_BODY_CLIENT_SEEN_STORE_MEDIA_LIST);
    STRFIELD("client_view_state_media_list", IG_BODY_CLIENT_VIEW_STATE_MEDIA_LIST);
    STRFIELD("device_timezone_name", IG_BODY_DEVICE_TIMEZONE_NAME);
    STRFIELD("feed_reshare_info", IG_BODY_FEED_RESHARE_INFO);
    STRFIELD("phone_id", IG_BODY_PHONE_ID);
    STRFIELD("reason", IG_BODY_REASON);
    NUMFIELD("battery_level", IG_BODY_BATTERY_LEVEL);
    STRFIELD("timezone_offset", IG_BODY_TIMEZONE_OFFSET);
    STRFIELD("device_id", IG_BODY_DEVICE_ID);
    STRFIELD("include_attribution_ui_data", IG_BODY_INCLUDE_ATTRIBUTION_UI_DATA);
    STRFIELD("push_disabled", IG_BODY_PUSH_DISABLED);
    STRFIELD("request_id", IG_BODY_REQUEST_ID);
    S("\"request_build_time\":\""); S(ts); S("\",");
    STRFIELD("_uuid", IG_BODY__UUID);
    NUMFIELD("is_charging", IG_BODY_IS_CHARGING);
    NUMFIELD("is_dark_mode", IG_BODY_IS_DARK_MODE);
    S("\"will_sound_on\":"); { char nb[4]; mbedtls_nt4_snprintf(nb, sizeof(nb), "%d", will_sound_on); S(nb); } S(",");
    STRFIELD("session_id", IG_BODY_SESSION_ID);
    STRFIELD("session_level_signals", IG_BODY_SESSION_LEVEL_SIGNALS);
    STRFIELD("bloks_versioning_id", IG_BODY_BLOKS_VERSIONING_ID);
    S("\"is_pull_to_refresh\":\""); S(IG_BODY_IS_PULL_TO_REFRESH); S("\"");
    S("}");
#undef S
#undef STRFIELD
#undef NUMFIELD

    if (p >= (int)sizeof(buf)) return NULL; /* would have overflowed -- fail loudly */

    char *out = (char *)malloc((size_t)p + 1);
    if (!out) return NULL;
    memcpy(out, buf, (size_t)p);
    out[p] = 0;
    if (out_len) *out_len = p;
    return out;
}
