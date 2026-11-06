/*
 * bbsmail: gives a mail from the MTA to mail.svc, which delivers it to the
 * mailbox of a BBS user.
 *
 * Usage: bbsmail <bbs_uid>
 *
 * The receiver may be "userid" or "userid.bbs"; the mail is read from
 * stdin. The exit code is 0 when the mail is delivered, or not delivered
 * for good (e.g. no such user, see etc/mailog), so it does not bounce; and
 * EX_TEMPFAIL when mail.svc can't take it now, so the MTA tries again
 * later.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "bbs.h"

static int
write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
	ssize_t n = write(fd, buf, len);
	if (n < 0 && errno == EINTR)
	    continue;
	if (n <= 0)
	    return -1;
	buf += n;
	len -= (size_t)n;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    char buf[8192];
    size_t len = 0;
    ssize_t n;
    int fd;

    if (argc < 2) {
	printf("Usage:\t%s <bbs_uid>\n", argv[0]);
	return -1;
    }
    if (strpbrk(argv[1], "\r\n")) {
	puts("Bad receiver");
	return 0;
    }

    strlcpy(addr.sun_path, MAILSVC_IN_PATH, sizeof(addr.sun_path));
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ||
	connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	perror(MAILSVC_IN_PATH);
	return EX_TEMPFAIL;
    }

    n = snprintf(buf, sizeof(buf), "DELIVER %s\n", argv[1]);
    if (n < 0 || (size_t)n >= sizeof(buf) || write_all(fd, buf, (size_t)n) < 0)
	return EX_TEMPFAIL;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) != 0) {
	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    return EX_TEMPFAIL;
	}
	if (write_all(fd, buf, (size_t)n) < 0)
	    return EX_TEMPFAIL;
    }
    shutdown(fd, SHUT_WR);

    /* OK, REJECT <why> or ERR <why> */
    while (len < sizeof(buf) - 1 &&
	   (n = read(fd, buf + len, sizeof(buf) - 1 - len)) != 0) {
	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	len += (size_t)n;
    }
    buf[len] = '\0';
    close(fd);
    if (len)
	fputs(buf, stdout);

    if (strncmp(buf, "OK", 2) == 0 || strncmp(buf, "REJECT", 6) == 0)
	return 0;
    return EX_TEMPFAIL;
}
