// $Id$

// Copyright (c) 2011, Chen-Yu Tsai <wens@csie.org>
// All rights reserved.

#include <event2/listener.h>
#include <event2/bufferevent.h>

#ifndef MAX_ARGS
#define MAX_ARGS 100
#endif

extern const struct timeval *common_timeout;

extern void client_read_cb(struct bufferevent *bev, void *ctx);
extern void client_event_cb(struct bufferevent *bev, short events, void *ctx);
extern void setup_client(struct event_base *base, evutil_socket_t fd,
       	struct sockaddr *address, int socklen);
extern void setup_program();

extern int split_args(char *line, char ***argp);
