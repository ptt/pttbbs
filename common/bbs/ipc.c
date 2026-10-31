#include "cmbbs.h"
#include "cmsys.h"
#include "common.h"
#include "ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define IPC_BACKOFF_INIT_SEC  1
#define IPC_BACKOFF_MAX_SEC   30
/* Extra random spread (seconds) added to every reconnect delay. When ipc.svc
 * restarts, every mbbsd (tens of thousands) sees EOF at the same instant;
 * without a wide spread they would all reconnect and re-register within the
 * same second, overflowing the listen backlog and spiking the daemon. */
#define IPC_RECONNECT_SPREAD_SEC  4
/* ACK timeout for SUB: user initiated, so tolerate a busy host / daemon. */
#define IPC_SUB_ACK_TIMEOUT_MS    300

static int g_ipc_fd = -1;
static uint32_t g_ipc_seq = 0;

static time_t s_next_reconnect_time = 0;
static int s_backoff_sec = IPC_BACKOFF_INIT_SEC;
static int s_retry_count = 0;
static ipc_reconnect_cb_t s_reconnect_cb = NULL;

typedef struct ipc_pending_msg {
    ipc_hdr_t hdr;
    char payload[1024];
    struct ipc_pending_msg *next;
} ipc_pending_msg_t;

static ipc_pending_msg_t *s_pending_head = NULL;
static ipc_pending_msg_t *s_pending_tail = NULL;

static void
ipc_clear_pending_queue(void)
{
    ipc_pending_msg_t *cur = s_pending_head;
    while (cur) {
        ipc_pending_msg_t *next = cur->next;
        free(cur);
        cur = next;
    }
    s_pending_head = NULL;
    s_pending_tail = NULL;
}

static void
ipc_enqueue_pending(const ipc_hdr_t *hdr, const void *payload, size_t len)
{
    ipc_pending_msg_t *msg = (ipc_pending_msg_t *)malloc(sizeof(ipc_pending_msg_t));
    if (!msg)
        return;
    memset(msg, 0, sizeof(*msg));
    msg->hdr = *hdr;
    if (payload && len > 0) {
        size_t cpy = len > sizeof(msg->payload) ? sizeof(msg->payload) : len;
        memcpy(msg->payload, payload, cpy);
    }
    msg->next = NULL;
    if (s_pending_tail) {
        s_pending_tail->next = msg;
        s_pending_tail = msg;
    } else {
        s_pending_head = s_pending_tail = msg;
    }
}

int
ipc_has_pending(void)
{
    return s_pending_head != NULL;
}

