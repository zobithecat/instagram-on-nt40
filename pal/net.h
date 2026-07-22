/* pal/net.h — minimal blocking TCP sockets (Win32/NT4: Winsock 1.1, wsock32.dll).
 *
 * Winsock 1.1 (not 2) is what NT4 RTM ships natively — no ws2_32.dll dependency,
 * consistent with the freestanding, NT4-native-DLLs-only build. Blocking calls
 * keep this first cut simple; the Graph API client this feeds is not latency-
 * sensitive on a single-purpose feed reader.
 */
#ifndef PAL_NET_H
#define PAL_NET_H

typedef long PalSocket; /* >= 0 is a valid socket; -1 is invalid */

/* Bring the network subsystem up/down (WSAStartup/WSACleanup on Win32).
 * pal_net_init returns 0 on success, -1 on failure. Call once at startup. */
int  pal_net_init(void);
void pal_net_shutdown(void);

/* Blocking connect to host:port. `host` may be a dotted IPv4 address or a
 * hostname (resolved via gethostbyname). Returns a valid socket, or -1. */
PalSocket pal_tcp_connect(const char *host, unsigned short port);

/* Send exactly `len` bytes, looping internally as needed. 0 on success, -1 on
 * error (connection dropped mid-send counts as an error). */
int pal_tcp_send_all(PalSocket s, const void *buf, int len);

/* Receive up to `cap` bytes into buf. Returns bytes read (>0), 0 on an orderly
 * close by the peer, or -1 on error. */
int pal_tcp_recv(PalSocket s, void *buf, int cap);

void pal_tcp_close(PalSocket s);

#endif /* PAL_NET_H */
