#include "cmbbs.h"
#include "cmsys.h"
#include "common.h"
#include "ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define IPC_BACKOFF_INIT_SEC  1
#define IPC_BACKOFF_MAX_SEC   30

static int g_ipc_fd = -1;
static uint32_t g_ipc_seq = 0;

static time_t s_next_reconnect_time = 0;
static int s_backoff_sec = IPC_BACKOFF_INIT_SEC;
static int s_retry_count = 0;
static ipc_reconnect_cb_t s_reconnect_cb = NULL;

void
ipc_set_reconnect_callback(ipc_reconnect_cb_t cb)
{
    s_reconnect_cb = cb;
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

void
ipc_disconnect(void)
{
    if (g_ipc_fd >= 0) {
        close(g_ipc_fd);
        g_ipc_fd = -1;
    }
    s_backoff_sec = IPC_BACKOFF_INIT_SEC;
    s_retry_count = 0;
    int jitter_sec = (arc4random_uniform(1000) > 500) ? 1 : 0;
    s_next_reconnect_time = time(NULL) + s_backoff_sec + jitter_sec;
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
    while (timeout_ms > 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0)
            return -1;

        ipc_hdr_t hdr;
        if (toread(fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr))
            return -1;

        if (hdr.magic != IPC_MAGIC)
            return -1;

        if (hdr.type == IPC_TYPE_ACK) {
            if (expected_seq > 0 && hdr.seq != expected_seq)
                return -1;

            ipc_ack_t ack;
            if (hdr.len > 0) {
                size_t to_read = hdr.len > sizeof(ack) ? sizeof(ack) : hdr.len;
                if (toread(fd, &ack, (int)to_read) != (int)to_read)
                    return -1;
                int remaining = (int)(hdr.len - to_read);
                char dummy[64];
                while (remaining > 0) {
                    int chunk = remaining > (int)sizeof(dummy) ? (int)sizeof(dummy) : remaining;
                    if (toread(fd, dummy, chunk) != chunk)
                        break;
                    remaining -= chunk;
                }
                return ack.code;
            }
            return 0;
        }

        /* If another asynchronous packet arrived while waiting for ACK, drain payload and keep waiting */
        int remaining = (int)hdr.len;
        char dummy[64];
        while (remaining > 0) {
            int chunk = remaining > (int)sizeof(dummy) ? (int)sizeof(dummy) : remaining;
            if (toread(fd, dummy, chunk) != chunk)
                break;
            remaining -= chunk;
        }
    }
    return -1;
}