static int
ipc_write_exact(int fd, const void *buf, size_t len, int timeout_ms)
{
    if (fd < 0 || !buf)
        return -1;

    const char *p = (const char *)buf;
    size_t total = 0;
    struct pollfd pfd = { .fd = fd, .events = POLLOUT };

    while (total < len) {
        int pr;
        do {
            pr = poll(&pfd, 1, timeout_ms);
        } while (pr < 0 && errno == EINTR);

        if (pr <= 0 || !(pfd.revents & POLLOUT))
            return -1;

        ssize_t n = write(fd, p + total, len - total);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

static int
ipc_read_exact(int fd, void *buf, size_t len, int timeout_ms)
{
    if (fd < 0 || !buf)
        return -1;

    char *p = (char *)buf;
    size_t total = 0;
    struct pollfd pfd = { .fd = fd, .events = POLLIN };

    while (total < len) {
        int pr;
        do {
            pr = poll(&pfd, 1, timeout_ms);
        } while (pr < 0 && errno == EINTR);

        if (pr <= 0 || !(pfd.revents & (POLLIN | POLLHUP)))
            return -1;

        ssize_t n = read(fd, p + total, len - total);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

/* Setting a reconnect callback also enables automatic reconnection (see
 * ipc_check_reconnect). mbbsd does so in ipc_init() after login; before that
 * there is no session to register, so a reconnect would only create an
 * unregistered link that can never receive anything. */
void
ipc_set_reconnect_callback(ipc_reconnect_cb_t cb)
{
    s_reconnect_cb = cb;
}

int
ipc_reconnect_enabled(void)
{
    return s_reconnect_cb != NULL;
}

int
ipc_get_fd(void)
{
    return g_ipc_fd;
}

int
ipc_is_connected(void)
{
    return g_ipc_fd >= 0;
}

/* Schedule the next reconnect attempt using the current backoff plus random
 * jitter, then grow the backoff for the attempt after that. The backoff is
 * only reset after a fully successful (re)connect + registration. */
static void
ipc_schedule_reconnect(void)
{
    int delay = s_backoff_sec +
        (int)arc4random_uniform((uint32_t)(s_backoff_sec + IPC_RECONNECT_SPREAD_SEC + 1));
    s_next_reconnect_time = time(NULL) + delay;
    s_retry_count++;
    s_backoff_sec *= 2;
    if (s_backoff_sec > IPC_BACKOFF_MAX_SEC)
        s_backoff_sec = IPC_BACKOFF_MAX_SEC;
}

static void
ipc_reset_backoff(void)
{
    s_backoff_sec = IPC_BACKOFF_INIT_SEC;
    s_retry_count = 0;
    s_next_reconnect_time = 0;
}

void
ipc_disconnect(void)
{
    if (g_ipc_fd >= 0) {
        close(g_ipc_fd);
        g_ipc_fd = -1;
    }
    ipc_clear_pending_queue();
    ipc_schedule_reconnect();
}

int
ipc_connect(void)
{
    if (g_ipc_fd >= 0)
        return g_ipc_fd;

    const char *sock_path = GET_SVC_SOCKET(ipc);
    int fd = toconnect3(sock_path, 0, 50000);
    if (fd < 0) {
        return -1;
    }

    /* Make socket non-blocking for event-driven polling in main loop */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    g_ipc_fd = fd;
    return g_ipc_fd;
}

int
ipc_read_ack(int fd, uint32_t expected_seq, int timeout_ms)
{
    struct timeval start_tv, now_tv;
    gettimeofday(&start_tv, NULL);
    const int total_ms = timeout_ms;

    while (timeout_ms > 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr;
        do {
            pr = poll(&pfd, 1, timeout_ms);
        } while (pr < 0 && errno == EINTR);

        if (pr <= 0)
            return -1;

        ipc_hdr_t hdr;
        if (ipc_read_exact(fd, &hdr, sizeof(hdr), timeout_ms) != 0) {
            ipc_disconnect();
            return -1;
        }

        if (hdr.magic != IPC_MAGIC || hdr.version != IPC_VERSION) {
            ipc_disconnect();
            return -1;
        }

        if (hdr.type == IPC_TYPE_ACK) {
            ipc_ack_t ack;
            memset(&ack, 0, sizeof(ack));
            if (hdr.len > 0) {
                size_t to_read = hdr.len > sizeof(ack) ? sizeof(ack) : hdr.len;
                if (ipc_read_exact(fd, &ack, to_read, timeout_ms) != 0) {
                    ipc_disconnect();
                    return -1;
                }
                int remaining = (int)(hdr.len - to_read);
                char dummy[64];
                while (remaining > 0) {
                    int chunk = remaining > (int)sizeof(dummy) ? (int)sizeof(dummy) : remaining;
                    if (ipc_read_exact(fd, dummy, chunk, timeout_ms) != 0) {
                        ipc_disconnect();
                        return -1;
                    }
                    remaining -= chunk;
                }
            }

            if (expected_seq > 0 && hdr.seq != expected_seq) {
                /* Discard mismatched/old ACK and keep waiting for expected seq */
                gettimeofday(&now_tv, NULL);
                long elapsed = (now_tv.tv_sec - start_tv.tv_sec) * 1000 +
                               (now_tv.tv_usec - start_tv.tv_usec) / 1000;
                if (elapsed >= total_ms)
                    return -1;
                timeout_ms = total_ms - (int)elapsed;
                continue;
            }

            return ack.code;
        }

        /* Another asynchronous packet arrived while waiting for ACK.
         * Read payload and enqueue into pending queue so it is never dropped! */
        char payload[1024];
        size_t to_read = hdr.len > sizeof(payload) ? sizeof(payload) : hdr.len;
        if (to_read > 0) {
            if (ipc_read_exact(fd, payload, to_read, timeout_ms) != 0) {
                ipc_disconnect();
                return -1;
            }
        }
        int remaining = (int)(hdr.len - to_read);
        char dummy[64];
        while (remaining > 0) {
            int chunk = remaining > (int)sizeof(dummy) ? (int)sizeof(dummy) : remaining;
            if (ipc_read_exact(fd, dummy, chunk, timeout_ms) != 0) {
                ipc_disconnect();
                return -1;
            }
            remaining -= chunk;
        }

        ipc_enqueue_pending(&hdr, payload, to_read);

        gettimeofday(&now_tv, NULL);
        long elapsed = (now_tv.tv_sec - start_tv.tv_sec) * 1000 +
                       (now_tv.tv_usec - start_tv.tv_usec) / 1000;
        if (elapsed >= total_ms)
            return -1;
        timeout_ms = total_ms - (int)elapsed;
    }
    return -1;
}

int
ipc_try_reconnect(void)
{
    if (g_ipc_fd >= 0)
        return g_ipc_fd;

    const char *sock_path = GET_SVC_SOCKET(ipc);
    int fd = toconnect3(sock_path, 0, 50000);
    if (fd < 0) {
        /* Reconnect failed, increase backoff */
        ipc_schedule_reconnect();
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    g_ipc_fd = fd;

    /* Notify callback to restore application state and re-attach to event
     * loop. The callback may fail (e.g. registration timed out) and call
     * ipc_disconnect(), which already scheduled the next attempt with a
     * grown backoff: do not treat that as success, or we would retry on
     * every event loop iteration. */
    if (s_reconnect_cb) {
        s_reconnect_cb(g_ipc_fd);
        if (g_ipc_fd < 0)
            return -1;
    }

    /* Success: reset backoff state */
    ipc_reset_backoff();
    return g_ipc_fd;
}

int
ipc_check_reconnect(void)
{
    if (g_ipc_fd >= 0)
        return g_ipc_fd;
    if (!ipc_reconnect_enabled())
        return -1;
    time_t now = time(NULL);
    if (now < s_next_reconnect_time)
        return -1;
    return ipc_try_reconnect();
}

int
ipc_register(const char *userid, pid_t pid, int uslot)
{
    if (g_ipc_fd < 0 && ipc_connect() < 0)
        return -1;

    ipc_register_t reg;
    memset(&reg, 0, sizeof(reg));
    STRLCPY(reg.userid, userid);
    reg.pid = (uint32_t)pid;
    reg.uslot = (int32_t)uslot;

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_REGISTER,
        .seq = seq,
        .len = (uint32_t)sizeof(reg),
    };

    if (ipc_write_exact(g_ipc_fd, &hdr, sizeof(hdr), 100) != 0 ||
        ipc_write_exact(g_ipc_fd, &reg, sizeof(reg), 100) != 0) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, 100);
}

int
ipc_wmsg_send(const char *to_userid, const char *from_userid,
              pid_t from_pid, int from_uslot, int msgmode, const char *text)
{
    if (!to_userid || !*to_userid || !text)
        return -1;

    if (g_ipc_fd < 0 && ipc_check_reconnect() < 0)
        return -1;

    ipc_wmsg_t wmsg;
    memset(&wmsg, 0, sizeof(wmsg));
    STRLCPY(wmsg.to_userid, to_userid);
    if (from_userid)
        STRLCPY(wmsg.from_userid, from_userid);
    wmsg.from_pid = (uint32_t)from_pid;
    wmsg.from_uslot = (int32_t)from_uslot;
    wmsg.msgmode = msgmode;
    wmsg.timestamp = (uint32_t)time(NULL);
    STRLCPY(wmsg.text, text);

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_WMSG_SEND,
        .seq = seq,
        .len = (uint32_t)sizeof(wmsg),
    };

    if (ipc_write_exact(g_ipc_fd, &hdr, sizeof(hdr), 100) != 0 ||
        ipc_write_exact(g_ipc_fd, &wmsg, sizeof(wmsg), 100) != 0) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, 50);
}

int
ipc_talk_send(const char *to_userid, const char *from_userid,
              pid_t from_pid, int from_uslot, int sig, int is_urgent)
{
    if (!to_userid || !*to_userid || !from_userid)
        return -1;

    if (g_ipc_fd < 0 && ipc_check_reconnect() < 0)
        return -1;

    ipc_talk_t talk;
    memset(&talk, 0, sizeof(talk));
    STRLCPY(talk.to_userid, to_userid);
    STRLCPY(talk.from_userid, from_userid);
    talk.from_pid = (uint32_t)from_pid;
    talk.from_uslot = (int32_t)from_uslot;
    talk.sig = (int32_t)sig;
    talk.is_urgent = (int32_t)is_urgent;

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_TALK_REQ,
        .seq = seq,
        .len = (uint32_t)sizeof(talk),
    };

    if (ipc_write_exact(g_ipc_fd, &hdr, sizeof(hdr), 100) != 0 ||
        ipc_write_exact(g_ipc_fd, &talk, sizeof(talk), 100) != 0) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, 50);
}

