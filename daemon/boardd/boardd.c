// $Id$

// Copyright (c) 2010, Chen-Yu Tsai <wens@csie.org>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. The names of the author and contributors may be used to endorse or
//    promote products derived from this software without specific prior
//    written permission.
//
// THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <event.h>

#include "cmsys.h"
#include "cmbbs.h"
#include "config.h"
#include "var.h"

static struct event ev_listen;
static int clients = 0;

static const char whitespace[] = " \t\r\n";

#define TIMEOUT 600
#define MAX_CLIENTS 1000
#define BUF_SIZE 8192

typedef struct {
    struct bufferevent * bufev;
    int fd;
} conn_ctx;

typedef struct {
    char *cmd;
    void (*func)(conn_ctx *ctx, const char * arg);
} CMD;

static char databuf[BUF_SIZE];

void cb_endconn(struct bufferevent *bufev, short what, void *arg);

// helper function

#define BOARD_HIDDEN(bptr) (bptr->brdattr & (BRD_HIDE | BRD_TOP) || \
    (bptr->level & ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK) && \
     !(bptr->brdattr & BRD_POSTMASK)))
void answer_key(const char *key, int keylen, struct evbuffer *buf)
{
    char *data, *p;
    int bid;
    boardheader_t *bptr;

    if (isdigit(*key)) {
	if ((bid = atoi(key)) == 0 || bid > MAX_BOARD)
	    return;

	if ((p = memchr(key, '.', keylen)) == NULL)
	    return;

	p++;
	bptr = getbcache(bid);

	if (!bptr->brdname[0] || BOARD_HIDDEN(bptr))
	    return;

	if (strncmp(p, "isboard", 7) == 0)
	    data = (bptr->brdattr & BRD_GROUPBOARD) ? "0" : "1";
	else if (strncmp(p, "hidden", 6) == 0)
	    data = BOARD_HIDDEN(bptr) ? "1" : "0";
	else if (strncmp(p, "brdname", 7) == 0)
	    data = bptr->brdname;
	else if (strncmp(p, "over18", 6) == 0)
	    data = (bptr->brdattr & BRD_OVER18) ? "1" : "0";
	else if (strncmp(p, "title", 5) == 0)
	    data = bptr->title;
	else if (strncmp(p, "BM", 2) == 0)
	    data = bptr->BM;
	else if (strncmp(p, "parent", 6) == 0) {
	    snprintf(databuf, sizeof(databuf), "%d", bptr->parent);
	    data = databuf;
	} else if (strncmp(p, "children", 8) == 0) {
	    if (!(bptr->brdattr & BRD_GROUPBOARD))
		return;

	    data = p = databuf;
	    for (bid = bptr->firstchild[1]; bid > 0; bid = bptr->next[1]) {
		bptr = getbcache(bid);
		p += snprintf(p, sizeof(databuf) - (databuf - p), "%d,", bid);
	    }

	    *--p = '\0';
	} else
	    return;
    } else if (strncmp(key, "tobid.", 6) == 0) {
	bid = getbnum(key + 6);
	bptr = getbcache(bid);

	if (!bptr->brdname[0] || BOARD_HIDDEN(bptr))
	    return;

	snprintf(databuf, sizeof(databuf), "%d", bid);
	data = databuf;
#if HOTBOARDCACHE
    } else if (strncmp(key, "hotboards", 9) == 0) {
	data = p = databuf;
	for (bid = 0; bid < SHM->nHOTs; bid++) {
	    bptr = getbcache(SHM->HBcache[bid] + 1);
	    if (BOARD_HIDDEN(bptr))
		continue;
	    p += snprintf(p, sizeof(databuf) - (databuf - p), "%d,", SHM->HBcache[bid] + 1);
	}

	*--p = '\0';
#endif
    } else
	return;

    evbuffer_add_printf(buf, "VALUE %.*s 0 %ld\r\n%s\r\n", keylen, key, strlen(data), data);
}

// Command functions

