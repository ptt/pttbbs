/* $Id$ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "osdep.h"
#include "cmsys.h"

int
daemonize(const char * pidfile, const char * logfile)
{
    int fd;
    char buf[32];
    pid_t pid;

    umask(022);

    if ((pid = fork()) < 0) {
	perror("Fork failed");
	exit(1);
    }

    if (pid > 0)
	exit(0);

    if (setsid() < 0) {
	perror("Setsid failed");
	exit(-1);
    }

    if ((pid = fork()) < 0) {
	perror("Fork failed");
	exit(1);
    }

    if (pid > 0)
	exit(0);

    if (pidfile) {
	if ((fd = creat(pidfile, 0644)) >= 0) {
	    snprintf(buf, sizeof(buf), "%d", (int)getpid());
	    write(fd, buf, strlen(buf));
	    close(fd);
	} else
	    perror("Can't open PID file");
    }

    if (logfile) {
	if ((fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0) {
	    perror("Can't open logfile");
	    exit(1);
	}
	if (fd != 2) {
	    dup2(fd, 2);
	    close(fd);
	}
    }

    if (chdir("/") < 0) {
	perror("Can't chdir to root");
	exit(-1);
    }

#if 0
    fd = getdtablesize();
    while (fd > 2)
	close(--fd);
#endif

    if ((fd = open("/dev/null", O_RDWR)) < 0) {
	perror("Can't open /dev/null");
	exit(1);
    }

    dup2(fd, 0);
    dup2(fd, 1);
    if (!logfile)
	dup2(fd, 2);

    if (fd > 2)
	close(fd);

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGCHLD, SIG_IGN);

    return 0;
}
