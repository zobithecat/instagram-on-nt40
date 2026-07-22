/* pal/pal_common_win32.c — Win32/NT4 PAL pieces that need only kernel32/user32
 * (no GDI). Split out of pal_win32.c so non-GUI binaries (like the nettest
 * bring-up program) can link logging/file-reading without pulling in the
 * window/DIB code and its gdi32/comctl32 imports. */
#include "pal.h"

#include <windows.h>
#include <stdarg.h>
#include <stdlib.h> /* malloc/free — provided by pal/nt4_crt.c */

/* ---- COM1 debug logging ---------------------------------------------------*/

void pal_log(const char *fmt, ...) {
    static HANDLE com = INVALID_HANDLE_VALUE;
    static int    tried = 0;
    if (!tried) {
        tried = 1;
        /* QEMU maps guest COM1 to the host -serial file. Plain CreateFile. */
        com = CreateFileA("COM1", GENERIC_WRITE, 0, NULL,
                          OPEN_EXISTING, 0, NULL);
    }
    /* wvsprintfA is in user32 (NT4-native) — no C runtime needed. It supports
     * %d/%u/%x/%s/%c/%ld/%lu (no floats), which is all pal_log uses. Max output
     * is 1024 bytes per the Win32 contract, so size the buffer accordingly. */
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    buf[n++] = '\r';
    buf[n++] = '\n';
    if (com != INVALID_HANDLE_VALUE) {
        DWORD wrote;
        WriteFile(com, buf, (DWORD)n, &wrote, NULL);
    }
    OutputDebugStringA(buf); /* also visible to a host debugger if attached */
}

void *pal_read_file(const char *path, int *len) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { pal_log("read_file: open failed: %s", path); return NULL; }
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(h); return NULL; }
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, size, &got, NULL);
    CloseHandle(h);
    if (!ok || got != size) { free(buf); return NULL; }
    if (len) *len = (int)size;
    return buf;
}
