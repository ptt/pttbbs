#include "cmbbs.h"
#include "common.h"
#include "cmsys.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

int
svc_connect(const char *sock_path)
{
    if (!sock_path || !*sock_path)
        return -1;
    return toconnect3(sock_path, 0, SVC_CONNECT_TIMEOUT_US);
}

int
svc_write(int fd, const void *buf, size_t len)
{
    if (fd < 0 || !buf)
        return -1;
    return towrite(fd, buf, (int)len);
}

int
svc_read(int fd, void *buf, size_t maxlen, int timeout_ms)
{
    if (fd < 0 || !buf || maxlen == 0)
        return -1;
    if (timeout_ms <= 0)
        timeout_ms = SVC_READ_TIMEOUT_MS;

    char *p = (char *)buf;
    size_t total = 0;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (total < maxlen) {
        int pr;
        do {
            pr = poll(&pfd, 1, timeout_ms);
        } while (pr < 0 && errno == EINTR);

        if (pr <= 0 || !(pfd.revents & (POLLIN | POLLHUP)))
            break;

        ssize_t r = read(fd, p + total, maxlen - total);
        if (r <= 0)
            break;
        total += (size_t)r;
        p[total] = '\0';
        if (strchr(p, '\n') != NULL)
            break;
    }
    return (int)total;
}

int
svc_request(const char *sock_path, const char *req, char *resp, size_t resp_size)
{
    if (!sock_path || !req || !resp || resp_size == 0)
        return -1;

    int sfd = svc_connect(sock_path);
    if (sfd < 0)
        return -1;

    int len = (int)strlen(req);
    if (svc_write(sfd, req, (size_t)len) != len) {
        close(sfd);
        return -1;
    }

    int n = svc_read(sfd, resp, resp_size - 1, SVC_READ_TIMEOUT_MS);
    close(sfd);
    if (n <= 0)
        return -1;
    resp[n] = '\0';
    return n;
}