int
ipc_try_reconnect(void)
{
    if (g_ipc_fd >= 0)
        return g_ipc_fd;

    time_t now = time(NULL);
    const char *sock_path = GET_SVC_SOCKET(ipc);
    int fd = toconnect3(sock_path, 0, 50000);
    if (fd < 0) {
        /* Reconnect failed, increase backoff */
        s_retry_count++;
        s_backoff_sec = s_backoff_sec * 2;
        if (s_backoff_sec > IPC_BACKOFF_MAX_SEC)
            s_backoff_sec = IPC_BACKOFF_MAX_SEC;
        int jitter_sec = (arc4random_uniform(1000) > 500) ? 1 : 0;
        s_next_reconnect_time = now + s_backoff_sec + jitter_sec;
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    g_ipc_fd = fd;

    /* Notify callback to restore application state and re-attach to event loop */
    if (s_reconnect_cb) {
        s_reconnect_cb(g_ipc_fd);
    }

    /* Success: reset backoff state */
    s_backoff_sec = IPC_BACKOFF_INIT_SEC;
    s_retry_count = 0;
    s_next_reconnect_time = 0;
    return g_ipc_fd;
}

int
ipc_check_reconnect(void)
{
    if (g_ipc_fd >= 0)
        return g_ipc_fd;
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

    if (towrite(g_ipc_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        towrite(g_ipc_fd, &reg, sizeof(reg)) != (int)sizeof(reg)) {
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

    if (towrite(g_ipc_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        towrite(g_ipc_fd, &wmsg, sizeof(wmsg)) != (int)sizeof(wmsg)) {
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

    if (towrite(g_ipc_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        towrite(g_ipc_fd, &talk, sizeof(talk)) != (int)sizeof(talk)) {
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
    if (bid == 0 || aid == 0)
        return -1;

    if (g_ipc_fd < 0 && ipc_check_reconnect() < 0)
        return -1;

    ipc_pubsub_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.bid = bid;
    ps.timestamp = (uint32_t)time(NULL);
    ps.aid = aid;

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_SUB,
        .seq = seq,
        .len = (uint32_t)sizeof(ps),
    };

    if (towrite(g_ipc_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        towrite(g_ipc_fd, &ps, sizeof(ps)) != (int)sizeof(ps)) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, 50);
}

int
ipc_unsub(uint32_t bid, aidu_t aid)
{
    if (bid == 0 || aid == 0)
        return -1;

    if (g_ipc_fd < 0)
        return 0;

    ipc_pubsub_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.bid = bid;
    ps.timestamp = (uint32_t)time(NULL);
    ps.aid = aid;

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_UNSUB,
        .seq = seq,
        .len = (uint32_t)sizeof(ps),
    };

    if (towrite(g_ipc_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        towrite(g_ipc_fd, &ps, sizeof(ps)) != (int)sizeof(ps)) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, 50);
}

int
ipc_pub(uint32_t bid, aidu_t aid, const char *text)
{
    if (bid == 0 || aid == 0 || !text)
        return -1;

    if (g_ipc_fd < 0 && ipc_check_reconnect() < 0)
        return -1;

    ipc_pubsub_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.bid = bid;
    ps.timestamp = (uint32_t)time(NULL);
    ps.aid = aid;
    STRLCPY(ps.text, text);
    size_t len = strlen(ps.text);
    while (len > 0 && (ps.text[len - 1] == '\n' || ps.text[len - 1] == '\r'))
        ps.text[--len] = '\0';

    uint32_t seq = ++g_ipc_seq;
    ipc_hdr_t hdr = {
        .magic = IPC_MAGIC,
        .version = IPC_VERSION,
        .type = IPC_TYPE_PUB,
        .seq = seq,
        .len = (uint32_t)sizeof(ps),
    };

    if (towrite(g_ipc_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        towrite(g_ipc_fd, &ps, sizeof(ps)) != (int)sizeof(ps)) {
        ipc_disconnect();
        return -1;
    }

    return ipc_read_ack(g_ipc_fd, seq, 50);
}

int
ipc_read_packet(int fd, ipc_hdr_t *hdr, void *payload_buf, size_t bufsz)
{
    if (fd < 0 || !hdr)
        return -1;

    /* Check if packet header has arrived */
    ssize_t n = read(fd, hdr, sizeof(*hdr));
    if (n <= 0) {
        return (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) ? 0 : -1;
    }
    if (n < (ssize_t)sizeof(*hdr)) {
        /* Read remaining header bytes */
        if (toread(fd, ((char *)hdr) + n, (int)(sizeof(*hdr) - n)) != (int)(sizeof(*hdr) - n))
            return -1;
    }

    if (hdr->magic != IPC_MAGIC || hdr->version != IPC_VERSION)
        return -1;

    if (hdr->len > 0) {
        size_t to_read = hdr->len;
        if (payload_buf && bufsz > 0) {
            if (to_read > bufsz)
                to_read = bufsz;
            if (toread(fd, payload_buf, (int)to_read) != (int)to_read)
                return -1;
        } else {
            to_read = 0;
        }

        /* Drain any remaining payload bytes */
        int remaining = (int)(hdr->len - to_read);
        char dummy[64];
        while (remaining > 0) {
            int chunk = remaining > (int)sizeof(dummy) ? (int)sizeof(dummy) : remaining;
            if (toread(fd, dummy, chunk) != chunk)
                break;
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

    if (towrite(fd, &req_hdr, sizeof(req_hdr)) != (int)sizeof(req_hdr)) {
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