void
cmd_get(conn_ctx *ctx, const char *arg)
{
    const char *key;
    size_t keylen;
    static struct evbuffer *buf;

    key = arg + strspn(arg, whitespace);

    if (*key == '\0') {
	bufferevent_write(ctx->bufev, "ERROR\r\n", 7);
	return;
    }

    if (buf == NULL)
	if ((buf = evbuffer_new()) == NULL) {
	    const char msg[] = "SERVER_ERROR Can't allocate buffer\r\n";
	    bufferevent_write(ctx->bufev, msg, strlen(msg));
	    return;
	}

    while (*key) {
	keylen = strcspn(key, whitespace);

	answer_key(key, keylen, buf);

	bufferevent_write_buffer(ctx->bufev, buf);

	key += keylen;
	key += strspn(key, whitespace);
    }

    bufferevent_write(ctx->bufev, "END\r\n", 5);
}

void
cmd_version(conn_ctx *ctx, const char *arg)
{
    const char msg[] = "VERSION 0.0.1\r\n";
    bufferevent_write(ctx->bufev, msg, strlen(msg));
}

void
cmd_unknown(conn_ctx *ctx, const char *arg)
{
    const char msg[] = "SERVER_ERROR Not implemented\r\n";
    bufferevent_write(ctx->bufev, msg, strlen(msg));
}

void
cmd_quit(conn_ctx *ctx, const char * arg)
{
    cb_endconn(ctx->bufev, 0, ctx);
}

static const CMD cmdlist[] = {
    {"get", cmd_get},
    {"quit", cmd_quit},
    {"version", cmd_version},
    {NULL, cmd_unknown}
};

// Callbacks

void
cb_client(struct bufferevent *bufev, void *arg)
{
    conn_ctx *ctx = arg;
    char *p, *cmd, *line = NULL;
    int i;

    if ((line = evbuffer_readline(EVBUFFER_INPUT(bufev))) == NULL)
	return;

    cmd = line + strspn(line, whitespace);
    p = cmd + strcspn(cmd, whitespace);
    *p++ = '\0';
    p += strspn(p, whitespace);

    for (i = 0; cmdlist[i].cmd; i++)
	if (strcasecmp(line, cmdlist[i].cmd) == 0)
	    break;
    (cmdlist[i].func)(ctx, p);

    free(line);
}

void
cb_endconn(struct bufferevent *bufev, short what, void *arg)
{
    conn_ctx *ctx = arg;

    close(ctx->fd);
    bufferevent_free(bufev);
    free(ctx);

    if (clients == MAX_CLIENTS)
	event_add(&ev_listen, NULL);
    clients--;
}

void
setup_client(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    conn_ctx *ctx = calloc(1, sizeof(conn_ctx));

    if (ctx == NULL) {
	close(fd);
	return;
    }

    if ((ctx->bufev = bufferevent_new(fd, cb_client, NULL, cb_endconn, ctx)) == NULL) {
	free(ctx);
	close(fd);
	return;
    }

    ctx->fd = fd;
    bufferevent_settimeout(ctx->bufev, TIMEOUT, TIMEOUT);
    bufferevent_enable(ctx->bufev, EV_READ | EV_WRITE);
}

void
cb_listen(int fd, short event, void *arg)
{
    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);
    int cfd;

    if ((cfd = accept(fd, (struct sockaddr *)&clientaddr, &len)) < 0 )
	return;

    setup_client(cfd);

    clients++;

    if (clients > MAX_CLIENTS)
	event_del(&ev_listen);
}

int main(int argc, char *argv[])
{
    int ch, sfd = 0, inetd = 0, daemon = 1;
    char *iface_ip = "127.0.0.1:5150";

    Signal(SIGPIPE, SIG_IGN);
    while ((ch = getopt(argc, argv, "Dil:h")) != -1)
	switch (ch) {
	    case 'D':
		daemon = 0;
		break;
	    case 'i':
		inetd = 1;
		break;
	    case 'l':
		iface_ip = optarg;
		break;
	    case 'h':
	    default:
		fprintf(stderr, "usage: boardd [-D] [-i] [-l [interface_ip]:port]\n");
		return 1;
	}

    if (!inetd)
	if ((sfd = tobind(iface_ip)) < 0)
	    return 1;

    setuid(BBSUID);
    setgid(BBSGID);

    srandom(getpid() + time(NULL));
    attach_SHM();

    if (daemon)
	daemonize(BBSHOME "/run/boardd.pid", NULL);

    event_init();
    if (!inetd) {
	fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK);
	event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, cb_listen, &ev_listen);
	event_add(&ev_listen, NULL);
    } else
	setup_client(0);
    event_dispatch();

    return 0;
}