int
ipc_chat_connect(void)
{
    return toconnect3(IPC_XCHAT_SOCK_PATH, 0, 50000);
}

int
ipc_sub(uint32_t bid, aidu_t aid)
{
    aidu_t raw_aid = aidu_raw(aid);
    if (bid == 0 || raw_aid == 0)
        return -1;

    if (g_ipc_fd < 0 && ipc_check_reconnect() < 0)
        return -1;

    ipc_pubsub_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.bid = bid;
    ps.timestamp = (uint32_t)time(NULL);
    ps.aid = raw_aid;

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_SUB,
        .seq = seq,
        .len = (uint32_t)sizeof(ps),
    };

    if (ipc_write_exact(g_ipc_fd, &hdr, sizeof(hdr), 100) != 0 ||
        ipc_write_exact(g_ipc_fd, &ps, sizeof(ps), 100) != 0) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, IPC_SUB_ACK_TIMEOUT_MS);
}

/* Fire-and-forget (seq = 0, no ACK): callers unsubscribe when leaving a
 * post, which must not block on the daemon. Stream ordering guarantees the
 * UNSUB is applied before any later SUB on the same connection. Returns 0 if
 * the request was sent (or there is no connection to unsubscribe from). */
int
ipc_unsub(uint32_t bid, aidu_t aid)
{
    aidu_t raw_aid = aidu_raw(aid);
    if (bid == 0 || raw_aid == 0)
        return -1;

    if (g_ipc_fd < 0)
        return 0;

    ipc_pubsub_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.bid = bid;
    ps.timestamp = (uint32_t)time(NULL);
    ps.aid = raw_aid;

    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_UNSUB,
        .seq = 0,
        .len = (uint32_t)sizeof(ps),
    };

    if (ipc_write_exact(g_ipc_fd, &hdr, sizeof(hdr), 100) != 0 ||
        ipc_write_exact(g_ipc_fd, &ps, sizeof(ps), 100) != 0) {
        ipc_disconnect();
        return -1;
    }

    return 0;
}

