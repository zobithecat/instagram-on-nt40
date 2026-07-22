/* net/ig_feed_main.c — the real thing: fetch the user's actual Instagram home
 * feed via the private mobile API, straight from NT4, using a session
 * transferred from a real (already-logged-in) instagrapi session. No bridge
 * process at request time -- this exe talks directly to i.instagram.com over
 * our own mbedTLS/socket stack, same pattern as net/graphtest_main.c. No
 * window; all output goes to COM1 via pal_log. */
#include <windows.h>
#include <stdlib.h> /* free -- provided by pal/nt4_crt.c */
#include "pal.h"
#include "net.h"
#include "http.h"
#include "model_private.h"
#include "ig_private.h"
#include "../vm/ig_session_private.h" /* IG_PRIVATE_HOST/PORT/PATH */
#include "digicert_g2_root.h" /* same CA as graph.instagram.com -- i.instagram.com
                               * shares the same DigiCert Global G2 intermediate */

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#define RECV_INITIAL_CAP (128 * 1024) /* real feed responses can run several
                                       * hundred KB; grown via realloc below
                                       * rather than a fixed truncating cap */

static int nt4_ssl_send(void *ctx, const unsigned char *buf, size_t len) {
    PalSocket s = *(PalSocket *)ctx;
    if (pal_tcp_send_all(s, buf, (int)len) != 0) return -0x004E;
    return (int)len;
}

static int nt4_ssl_recv(void *ctx, unsigned char *buf, size_t len) {
    PalSocket s = *(PalSocket *)ctx;
    int n = pal_tcp_recv(s, buf, (int)len);
    if (n < 0) return -0x004C;
    return n;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== ig_feed boot ===");

    if (pal_net_init() != 0) { pal_log("ig_feed: net init failed"); return 1; }

    PalSocket sock = pal_tcp_connect(IG_PRIVATE_HOST, IG_PRIVATE_PORT);
    if (sock < 0) { pal_log("ig_feed: connect failed"); pal_net_shutdown(); return 1; }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);

    int rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (rc != 0) { pal_log("ig_feed: drbg seed failed %d", rc); goto cleanup; }

    rc = mbedtls_x509_crt_parse(&cacert, k_digicert_g2_root_der, k_digicert_g2_root_der_len);
    if (rc != 0) { pal_log("ig_feed: ca parse failed %d", rc); goto cleanup; }

    rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) { pal_log("ig_feed: config_defaults failed %d", rc); goto cleanup; }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    rc = mbedtls_ssl_setup(&ssl, &conf);
    if (rc != 0) { pal_log("ig_feed: ssl_setup failed %d", rc); goto cleanup; }

    rc = mbedtls_ssl_set_hostname(&ssl, IG_PRIVATE_HOST);
    if (rc != 0) { pal_log("ig_feed: set_hostname failed %d", rc); goto cleanup; }

    mbedtls_ssl_set_bio(&ssl, &sock, nt4_ssl_send, nt4_ssl_recv, NULL);

    pal_log("ig_feed: handshake with %s...", IG_PRIVATE_HOST);
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            pal_log("ig_feed: handshake failed %d", rc);
            goto cleanup;
        }
    }
    pal_log("ig_feed: handshake OK, cipher=%s", mbedtls_ssl_get_ciphersuite(&ssl));

    /* --- build the real timeline request --- */
    HttpHeader headers[IG_STATIC_HEADERS_COUNT + IG_FRESH_HEADERS_COUNT];
    char scratch[IG_FRESH_SCRATCH_BYTES];
    int nheaders = ig_build_headers(headers, IG_STATIC_HEADERS_COUNT + IG_FRESH_HEADERS_COUNT,
                                    scratch, sizeof(scratch));
    pal_log("ig_feed: built %d headers", nheaders);

    int body_len = 0;
    char *body = ig_build_timeline_body(&body_len);
    if (!body) { pal_log("ig_feed: body build failed"); goto cleanup; }
    pal_log("ig_feed: built %d-byte body", body_len);

    int req_len = 0;
    char *req = http_build_request("POST", IG_PRIVATE_HOST, IG_PRIVATE_PATH,
                                   headers, nheaders, body, body_len, &req_len);
    free(body);
    if (!req) { pal_log("ig_feed: build_request failed"); goto cleanup; }

    int written = 0;
    while (written < req_len) {
        int n = mbedtls_ssl_write(&ssl, (unsigned char *)req + written, (size_t)(req_len - written));
        if (n < 0) { pal_log("ig_feed: ssl_write failed %d", n); free(req); goto cleanup; }
        written += n;
    }
    free(req);
    pal_log("ig_feed: sent %d bytes", written);

    int cap = RECV_INITIAL_CAP;
    char *buf = (char *)malloc((size_t)cap);
    int total = 0;
    if (buf) {
        for (;;) {
            if (total >= cap) { /* grow: response is bigger than expected */
                int newcap = cap * 2;
                char *nb = (char *)realloc(buf, (size_t)newcap);
                if (!nb) { pal_log("ig_feed: realloc to %d failed", newcap); break; }
                buf = nb; cap = newcap;
                pal_log("ig_feed: grew recv buffer to %d bytes", cap);
            }
            int n = mbedtls_ssl_read(&ssl, (unsigned char *)buf + total, (size_t)(cap - total));
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) break;
            if (n < 0) { pal_log("ig_feed: ssl_read error %d", n); break; }
            total += n;
        }
    }
    mbedtls_ssl_close_notify(&ssl);
    pal_log("ig_feed: received %d bytes", total);

    if (buf && total > 0) {
        HttpResponse resp;
        if (http_parse_response(buf, total, &resp) == 0) {
            pal_log("ig_feed: HTTP status=%d body_len=%d", resp.status, resp.body_len);
            if (resp.status == 200) {
                PrivateFeed *feed = feed_from_private_json(resp.body, resp.body_len);
                if (feed) {
                    pal_log("ig_feed: *** REAL FEED: %d posts ***", feed->count);
                    for (int i = 0; i < feed->count; i++) {
                        PrivatePost *p = &feed->posts[i];
                        pal_log("ig_feed: [%d] @%s type=%d slides=%d likes=%ld img=%d",
                                i, p->username ? p->username : "?", p->media_type,
                                p->num_slides, p->like_count, p->image_url != NULL);
                        if (p->caption) pal_log("ig_feed:     \"%s\"", p->caption);
                    }
                    private_feed_free(feed);
                } else {
                    pal_log("ig_feed: feed_from_private_json failed");
                }
            } else {
                /* Non-200 (e.g. an auth error if the session ever needs
                 * refreshing) -- log a slice of the body for diagnosis. */
                char chunk[201];
                int n = resp.body_len; if (n > 200) n = 200;
                for (int i = 0; i < n; i++) chunk[i] = resp.body[i];
                chunk[n] = 0;
                pal_log("ig_feed: non-200 body: %s", chunk);
            }
            http_response_free(&resp);
        } else {
            pal_log("ig_feed: http_parse_response failed");
        }
    }
    free(buf);
    pal_log("=== ig_feed done ===");

cleanup:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    pal_tcp_close(sock);
    pal_net_shutdown();
    ExitProcess(0);
    return 0;
}
