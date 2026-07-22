/* net/graphtest_main.c — milestone 5 stretch goal: real TLS 1.2 handshake +
 * X.509 verification against the REAL graph.instagram.com, on real NT4. No
 * access token yet (that needs a Meta developer app + OAuth, done later), so
 * the Graph API is expected to answer with its own JSON error body — the win
 * here is that our from-scratch TLS/HTTP stack round-trips against Meta's
 * real production edge at all. Root CA: DigiCert Global Root G2 (verified via
 * `openssl s_client -showcerts` + macOS's trust store, see net/digicert_g2_root.h).
 * No window; all output goes to COM1 via pal_log. */
#include <windows.h>
#include <stdlib.h> /* free — provided by pal/nt4_crt.c */
#include "pal.h"
#include "net.h"
#include "http.h"
#include "digicert_g2_root.h" /* k_digicert_g2_root_der[] */

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#define GRAPH_HOST "graph.instagram.com"
#define GRAPH_PORT 443
#define RECV_CAP   16384

static int nt4_ssl_send(void *ctx, const unsigned char *buf, size_t len) {
    PalSocket s = *(PalSocket *)ctx;
    if (pal_tcp_send_all(s, buf, (int)len) != 0) return -0x004E; /* MBEDTLS_ERR_NET_SEND_FAILED */
    return (int)len;
}

static int nt4_ssl_recv(void *ctx, unsigned char *buf, size_t len) {
    PalSocket s = *(PalSocket *)ctx;
    int n = pal_tcp_recv(s, buf, (int)len);
    if (n < 0) return -0x004C; /* MBEDTLS_ERR_NET_RECV_FAILED */
    return n;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== graphtest boot ===");

    if (pal_net_init() != 0) { pal_log("graphtest: net init failed"); return 1; }

    pal_log("graphtest: resolving + connecting to %s:%d...", GRAPH_HOST, GRAPH_PORT);
    PalSocket sock = pal_tcp_connect(GRAPH_HOST, GRAPH_PORT);
    if (sock < 0) {
        pal_log("graphtest: connect to %s failed", GRAPH_HOST);
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
    if (rc != 0) { pal_log("graphtest: ctr_drbg_seed failed %d", rc); goto cleanup; }

    rc = mbedtls_x509_crt_parse(&cacert, k_digicert_g2_root_der, k_digicert_g2_root_der_len);
    if (rc != 0) { pal_log("graphtest: root ca parse failed %d", rc); goto cleanup; }
    pal_log("graphtest: DigiCert Global Root G2 loaded (%u bytes DER)", k_digicert_g2_root_der_len);

    rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) { pal_log("graphtest: ssl_config_defaults failed %d", rc); goto cleanup; }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    rc = mbedtls_ssl_setup(&ssl, &conf);
    if (rc != 0) { pal_log("graphtest: ssl_setup failed %d", rc); goto cleanup; }

    rc = mbedtls_ssl_set_hostname(&ssl, GRAPH_HOST); /* SNI + cert CN/SAN check */
    if (rc != 0) { pal_log("graphtest: set_hostname failed %d", rc); goto cleanup; }

    mbedtls_ssl_set_bio(&ssl, &sock, nt4_ssl_send, nt4_ssl_recv, NULL);

    pal_log("graphtest: starting handshake against real graph.instagram.com...");
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            pal_log("graphtest: handshake failed %d", rc);
            uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
            if (flags != 0xFFFFFFFF) pal_log("graphtest: verify flags=0x%x", (unsigned)flags);
            goto cleanup;
        }
    }
    pal_log("graphtest: *** HANDSHAKE OK vs REAL graph.instagram.com *** cipher=%s",
            mbedtls_ssl_get_ciphersuite(&ssl));

    /* No access token yet -- expect a Graph API error JSON, which is a
     * perfectly good sign: it means our TLS+HTTP round-tripped through
     * Meta's real edge and their application layer answered us. */
    HttpHeader hdrs[] = { { "Connection", "close" } };
    int req_len = 0;
    char *req = http_build_request("GET", GRAPH_HOST, "/me?fields=id",
                                   hdrs, 1, NULL, 0, &req_len);
    if (!req) { pal_log("graphtest: build_request failed"); goto cleanup; }

    int written = 0;
    while (written < req_len) {
        int n = mbedtls_ssl_write(&ssl, (unsigned char *)req + written, (size_t)(req_len - written));
        if (n < 0) { pal_log("graphtest: ssl_write failed %d", n); free(req); goto cleanup; }
        written += n;
    }
    free(req);
    pal_log("graphtest: sent %d bytes over TLS", written);

    char *buf = (char *)malloc(RECV_CAP);
    int total = 0;
    if (buf) {
        for (;;) {
            int n = mbedtls_ssl_read(&ssl, (unsigned char *)buf + total, (size_t)(RECV_CAP - total));
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) break;
            if (n < 0) { pal_log("graphtest: ssl_read error %d", n); break; }
            total += n;
            if (total >= RECV_CAP) break;
        }
    }
    mbedtls_ssl_close_notify(&ssl);
    pal_log("graphtest: received %d bytes over TLS", total);

    if (buf && total > 0) {
        HttpResponse resp;
        if (http_parse_response(buf, total, &resp) == 0) {
            pal_log("graphtest: HTTP status=%d body_len=%d", resp.status, resp.body_len);
            /* log the body in ~200-byte chunks (pal_log's buffer caps at ~1KB) */
            int off = 0;
            while (off < resp.body_len) {
                char chunk[201];
                int n = resp.body_len - off; if (n > 200) n = 200;
                for (int i = 0; i < n; i++) chunk[i] = resp.body[off + i];
                chunk[n] = 0;
                pal_log("graphtest: body[%d..]: %s", off, chunk);
                off += n;
            }
            http_response_free(&resp);
        } else {
            pal_log("graphtest: http_parse_response failed");
        }
    }
    free(buf);
    pal_log("=== graphtest done ===");

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