int
ipc_pub(uint32_t bid, aidu_t aid, const char *text)
{
    aidu_t raw_aid = aidu_raw(aid);
    if (bid == 0 || raw_aid == 0 || !text)
        return -1;

    if (g_ipc_fd < 0 && ipc_check_reconnect() < 0)
        return -1;

    ipc_pubsub_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.bid = bid;
    ps.timestamp = (uint32_t)time(NULL);
    ps.aid = raw_aid;
    STRLCPY(ps.text, text);
    size_t len = strlen(ps.text);
    while (len > 0 && (ps.text[len - 1] == '\n' || ps.text[len - 1] == '\r'))
        ps.text[--len] = '\0';

    /* Fire-and-forget: seq = 0 signals to daemon not to send ACK */
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_PUB,
        .seq = 0,
        .len = (uint32_t)sizeof(ps),
    };

    if (ipc_write_exact(g_ipc_fd, &hdr, sizeof(hdr), 100) != 0 ||
        ipc_write_exact(g_ipc_fd, &ps, sizeof(ps), 100) != 0) {
        ipc_disconnect();
        return -1;
    }

    return 0;
}

int
ipc_read_packet(int fd, ipc_hdr_t *hdr, void *payload_buf, size_t bufsz)
{
    if (fd < 0 || !hdr)
        return -1;

    /* Check if there is an enqueued pending packet first */
    if (s_pending_head) {
        ipc_pending_msg_t *msg = s_pending_head;
        *hdr = msg->hdr;
        if (payload_buf && bufsz > 0 && msg->hdr.len > 0) {
            size_t cpy = msg->hdr.len > bufsz ? bufsz : msg->hdr.len;
            memcpy(payload_buf, msg->payload, cpy);
        }
        s_pending_head = msg->next;
        if (!s_pending_head)
            s_pending_tail = NULL;
        free(msg);
        return 1;
    }

    /* Check if packet header has arrived */
    ssize_t n = read(fd, hdr, sizeof(*hdr));
    if (n <= 0) {
        return (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) ? 0 : -1;
    }
    if (n < (ssize_t)sizeof(*hdr)) {
        /* Read remaining header bytes with timeout */
        if (ipc_read_exact(fd, ((char *)hdr) + n, sizeof(*hdr) - n, 100) != 0) {
            ipc_disconnect();
            return -1;
        }
    }

    if (hdr->magic != IPC_MAGIC || hdr->version != IPC_VERSION) {
        ipc_disconnect();
        return -1;
    }

    if (hdr->len > 0) {
        size_t to_read = hdr->len;
        if (payload_buf && bufsz > 0) {
            if (to_read > bufsz)
                to_read = bufsz;
            if (ipc_read_exact(fd, payload_buf, to_read, 100) != 0) {
                ipc_disconnect();
                return -1;
            }
        } else {
            to_read = 0;
        }

        /* Drain any remaining payload bytes */
        int remaining = (int)(hdr->len - to_read);
        char dummy[64];
        while (remaining > 0) {
            int chunk = remaining > (int)sizeof(dummy) ? (int)sizeof(dummy) : remaining;
            if (ipc_read_exact(fd, dummy, chunk, 100) != 0) {
                ipc_disconnect();
                return -1;
            }
            remaining -= chunk;
        }
    }
    return 1;
}

