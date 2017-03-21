#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <event2/buffer.h>

#include "var.h"
#include "cmbbs.h"
#include "boardd.h"

int
is_valid_article_filename(const char *filename)
{
    return !strncmp(filename, "M.", 2);
}

static int
answer_file(struct evbuffer *buf, const char *path, struct stat *st,
	    const char *ck, int cklen, int offset, int maxlen)
{
    struct stat local_st;
    int fd;
    int ret = 0;

    if (st == NULL)
	st = &local_st;

    if ((fd = open(path, O_RDONLY)) < 0 || fstat(fd, st) < 0) {
	ret = SELECT_ENOTFOUND;
	goto answer_file_errout;
    }

    if (ck && cklen) {
	char ckbuf[128];
	snprintf(ckbuf, sizeof(ckbuf), "%d-%d", (int) st->st_dev, (int) st->st_ino);
	if ((size_t) cklen != strlen(ckbuf) || strncmp(ck, ckbuf, cklen) != 0) {
	    ret = SELECT_ECHANGED;
	    goto answer_file_errout;
	}
    }

    if (offset < 0)
	offset += st->st_size;
    if (offset < 0)
	offset = 0;
    if (offset > st->st_size) {
	ret = SELECT_ERANGE;
	goto answer_file_errout;
    }

    if (maxlen < 0 || offset + maxlen > st->st_size)
	maxlen = st->st_size - offset;

    if (maxlen == 0) {
	close(fd);
	return 0;
    }

    if (evbuffer_add_file(buf, fd, offset, maxlen) == 0)
	return 0;

answer_file_errout:
    if (fd >= 0)
	close(fd);
    if (ret < 0)
	return ret;
    return SELECT_EUNKNOWN;
}

static int
find_good_truncate_point_from_begin(const char *content, int size)
{
    int last_startline = 0;
    int last_charend = 0;
    int last_dbcstail = 0;
    int i;
    const char *p;
    for (i = 1, p = content; i <= size; i++, p++) {
	if (i > last_dbcstail) {
	    if (IS_DBCSLEAD(*p)) {
		last_dbcstail = i + 1;
		if (i + 1 <= size)
		    last_charend = i + 1;
	    } else
		last_charend = i;
	}
	if (*p == '\n')
	    last_startline = i;
    }
    return last_startline > 0 ? last_startline : last_charend;
}

static int
find_good_truncate_point_from_end(const char *content, int size)
{
    int i;
    const char *p;
    for (i = 1, p = content; i <= size; i++, p++)
	if (*p == '\n')
	    return i;
    return 0;
}

static int
select_article_head(const char *data, int len, int *offset, int *size, void *ctx)
{
    *offset = 0;
    *size = find_good_truncate_point_from_begin(data, len);
    return 0;
}

static int
select_article_tail(const char *data, int len, int *offset, int *size, void *ctx)
{
    *offset = find_good_truncate_point_from_end(data, len);
    *size = find_good_truncate_point_from_begin(data + *offset, len - *offset);
    return 0;
}

static int
select_article_part(const char *data, int len, int *offset, int *size, void *ctx)
{
    *offset = 0;
    *size = len;
    return 0;
}

static void
cleanup_evbuffer(const void *data, size_t datalen, void *extra)
{
    evbuffer_free((struct evbuffer *)extra);
}

static int
evbuffer_slice(struct evbuffer *buf, int offset, int size)
{
    int len = evbuffer_get_length(buf);
    if (offset + size > len)
	return -1;

    struct evbuffer *back = evbuffer_new();
    evbuffer_add_buffer(back, buf);

    if (evbuffer_add_reference(buf, evbuffer_pullup(back, len) + offset,
			       size, cleanup_evbuffer, back) == 0)
	return 0;

    evbuffer_free(back);
    return -1;
}

typedef int (*select_part_func)(const char *data, int len, int *offset,
				int *size, void *ctx);

int
select_article(struct evbuffer *buf, select_result_t *result, const select_spec_t *spec)
{
    select_part_func sfunc;
    switch (spec->type) {
	case SELECT_TYPE_HEAD: sfunc = select_article_head; break;
	case SELECT_TYPE_TAIL: sfunc = select_article_tail; break;
	case SELECT_TYPE_PART: sfunc = select_article_part; break;
	default: return SELECT_EARG;
    }

    struct stat st;
    int ret;
    if ((ret = answer_file(buf, spec->filename, &st, spec->cachekey,
		           spec->cachekey_len, spec->offset, spec->maxlen)) < 0)
	return ret;

    int sel_offset, sel_size;
    int len = evbuffer_get_length(buf);
    const char *data = (const char *) evbuffer_pullup(buf, -1);
    if (sfunc(data, len, &sel_offset, &sel_size, NULL) != 0 ||
	evbuffer_slice(buf, sel_offset, sel_size) != 0)
	return SELECT_EUNKNOWN;

    if (result != NULL) {
	snprintf(result->cachekey, sizeof(result->cachekey), "%d-%d",
		 (int)st.st_dev, (int)st.st_ino);
	result->size = st.st_size;
	result->sel_offset = sel_offset;
	result->sel_size = sel_size;
    }
    return SELECT_OK;
}
