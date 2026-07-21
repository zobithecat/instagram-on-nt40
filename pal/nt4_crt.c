/* pal/nt4_crt.c — freestanding C-runtime shim for the NT4 target.
 *
 * Homebrew's mingw-w64 links the Universal CRT (api-ms-win-crt-*.dll), which
 * does not exist on NT4 (it's a Windows 10 component) -> "unable to load DLL".
 * Rather than fight the toolchain for a msvcrt build, we drop the C runtime
 * entirely (-nostdlib) and provide the handful of symbols the compiler and our
 * own code emit, backed by Win32 (kernel32 heap). The resulting exe imports
 * ONLY kernel32/user32/gdi32/advapi32 — all present on NT4 RTM.
 *
 * Compiled only into the NT4 build (it lives in pal/, which the native test
 * build does not touch, so there is no clash with the host libc).
 */
#include <windows.h>

/* ---- heap: malloc/calloc/free/realloc over the process heap ---- */

void *malloc(size_t n) {
    return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
}
void *calloc(size_t count, size_t size) {
    /* callers guard against overflow before reaching here (see surface_alloc) */
    size_t n = count * size;
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
}
void free(void *p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}
void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    return HeapReAlloc(GetProcessHeap(), 0, p, n);
}

/* ---- mem primitives the compiler may emit for struct/array ops ----
 * Built with -fno-builtin -fno-tree-loop-distribute-patterns so these byte
 * loops are not "optimized" back into self-recursive memcpy/memset calls. */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* ---- PE entry point (replaces the CRT's WinMainCRTStartup) ----
 * -nostdlib means no CRT startup runs, so we set up nothing beyond calling
 * WinMain. No argc/argv, no atexit, no global C++ ctors (we have none). */
extern int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

void mainCRTStartup(void) {
    int r = WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWNORMAL);
    ExitProcess((UINT)r);
}
