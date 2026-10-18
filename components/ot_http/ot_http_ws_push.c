// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What goes out into /ws: the list of live sockets, the snapshot on connection, the deltas and the
// keepalive. The second socket file rather than a continuation of ot_http_ws.c: there are the
// routes, the ticket and the lifecycle, here is the push. The file ceiling (CLAUDE.md, 350 lines)
// cuts along exactly this seam.
//
// **The shape of the frames is set by the CLIENT, not by the firmware's convenience.**
// web/src/api/ws.ts, type InboundFrame, accepts exactly three:
//
//     {"type":"state","values":{…}}   the whole state, once per connection
//     {"type":"delta","values":{…}}   what has changed
//     {"type":"ping"}                 silence, so that it can be told from a dead socket
//
// A frame of any other shape is discarded SILENTLY by isInboundFrame() in ws.ts. So "sending
// an entity document instead of a frame" is not "sending something else", it is "sending nothing":
// the page stays empty while everything is fine in the device's log.
//
// `values` is a FLAT map "key -> value": a number, true/false, an enum's option string or null.
// The same values GET /api/state hands out, printed by the same function (ot_api_render_frame),
// and not one field beyond them: the web interface has no privileged endpoint.
// ot_api.h says what went wrong while this file printed them itself. **DO NOT** add a field here
// that is not in the ot_api projection: if the socket needs it, REST is wrong.
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "lwip/sockets.h"

#include "ot_api.h"
#include "ot_http_internal.h"
#include "ot_registry.h"
#include "ot_state.h"

static const char *TAG = "ws";

// The client has agreed to receive something at least once every 15 s and to consider the socket
// dead after 35 s of silence (ws.ts, SILENCE_TIMEOUT_MS). Ten -- so that a missed tick and a server
// task busy for a few seconds still fit inside the promise. **DO NOT** raise it to fifteen "since
// fifteen is what was promised": the promise is a ceiling, not a target.
#define WS_PING_MS 10000

static const char WS_PING[] = "{\"type\":\"ping\"}";

typedef struct {
    int      fd;
    bool     snapshot;    // the full state has already gone out to this socket
    bool     unbuffered;  // the non-blocking send has already been installed
    uint32_t last_tx_ms;
} ws_client_t;

// Belongs entirely to THE SERVER TASK (see ot_http_internal.h): the pre-handshake callback adds,
// the push reads and cleans up, and both are one thread. That is why there is no mutex, and it is
// not forgetfulness.
static ws_client_t s_client[OT_HTTP_MAX_CLIENTS];

// Two flags that ANOTHER task touches -- the tick timer.
//
// s_idle: there are no sockets, there is no work to queue. Without it the tick woke the server
// task 86,400 times a day on a device nobody is connected to.
//
// s_queued: the work is already queued. It keeps ticks from piling up if the server task is busy
// for a long time; a missed tick loses nothing -- the "changed" marks wait in ot_state until
// somebody takes them.
//
// A value of either that is one tick out of date costs nothing: extra work goes out through
// prune(), and a missed tick is caught up by the next one.
static volatile bool s_idle = true;
static volatile bool s_queued;

// --- sending --------------------------------------------------------------------------

// A non-blocking send INSTEAD of the standard one. The standard one is send() with an SO_SNDTIMEO
// of 5 s from the server task: a phone that has fallen asleep with the tab open stops reading, the
// TCP window closes, and the very first frame stops the WHOLE HTTP server for five seconds. That
// does not touch the bus (it has priority 10 against httpd's 5, the bus deadline is not violated) -- what
// breaks is control.
//
// A short write here is an ERROR, not a success, and that is the main thing about this function.
// httpd_ws_send_frame_async() calls send_fn exactly once for the header and once for the body and
// does NOT write out what was left unwritten (httpd_ws.c:454-465): returning "less was written
// than was asked for" to it means sending a truncated frame and desynchronising the WebSocket
// stream for ever. We return a failure -- the caller will close the socket, the browser will
// reconnect and receive a snapshot.
//
// The price: a client whose receive buffer is full at the moment of a tick loses the connection
// instead of waiting. A frame is about two kilobytes against 5744 bytes of TCP_SND_BUF, so a full
// buffer means a client that has not been reading for seconds, not a slow network.
static int ws_send_nonblocking(httpd_handle_t hd, int fd, const char *buf, size_t len,
                               int flags)
{
    (void)hd;
    const int n = send(fd, buf, len, flags | MSG_DONTWAIT);
    if (n < 0 || (size_t)n < len)
        return HTTPD_SOCK_ERR_FAIL;
    return n;
}

