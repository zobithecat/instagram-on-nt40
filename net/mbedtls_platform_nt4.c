/* net/mbedtls_platform_nt4.c — the platform glue mbedTLS needs beyond what
 * pal/nt4_crt.c already provides (calloc, free, the mem-family, the str-family), per the config
 * decisions documented in mbedtls_config_nt4.h:
 *
 *   - time()                     : GetSystemTimeAsFileTime, no CRT involved.
 *   - mbedtls_platform_gmtime_r(): pure calendar math (Howard Hinnant's
 *     civil_from_days), no OS call, so it works identically on any platform.
 *   - mbedtls_nt4_{v,}snprintf() : a small C99-semantics snprintf covering
 *     exactly the conversions the compiled-in library files use (verified by
 *     grepping every library/ source file for its format strings: %s %c %d
 *     %u %x %X %lu with
 *     optional zero/space-padded width — no floats, no %p; those only occur
 *     in debug.c/error.c/self-test code we don't compile).
 *
 * calloc/free (mbedtls_calloc/mbedtls_free) and time (mbedtls_time) are left
 * at their default plain-libc-name mapping in mbedtls_config_nt4.h, so they
 * resolve directly against pal/nt4_crt.c's calloc/free and this file's time()
 * — no macros or runtime registration needed for those three.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <time.h>
#include <stdarg.h>
#include <stddef.h>

/* ---- time() via the Win32 clock (no CRT) ---------------------------------*/

time_t time(time_t *t) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t100ns = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    /* 116444736000000000 = 100ns ticks between 1601-01-01 and 1970-01-01 */
    time_t now = (time_t)((t100ns - 116444736000000000ULL) / 10000000ULL);
    if (t) *t = now;
    return now;
}

/* ---- mbedtls_platform_gmtime_r(): civil_from_days, proleptic Gregorian ---*/

struct tm *mbedtls_platform_gmtime_r(const time_t *tt, struct tm *tm_buf) {
    long long secs = (long long)*tt;
    long long days = secs / 86400;
    long long rem  = secs % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }

    int sec  = (int)(rem % 60); rem /= 60;
    int minu = (int)(rem % 60); rem /= 60;
    int hour = (int)rem;

    /* Hinnant's algorithm: days-since-1970-01-01 -> (year, month, day). */
    long long z = days + 719468; /* shift so day 0 == 0000-03-01 */
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long long doe = (unsigned long long)(z - era * 146097);           /* [0, 146096] */
    unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
    long long year = (long long)yoe + era * 400;
    unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);           /* [0, 365] */
    unsigned long long mp  = (5 * doy + 2) / 153;                              /* [0, 11] */
    unsigned long long day = doy - (153 * mp + 2) / 5 + 1;                     /* [1, 31] */
    unsigned long long mon = mp + (mp < 10 ? 3 : (unsigned long long)-9);      /* [1, 12] */
    if (mon <= 2) year += 1;

    /* 1970-01-01 (day 0) was a Thursday (tm_wday == 4). */
    int wday = (int)(((days % 7) + 7 + 4) % 7);

    tm_buf->tm_sec   = sec;
    tm_buf->tm_min   = minu;
    tm_buf->tm_hour  = hour;
    tm_buf->tm_mday  = (int)day;
    tm_buf->tm_mon   = (int)mon - 1;
    tm_buf->tm_year  = (int)(year - 1900);
    tm_buf->tm_wday  = wday;
    tm_buf->tm_yday  = 0;  /* not used by mbedTLS's validity-period checks */
    tm_buf->tm_isdst = 0;  /* UTC only */
    return tm_buf;
}

/* ---- minimal, C99-return-semantics snprintf/vsnprintf ---------------------
 * snprintf's contract (which callers like MBEDTLS_X509_SAFE_SNPRINTF rely on):
 * return the number of characters that WOULD have been written given
 * unlimited space (excluding the NUL), even when the buffer was too small to
 * hold them all. Truncated output is still NUL-terminated (unless cap == 0). */

static int emit(char *buf, int cap, int *pos, const char *s, int len) {
    for (int i = 0; i < len; i++) {
        if (*pos < cap) buf[*pos] = s[i];
        (*pos)++;
    }
    return len;
}

static int emit_uint(char *buf, int cap, int *pos, unsigned long v, int base, int upper,
                     int width, int zero_pad) {
    char digits[32];
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { digits[n++] = hex[v % (unsigned)base]; v /= (unsigned)base; } while (v && n < (int)sizeof(digits));
    int pad = width - n;
    for (int i = 0; i < pad; i++) emit(buf, cap, pos, zero_pad ? "0" : " ", 1);
    char rev[32];
    for (int i = 0; i < n; i++) rev[i] = digits[n - 1 - i];
    return emit(buf, cap, pos, rev, n) + (pad > 0 ? pad : 0);
}

int mbedtls_nt4_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    int cap = (n > 0) ? (int)n - 1 : 0; /* room for content, NUL reserved separately */
    int pos = 0;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { emit(buf, cap, &pos, p, 1); continue; }
        p++;
        int zero_pad = 0, width = 0, is_long = 0;
        if (*p == '0') { zero_pad = 1; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (*p == 'l') { is_long = 1; p++; }
        if (*p == 'z') { p++; } /* size_t modifier: treat as unsigned long on this ILP32 target */

        switch (*p) {
        case '%': emit(buf, cap, &pos, "%", 1); break;
        case 'c': { char c = (char)va_arg(ap, int); emit(buf, cap, &pos, &c, 1); break; }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0; while (s[len]) len++;
            emit(buf, cap, &pos, s, len);
            break;
        }
        case 'd': case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { emit(buf, cap, &pos, "-", 1); v = -v; }
            emit_uint(buf, cap, &pos, (unsigned long)v, 10, 0, width, zero_pad);
            break;
        }
        case 'u':
            emit_uint(buf, cap, &pos, is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
                     10, 0, width, zero_pad);
            break;
        case 'x':
            emit_uint(buf, cap, &pos, is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
                     16, 0, width, zero_pad);
            break;
        case 'X':
            emit_uint(buf, cap, &pos, is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
                     16, 1, width, zero_pad);
            break;
        default:
            emit(buf, cap, &pos, "%", 1);
            if (*p) emit(buf, cap, &pos, p, 1);
            break;
        }
        if (!*p) break; /* trailing '%' with nothing after it */
    }

    if (n > 0) buf[(pos < cap) ? pos : cap] = 0;
    return pos;
}

int mbedtls_nt4_snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = mbedtls_nt4_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
