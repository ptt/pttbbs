// $Id$
// Barebone TCP socket server daemon based on libevent 2.0

// Copyright (c) 2011, Chen-Yu Tsai <wens@csie.org>
// All rights reserved.

// This is a simple TCP/IP server daemon based on libevent 2.0.
// This program does not depend on anything other than libevent 2.0.
// Without additional code linked in, this program alone will behave as an
// echo server.
//
// You can supply your client_read_cb(), client_event_cb(), setup_client(), or
// setup_program() functions to modify the behavior of the server.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>
#include <event2/listener.h>
#ifdef SERVER_USE_PTHREADS
#include <event2/thread.h>
#endif

#include "server.h"

static const struct timeval timeout = {600, 0};
const struct timeval *common_timeout = &timeout;

int
split_args(char *line, char ***argp)
{
    int argc = 0;
    char *p, **argv;

    if ((argv = calloc(MAX_ARGS + 1, sizeof(char *))) == NULL)
	return -1;

    while ((p = strsep(&line, " \t\r\n")) != NULL) {
	argv[argc++] = p;

	if (argc == MAX_ARGS)
	    break;
    }

    argv = realloc(argv, (argc + 1) * sizeof(char *));
    *argp = argv;

    return argc;
}

void __attribute__((weak))
client_read_cb(struct bufferevent *bev, void *ctx)
{
    bufferevent_write_buffer(bev, bufferevent_get_input(bev));
}

void __attribute__((weak))
client_event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR)
	perror("Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_TIMEOUT | BEV_EVENT_ERROR)) {
	bufferevent_free(bev);
    }
}

void __attribute__((weak))
setup_client(struct event_base *base, evutil_socket_t fd,
       	struct sockaddr *address, int socklen)
{
    struct bufferevent *bev = bufferevent_socket_new(base, fd,
	    BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS | BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, client_read_cb, NULL, client_event_cb, NULL);
    bufferevent_set_timeouts(bev, common_timeout, common_timeout);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static void
accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
       	struct sockaddr *address, int socklen, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    return setup_client(base, fd, address, socklen);
}

int __attribute__((weak)) daemon(int nochdir, int noclose);
void __attribute__((weak)) setup_program();

int main(int argc, char *argv[])
{
    int ch, inetd = 0, run_as_daemon = 1;
    const char *iface_ip = "127.0.0.1:5150";
    struct event_base *base;
    struct evconnlistener *listener;

    while ((ch = getopt(argc, argv, "Dil:h")) != -1)
	switch (ch) {
	    case 'D':
		run_as_daemon = 0;
		break;
	    case 'i':
		inetd = 1;
		break;
	    case 'l':
		iface_ip = optarg;
		break;
	    case 'h':
	    default:
		fprintf(stderr, "Usage: %s [-D] [-i] [-l interface_ip:port]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

    if (run_as_daemon && !inetd)
	if (daemon(1, 1) < 0) {
	    perror("daemon");
	    exit(EXIT_FAILURE);
	}

#ifdef SERVER_USE_PTHREADS
    evthread_use_pthreads();
#endif

    base = event_base_new();
    assert(base);

#ifdef SERVER_USE_PTHREADS
    evthread_make_base_notifiable(base);
#endif

    common_timeout = event_base_init_common_timeout(base, &timeout);

    if (!inetd) {
	struct sockaddr sa;
	int len = sizeof(sa);

	if (evutil_parse_sockaddr_port(iface_ip, &sa, &len) < 0)
	    exit(EXIT_FAILURE);

	listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_REUSEABLE,
		100, &sa, len);

	if (!listener)
	    exit(EXIT_FAILURE);
    } else {
	struct sockaddr sa;
	socklen_t len = sizeof(sa);
	getpeername(0, &sa, &len);
	setup_client(base, 0, &sa, len);
    }

    if (setup_program)
	setup_program();

    signal(SIGPIPE, SIG_IGN);

    event_base_dispatch(base);

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
