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

#include "boardd.h"
#include "proto.h"

#define DEFAULT_ARTICLE_LIST 20

static int g_convert_to_utf8 = 1;

// helper function

#define BOARD_HIDDEN(bptr) (bptr->brdattr & (BRD_HIDE | BRD_TOP) || \
    (bptr->level & ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK) && \
     !(bptr->brdattr & BRD_POSTMASK)))

static void
dir_list(struct evbuffer *buf, const char *path, int offset, int length)
{
    int total, fd = -1;
    fileheader_t fhdr;

    total = get_num_records(path, sizeof(fileheader_t));

    if (total <= 0)
	return;

    while (offset < 0)
	offset += total;

    while (length < 0 || length-- > 0) {
	if (get_records_keep(path, &fhdr, sizeof(fhdr), ++offset, 1, &fd) <= 0)
	    break;

	DBCS_safe_trim(fhdr.title);

	evbuffer_add_printf(buf, "%d,%s,%s,%d,%d,%s,%s\n",
		offset, fhdr.filename, fhdr.date, fhdr.recommend,
		fhdr.filemode, fhdr.owner, fhdr.title);
    }

    if (fd >= 0)
	close(fd);
}

static void
article_list(struct evbuffer *buf, boardheader_t *bptr, int offset, int length)
{
    char path[PATH_MAX];
    setbfile(path, bptr->brdname, FN_DIR);
    dir_list(buf, path, offset, length);
}

static void
bottom_article_list(struct evbuffer *buf, boardheader_t *bptr)
{
    char path[PATH_MAX];
    setbfile(path, bptr->brdname, FN_DIR ".bottom");
    dir_list(buf, path, 0, -1);
}

