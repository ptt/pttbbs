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

static void act_inflight_lost(void);

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
    act_inflight_lost();
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
        act_inflight_lost();
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

static char g_tracked[IDLEN + 1];   // mbbsd: the user of this process
static uint32_t g_reads;            // Latest count from the service (or local)
// Read deltas sent but not yet answered (one reply per "read", in order), so
// the result includes reads still in flight.
static uint32_t g_inflight[64];
static unsigned g_inflight_head, g_inflight_len;
static uint32_t g_inflight_sum;

static int g_kicked;
static int g_delay_ms;              // Pause requested by the latest "reads"
static int g_nav_delay_ms;          // Pause requested by the latest "pace"
static int g_nav_deny;              // The latest "pace" said "deny"

static void
act_inflight_clear(void)
{
    g_inflight_head = g_inflight_len = 0;
    g_inflight_sum = 0;
}

// The connection is gone: in-flight reads will not be answered, keep them.
static void
act_inflight_lost(void)
{
    g_reads += g_inflight_sum;
    act_inflight_clear();
}

static void
act_inflight_pop(void)
{
    if (!g_inflight_len)
        return;
    g_inflight_sum -= g_inflight[g_inflight_head];
    g_inflight_head = (g_inflight_head + 1) % ARRAY_SIZE(g_inflight);
    g_inflight_len--;
}

static void
act_inflight_push(uint32_t delta)
{
    if (g_inflight_len == ARRAY_SIZE(g_inflight)) {
        g_reads += g_inflight[g_inflight_head];     // Assume it was counted
        act_inflight_pop();
    }
    g_inflight[(g_inflight_head + g_inflight_len) % ARRAY_SIZE(g_inflight)] = delta;
    g_inflight_len++;
    g_inflight_sum += delta;
}

static void
act_handle(char *msg, activity_verdict_cb cb, void *arg)
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
        g_timeouts = 0;
        if (!cb)
            return;
        if (strcmp(tok[2], "deny") == 0)
            cb(reqid, 1, n >= 4 ? atoi(tok[3]) : 1, p ? p : "", arg);
        else
            cb(reqid, 0, 0, NULL, arg);
    } else if (strcmp(tok[0], "reads") == 0 && n >= 3) {
        if (strcasecmp(tok[1], g_tracked) == 0) {
            act_inflight_pop();
            g_reads = (uint32_t)strtoul(tok[2], NULL, 10);
            g_delay_ms = n >= 4 ? atoi(tok[3]) : 0;
        }
    } else if (strcmp(tok[0], "pace") == 0 && n >= 3) {
        if (strcasecmp(tok[1], g_tracked) == 0) {
            g_nav_delay_ms = atoi(tok[2]);
            g_nav_deny = n >= 4 && strncmp(tok[3], "deny", 4) == 0;
        }
    } else if (strcmp(tok[0], "kick") == 0 && n >= 2) {
        if (strcasecmp(tok[1], g_tracked) == 0)
            g_kicked = 1;
    }
}

void
activity_client_dispatch(activity_verdict_cb cb, void *arg)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return;

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
        act_handle(buf, cb, arg);
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

static void
act_track_user(const char *userid)
{
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return;

    if (strcasecmp(g_tracked, userid) != 0) {
        strlcpy(g_tracked, userid, sizeof(g_tracked));
        g_reads = 0;
        act_inflight_clear();
        g_kicked = 0;
        g_delay_ms = 0;
        g_nav_delay_ms = 0;
        g_nav_deny = 0;
    }
}

uint32_t
activity_read_post(const char *userid, time4_t firstlogin,
                   uint32_t client_code, int is_exempt,
                   int bid, const char *filename, int *delay_ms)
{
    if (delay_ms)
        *delay_ms = 0;
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return 0;
    if (!act_field_ok(userid))
        return 0;
    act_track_user(userid);
    if (is_exempt || !act_field_ok(filename)) {
        g_reads++;
        return g_reads + g_inflight_sum;
    }

    activity_client_dispatch(NULL, NULL);
    if (act_send("read %s %d %x %d %s", userid, (int)firstlogin, client_code,
                 bid, filename) == 0) {
        act_inflight_push(1);   // Estimate until the reply arrives
        // The service answers in microseconds; a short wait gets the verdict
        // for this read instead of applying it on the next one.
        struct pollfd pfd = { g_fd, POLLIN, 0 };
        if (poll(&pfd, 1, ACTIVITY_READ_WAIT_MS) > 0)
            activity_client_dispatch(NULL, NULL);
    } else {
        g_reads++;              // Not sent: count locally
    }

    if (delay_ms) {
        *delay_ms = g_delay_ms;
        g_delay_ms = 0;         // Apply each delay once
    }
    return g_reads + g_inflight_sum;
}

int
activity_nav_track(const char *userid, time4_t firstlogin,
                   const char *hostip, uint32_t client_code,
                   int is_exempt, int nav_type, int target_bid,
                   int value, int *delay_ms)
{
    int deny;

    if (delay_ms)
        *delay_ms = 0;
    if (!IS_ENABLED(CONFIG_ACTIVITY_SVC))
        return 0;

    char ip[64];

    if (is_exempt || !act_field_ok(userid))
        return 0;
    act_track_user(userid);
    activity_client_dispatch(NULL, NULL);
    g_nav_delay_ms = g_nav_deny = 0;    // Only this request's reply counts
    act_norm_ip(hostip, ip, sizeof(ip));
    if ((nav_type == ACTIVITY_NAV_SEARCH_RATING ?
         act_send("nav %s %d %s %x %d %d %d", userid, (int)firstlogin, ip,
                  client_code, nav_type, target_bid, value) :
         act_send("nav %s %d %s %x %d %d", userid, (int)firstlogin, ip,
                  client_code, nav_type, target_bid)) == 0) {
        // Like reads: a short wait gets the pace (or kick) for this operation.
        struct pollfd pfd = { g_fd, POLLIN, 0 };
        if (poll(&pfd, 1, ACTIVITY_READ_WAIT_MS) > 0)
            activity_client_dispatch(NULL, NULL);
    }
    if (delay_ms)
        *delay_ms = g_nav_delay_ms;
    // A late reply may belong to an earlier request: only ratings are denied.
    deny = g_nav_deny && nav_type == ACTIVITY_NAV_SEARCH_RATING;
    g_nav_delay_ms = g_nav_deny = 0;    // Apply each once
    return g_kicked ? -1 : deny ? 1 : 0;
}
