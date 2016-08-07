#ifndef _BOARDD_PROTO_H_
#define _BOARDD_PROTO_H_
#include <event2/buffer.h>
#include "pttstruct.h"

// article.c
int is_valid_article_filename(const char *filename);

int select_article_head(const char *data, int len, int *offset, int *size,
			void *ctx);
int select_article_tail(const char *data, int len, int *offset, int *size,
			void *ctx);
int select_article_part(const char *data, int len, int *offset, int *size,
			void *ctx);

typedef int (*select_part_func)(const char *data, int len, int *offset,
				int *size, void *ctx);

int answer_articleselect(struct evbuffer *buf, const boardheader_t *bptr,
			 const char *rest_key, select_part_func sfunc,
			 void *ctx);

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

int select_article(struct evbuffer *buf, select_result_t *result,
		   const select_spec_t *spec);

// convert.c
struct evbuffer *evbuffer_b2u(struct evbuffer *source);

#endif
