// $Id$
// Memcached protocol based board data export daemon

// Copyright (c) 2010-2011, Chen-Yu Tsai <wens@csie.org>
// All rights reserved.

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <cmbbs.h>
#include <var.h>
#include <perm.h>

#include "daemon/boardd/boardd.h"

void
setup_program()
{
    setuid(BBSUID);
    setgid(BBSGID);
    chdir(BBSHOME);

    attach_SHM();
}

int main(int argc, char *argv[])
{
    int ch, run_as_daemon = 1;
    const char *iface_ip = "127.0.0.1:5150";

    while ((ch = getopt(argc, argv, "sDl:h")) != -1)
	switch (ch) {
	    case 's':
		fprintf(stderr,
			"Warning: GRPC endpoint is already the default. "
			"Specifying this flag will soon be an error. (-s)\n");
		break;
	    case 'D':
		run_as_daemon = 0;
		break;
	    case 'l':
		iface_ip = optarg;
		break;
	    case 'h':
	    default:
		fprintf(stderr, "Usage: %s [-D] [-l interface_ip:port]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

    if (run_as_daemon)
	if (daemon(1, 1) < 0) {
	    perror("daemon");
	    exit(EXIT_FAILURE);
	}

    setup_program();

    signal(SIGPIPE, SIG_IGN);

    char *ipport = strdup(iface_ip);
    char *ip = strtok(ipport, ":");
    char *port = strtok(NULL, ":");
    start_grpc_server(ip, atoi(port));
    free(ipport);

    return 0;
}

#ifdef __linux__

int
daemon(int nochdir, int noclose)
{
    int fd;

    switch (fork()) {
	case -1:
	    return -1;
	case 0:
	    break;
	default:
	    _exit(0);
    }

    if (setsid() == -1)
	return -1;

    if (!nochdir)
	chdir("/");

    if (!noclose && (fd = open("/dev/null", O_RDWR)) >= 0) {
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	if (fd > 2)
	    close(fd);
    }

    return 0;
}

#endif // __linux__
