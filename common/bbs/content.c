// Content filtering.
// This implementation is based on CONFIG_CONTENT_SVC. Without service running,
// nothing is checked.

#include "bbs.h"
#include "content.h"

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>

static const char * const kind_names[] = {
    [CONTENT_KIND_COMMENT] = "comment",
    [CONTENT_KIND_WMSG]    = "wmsg",
    [CONTENT_KIND_TITLE]   = "title",
    [CONTENT_KIND_POST]    = "post",
    [CONTENT_KIND_MAIL]    = "mail",
};

static const char *
content_kind_name(int kind)
{
    if (kind <= 0 || kind >= (int)ARRAY_SIZE(kind_names))
        return NULL;
    return kind_names[kind];
}

// Copies a header field: "-" if empty, whitespace replaced.
static void
content_field(char *out, size_t size, const char *s)
{
    if (!s || !*s) {
        strlcpy(out, "-", size);
        return;
    }
    strlcpy(out, s, size);
    for (char *p = out; *p; p++) {
        if ((unsigned char)*p <= ' ')
            *p = '_';
    }
}

static int
content_token(const char **pp, const char *end, char *out, size_t size)
{
    const char *p = *pp;
    size_t n = 0;

    while (p < end && *p == ' ')
        p++;
    if (p >= end)
        return -1;
    while (p < end && *p != ' ') {
        if (n + 1 < size)
            out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    *pp = p;
    return 0;
}

static int
content_parse_verdict(const char *resp, size_t len, content_verdict_t *v)
{
    const char *p = resp, *end, *eol;
    char tok[64];

    memset(v, 0, sizeof(*v));
    eol = memchr(resp, '\n', len);
    end = eol ? eol : resp + len;

    if (content_token(&p, end, tok, sizeof(tok)) < 0)
        return -1;
    if (strcmp(tok, "deny") == 0)
        v->deny = 1;
    else if (strcmp(tok, "allow") != 0)
        return -1;

    if (content_token(&p, end, tok, sizeof(tok)) < 0)
        return -1;
    if (strcmp(tok, "alert") == 0)
        v->ui = CONTENT_UI_ALERT;

    if (content_token(&p, end, tok, sizeof(tok)) < 0)
        return -1;
    if (strcmp(tok, "ban") == 0)
        v->penalty = CONTENT_PENALTY_BAN;

    if (content_token(&p, end, tok, sizeof(tok)) < 0)
        return -1;
    v->report = (strcmp(tok, "report") == 0);

    if (content_token(&p, end, v->reporter, sizeof(v->reporter)) < 0)
        return -1;
    if (strcmp(v->reporter, "-") == 0)
        v->reporter[0] = '\0';

    // The reason runs to the end of the line.
    if (p < end && *p == ' ')
        p++;
    size_t n = end - p;
    if (n >= sizeof(v->reason))
        n = sizeof(v->reason) - 1;
    memcpy(v->reason, p, n);
    v->reason[n] = '\0';
    chomp(v->reason);
    // Lines of the reason are separated by tabs on the wire.
    for (char *t = v->reason; (t = strchr(t, '\t')); t++)
        *t = '\n';

    if (eol && eol + 1 < resp + len) {
        n = resp + len - (eol + 1);
        if (n >= sizeof(v->details))
            n = sizeof(v->details) - 1;
        memcpy(v->details, eol + 1, n);
        v->details[n] = '\0';
    }
    return 0;
}

int
content_filter(int kind, const char *userid, const char *ip,
               const char *board, const char *target, unsigned flags,
               int extra, const char *path, const char *title,
               const char *text, size_t text_len, int timeout_ms,
               content_verdict_t *v)
{
    char f_userid[IDLEN + 2], f_ip[64], f_board[IDLEN + 2], f_target[64],
         f_path[PATHLEN];
    char header[512], resp[CONTENT_SVC_RESP_MAX + 1024];
    const char *kname = content_kind_name(kind);
    size_t title_len, total = 0;
    struct timeval start, cur;
    int fd, ok = 0;

    memset(v, 0, sizeof(*v));
    if (!IS_ENABLED(CONFIG_CONTENT_SVC) || !kname)
        return 0;

    if (!title)
        title = "";
    if (!text)
        text = "", text_len = 0;
    title_len = strlen(title);
    if (text_len > CONTENT_SVC_TEXT_MAX)
        text_len = CONTENT_SVC_TEXT_MAX;
    if (timeout_ms <= 0)
        timeout_ms = CONTENT_SVC_TIMEOUT_MS;

    content_field(f_userid, sizeof(f_userid), userid);
    content_field(f_ip, sizeof(f_ip), ip);
    content_field(f_board, sizeof(f_board), board);
    content_field(f_target, sizeof(f_target), target);
    content_field(f_path, sizeof(f_path), path);
    snprintf(header, sizeof(header), "check %s %s %s %s %s %x %d %s %zu %zu\n",
             kname, f_userid, f_ip, f_board, f_target, flags, extra, f_path,
             title_len, text_len);

    if ((fd = svc_connect(CONTENT_SVC_PATH)) < 0)
        return 0;

    if (towrite(fd, header, strlen(header)) < 0 ||
        towrite(fd, title, title_len) < 0 ||
        towrite(fd, text, text_len) < 0) {
        close(fd);
        return 0;
    }
    shutdown(fd, SHUT_WR);

    // Read the verdict until the service closes the connection.
    gettimeofday(&start, NULL);
    while (total < sizeof(resp) - 1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int elapsed, pr;
        ssize_t r;

        gettimeofday(&cur, NULL);
        elapsed = (cur.tv_sec - start.tv_sec) * 1000 +
                  (cur.tv_usec - start.tv_usec) / 1000;
        if (elapsed >= timeout_ms)
            break;
        pr = poll(&pfd, 1, timeout_ms - elapsed);
        if (pr < 0 && errno == EINTR)
            continue;
        if (pr <= 0)
            break;
        r = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            ok = (r == 0);
            break;
        }
        total += r;
    }
    close(fd);

    if (!ok || total == 0)
        return 0;
    resp[total] = '\0';
    if (content_parse_verdict(resp, total, v) < 0) {
        memset(v, 0, sizeof(*v));
        return 0;
    }
    return 1;
}
