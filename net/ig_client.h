/* net/ig_client.h — fetch the real Instagram home feed via the private
 * mobile API, for use from the GUI app (ui/main.c), reusing the exact request
 * assembly proven out headlessly in net/ig_feed_main.c. */
#ifndef NET_IG_CLIENT_H
#define NET_IG_CLIENT_H

#include "model_private.h"

/* POST feed/timeline/ against i.instagram.com using the transferred session
 * (vm/ig_session_private.h) and parse the response. Returns a PrivateFeed*
 * (caller private_feed_free()s) on success, or NULL on any connect/TLS/HTTP/
 * non-200/parse failure — callers should fall back to demo content. */
PrivateFeed *ig_fetch_timeline(void);

#endif /* NET_IG_CLIENT_H */
