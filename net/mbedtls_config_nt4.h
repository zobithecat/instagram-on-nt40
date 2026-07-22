/* net/mbedtls_config_nt4.h — custom mbedTLS 2.28 config for the NT4 client.
 *
 * Replaces mbedtls/config.h entirely (via -DMBEDTLS_CONFIG_FILE). Scoped to
 * exactly what a TLS 1.2 client talking to graph.instagram.com needs:
 * ECDHE-RSA/ECDSA + RSA key exchange, AES-GCM/CBC, SHA-256/384/1, X.509 cert
 * verification with real time-based validity checks. Everything else (server
 * side, legacy protocols, debug/error strings, self-tests, filesystem I/O,
 * mbedTLS's own net/timing/threading layers) is left off: smaller binary, and
 * no dependencies our freestanding NT4 CRT doesn't already provide.
 *
 * Platform notes (see net/mbedtls_platform_nt4.c):
 *   - MBEDTLS_PLATFORM_C is mandatory on _WIN32 (check_config.h enforces it).
 *   - calloc/free/time are left at their plain-libc-name default mapping;
 *     pal/nt4_crt.c already provides calloc/free, and mbedtls_platform_nt4.c
 *     provides time() (GetSystemTimeAsFileTime-backed).
 *   - snprintf/vsnprintf: _WIN32 forces an ALT or MACRO; we use MACRO to
 *     avoid runtime function-pointer registration, pointing at our own
 *     mbedtls_platform_nt4.c implementations.
 *   - gmtime_r: MBEDTLS_PLATFORM_GMTIME_R_ALT lets us define
 *     mbedtls_platform_gmtime_r() directly (pure calendar math, no OS call).
 */
#ifndef MBEDTLS_CONFIG_NT4_H
#define MBEDTLS_CONFIG_NT4_H

/* ---- platform glue ---- */
/* Prototypes for the macro targets below (net/mbedtls_platform_nt4.c) — every
 * file that includes this config expands mbedtls_snprintf/vsnprintf to these
 * names, so they need to be declared wherever that expansion happens. */
#include <stdarg.h>
#include <stddef.h>
int mbedtls_nt4_snprintf(char *buf, size_t n, const char *fmt, ...);
int mbedtls_nt4_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO   mbedtls_nt4_snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO  mbedtls_nt4_vsnprintf
#define MBEDTLS_PLATFORM_GMTIME_R_ALT
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

/* ---- hashes ---- */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C     /* legacy signature_algorithms compat */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C   /* covers SHA-384 too */

/* ---- symmetric ciphers ---- */
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C

/* ---- bignum / RSA / ECC ---- */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15 /* classic RSA PKCS#1 v1.5 — what TLS 1.2 RSA suites/sigs use */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED

/* ---- X.509 ---- */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* ---- TLS (client only, 1.2 only) ---- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* ---- RNG (uses mbedTLS's own Win32 CryptoAPI entropy source, see
 * third_party/mbedtls/library/entropy_poll.c's _WIN32 branch — no glue needed) ---- */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* Deliberately OFF (see file header): MBEDTLS_SSL_SRV_C, MBEDTLS_NET_C,
 * MBEDTLS_TIMING_C, MBEDTLS_THREADING_C, MBEDTLS_FS_IO, MBEDTLS_SELF_TEST,
 * MBEDTLS_DEBUG_C, MBEDTLS_ERROR_C, MBEDTLS_PEM_PARSE_C, MBEDTLS_MD5_C,
 * MBEDTLS_SSL_PROTO_TLS1 / TLS1_1. */

#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_NT4_H */