static void drop(httpd_handle_t server, ws_client_t *c)
{
    // CLOSE it, do not forget it. A forgotten descriptor remains a session of the server: it
    // occupies one of the seven slots and holds buffers until the client itself falls off by TCP.
    httpd_sess_trigger_close(server, c->fd);
    c->fd = -1;
}

static bool send_text(httpd_handle_t server, ws_client_t *c, const char *payload,
                      size_t len, uint32_t now)
{
    if (!c->unbuffered && httpd_sess_set_send_override(server, c->fd,
                                                       ws_send_nonblocking) == ESP_OK)
        c->unbuffered = true;

    httpd_ws_frame_t frame = {
        .final   = true,
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len     = len,
    };
    if (httpd_ws_send_frame_async(server, c->fd, &frame) != ESP_OK) {
        drop(server, c);
        return false;
    }
    c->last_tx_ms = now;

    // **This is precisely the fix for the LRU eviction.** httpd_sess_process() advances a
    // session's counter only when the session has handled an INCOMING request (httpd_sess.c:436),
    // and the channel is one-way -- so without this line the socket's counter is frozen for ever
    // and it is always the oldest. The eighth connection (a second tab, the browser's keep-alive,
    // a captive-portal probe from a phone) would with lru_purge_enable kill exactly it.
    httpd_sess_update_lru_counter(server, c->fd);
    return true;
}

// --- rendering the frame ---------------------------------------------------------------

// The frame is ot_api's. Zero means it did not fit and there is nothing to send: snprintf's
// "required size" used as a length is how a document truncated at the buffer boundary goes out
// looking valid, right up until somebody tries to parse it.
static size_t render(char *out, size_t cap, const char *type, const uint32_t *mask)
{
    const size_t need = ot_api_render_frame(type, mask, out, cap);
    return need < cap ? need : 0;
}

// --- the socket list -------------------------------------------------------------------

// Removes descriptors behind which there is no socket any more. It asks the server rather than
// remembering itself: the closing of a connection is not reported to the handler in any way, and a
// list maintained only by successful sends would accumulate the numbers of closed sockets until
// the first failed write.
static int prune(httpd_handle_t server)
{
    int live = 0;
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++) {
        if (s_client[i].fd < 0)
            continue;
        if (httpd_ws_get_fd_info(server, s_client[i].fd) != HTTPD_WS_CLIENT_WEBSOCKET)
            s_client[i].fd = -1;
        else
            live++;
    }
    return live;
}

void ot_ws_push_forget_all(void)
{
    memset(s_client, 0, sizeof s_client);
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++)
        s_client[i].fd = -1;
    s_idle   = true;
    s_queued = false;
}

bool ot_ws_push_room(httpd_handle_t server)
{
    prune(server);
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++)
        if (s_client[i].fd < 0)
            return true;
    return false;
}

bool ot_ws_push_add(httpd_handle_t server, int fd)
{
    prune(server);
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++)
        if (s_client[i].fd == fd)
            return true;
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++)
        if (s_client[i].fd < 0) {
            // snapshot == false: the very first tick will send this socket the full state.
            s_client[i] = (ws_client_t){ .fd = fd, .last_tx_ms = ot_http_now_ms() };
            s_idle      = false;
            return true;
        }
    return false;
}

// --- the tick ---------------------------------------------------------------------------

