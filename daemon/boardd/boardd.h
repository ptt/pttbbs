#ifndef _BOARDD_H
#   define _BOARDD_H

int process_line(struct evbuffer *output, void *ctx, char *line);

void session_create(struct event_base *base, evutil_socket_t fd,
		    struct sockaddr *address, int socklen);

void session_init();
void session_shutdown();

#define NUM_THREADS 8

#endif
