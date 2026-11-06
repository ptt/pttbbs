// Activity client: talks to activity.svc over one persistent, non-blocking
// SOCK_SEQPACKET connection (see include/activity.h). Nothing here ever
// blocks; without activity.svc (or with CONFIG_ACTIVITY_SVC disabled) every
// hook allows everything.

#include "bbs.h"
#include "activity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>

///////////////////////////////////////////////////////////////////////////
// Connection

static int g_fd = -1;
static pid_t g_pid;             // Owner; a forked child must reconnect
static time_t g_retry_at;       // No connection attempts before this time
static int g_timeouts;          // Consecutive verdict timeouts
static uint32_t g_reqid;

// Closes the connection. If `backoff`, no reconnection is tried for
// ACTIVITY_SVC_RETRY_SEC; otherwise the next call reconnects right away (e.g.
// after the service restarted), and only a failed connect starts the wait.
static void
act_disconnect(int backoff)
{
    if (g_fd >= 0)
        close(g_fd);
    g_fd = -1;
    g_timeouts = 0;
    g_retry_at = backoff ? time(NULL) + ACTIVITY_SVC_RETRY_SEC : 0;
}

static int
act_connect(void)
{
    struct sockaddr_un addr;
    int fd;

    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return -1;
    if (g_fd >= 0 && g_pid != getpid()) {
        // Inherited across fork: drop our copy, never share the stream.
        close(g_fd);
        g_fd = -1;
        g_retry_at = 0;
    }
    if (g_fd >= 0)
        return 0;
    if (time(NULL) < g_retry_at)
        return -1;

    if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0) {
        g_retry_at = time(NULL) + ACTIVITY_SVC_RETRY_SEC;
        return -1;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, ACTIVITY_SVC_PATH, sizeof(addr.sun_path));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        g_retry_at = time(NULL) + ACTIVITY_SVC_RETRY_SEC;
        return -1;
    }
    g_fd = fd;
    g_pid = getpid();
    g_timeouts = 0;
    return 0;
}

static int act_send(const char *fmt, ...) GCC_CHECK_FORMAT(1, 2);

// Sends one message. Returns 0 if sent, -1 if dropped.
static int
act_send(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len <= 0 || len >= (int)sizeof(buf))
        return -1;

    // A broken connection (e.g. the service restarted) gets one immediate
    // reconnect; a full buffer just drops the message.
    for (int attempt = 0; attempt < 2; attempt++) {
        if (act_connect() < 0)
            return -1;
        if (send(g_fd, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL) == len)
            return 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return -1;
        act_disconnect(0);
    }
    return -1;
}

int
activity_client_fd(void)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return -1;

    if (g_fd >= 0 && g_pid != getpid())
        return -1;
    return g_fd;
}

void
activity_client_timeout(void)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return;

    if (++g_timeouts >= ACTIVITY_SVC_MAX_TIMEOUTS)
        act_disconnect(1);  // Stuck service: stop asking for a while
}

///////////////////////////////////////////////////////////////////////////
// Replies

// The mbbsd request waiting for its reply (mbbsd sends one at a time). Replies
// nobody waits for, e.g. ones that arrive too late, are dropped.
enum { ACT_WAIT_READ = 1, ACT_WAIT_NAV, ACT_WAIT_FWD };
struct act_wait {
    int kind;               // ACT_WAIT_*
    int type;               // NAV: the request type
    uint32_t reqid;         // FWD
    int done;
    int delay_ms;           // READ, NAV: pause requested
    int deny;               // NAV, FWD: deny this request / do not send
    char *msg;              // NAV, FWD: why denied (may be NULL)
    size_t msgsz;
};

// Keeps a denial message (the rest of a reply) in w.
static void
act_keep_msg(struct act_wait *w, const char *msg)
{
    if (w->msg && w->msgsz)
        strlcpy(w->msg, msg ? msg : "", w->msgsz);
}