static void
answer_key(struct evbuffer *buf, const char *key)
{
    int bid;
    boardheader_t *bptr;

    if (isdigit(*key)) {
	char *p;

	bid = strtol(key, &p, 10);
	if (bid < 1 || bid > MAX_BOARD)
	    return;

	if (*p != '.')
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
	else if (strcmp(key, "nuser") == 0)
	    evbuffer_add_printf(buf, "%d", bptr->nuser);
	else if (strcmp(key, "count") == 0) {
	    char path[PATH_MAX];
	    setbfile(path, bptr->brdname, FN_DIR);
	    evbuffer_add_printf(buf, "%d", get_num_records(path, sizeof(fileheader_t)));
	} else if (strcmp(key, "children") == 0) {
	    if (!(bptr->brdattr & BRD_GROUPBOARD))
		return;

	    const int type = BRD_GROUP_LL_TYPE_CLASS;
	    if (bptr->firstchild[type] == 0)
		resolve_board_group(bid, type);

	    for (bid = bptr->firstchild[type];
		 bid > 0; bid = bptr->next[type]) {
		bptr = getbcache(bid);
		evbuffer_add_printf(buf, "%d,", bid);
	    }
	} else if (strcmp(key, "bottoms") == 0) {
	    bottom_article_list(buf, bptr);
	} else if (strncmp(key, "articles.", 9) == 0) {
	    int offset, length;

	    key += 9;

	    if (!isdigit(*key) && *key != '-')
		return;

	    offset = atoi(key);
	    p = strchr(key, '.');

	    if (!p || (length = atoi(p+1)) == 0)
		length = DEFAULT_ARTICLE_LIST;

	    article_list(buf, bptr, offset, length);
	} else if (strncmp(key, "article.", 8) == 0) {
	    if (!is_valid_article_filename(key + 8))
		return;

	    char path[PATH_MAX];
	    struct stat st;
	    int fd;

	    setbfile(path, bptr->brdname, key + 8);
	    if ((fd = open(path, O_RDONLY)) < 0)
		return;
	    if (fstat(fd, &st) < 0 ||
		st.st_size == 0 ||
		evbuffer_add_file(buf, fd, 0, st.st_size) != 0)
		close(fd);
	} else if (strncmp(key, "articlestat.", 12) == 0) {
	    if (!is_valid_article_filename(key + 12))
		return;

	    char path[PATH_MAX];
	    struct stat st;

	    setbfile(path, bptr->brdname, key + 12);
	    if (stat(path, &st) < 0)
		return;

	    evbuffer_add_printf(buf, "%d-%d,%ld", (int) st.st_dev, (int) st.st_ino, st.st_size);
	} else if (strncmp(key, "articlepart.", 12) == 0) {
	    answer_articleselect(buf, bptr, key + 12, select_article_part, NULL);
	} else if (strncmp(key, "articlehead.", 12) == 0) {
	    answer_articleselect(buf, bptr, key + 12, select_article_head, NULL);
	} else if (strncmp(key, "articletail.", 12) == 0) {
	    answer_articleselect(buf, bptr, key + 12, select_article_tail, NULL);
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

int
cmd_get(struct evbuffer *output, void *ctx, int argc, char **argv)
{
    struct evbuffer *buf = evbuffer_new();

    if (*argv == NULL) {
	evbuffer_add_reference(output, "ERROR\r\n", 7, NULL, NULL);
	return 0;
    }

    do {
	answer_key(buf, *argv);
	if (evbuffer_get_length(buf) == 0)
	    continue;
	if (g_convert_to_utf8) {
	    buf = evbuffer_b2u(buf);
	    if (buf == NULL) {
		// Failed to convert
		buf = evbuffer_new();
		continue;
	    }
	}
	evbuffer_add_printf(output, "VALUE %s 0 %ld\r\n", *argv, evbuffer_get_length(buf));
	evbuffer_add_buffer(output, buf);
	evbuffer_add_printf(output, "\r\n");
    } while (*++argv);

    evbuffer_add_reference(output, "END\r\n", 5, NULL, NULL);

    evbuffer_free(buf);
    return 0;
}

int
cmd_version(struct evbuffer *output, void *ctx, int argc, char **argv)
{
    static const char msg[] = "VERSION 0.0.2\r\n";
    evbuffer_add_reference(output, msg, strlen(msg), NULL, NULL);
    return 0;
}

int
cmd_unknown(struct evbuffer *output, void *ctx, int argc, char **argv)
{
    static const char msg[] = "SERVER_ERROR Not implemented\r\n";
    evbuffer_add_reference(output, msg, strlen(msg), NULL, NULL);
    return 0;
}

int
cmd_quit(struct evbuffer *output, void *ctx, int argc, char **argv)
{
    return -1;
}

static const struct {
    const char *cmd;
    int (*func)(struct evbuffer *output, void *ctx, int argc, char **argv);
} cmdlist[] = {
    {"get", cmd_get},
    {"quit", cmd_quit},
    {"version", cmd_version},
    {NULL, cmd_unknown}
};

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

int
process_line(struct evbuffer *output, void *ctx, char *line)
{
    char **argv;
    int argc = split_args(line, &argv);
    int i;

    for (i = 0; cmdlist[i].cmd; i++)
	if (evutil_ascii_strcasecmp(line, cmdlist[i].cmd) == 0)
	    break;

    int result = (cmdlist[i].func)(output, ctx, argc - 1, argv + 1);

    free(argv);
    free(line);
    return result;
}

void
setup_program()
{
    setuid(BBSUID);
    setgid(BBSGID);
    chdir(BBSHOME);

    attach_SHM();
}

int main(int argc, char *argv[])
{
    int ch, use_grpc_server = 0, run_as_daemon = 1;
    const char *iface_ip = "127.0.0.1:5150";

    while ((ch = getopt(argc, argv, "5sDl:h")) != -1)
	switch (ch) {
	    case '5':
		g_convert_to_utf8 = 0;
		break;
	    case 's':
		use_grpc_server = 1;
		break;
	    case 'D':
		run_as_daemon = 0;
		break;
	    case 'l':
		iface_ip = optarg;
		break;
	    case 'h':
	    default:
		fprintf(stderr, "Usage: %s [-5] [-D] [-l interface_ip:port]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

    if (run_as_daemon)
	if (daemon(1, 1) < 0) {
	    perror("daemon");
	    exit(EXIT_FAILURE);
	}

    setup_program();

    signal(SIGPIPE, SIG_IGN);

    char *ipport = strdup(iface_ip);
    char *ip = strtok(ipport, ":");
    char *port = strtok(NULL, ":");
    if (use_grpc_server)
	start_grpc_server(ip, atoi(port));
    else
	start_server(ip, atoi(port));
    free(ipport);

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
