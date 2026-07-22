/* net/nettest_main.c — milestone 4 bring-up: exercise pal sockets + core/http +
 * core/json + core/model end to end on real NT4, against a plain-HTTP mock
 * server on the host (10.0.2.2, reachable through QEMU's user-net gateway).
 * No window; all output goes to COM1 via pal_log so the host can tail it.
 * TLS comes later — this proves everything *except* the transport security. */
#include <windows.h>
#include <stdlib.h> /* free — provided by pal/nt4_crt.c */
#include "pal.h"
#include "net.h"
#include "http.h"
#include "model.h"

#define MOCK_HOST "10.0.2.2"
#define MOCK_PORT 8080
#define RECV_CAP  16384

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    pal_log("=== nettest boot ===");

    if (pal_net_init() != 0) { pal_log("nettest: net init failed"); return 1; }

    PalSocket s = pal_tcp_connect(MOCK_HOST, MOCK_PORT);
    if (s < 0) {
        pal_log("nettest: connect to %s:%d failed", MOCK_HOST, MOCK_PORT);
        pal_net_shutdown();
        return 1;
    }

    HttpHeader hdrs[] = { { "Connection", "close" } };
    int req_len = 0;
    char *req = http_build_request("GET", MOCK_HOST, "/me/media?fields=id,caption,media_url",
                                   hdrs, 1, NULL, 0, &req_len);
    if (!req) { pal_log("nettest: build_request failed"); pal_tcp_close(s); pal_net_shutdown(); return 1; }

    pal_log("nettest: sending %d-byte request", req_len);
    int send_rc = pal_tcp_send_all(s, req, req_len);
    free(req);
    if (send_rc != 0) { pal_log("nettest: send failed"); pal_tcp_close(s); pal_net_shutdown(); return 1; }

    /* Connection: close, so read until the peer closes (or our buffer fills). */
    char *buf = (char *)malloc(RECV_CAP);
    int total = 0;
    if (buf) {
        for (;;) {
            int n = pal_tcp_recv(s, buf + total, RECV_CAP - total);
            if (n <= 0) break;
            total += n;
            if (total >= RECV_CAP) break;
        }
    }
    pal_tcp_close(s);
    pal_net_shutdown();
    pal_log("nettest: received %d bytes", total);

    if (!buf || total <= 0) { pal_log("nettest: no data received"); free(buf); return 1; }

    HttpResponse resp;
    if (http_parse_response(buf, total, &resp) != 0) {
        pal_log("nettest: http_parse_response failed");
        free(buf);
        return 1;
    }
    pal_log("nettest: status=%d body_len=%d", resp.status, resp.body_len);

    Feed *feed = feed_from_graph_json(resp.body, resp.body_len);
    if (!feed) {
        pal_log("nettest: feed_from_graph_json failed");
    } else {
        pal_log("nettest: parsed %d posts, cursor=%s", feed->count,
                feed->next_cursor ? feed->next_cursor : "(none)");
        for (int i = 0; i < feed->count; i++) {
            Post *p = &feed->posts[i];
            pal_log("nettest: post[%d] id=%s user=%s likes=%d", i,
                    p->id ? p->id : "?", p->username ? p->username : "?", (int)p->like_count);
        }
        feed_free(feed);
    }

    http_response_free(&resp);
    free(buf);
    pal_log("=== nettest done ===");
    ExitProcess(0);
    return 0;
}