static void
act_handle(char *msg, activity_verdict_cb cb, void *arg, struct act_wait *w)
{
    char *p = msg, *tok[4];
    int n;

    // Split up to 4 fields; the rest (a deny message) stays in p.
    for (n = 0; n < 4 && p; n++) {
        tok[n] = p;
        if ((p = strchr(p, ' ')) != NULL)
            *p++ = '\0';
    }

    if (strcmp(tok[0], "verdict") == 0 && n >= 3) {
        uint32_t reqid = (uint32_t)strtoul(tok[1], NULL, 10);
        int deny = strcmp(tok[2], "deny") == 0;
        g_timeouts = 0;
        if (cb) {
            if (deny)
                cb(reqid, 1, n >= 4 ? atoi(tok[3]) : 1, p ? p : "", arg);
            else
                cb(reqid, 0, 0, NULL, arg);
        } else if (w && w->kind == ACT_WAIT_FWD && w->reqid == reqid) {
            w->deny = deny;
            if (deny)   // "deny 0 <message>"
                act_keep_msg(w, p);
            w->done = 1;
        }
    } else if (strcmp(tok[0], "reads") == 0 && n >= 3) {
        // "reads <userid> <count> <delay_ms>": the count is not used.
        if (w && w->kind == ACT_WAIT_READ && !w->done) {
            w->delay_ms = n >= 4 ? atoi(tok[3]) : 0;
            w->done = 1;
        }
    } else if (strcmp(tok[0], "pace") == 0 && n >= 3) {
        if (w && w->kind == ACT_WAIT_NAV && !w->done) {
            w->delay_ms = atoi(tok[2]);
            // "deny <type>"; a bare "deny" (older services) is for scores.
            // A late reply may belong to an earlier request: deny only the
            // same type.
            if (n >= 4 && strcmp(tok[3], "deny") == 0) {
                w->deny = (p ? atoi(p) : ACTIVITY_NAV_SEARCH_SCORE) == w->type;
                // "deny <type> <message>"
                if (w->deny)
                    act_keep_msg(w, p && strchr(p, ' ') ? strchr(p, ' ') + 1 : NULL);
            }
            w->done = 1;
        }
    }
}

static void
act_dispatch(activity_verdict_cb cb, void *arg, struct act_wait *w)
{
    char buf[ACTIVITY_SVC_MSG_MAX];

    if (activity_client_fd() < 0)
        return;
    for (;;) {
        ssize_t n = recv(g_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (n == 0) {
            act_disconnect(0);
            return;
        }
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                act_disconnect(0);
            return;
        }
        buf[n] = '\0';
        act_handle(buf, cb, arg, w);
    }
}

void
activity_client_dispatch(activity_verdict_cb cb, void *arg)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return;
    act_dispatch(cb, arg, NULL);
}

// Waits up to timeout_ms for the reply to w. Other replies (e.g. a late one
// to an earlier request) may come first.
static void
act_wait_reply(struct act_wait *w, int timeout_ms)
{
    struct timespec start, t;

    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!w->done && g_fd >= 0) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        int left = timeout_ms - (int)((t.tv_sec - start.tv_sec) * 1000 +
                                      (t.tv_nsec - start.tv_nsec) / 1000000);
        struct pollfd pfd = { g_fd, POLLIN, 0 };
        if (left <= 0 || poll(&pfd, 1, left) <= 0)
            break;
        act_dispatch(NULL, NULL, w);
    }
}

///////////////////////////////////////////////////////////////////////////
// Hooks

// Fields are space separated; refuse anything that would break that.
static int
act_field_ok(const char *s)
{
    if (!s || !*s)
        return 0;
    for (; *s; s++)
        if ((unsigned char)*s <= ' ')
            return 0;
    return 1;
}

// IPv4 as is; IPv4-mapped IPv6 as IPv4; other IPv6 reduced to its /64,
// since one host can trivially rotate addresses inside its /64.
static const char *
act_norm_ip(const char *in, char *out, size_t sz)
{
    struct in6_addr a6;

    if (!act_field_ok(in))
        return "-";
    if (!strchr(in, ':') || inet_pton(AF_INET6, in, &a6) != 1) {
        strlcpy(out, in, sz);
        return out;
    }
    if (IN6_IS_ADDR_V4MAPPED(&a6)) {
        if (!inet_ntop(AF_INET, &a6.s6_addr[12], out, sz))
            strlcpy(out, in, sz);
        return out;
    }
    memset(&a6.s6_addr[8], 0, 8);
    if (!inet_ntop(AF_INET6, &a6, out, sz))
        strlcpy(out, in, sz);
    else
        strlcat(out, "/64", sz);
    return out;
}

