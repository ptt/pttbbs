/* $Id$ */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <cmsys.h>
#include <cmbbs.h>
#include <var.h>

#include "server.h"

struct pop3_ctx {
    int state;
    char userid[32];
    int uid;
};

enum {
    POP3_AUTH,
    POP3_TRANS,
    POP3_UPDATE,
    POP3_CLEANUP
};

typedef struct {
    char *cmd;
    void (*func)(struct bufferevent *, struct pop3_ctx *, int argc, char **);
} CMD;

static const char msg_ok[] = "+OK\r\n";
static const char msg_err_no_user[] = "-ERR user not found\r\n";
static const char msg_err_no_msg[] = "-ERR no such message\r\n";

void
cmd_unknown(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    static const char msg[] = "-ERR unknown command\r\n";
    evbuffer_add_reference(bufferevent_get_output(bev), msg, strlen(msg), NULL, NULL);
}

void
cmd_capa(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    static const char msg[] = "+OK\r\nUSER\r\n.\r\n";
    evbuffer_add_reference(bufferevent_get_output(bev), msg, strlen(msg), NULL, NULL);
}

void
cmd_user(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    ctx->uid = searchuser(*argv, ctx->userid);
    if (ctx->uid < 1 || ctx->uid > MAX_USERS)
	evbuffer_add_reference(bufferevent_get_output(bev),
		msg_err_no_user, strlen(msg_err_no_user), NULL, NULL);
    else
	evbuffer_add_reference(bufferevent_get_output(bev),
		msg_ok, strlen(msg_ok), NULL, NULL);
}

void
cmd_pass(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    userec_t xuser;
    char * pw;

    if (passwd_query(ctx->uid, &xuser) < 0) {
	evbuffer_add_reference(bufferevent_get_output(bev),
		msg_err_no_user, strlen(msg_err_no_user), NULL, NULL);
	return;
    }

    pw = crypt(*argv, xuser.passwd);
    if (strcmp(pw, xuser.passwd) == 0) {
	evbuffer_add_reference(bufferevent_get_output(bev),
		msg_ok, strlen(msg_ok), NULL, NULL);
	ctx->state = POP3_TRANS;
    } else {
	static const char msg[] = "-ERR password incorrect \r\n";
	evbuffer_add_reference(bufferevent_get_output(bev),
	       	msg, strlen(msg), NULL, NULL);
    }
}

void
cmd_quit(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    static const char msg[] = "+OK bye\r\n";
    evbuffer_add_reference(bufferevent_get_output(bev),
	    msg, strlen(msg), NULL, NULL);
    
    const struct timeval tv = {0, 1000};
    bufferevent_set_timeouts(bev, &tv, &tv);

    if (ctx->state == POP3_TRANS)
	ctx->state = POP3_UPDATE;
    else
	ctx->state = POP3_CLEANUP;
}

static const CMD auth_cmdlist[] = {

    {"user", cmd_user},
    {"pass", cmd_pass},
    {"quit", cmd_quit},
    {"capa", cmd_capa},
    {NULL, cmd_unknown}
};

/* Transaction state commands not implemented */
void
cmd_stat(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    static const char msg[] = "+OK 0 0\r\n";
    evbuffer_add_reference(bufferevent_get_output(bev), msg, strlen(msg), NULL, NULL);
}

void
cmd_list(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    static const char msg[] = "+OK\r\n.\r\n";
    if (*argv)
	evbuffer_add_reference(bufferevent_get_output(bev),
		msg_err_no_msg, strlen(msg_err_no_msg), NULL, NULL);
    else
	evbuffer_add_reference(bufferevent_get_output(bev), msg, strlen(msg), NULL, NULL);
}

void
cmd_retr(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    evbuffer_add_reference(bufferevent_get_output(bev),
	    msg_err_no_msg, strlen(msg_err_no_msg), NULL, NULL);
}

void
cmd_dele(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    evbuffer_add_reference(bufferevent_get_output(bev),
	    msg_err_no_msg, strlen(msg_err_no_msg), NULL, NULL);
}

void
cmd_noop(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    evbuffer_add_reference(bufferevent_get_output(bev),
	    msg_ok, strlen(msg_ok), NULL, NULL);
}

void
cmd_rset(struct bufferevent *bev, struct pop3_ctx *ctx, int argc, char **argv)
{
    evbuffer_add_reference(bufferevent_get_output(bev),
	    msg_ok, strlen(msg_ok), NULL, NULL);
}

static const CMD trans_cmdlist[] = {
    {"stat", cmd_stat},
    {"list", cmd_list},
    {"retr", cmd_retr},
    {"dele", cmd_dele},
    {"noop", cmd_noop},
    {"rset", cmd_rset},
    {"capa", cmd_capa},
    {"quit", cmd_quit},
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
    struct pop3_ctx *pop3_ctx = ctx;

    if (!line)
	return;

    argc = split_args(line, &argv);

    if (pop3_ctx->state == POP3_AUTH) {
	for (i = 0; auth_cmdlist[i].cmd; i++)
	    if (evutil_ascii_strcasecmp(argv[0], auth_cmdlist[i].cmd) == 0)
		break;
	(auth_cmdlist[i].func)(bev, ctx, argc - 1, argv + 1);
    } else if (pop3_ctx->state == POP3_TRANS) {
	for (i = 0; trans_cmdlist[i].cmd; i++)
	    if (evutil_ascii_strcasecmp(argv[0], trans_cmdlist[i].cmd) == 0)
		break;
	(trans_cmdlist[i].func)(bev, ctx, argc - 1, argv + 1);
    }

    free(argv);
    free(line);
}

void
client_event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_TIMEOUT | BEV_EVENT_ERROR)) {
	bufferevent_free(bev);
	free(ctx);
    }
}

void setup_client(struct event_base *base, evutil_socket_t fd,
       	struct sockaddr *address, int socklen)
{
    struct pop3_ctx *ctx = calloc(1, sizeof(struct pop3_ctx));
    ctx->state = POP3_AUTH;

    struct bufferevent *bev = bufferevent_socket_new(base, fd,
	    BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(bev, client_read_cb, NULL, client_event_cb, ctx);
    bufferevent_set_timeouts(bev, common_timeout, common_timeout);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    evbuffer_add_reference(bufferevent_get_output(bev),
	    "+OK PttBBS POP3 ready\r\n", 23, NULL, NULL);
}

void
setup_program()
{
    setuid(BBSUID);
    setgid(BBSGID);
    chdir(BBSHOME);

    attach_SHM();
}

