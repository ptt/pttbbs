#ifndef _DAEMON_BOARDD_BOARDD_H_
#define _DAEMON_BOARDD_BOARDD_H_
#include <event2/buffer.h>
#include "pttstruct.h"

// article.c
int is_valid_article_filename(const char *filename);

typedef struct {
    int type;
    const char *cachekey;
    int cachekey_len;
    int offset;
    int maxlen;
    const char *filename;
} select_spec_t;

#define SELECT_TYPE_HEAD (0)
#define SELECT_TYPE_TAIL (1)
#define SELECT_TYPE_PART (2)

typedef struct {
    char cachekey[16];
    size_t size;
    int sel_offset;
    int sel_size;
} select_result_t;

#define SELECT_OK (0)
#define SELECT_EUNKNOWN  (-1)
#define SELECT_ENOTFOUND (-2)
#define SELECT_ECHANGED  (-3)
#define SELECT_ERANGE    (-4)
#define SELECT_EARG      (-5)

int select_article(struct evbuffer *buf, select_result_t *result,
		   const select_spec_t *spec);

// convert.c
struct evbuffer *evbuffer_b2u(struct evbuffer *source);

// board_service_impl.cpp
void start_grpc_server(const char *host, unsigned short port);

#endif