void
activity_login_failed(const char *userid, const char *hostip)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return;

    char ip[64];

    if (act_field_ok(userid))
        act_send("fail %s %s", userid, act_norm_ip(hostip, ip, sizeof(ip)));
}

uint32_t
activity_login_query(const char *userid, const char *hostip, int is_exempt)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return 0;

    char ip[64];

    if (!act_field_ok(userid))
        return 0;
    if (++g_reqid == 0)
        g_reqid = 1;
    if (act_send("login %u %s %s %d", g_reqid, userid,
                 act_norm_ip(hostip, ip, sizeof(ip)), is_exempt ? 1 : 0) < 0)
        return 0;
    return g_reqid;
}

void
activity_read_post(const char *userid, time4_t firstlogin,
                   uint32_t client_code, int is_exempt,
                   int bid, const char *filename, int *delay_ms)
{
    struct act_wait w = { .kind = ACT_WAIT_READ };

    if (delay_ms)
        *delay_ms = 0;
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return;
    if (is_exempt || !act_field_ok(userid) || !act_field_ok(filename))
        return;

    act_dispatch(NULL, NULL, NULL);     // Drop stale replies
    // The service answers in microseconds; a short wait gets the verdict for
    // this read.
    if (act_send("read %s %d %x %d %s", userid, (int)firstlogin, client_code,
                 bid, filename) == 0)
        act_wait_reply(&w, ACTIVITY_READ_WAIT_MS);
    if (delay_ms)
        *delay_ms = w.delay_ms;
}

int
activity_nav_track(const char *userid, time4_t firstlogin,
                   const char *hostip, uint32_t client_code,
                   int is_exempt, int nav_type, int target_bid,
                   int value, int *delay_ms, char *msg, size_t msgsz)
{
    struct act_wait w = { .kind = ACT_WAIT_NAV, .type = nav_type,
                          .msg = msg, .msgsz = msgsz };

    if (delay_ms)
        *delay_ms = 0;
    if (msg && msgsz)
        *msg = '\0';
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return 0;

    char ip[64];

    if (is_exempt || !act_field_ok(userid))
        return 0;
    act_dispatch(NULL, NULL, NULL);     // Drop stale replies
    act_norm_ip(hostip, ip, sizeof(ip));
    if ((nav_type == ACTIVITY_NAV_SEARCH_SCORE ?
         act_send("nav %s %d %s %x %d %d %d", userid, (int)firstlogin, ip,
                  client_code, nav_type, target_bid, value) :
         act_send("nav %s %d %s %x %d %d", userid, (int)firstlogin, ip,
                  client_code, nav_type, target_bid)) == 0)
        act_wait_reply(&w, ACTIVITY_READ_WAIT_MS);
    if (delay_ms)
        *delay_ms = w.delay_ms;
    return w.deny;
}

int
activity_forward(const char *userid, time4_t firstlogin,
                 const char *hostip, uint32_t client_code, int is_exempt,
                 int bid, const char *filename, int kind,
                 const char *address, const char *via, time4_t mtime,
                 char *msg, size_t msgsz)
{
    if (msg && msgsz)
        *msg = '\0';
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return 0;

    char ip[64];
    struct act_wait w = { .kind = ACT_WAIT_FWD, .msg = msg, .msgsz = msgsz };

    if (is_exempt || !act_field_ok(userid))
        return 0;
    act_dispatch(NULL, NULL, NULL);     // Drop stale replies
    if (++g_reqid == 0)
        g_reqid = 1;
    w.reqid = g_reqid;
    act_norm_ip(hostip, ip, sizeof(ip));
    if (act_send("fwd %u %s %d %s %x %d %s %d %s %s %d", w.reqid, userid,
                 (int)firstlogin, ip, client_code, bid,
                 act_field_ok(filename) ? filename : "-", kind,
                 act_field_ok(address) ? address : "-",
                 act_field_ok(via) ? via : "-",
                 mtime > 0 ? (int)mtime : 0) == 0)
        act_wait_reply(&w, ACTIVITY_FORWARD_WAIT_MS);
    return w.deny;
}
