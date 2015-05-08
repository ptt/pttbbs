#ifndef _BOARDD_H
#   define _BOARDD_H

int process_line(struct evbuffer *output, void *ctx, char *line);
void start_server(const char *host, unsigned short port);

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

#ifndef MAX_ARGS
#define MAX_ARGS 100
#endif

#endif
