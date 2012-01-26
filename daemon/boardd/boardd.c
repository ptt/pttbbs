// $Id$
// Memcached protocol based board data export daemon

// Copyright (c) 2010-2011, Chen-Yu Tsai <wens@csie.org>
// All rights reserved.

// TODO:
//  1. [done] add hotboard support
//  2. [done] rewrite with libevent 2.0
//  3. [done] split out independent server code
//  4. [done] add article list support
//  5. [done] add article content support
//  6. [done] encode output in UTF-8 (with UAO support)
//  7. encode article list in JSON for better structure

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include <cmbbs.h>
#include <var.h>
#include <perm.h>

#include "server.h"

#define CONVERT_TO_UTF8

#define DEFAULT_ARTICLE_LIST 20

// helper function

#define BOARD_HIDDEN(bptr) (bptr->brdattr & (BRD_HIDE | BRD_TOP) || \
    (bptr->level & ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK) && \
     !(bptr->brdattr & BRD_POSTMASK)))

static void
article_list(struct evbuffer *buf, boardheader_t *bptr, int offset, int length)
{
    int total, fd = -1;
    char path[PATH_MAX];
    fileheader_t fhdr;

    setbfile(path, bptr->brdname, FN_DIR);
    total = get_num_records(path, sizeof(fileheader_t));

    if (total <= 0)
	return;

    while (offset < 0)
	offset += total;

    while (length-- > 0) {
	if (get_records_keep(path, &fhdr, sizeof(fhdr), ++offset, 1, &fd) <= 0)
	    break;

	evbuffer_add_printf(buf, "%d,%s,%s,%d,%d,%s,%s\n",
		offset, fhdr.filename, fhdr.date, fhdr.recommend,
		fhdr.filemode, fhdr.owner, fhdr.title);
    }

    if (fd >= 0)
	close(fd);
}

static void
answer_key(struct evbuffer *buf, const char *key)
{
    int bid;
    boardheader_t *bptr;

    if (isdigit(*key)) {
	char *p;

	if ((bid = atoi(key)) == 0 || bid > MAX_BOARD)
	    return;

	if ((p = strchr(key, '.')) == NULL)
	    return;

	bptr = getbcache(bid);

	if (!bptr->brdname[0] || BOARD_HIDDEN(bptr))
	    return;

	key = p + 1;

	if (strcmp(key, "isboard") == 0)
	    evbuffer_add_printf(buf, "%d", (bptr->brdattr & BRD_GROUPBOARD) ? 0 : 1);
	else if (strcmp(key, "over18") == 0)
	    evbuffer_add_printf(buf, "%d", (bptr->brdattr & BRD_OVER18) ? 1 : 0);
	else if (strcmp(key, "hidden") == 0)
	    evbuffer_add_printf(buf, "%d", BOARD_HIDDEN(bptr) ? 1 : 0);
	else if (strcmp(key, "brdname") == 0)
	    evbuffer_add(buf, bptr->brdname, strlen(bptr->brdname));
	else if (strcmp(key, "title") == 0)
	    evbuffer_add(buf, bptr->title + 7, strlen(bptr->title) - 7);
	else if (strcmp(key, "class") == 0)
	    evbuffer_add(buf, bptr->title, 4);
	else if (strcmp(key, "BM") == 0)
	    evbuffer_add(buf, bptr->BM, strlen(bptr->BM));
	else if (strcmp(key, "parent") == 0)
	    evbuffer_add_printf(buf, "%d", bptr->parent);
	else if (strcmp(key, "count") == 0) {
	    char path[PATH_MAX];
	    setbfile(path, bptr->brdname, FN_DIR);
	    evbuffer_add_printf(buf, "%d", get_num_records(path, sizeof(fileheader_t)));
	} else if (strcmp(key, "children") == 0) {
	    if (!(bptr->brdattr & BRD_GROUPBOARD))
		return;

	    for (bid = bptr->firstchild[1]; bid > 0; bid = bptr->next[1]) {
		bptr = getbcache(bid);
		evbuffer_add_printf(buf, "%d,", bid);
	    }
	} else if (strncmp(key, "articles.", 9) == 0) {
	    int offset, length;

	    key += 9;

	    if (!isdigit(*key) && *key != '-')
		return;

	    offset = atoi(key);
	    p = strchr(key, '.');

	    if (!p || (length = atoi(p+1)) == 0)
		length = DEFAULT_ARTICLE_LIST;

	    return article_list(buf, bptr, offset, length);
	} else if (strncmp(key, "article.", 8) == 0) {
	    if (strncmp(key + 8, "M.", 2) != 0)
		return;

	    char path[PATH_MAX];
	    struct stat st;
	    int fd;

	    setbfile(path, bptr->brdname, key + 8);
	    if ((fd = open(path, O_RDONLY)) < 0 || fstat(fd, &st) < 0)
		return;

	    evbuffer_add_file(buf, fd, 0, st.st_size);
	} else
	    return;
    } else if (strncmp(key, "tobid.", 6) == 0) {
	bid = getbnum(key + 6);
	bptr = getbcache(bid);

	if (!bptr->brdname[0] || BOARD_HIDDEN(bptr))
	    return;

	evbuffer_add_printf(buf, "%d", bid);
#if HOTBOARDCACHE
    } else if (strncmp(key, "hotboards", 9) == 0) {
	for (bid = 0; bid < SHM->nHOTs; bid++) {
	    bptr = getbcache(SHM->HBcache[bid] + 1);
	    if (BOARD_HIDDEN(bptr))
		continue;
	    evbuffer_add_printf(buf, "%d,", SHM->HBcache[bid] + 1);
	}
#endif
    }
}

