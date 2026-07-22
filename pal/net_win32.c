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

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR) {
        pal_log("pal_tcp_connect: connect(%s:%u) failed %d", host, port, WSAGetLastError());
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
