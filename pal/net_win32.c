/* pal/net_win32.c — see net.h. Winsock 1.1 (wsock32.dll), the stack NT4 RTM
 * ships natively. WIN32_LEAN_AND_MEAN + explicit <winsock.h> keeps windows.h
 * from pulling in winsock2.h, which would conflict with these 1.1 symbols. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>

#include "net.h"
#include "pal.h" /* pal_log */

int pal_net_init(void) {
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(1, 1), &wsa);
    if (rc != 0) { pal_log("pal_net_init: WSAStartup failed %d", rc); return -1; }
    return 0;
}

void pal_net_shutdown(void) {
    WSACleanup();
}

/* dotted IPv4 first (no DNS round-trip), else resolve via gethostbyname. */
static unsigned long resolve_host(const char *host) {
    unsigned long addr = inet_addr(host);
    if (addr != INADDR_NONE) return addr;

    struct hostent *he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) {
        pal_log("resolve_host: cannot resolve %s (err %d)", host, WSAGetLastError());
        return INADDR_NONE;
    }
    unsigned long result;
    /* h_addr_list[0] is 4 bytes of network-order IPv4; avoid an alignment-
     * unsafe pointer cast by copying byte-for-byte. */
    for (int i = 0; i < 4; i++) ((unsigned char *)&result)[i] = (unsigned char)he->h_addr_list[0][i];
    return result;
}

/* Blocking connect()/send()/recv() have no built-in timeout: a stalled peer
 * (or a route that black-holes instead of RSTing) hangs the calling thread
 * forever. This app has no worker thread -- a hang here freezes the whole
 * GUI before its window even exists -- so every socket gets an explicit
 * bound: a non-blocking connect() + select() for the connect phase, and
 * SO_RCVTIMEO/SO_SNDTIMEO (both take a DWORD ms on Winsock, unlike POSIX's
 * timeval) for all I/O after that. */
#define PAL_NET_TIMEOUT_MS 15000

static int connect_with_timeout(SOCKET s, const struct sockaddr_in *sin) {
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    int rc = connect(s, (const struct sockaddr *)sin, sizeof(*sin));
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) return -1;

    if (rc == SOCKET_ERROR) {
        fd_set wfds, efds;
        FD_ZERO(&wfds); FD_SET(s, &wfds);
        FD_ZERO(&efds); FD_SET(s, &efds);
        struct timeval tv = { PAL_NET_TIMEOUT_MS / 1000, (PAL_NET_TIMEOUT_MS % 1000) * 1000 };
        int n = select(0, NULL, &wfds, &efds, &tv);
        if (n <= 0 || !FD_ISSET(s, &wfds)) return -1; /* timed out, or failed */
    }

    u_long block = 0;
    ioctlsocket(s, FIONBIO, &block);

    DWORD timeout_ms = PAL_NET_TIMEOUT_MS;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    return 0;
}

PalSocket pal_tcp_connect(const char *host, unsigned short port) {
    unsigned long addr = resolve_host(host);
    if (addr == INADDR_NONE) return -1;

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        pal_log("pal_tcp_connect: socket() failed %d", WSAGetLastError());
        return -1;
    }

    struct sockaddr_in sin;
    ZeroMemory(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port);
    sin.sin_addr.s_addr = addr;

    if (connect_with_timeout(s, &sin) != 0) {
        pal_log("pal_tcp_connect: connect(%s:%u) failed/timed out (%d)", host, port, WSAGetLastError());
        closesocket(s);
        return -1;
    }
    pal_log("pal_tcp_connect: connected to %s:%u", host, port);
    return (PalSocket)s;
}

int pal_tcp_send_all(PalSocket s, const void *buf, int len) {
    const char *p = (const char *)buf;
    int left = len;
    while (left > 0) {
        int n = send((SOCKET)s, p, left, 0);
        if (n == SOCKET_ERROR || n == 0) {
            pal_log("pal_tcp_send_all: send() failed %d", WSAGetLastError());
            return -1;
        }
        p += n;
        left -= n;
    }
    return 0;
}

int pal_tcp_recv(PalSocket s, void *buf, int cap) {
    int n = recv((SOCKET)s, (char *)buf, cap, 0);
    if (n == SOCKET_ERROR) {
        pal_log("pal_tcp_recv: recv() failed %d", WSAGetLastError());
        return -1;
    }
    return n;
}

void pal_tcp_close(PalSocket s) {
    closesocket((SOCKET)s);
}
