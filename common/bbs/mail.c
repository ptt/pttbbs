// ends mail to the internet.
#include "cmbbs.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* Copying the content into the spool takes a while for a large mail */
#define MAILSVC_TIMEOUT_MS  5000
/* Same as the limit of mail.svc */
#define MAILSVC_MAX_SIZE    (32 << 20)

// Turn CR and LF into spaces.
static const char *
one_line(char *buf, size_t size, const char *s)
{
    char *p;

    strlcpy(buf, s ? s : "", size);
    for (p = buf; *p; p++)
        if (*p == '\r' || *p == '\n')
            *p = ' ';
    return buf;
}

static int
mailsvc_request(const char *header, int fd, off_t len)
{
    char buf[8192];
    int sfd, n;
    off_t left = len;

    if ((sfd = svc_connect(MAILSVC_OUT_PATH)) < 0)
        return -1;
    n = (int)strlen(header);
    if (svc_write(sfd, header, n) != n)
        goto fail;
    while (fd >= 0 && left > 0) {
        ssize_t r = read(fd, buf, left < (off_t)sizeof(buf) ? (size_t)left : sizeof(buf));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || svc_write(sfd, buf, (size_t)r) != r)
            goto fail;
        left -= r;
    }
    shutdown(sfd, SHUT_WR);

    n = svc_read(sfd, buf, sizeof(buf) - 1, MAILSVC_TIMEOUT_MS);
    close(sfd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    return (strncmp(buf, "OK", 2) == 0 &&
            (buf[2] == '\n' || buf[2] == ' ' || buf[2] == '\0')) ? 0 : -1;

fail:
    close(sfd);
    return -1;
}

int
mail_internet(const char *fpath, const char *title, const char *rcpt,
              const char *from)
{
    char header[1024], t[STRLEN * 2], r[STRLEN * 2], f[STRLEN];
    struct stat st;
    int fd, ret;

    if ((fd = open(fpath, O_RDONLY)) < 0)
        return -1;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
        st.st_size > MAILSVC_MAX_SIZE) {
        close(fd);
        return -1;
    }
    if (snprintf(header, sizeof(header),
                 "SEND\nRcpt: %s\nFrom: %s\nSubject: %s\nLength: %lld\n\n",
                 one_line(r, sizeof(r), rcpt), one_line(f, sizeof(f), from),
                 one_line(t, sizeof(t), title), (long long)st.st_size) >=
        (int)sizeof(header)) {
        close(fd);
        return -1;
    }
    ret = mailsvc_request(header, fd, st.st_size);
    close(fd);
    return ret;
}

int
mail_ziphome(const char *userid, const char *rcpt)
{
    char header[512], r[STRLEN * 2], u[IDLEN + 1];

    if (snprintf(header, sizeof(header), "ZIPHOME\nUser: %s\nRcpt: %s\n\n",
                 one_line(u, sizeof(u), userid),
                 one_line(r, sizeof(r), rcpt)) >= (int)sizeof(header))
        return -1;
    return mailsvc_request(header, -1, 0);
}
