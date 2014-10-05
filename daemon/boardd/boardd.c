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

	DBCS_safe_trim(fhdr.title);

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
	    if ((fd = open(path, O_RDONLY)) < 0)
		return;
            if (fstat(fd, &st) < 0 ||
                st.st_size == 0 ||
                evbuffer_add_file(buf, fd, 0, st.st_size) != 0)
                close(fd);
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


#ifdef CONVERT_TO_UTF8

static int
move_string_end(char **buf)
{
    int n = 0;
    while (**buf != '\0') {
	(*buf)++;
	n++;
    }
    return n;
}

// Make ANSI control code
//   fg, bg, bright are the original color code (eg. 30, 42, 1)
//   provide -1 means no change
//   all -1 means reset
static void
make_ansi_ctrl(char *buf, int size, int fg, int bg, int bright)
{
    int sep = 0;
    strncpy(buf, "\033[", size);
    size -= move_string_end(&buf);
    if (bright >= 0) {
	snprintf(buf, size, "%s%d", sep ? ";" : "", bright);
	size -= move_string_end(&buf);
	sep = 1;
    }
    if (fg >= 0) {
	snprintf(buf, size, "%s%d", sep ? ";" : "", fg);
	size -= move_string_end(&buf);
	sep = 1;
    }
    if (bg >= 0) {
	snprintf(buf, size, "%s%d", sep ? ";" : "", bg);
	size -= move_string_end(&buf);
	sep = 1;
    }
    snprintf(buf, size, "m");
}

// Make extended ANSI control code
//   1 ==> 111, 0 ==> 110,
//   3x ==> 13x, 4y ==> 14y.
//   provide -1 means no change
//   all -1 means reset
static void
make_ext_ansi_ctrl(char *buf, int size, int fg, int bg, int bright)
{
    make_ansi_ctrl(buf, size,
                   fg >= 0 ? 100 + fg : fg,
                   bg >= 0 ? 100 + bg : bg,
                   bright >= 0 ? 110 + bright : bright);
}

static int
evbuffer_add_ansi_escape_code(struct evbuffer *destination, int fg, int bg, int bright)
{
    char ansicode[16];
    make_ansi_ctrl(ansicode, sizeof(ansicode), fg, bg, bright);
    return evbuffer_add_printf(destination, ansicode, strlen(ansicode));
}

static int
evbuffer_add_ext_ansi_escape_code(struct evbuffer *destination, int fg, int bg, int bright)
{
    char ansicode[24];
    make_ext_ansi_ctrl(ansicode, sizeof(ansicode), fg, bg, bright);
    return evbuffer_add_printf(destination, ansicode, strlen(ansicode));
}

// Converts given evbuffer contents to UTF-8 and returns the new buffer.
// The original buffer is freed. Returns NULL on error

struct evbuffer *
evbuffer_b2u(struct evbuffer *source)
{
    unsigned char c[16];
    int out = 0;

    if (evbuffer_get_length(source) == 0)
	return source;

    struct evbuffer *destination = evbuffer_new();

    // Peek at first byte
    while (evbuffer_copyout(source, c, 1) > 0) {
	if (isascii(c[0])) {
	    if (evbuffer_add(destination, c, 1) < 0)
		break;

	    // Remove byte from source buffer
	    evbuffer_drain(source, 1);
	    out++;
	} else {
	    // Big5
	    int todrain = 2;

	    // Handle in-character colors
	    int fg = -1, bg = -1, bright = -1;
	    int n = evbuffer_copyout(source, c, sizeof(c));
	    if (n < 2)
		break;
	    while (c[1] == '\033') {
		c[n - 1] = '\0';

		// At least have \033[m
		if (n < 4 || c[2] != '[')
		    break;

		unsigned char *p = c + 3;
		if (*p == 'm') {
		    // ANSI reset
		    fg = 7;
		    bg = 0;
		    bright = 0;
		}
		while (1) {
		    int v = (int) strtol((char *)p, (char **)&p, 10);
		    if (*p != 'm' && *p != ';')
			break;

		    if (v == 0)
			bright = 0;
		    else if (v == 1)
			bright = 1;
		    else if (v >= 30 && v <= 37)
			fg = v;
		    else if (v >= 40 && v <= 47)
			bg = v;

		    if (*p == 'm')
			break;
		    p++;
		}
		if (*p != 'm') {
		    // Skip malicious or unsupported codes
		    fg = bg = bright = -1;
		    break;
		} else {
		    evbuffer_drain(source, p - c + 1);
		    todrain = 1; // We keep a byte on buffer, so fix offset
		    n = evbuffer_copyout(source, c + 1, sizeof(c) - 1);
		    if (n < 1)
			break;
		    n++;
		}
	    }
#ifdef EXTENDED_INCHAR_ANSI
	    // Output control codes before the Big5 character
	    if (fg >= 0 || bg >= 0 || bright >= 0) {
                int dlen = evbuffer_add_ext_ansi_escape_code(destination, fg, bg, bright);
                if (dlen < 0)
                    break;
                out += dlen;
	    }
#endif

	    // n may be changed, check again
	    if (n < 2)
		break;

	    uint8_t utf8[4];
	    int len = ucs2utf(b2u_table[c[0] << 8 | c[1]], utf8);
	    utf8[len] = 0;

	    if (evbuffer_add(destination, utf8, len) < 0)
		break;

#ifndef EXTENDED_INCHAR_ANSI
            // Output in-char control codes to make state consistent
            if (fg >= 0 || bg >= 0 || bright >= 0) {
                int dlen = evbuffer_add_ansi_escape_code(destination, fg, bg, bright);
                if (dlen < 0)
                    break;
                out += dlen;
            }
#endif

	    // Remove DBCS character from source buffer
	    evbuffer_drain(source, todrain);
	    out += len;
	}
    }

    if (evbuffer_get_length(source) == 0 && out) {
	// Success
	evbuffer_free(source);
	return destination;
    }

    // Fail
    evbuffer_free(source);
    evbuffer_free(destination);
    return NULL;
}

#endif // CONVERT_TO_UTF8

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
#ifdef CONVERT_TO_UTF8
	buf = evbuffer_b2u(buf);
	if (buf == NULL) {
	    // Failed to convert
	    buf = evbuffer_new();
	    continue;
	}
#endif
	evbuffer_add_printf(output, "VALUE %s 0 %ld\r\n", *argv, evbuffer_get_length(buf));
	evbuffer_add_buffer(output, buf);
	evbuffer_add_printf(output, "\r\n");
    } while (*++argv);

    evbuffer_add_reference(output, "END\r\n", 5, NULL, NULL);

    evbuffer_free(buf);
}

void
cmd_version(struct bufferevent *bev, void *ctx, int argc, char **argv)
{
    static const char msg[] = "VERSION 0.0.2\r\n";
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

