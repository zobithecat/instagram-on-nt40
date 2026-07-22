/* net/tlstest_main.c — milestone 5 bring-up: full stack over real TLS 1.2 on
 * real NT4 — pal sockets + mbedTLS handshake/record layer (with actual X.509
 * chain verification, not skipped) + core/http + core/json + core/model,
 * against a self-signed HTTPS mock on the host (tools/mock_graph_server_tls.py).
 * No window; all output goes to COM1 via pal_log. */
#include <windows.h>
#include <stdlib.h> /* free — provided by pal/nt4_crt.c */
#include "pal.h"
#include "net.h"
#include "http.h"
#include "model.h"
#include "mock_ca_cert.h" /* k_mock_ca_der[] — our test mock's self-signed cert */

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#define MOCK_HOST "10.0.2.2"      /* TCP destination: the QEMU host, via SLIRP */
#define MOCK_SNI  "nt4mock.local" /* SNI / cert CN+SAN — mbedTLS 2.28 only checks
                                    * dNSName SANs (see x509_crt_check_san), not
                                    * IP SANs, so the mock cert uses a DNS name. */
#define MOCK_PORT 8443
#define RECV_CAP  16384

/* ---- BIO glue: mbedTLS <-> pal sockets, per mbedtls_ssl_{send,recv}_t ---- */

static int nt4_ssl_send(void *ctx, const unsigned char *buf, size_t len) {
    PalSocket s = *(PalSocket *)ctx;
    if (pal_tcp_send_all(s, buf, (int)len) != 0) return -0x004E; /* MBEDTLS_ERR_NET_SEND_FAILED */
    return (int)len;
}

static int nt4_ssl_recv(void *ctx, unsigned char *buf, size_t len) {
    PalSocket s = *(PalSocket *)ctx;
    int n = pal_tcp_recv(s, buf, (int)len);
    if (n < 0) return -0x004C; /* MBEDTLS_ERR_NET_RECV_FAILED */
    return n; /* 0 = orderly close, matches mbedtls_ssl_recv_t's contract */
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== tlstest boot ===");

    if (pal_net_init() != 0) { pal_log("tlstest: net init failed"); return 1; }

    PalSocket sock = pal_tcp_connect(MOCK_HOST, MOCK_PORT);
    if (sock < 0) {
        pal_log("tlstest: connect to %s:%d failed", MOCK_HOST, MOCK_PORT);
        pal_net_shutdown();
        return 1;
    }

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
    if (rc != 0) { pal_log("tlstest: ctr_drbg_seed failed %d", rc); goto cleanup; }

    rc = mbedtls_x509_crt_parse(&cacert, k_mock_ca_der, k_mock_ca_der_len);
    if (rc != 0) { pal_log("tlstest: ca cert parse failed %d", rc); goto cleanup; }
    pal_log("tlstest: trusted CA loaded (%u bytes DER)", k_mock_ca_der_len);

    rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) { pal_log("tlstest: ssl_config_defaults failed %d", rc); goto cleanup; }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    rc = mbedtls_ssl_setup(&ssl, &conf);
    if (rc != 0) { pal_log("tlstest: ssl_setup failed %d", rc); goto cleanup; }

    rc = mbedtls_ssl_set_hostname(&ssl, MOCK_SNI); /* SNI + cert name/SAN check */
    if (rc != 0) { pal_log("tlstest: set_hostname failed %d", rc); goto cleanup; }

    mbedtls_ssl_set_bio(&ssl, &sock, nt4_ssl_send, nt4_ssl_recv, NULL);

    pal_log("tlstest: starting handshake...");
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            pal_log("tlstest: handshake failed %d (verify flags check follows)", rc);
            uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
            if (flags != 0xFFFFFFFF) pal_log("tlstest: verify flags=0x%x", (unsigned)flags);
            goto cleanup;
        }
    }
    pal_log("tlstest: handshake OK, cipher=%s", mbedtls_ssl_get_ciphersuite(&ssl));

    HttpHeader hdrs[] = { { "Connection", "close" } };
    int req_len = 0;
    char *req = http_build_request("GET", MOCK_HOST, "/me/media?fields=id,caption,media_url",
                                   hdrs, 1, NULL, 0, &req_len);
    if (!req) { pal_log("tlstest: build_request failed"); goto cleanup; }

    int written = 0;
    while (written < req_len) {
        int n = mbedtls_ssl_write(&ssl, (unsigned char *)req + written, (size_t)(req_len - written));
        if (n < 0) { pal_log("tlstest: ssl_write failed %d", n); free(req); goto cleanup; }
        written += n;
    }
    free(req);
    pal_log("tlstest: sent %d bytes over TLS", written);

    char *buf = (char *)malloc(RECV_CAP);
    int total = 0;
    if (buf) {
        for (;;) {
            int n = mbedtls_ssl_read(&ssl, (unsigned char *)buf + total, (size_t)(RECV_CAP - total));
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) break;
            if (n < 0) { pal_log("tlstest: ssl_read error %d", n); break; }
            total += n;
            if (total >= RECV_CAP) break;
        }
    }
    mbedtls_ssl_close_notify(&ssl);
    pal_log("tlstest: received %d bytes over TLS", total);

    if (buf && total > 0) {
        HttpResponse resp;
        if (http_parse_response(buf, total, &resp) == 0) {
            pal_log("tlstest: status=%d body_len=%d", resp.status, resp.body_len);
            Feed *feed = feed_from_graph_json(resp.body, resp.body_len);
            if (feed) {
                pal_log("tlstest: parsed %d posts, cursor=%s", feed->count,
                        feed->next_cursor ? feed->next_cursor : "(none)");
                for (int i = 0; i < feed->count; i++) {
                    Post *p = &feed->posts[i];
                    pal_log("tlstest: post[%d] id=%s user=%s likes=%d", i,
                            p->id ? p->id : "?", p->username ? p->username : "?",
                            (int)p->like_count);
                }
                feed_free(feed);
            } else {
                pal_log("tlstest: feed_from_graph_json failed");
            }
            http_response_free(&resp);
        } else {
            pal_log("tlstest: http_parse_response failed");
        }
    }
    free(buf);
    pal_log("=== tlstest done ===");

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
