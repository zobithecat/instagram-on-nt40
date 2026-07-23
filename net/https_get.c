/* net/https_get.c — see https_get.h. */
#include "https_get.h"
#include "digicert_g2_root.h"
#include "pal.h"
#include "net.h"

#include <stdlib.h> /* malloc/realloc/free -- provided by pal/nt4_crt.c */
#include <string.h> /* strncmp/strchr/strlen -- provided by pal/nt4_crt.c */

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#define HTTPS_PORT         443
#define RECV_INITIAL_CAP   (64 * 1024) /* grown via realloc below, not a cap */

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

int https_split_url(const char *url, char *host, int host_cap, const char **path_out) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) p += 7;
    else return -1; /* not an absolute http(s) URL */

    const char *slash = strchr(p, '/');
    int hlen = slash ? (int)(slash - p) : (int)strlen(p);
    if (hlen <= 0 || hlen >= host_cap) return -1;

    for (int i = 0; i < hlen; i++) host[i] = p[i];
    host[hlen] = 0;
    *path_out = slash ? slash : "/";
    return 0;
}

void *https_request(const char *host, const char *method, const char *path,
                    const HttpHeader *headers, int nheaders,
                    const void *body, int body_len,
                    int *out_status, int *out_len) {
    void *result = NULL;

    PalSocket sock = pal_tcp_connect(host, HTTPS_PORT);
    if (sock < 0) { pal_log("https_get: connect to %s failed", host); return NULL; }

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
    if (rc != 0) { pal_log("https_get: drbg seed failed %d", rc); goto cleanup; }

    rc = mbedtls_x509_crt_parse(&cacert, k_digicert_g2_root_der, k_digicert_g2_root_der_len);
    if (rc != 0) { pal_log("https_get: ca parse failed %d", rc); goto cleanup; }

    rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) { pal_log("https_get: config_defaults failed %d", rc); goto cleanup; }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    rc = mbedtls_ssl_setup(&ssl, &conf);
    if (rc != 0) { pal_log("https_get: ssl_setup failed %d", rc); goto cleanup; }

    rc = mbedtls_ssl_set_hostname(&ssl, host);
    if (rc != 0) { pal_log("https_get: set_hostname failed %d", rc); goto cleanup; }

    mbedtls_ssl_set_bio(&ssl, &sock, nt4_ssl_send, nt4_ssl_recv, NULL);

    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            pal_log("https_get: handshake with %s failed %d", host, rc);
            goto cleanup;
        }
    }

    {
        int req_len = 0;
        char *req = http_build_request(method, host, path, headers, nheaders, body, body_len, &req_len);
        if (!req) { pal_log("https_get: build_request failed"); goto cleanup; }

        int written = 0;
        while (written < req_len) {
            int n = mbedtls_ssl_write(&ssl, (unsigned char *)req + written, (size_t)(req_len - written));
            if (n < 0) { pal_log("https_get: ssl_write failed %d", n); free(req); goto cleanup; }
            written += n;
        }
        free(req);
    }

    {
        int cap = RECV_INITIAL_CAP;
        char *buf = (char *)malloc((size_t)cap);
        int total = 0;
        if (buf) {
            for (;;) {
                if (total >= cap) {
                    int newcap = cap * 2;
                    char *nb = (char *)realloc(buf, (size_t)newcap);
                    if (!nb) { pal_log("https_get: realloc to %d failed", newcap); break; }
                    buf = nb; cap = newcap;
                }
                int n = mbedtls_ssl_read(&ssl, (unsigned char *)buf + total, (size_t)(cap - total));
                if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) break;
                if (n < 0) { pal_log("https_get: ssl_read error %d", n); break; }
                total += n;
            }
        }
        mbedtls_ssl_close_notify(&ssl);

        if (buf && total > 0) {
            HttpResponse resp;
            if (http_parse_response(buf, total, &resp) == 0) {
                if (out_status) *out_status = resp.status;
                result = malloc((size_t)resp.body_len > 0 ? (size_t)resp.body_len : 1);
                if (result) {
                    for (int i = 0; i < resp.body_len; i++) ((char *)result)[i] = resp.body[i];
                    if (out_len) *out_len = resp.body_len;
                }
                http_response_free(&resp);
            } else {
                pal_log("https_get: http_parse_response failed for %s%s", host, path);
            }
        }
        free(buf);
    }

cleanup:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    pal_tcp_close(sock);
    return result;
}
