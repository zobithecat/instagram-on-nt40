/* pal/pal_win32.c — Win32/NT4 implementation of the PAL.
 *
 * Targets NT 4.0: 32bpp top-down DIB section as the compositor backbuffer,
 * BitBlt to the window on WM_PAINT, COM1 serial logging. No post-NT4 APIs
 * (no AlphaBlend/UpdateLayeredWindow/visual styles).
 */
#include "pal.h"

#include <windows.h>
#include <stdarg.h>
#include <stdlib.h> /* malloc/free — provided by pal/nt4_crt.c */

typedef struct {
    pal_render_fn render;
    void         *user;
    HBITMAP       dib;      /* DIB section = compositor backbuffer */
    HDC           mem_dc;   /* memory DC holding the DIB */
    Surface       fb;       /* aliases the DIB's pixels (top-down) */
    int           w, h;
} PalWindow;

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

/* ---- DIB-section backbuffer ----------------------------------------------*/

static void destroy_backbuffer(PalWindow *pw) {
    if (pw->mem_dc) { DeleteDC(pw->mem_dc); pw->mem_dc = NULL; }
    if (pw->dib)    { DeleteObject(pw->dib); pw->dib = NULL; }
    pw->fb.pixels = NULL;
}

/* (Re)create the top-down 32bpp DIB section at w x h and alias it as a Surface. */
static int create_backbuffer(PalWindow *pw, HDC wdc, int w, int h) {
    destroy_backbuffer(pw);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;     /* negative => top-down, matches Surface */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB; /* 0x00RRGGBB little-endian == B,G,R,X */

    void *bits = NULL;
    pw->mem_dc = CreateCompatibleDC(wdc);
    if (!pw->mem_dc) return -1;
    pw->dib = CreateDIBSection(pw->mem_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!pw->dib || !bits) { destroy_backbuffer(pw); return -1; }
    SelectObject(pw->mem_dc, pw->dib);

    surface_wrap(&pw->fb, (uint32_t *)bits, w, h, w);
    pw->w = w;
    pw->h = h;
    pal_log("pal: backbuffer %dx%d", w, h);
    return 0;
}

static void paint(PalWindow *pw, HWND hwnd) {
    if (!pw->fb.pixels) return;
    if (pw->render) pw->render(&pw->fb, pw->user);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    BitBlt(dc, 0, 0, pw->w, pw->h, pw->mem_dc, 0, 0, SRCCOPY);
    EndPaint(hwnd, &ps);
}

/* ---- window / message loop -----------------------------------------------*/

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PalWindow *pw = (PalWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        pw = (PalWindow *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pw);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC wdc = GetDC(hwnd);
        create_backbuffer(pw, wdc, rc.right - rc.left, rc.bottom - rc.top);
        ReleaseDC(hwnd, wdc);
        return 0;
    }
    case WM_SIZE:
        if (pw) {
            HDC wdc = GetDC(hwnd);
            create_backbuffer(pw, wdc, LOWORD(lp), HIWORD(lp));
            ReleaseDC(hwnd, wdc);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1; /* backbuffer covers the whole client; skip flicker */
    case WM_PAINT:
        if (pw) paint(pw, hwnd);
        return 0;
    case WM_DESTROY:
        if (pw) destroy_backbuffer(pw);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int pal_run_window(const char *title, int w, int h, pal_render_fn render, void *user) {
    static PalWindow pw;
    ZeroMemory(&pw, sizeof(pw));
    pw.render = render;
    pw.user   = user;

    HINSTANCE inst = GetModuleHandle(NULL);
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; /* we paint every pixel ourselves */
    wc.lpszClassName = "IgNt4Main";
    if (!RegisterClassA(&wc)) { pal_log("pal: RegisterClass failed %lu", GetLastError()); return 1; }

    /* size the window so the *client* area is w x h */
    RECT r = { 0, 0, w, h };
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowA("IgNt4Main", title, style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top,
                              NULL, NULL, inst, &pw);
    if (!hwnd) { pal_log("pal: CreateWindow failed %lu", GetLastError()); return 1; }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    pal_log("pal: window shown, entering loop");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    pal_log("pal: loop exit code=%d", (int)msg.wParam);
    return (int)msg.wParam;
}