// Command functions

void
cmd_get(struct bufferevent *bev, void *ctx, int argc, char **argv)
{
    struct evbuffer *output = bufferevent_get_output(bev),
		    *buf = evbuffer_new();

    if (*argv == NULL) {
	evbuffer_add_reference(output, "ERROR\r\n", 7, NULL, NULL);
	return;
    }

    do {
	answer_key(buf, *argv);
	if (evbuffer_get_length(buf) == 0)
	    continue;
	evbuffer_add_printf(output, "VALUE %s 0 %ld\r\n", *argv, evbuffer_get_length(buf));
	evbuffer_add_buffer(output, buf);
	evbuffer_add_printf(output, "\r\n");
    } while (*++argv);

    evbuffer_add_reference(output, "END\r\n", 5, NULL, NULL);
}

void
cmd_version(struct bufferevent *bev, void *ctx, int argc, char **argv)
{
    static const char msg[] = "VERSION 0.0.1\r\n";
    evbuffer_add_reference(bufferevent_get_output(bev), msg, strlen(msg), NULL, NULL);
}

void
cmd_unknown(struct bufferevent *bev, void *ctx, int argc, char **argv)
{
    static const char msg[] = "SERVER_ERROR Not implemented\r\n";
    evbuffer_add_reference(bufferevent_get_output(bev), msg, strlen(msg), NULL, NULL);
}

void
cmd_quit(struct bufferevent *bev, void *ctx, int argc, char **argv)
{
    bufferevent_free(bev);
}

static const struct {
    const char *cmd;
    void (*func)(struct bufferevent *bev, void *ctx, int argc, char **argv);
} cmdlist[] = {
    {"get", cmd_get},
    {"quit", cmd_quit},
    {"version", cmd_version},
    {NULL, cmd_unknown}
};

void
client_read_cb(struct bufferevent *bev, void *ctx)
{
    int argc, i;
    char **argv;
    size_t len;
    struct evbuffer *input = bufferevent_get_input(bev);
    char *line = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF);

    if (!line)
	return;

    argc = split_args(line, &argv);

    for (i = 0; cmdlist[i].cmd; i++)
	if (evutil_ascii_strcasecmp(line, cmdlist[i].cmd) == 0)
	    break;

    (cmdlist[i].func)(bev, ctx, argc - 1, argv + 1);

    free(argv);
    free(line);
}

void
setup_program()
{
    setuid(BBSUID);
    setgid(BBSGID);
    chdir(BBSHOME);

    attach_SHM();
}

#ifdef CONVERT_TO_UTF8

enum bufferevent_filter_result
filter_pass(struct evbuffer *source, struct evbuffer *destination,
	ev_ssize_t dst_limit, enum bufferevent_flush_mode mode, void *ctx)
{
    int len = evbuffer_get_length(source);

    if (len == 0)
	return BEV_NEED_MORE;

    len = evbuffer_remove_buffer(source, destination, dst_limit >= 0 ? dst_limit : len);

    return len > 0 ? BEV_OK : (len == 0 ? BEV_NEED_MORE : BEV_ERROR);
}

enum bufferevent_filter_result
filter_b2u(struct evbuffer *source, struct evbuffer *destination,
	ev_ssize_t dst_limit, enum bufferevent_flush_mode mode, void *ctx)
{
    char c[2];
    int out = 0;

    while (evbuffer_copyout(source, c, 1) > 0) {
	if (isascii(c[0])) {
	    if (dst_limit > 0 && out >= dst_limit)
		break;

	    if (evbuffer_add(destination, c, 1) < 0)
		return BEV_ERROR;
	    evbuffer_drain(source, 1);
	    out++;
	} else {
	    // Big5
	    if (dst_limit > 0 && (out + 1) >= dst_limit)
		break;

	    // c must be little endian.
	    c[1] = c[0];

	    // Get second byte
	    if (evbuffer_copyout(source, c, 1) <= 0)
		break;

	    uint16_t ucs = b2u_table[*(uint16_t*)c];
	    uint8_t utf8[4];

	    int len = ucs2utf(ucs, utf8);
	    utf8[len] = 0;

	    if (evbuffer_add(destination, utf8, len) < 0)
		return BEV_ERROR;
	    evbuffer_drain(source, 2);
	    out += len;
	}
    }

    return out ? BEV_OK : BEV_NEED_MORE;
}

void
setup_client(struct event_base *base, evutil_socket_t fd,
	struct sockaddr *address, int socklen)
{
    struct bufferevent *bev = bufferevent_socket_new(base, fd,
	    BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    struct bufferevent *bev2 = bufferevent_filter_new(bev, filter_b2u,
	    filter_pass, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS,
	    NULL, NULL);
    bufferevent_setcb(bev2, client_read_cb, NULL, client_event_cb, NULL);
    bufferevent_set_timeouts(bev2, common_timeout, common_timeout);
    bufferevent_enable(bev2, EV_READ|EV_WRITE);
}

#endif // CONVERT_TO_UTF8