int
ipc_chess_create_session(const char *caller, const char *callee, int sig)
{
    int fd = toconnect3(IPC_CHESS_SOCK_PATH, 0, 50000);
    if (fd < 0)
        return -1;

    char cmdbuf[128];
    int len = snprintf(cmdbuf, sizeof(cmdbuf), "CREATE %s %s %d\n", caller, callee, sig);
    if (towrite(fd, cmdbuf, len) != len) {
        close(fd);
        return -1;
    }

    char resp[16];
    int n = svc_read(fd, resp, sizeof(resp) - 1, 3000);
    if (n <= 0 || strncmp(resp, "OK", 2) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int
ipc_chess_join_session(const char *callee, const char *caller)
{
    int fd = toconnect3(IPC_CHESS_SOCK_PATH, 0, 50000);
    if (fd < 0)
        return -1;

    char cmdbuf[128];
    int len = snprintf(cmdbuf, sizeof(cmdbuf), "JOIN %s %s\n", callee, caller);
    if (towrite(fd, cmdbuf, len) != len) {
        close(fd);
        return -1;
    }

    char resp[16];
    int n = svc_read(fd, resp, sizeof(resp) - 1, 3000);
    if (n <= 0 || strncmp(resp, "OK", 2) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int
ipc_query_stats(ipc_stats_t *stats)
{
    if (!stats)
        return -1;

    int fd = g_ipc_fd;
    int need_close = 0;
    if (fd < 0) {
        const char *sock_path = GET_SVC_SOCKET(ipc);
        fd = toconnect3(sock_path, 0, 50000);
        if (fd < 0)
            return -1;
        need_close = 1;
    }

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t req_hdr;
    memset(&req_hdr, 0, sizeof(req_hdr));
    req_hdr.magic = IPC_MAGIC;
    req_hdr.version = IPC_VERSION;
    req_hdr.type = IPC_TYPE_STATS_REQ;
    req_hdr.seq = seq;
    req_hdr.len = 0;

    if (ipc_write_exact(fd, &req_hdr, sizeof(req_hdr), 1000) != 0) {
        if (need_close)
            close(fd);
        else
            ipc_disconnect();
        return -1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    if (poll(&pfd, 1, 1000) <= 0) {
        if (need_close)
            close(fd);
        return -1;
    }

    ipc_hdr_t resp_hdr;
    int r = ipc_read_packet(fd, &resp_hdr, stats, sizeof(*stats));
    if (need_close)
        close(fd);

    if (r < 0 || resp_hdr.type != IPC_TYPE_STATS_RESP)
        return -1;

    return 0;
}