// The "changed" marks are ONE set for all the sockets (ot_state, OT_CONSUMER_WEB), and they are
// cleared here before the frame goes out. That does not allow a change to be lost: a socket the
// delta did not reach is CLOSED right away, and the next connection starts with a snapshot. That
// is, the client either received the delta or will receive the full state -- there is no third
// outcome in which it shows a stale value indefinitely.
static void broadcast(httpd_handle_t server, uint32_t now)
{
    // A snapshot for those who have not seen one yet. A page that has just been opened must see
    // the whole state at once (see ws.ts, InboundFrame): without it a
    // second tab sits empty until every entity changes by itself, and ID 5 and ID 3 do not change
    // for hours.
    bool wanted = false;
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++)
        wanted = wanted || (s_client[i].fd >= 0 && !s_client[i].snapshot);
    if (wanted) {
        const size_t len = render(ot_http_scratch, OT_HTTP_SCRATCH_SIZE, "state", NULL);
        for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++) {
            ws_client_t *c = &s_client[i];
            if (c->fd < 0 || c->snapshot)
                continue;
            if (len == 0) {
                // The buffer is too small for the snapshot -- and it will not pass next time
                // either. Keeping the socket open would mean showing a connection over which
                // nothing will arrive.
                ESP_LOGE(TAG, "the snapshot did not fit in the buffer -- socket closed");
                drop(server, c);
            } else if (send_text(server, c, ot_http_scratch, len, now)) {
                c->snapshot = true;
            }
        }
    }

    // The delta. ONE frame per tick, not a frame per entity: after start-up there are dozens of
    // dirty entities, and dozens of sends in a row are dozens of chances to hold up the server
    // task on a slow client.
    uint32_t mask[(OT_ENTITY_COUNT + 31) / 32] = { 0 };
    bool     any = false;
    for (int i = 0; i < OT_ENTITY_COUNT; i++)
        if (ot_state_take_dirty(OT_CONSUMER_WEB, i)) {
            mask[i / 32] |= (uint32_t)1u << (i % 32);
            any = true;
        }
    if (any) {
        const size_t len = render(ot_http_scratch, OT_HTTP_SCRATCH_SIZE, "delta", mask);
        for (int i = 0; len > 0 && i < OT_HTTP_MAX_CLIENTS; i++)
            // Those who have just received a snapshot do not need the delta -- but it does them no
            // harm either: the same values written over the same keys.
            if (s_client[i].fd >= 0 && s_client[i].snapshot)
                send_text(server, &s_client[i], ot_http_scratch, len, now);
    }

    // Silence differs from a dead socket only by this frame: a TCP connection abandoned by a NAT
    // box or by an access point that has gone to sleep stays "open" for the browser indefinitely,
    // and without a ping the client would after 35 s go into an endless reconnection loop
    // (ws.ts, SILENCE_TIMEOUT_MS).
    for (int i = 0; i < OT_HTTP_MAX_CLIENTS; i++) {
        ws_client_t *c = &s_client[i];
        if (c->fd >= 0 && (uint32_t)(now - c->last_tx_ms) >= WS_PING_MS)
            send_text(server, c, WS_PING, sizeof WS_PING - 1, now);
    }
}

// Executed IN THE SERVER TASK: httpd_queue_work() puts the work into the control socket, and it is
// carried out by the same thread as the route handlers.
//
// **This is precisely the condition under which ot_http_scratch is legitimate.** The shared buffer
// is not re-entrant, and it is justified by exactly the fact that ONE task serves all the sockets
// (ot_http_internal.h). The timer callback runs in ANOTHER task and therefore writes not one byte
// into the buffer -- it only queues the work. **DO NOT** move the rendering into the timer
// callback to save one message: it would end up next to a handler that is rendering a response,
// and the corruption of the buffer would look like broken JSON at the client rather than like a
// race.
static void ws_work(void *arg)
{
    httpd_handle_t server = (httpd_handle_t)arg;
    s_queued = false;
    if (server == NULL)
        return;

    // Nobody is there -- we do NOT take the marks and we queue no more ticks until a new
    // handshake.
    if (prune(server) == 0) {
        s_idle = true;
        return;
    }
    broadcast(server, ot_http_now_ms());
}

// Called FROM THE TIMER TASK and does nothing more: no reading of state, no rendering, no walking
// of the socket list. Everything meaningful is in the server task.
void ot_ws_push_queue(httpd_handle_t server)
{
    if (server == NULL || s_idle || s_queued)
        return;
    s_queued = true;
    if (httpd_queue_work(server, ws_work, server) != ESP_OK)
        s_queued = false;
}
