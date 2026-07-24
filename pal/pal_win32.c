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
    pal_height_fn content_height;
    pal_click_fn  click;    /* may be NULL -- no interactive elements */
    void         *user;
    HBITMAP       dib;      /* DIB section = compositor backbuffer */
    HDC           mem_dc;   /* memory DC holding the DIB */
    Surface       fb;       /* aliases the DIB's pixels (top-down) */
    int           w, h;
    int           chrome_h; /* fixed top+bottom UI height (non-scrolling) */
    int           scroll_y; /* current vertical scroll offset (px) */
} PalWindow;

/* pal_log / pal_read_file live in pal/pal_common_win32.c (kernel32/user32
 * only, no gdi32) so non-GUI binaries can link them without pulling in the
 * window/DIB code below. */

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
    if (pw->render) pw->render(&pw->fb, pw->scroll_y, pw->user);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    BitBlt(dc, 0, 0, pw->w, pw->h, pw->mem_dc, 0, 0, SRCCOPY);
    EndPaint(hwnd, &ps);
}

/* Max scroll offset for the current client size (0 if nothing to scroll). */
static int max_scroll(PalWindow *pw) {
    if (!pw->content_height) return 0;
    int content = pw->content_height(pw->w, pw->user);
    int view = pw->h - pw->chrome_h;
    if (view < 0) view = 0;
    int m = content - view;
    return m > 0 ? m : 0;
}

/* Sync the OS scrollbar range/thumb to the content and clamp scroll_y. */
static void sync_scrollbar(PalWindow *pw, HWND hwnd) {
    if (!pw->content_height) return;
    int content = pw->content_height(pw->w, pw->user);
    int view = pw->h - pw->chrome_h;
    if (view < 1) view = 1;
    int m = max_scroll(pw);
    if (pw->scroll_y > m) pw->scroll_y = m;
    if (pw->scroll_y < 0) pw->scroll_y = 0;
    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = content > 0 ? content - 1 : 0;
    si.nPage  = (UINT)view;
    si.nPos   = pw->scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

/* Scroll by `delta` px, clamp, update the scrollbar, and repaint if moved. */
static void scroll_by(PalWindow *pw, HWND hwnd, int delta) {
    int m = max_scroll(pw);
    int ny = pw->scroll_y + delta;
    if (ny > m) ny = m;
    if (ny < 0) ny = 0;
    if (ny == pw->scroll_y) return;
    pw->scroll_y = ny;
    SetScrollPos(hwnd, SB_VERT, ny, TRUE);
    InvalidateRect(hwnd, NULL, FALSE);
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
        sync_scrollbar(pw, hwnd);
        return 0;
    }
    case WM_SIZE:
        if (pw) {
            HDC wdc = GetDC(hwnd);
            create_backbuffer(pw, wdc, LOWORD(lp), HIWORD(lp));
            ReleaseDC(hwnd, wdc);
            sync_scrollbar(pw, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_VSCROLL:
        if (pw) {
            int m = max_scroll(pw);
            int page = pw->h - pw->chrome_h - 20; if (page < 40) page = 40;
            int y = pw->scroll_y;
            switch (LOWORD(wp)) {
            case SB_LINEUP:   y -= 40; break;
            case SB_LINEDOWN: y += 40; break;
            case SB_PAGEUP:   y -= page; break;
            case SB_PAGEDOWN: y += page; break;
            case SB_TOP:      y = 0; break;
            case SB_BOTTOM:   y = m; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si; ZeroMemory(&si, sizeof(si));
                si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si);
                y = si.nTrackPos;
                break; }
            }
            if (y > m) y = m;
            if (y < 0) y = 0;
            if (y != pw->scroll_y) {
                pw->scroll_y = y;
                SetScrollPos(hwnd, SB_VERT, y, TRUE);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (pw) {
            int page = pw->h - pw->chrome_h - 20; if (page < 40) page = 40;
            switch (wp) {
            case VK_UP:    scroll_by(pw, hwnd, -40);      break;
            case VK_DOWN:  scroll_by(pw, hwnd, 40);       break;
            case VK_PRIOR: scroll_by(pw, hwnd, -page);    break;
            case VK_NEXT:  scroll_by(pw, hwnd, page);     break;
            case VK_HOME:  scroll_by(pw, hwnd, -(1 << 30)); break;
            case VK_END:   scroll_by(pw, hwnd, 1 << 30);    break;
            }
        }
        return 0;
    case WM_MOUSEWHEEL: /* NT4 SP3+ / IntelliMouse; harmless if never sent */
        if (pw) scroll_by(pw, hwnd, -((short)HIWORD(wp) / WHEEL_DELTA) * 48);
        return 0;
    case WM_LBUTTONDOWN:
        if (pw && pw->click) {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            if (pw->click(x, y, pw->scroll_y, pw->w, pw->user)) {
                /* content likely changed height (a card expanded/collapsed) --
                 * re-sync the scrollbar range/clamp and repaint, same as a
                 * resize does. */
                sync_scrollbar(pw, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
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

int pal_run_window(const char *title, int w, int h, int chrome_h,
                   pal_render_fn render, pal_height_fn content_height,
                   pal_click_fn click, void *user) {
    static PalWindow pw;
    ZeroMemory(&pw, sizeof(pw));
    pw.render         = render;
    pw.content_height = content_height;
    pw.click          = click;
    pw.chrome_h       = chrome_h;
    pw.user           = user;

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
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_VSCROLL;
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
