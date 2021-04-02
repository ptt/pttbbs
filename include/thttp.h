#ifndef THTTP_H
#   define THTTP_H
#include "buffer.h"

typedef struct THTTP {
    int fd;
    int (*read)(struct THTTP *t, void *buffer, int size);
    int (*write)(struct THTTP *t, const void *buffer, int size);
    BUFFER bresp;
    int code;
    int headers_at;
    int content_at;
    int content_length;
    int failed;
    int timeout_read;
    int timeout_connect;
} THTTP;

int thttp_init(THTTP *t);
int thttp_cleanup(THTTP *t);
void thttp_set_connect_timeout(THTTP *t, int microsecond);
void thttp_set_io_timeout(THTTP *t, int microsecond);
int thttp_get(THTTP *t, const char *addr, const char *uri, const char *host);
int thttp_post(THTTP *t, const char *addr, const char *uri, const char *host,
	       const char *content_type, const void *content,
	       int content_length);
int thttp_code(THTTP *t);
void *thttp_get_content(THTTP *t);
int thttp_content_length(THTTP *t);
int thttp_get_header(THTTP *t, const char *name, const char **valptr);

#endif
