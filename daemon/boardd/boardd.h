#ifndef _BOARDD_H
#   define _BOARDD_H

int process_line(struct evbuffer *output, void *ctx, char *line);
void start_server(const char *host, unsigned short port);
void start_grpc_server(const char *host, unsigned short port);

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

#ifndef MAX_ARGS
#define MAX_ARGS 100
#endif

#define SELECT_OK (0)
#define SELECT_EUNKNOWN  (-1)
#define SELECT_ENOTFOUND (-2)
#define SELECT_ECHANGED  (-3)
#define SELECT_ERANGE    (-4)
#define SELECT_EARG      (-5)

#endif
