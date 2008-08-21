/* $Id$ */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <event.h>

#include "bbs.h"

static struct timeval tv = {600, 0};
static struct event ev_listen;
static int clients = 0;

#define READ_BLOCK 512
#define MAX_CLIENTS 500

struct client_state {
    struct event ev;
    int state;
    int pop3_state;
    struct evbuffer *evb_read;
    struct evbuffer *evb_write;
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
    void (*func)(struct client_state *cs, const char * arg);
} CMD;

void
cmd_unknown(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+ERR unknown command\r\n");
}

void
cmd_user(struct client_state *cs, const char * arg)
{
    cs->uid = searchuser(arg, cs->userid);
    if (cs->uid < 1 || cs->uid > MAX_USERS)
	evbuffer_add_printf(cs->evb_write, "+ERR user not found\r\n");
    else
	evbuffer_add_printf(cs->evb_write, "+OK\r\n");
}

void
cmd_pass(struct client_state *cs, const char * arg)
{
    userec_t cuser;
    char * pw;

    if (passwd_query(cs->uid, &cuser) < 0) {
	evbuffer_add_printf(cs->evb_write, "+ERR user not found\r\n");
	return;
    }

    pw = crypt(arg, cuser.passwd);
    if (strcmp(pw, cuser.passwd) == 0) {
	evbuffer_add_printf(cs->evb_write, "+OK\r\n");
	cs->pop3_state = POP3_TRANS;
    }
    else
	evbuffer_add_printf(cs->evb_write, "+ERR password incorrect \r\n");
}

void
cmd_quit(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+OK bye\r\n");
    
    if (cs->pop3_state == POP3_TRANS)
	cs->pop3_state = POP3_UPDATE;
    else
	cs->pop3_state = POP3_CLEANUP;
}

static const CMD auth_cmdlist[] = {
    {"user", cmd_user},
    {"pass", cmd_pass},
    {"quit", cmd_quit},
    {NULL, cmd_unknown}
};

/* Transaction state commands not implemented */
void
cmd_stat(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+OK 0 0\r\n");
}

void
cmd_list(struct client_state *cs, const char * arg)
{
    if (*arg)
	evbuffer_add_printf(cs->evb_write, "+ERR no such message\r\n");
    else
	evbuffer_add_printf(cs->evb_write, "+OK\r\n.\r\n");
}

void
cmd_retr(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+ERR no such message\r\n");
}

void
cmd_dele(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+ERR no such message\r\n");
}

void
cmd_noop(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+OK\r\n");
}

void
cmd_rset(struct client_state *cs, const char * arg)
{
    evbuffer_add_printf(cs->evb_write, "+OK\r\n");
}

static const CMD trans_cmdlist[] = {
    {"stat", cmd_stat},
    {"list", cmd_list},
    {"retr", cmd_retr},
    {"dele", cmd_dele},
    {"noop", cmd_noop},
    {"rset", cmd_rset},
    {"quit", cmd_quit},
    {NULL, cmd_unknown}
};

void
cb_client(int cfd, short event, void *arg)
{
    struct client_state *cs = arg;
    char *p, *line = NULL;
    int i;

    // ignore clients that timeout
    if (event & EV_TIMEOUT)
	cs->pop3_state = POP3_CLEANUP;

    if (event & EV_READ)
	if (evbuffer_read(cs->evb_read, cfd, READ_BLOCK) < 0)
	    cs->pop3_state = POP3_CLEANUP;

    if (event & EV_WRITE)
	if (evbuffer_write(cs->evb_write, cfd) < 0)
		cs->pop3_state = POP3_CLEANUP;

    if ((line = evbuffer_readline(cs->evb_read)) == NULL)
	goto out;

    for (p = line; *p != ' ' && *p != '\0'; p++);
    if (*p == ' ')
	*p++ = '\0';

    if (cs->pop3_state == POP3_AUTH) {
	for (i = 0; auth_cmdlist[i].cmd; i++)
	    if (strcasecmp(line, auth_cmdlist[i].cmd) == 0)
		break;
	(auth_cmdlist[i].func)(cs, p);
    } else if (cs->pop3_state == POP3_TRANS) {
	for (i = 0; trans_cmdlist[i].cmd; i++)
	    if (strcasecmp(line, trans_cmdlist[i].cmd) == 0)
		break;
	(trans_cmdlist[i].func)(cs, p);
    }

    evbuffer_write(cs->evb_write, cfd);

    if (cs->pop3_state == POP3_UPDATE) {
	cs->pop3_state = POP3_CLEANUP;
    }

    if (cs->pop3_state == POP3_CLEANUP) {
	if (clients == MAX_CLIENTS)
	    event_add(&ev_listen, NULL);
	close(cfd);
	evbuffer_free(cs->evb_write);
	evbuffer_free(cs->evb_read);
	free(cs);
	clients--;
	return;
    }

out:
    if (EVBUFFER_LENGTH(cs->evb_write) > 0)
	event_set(&cs->ev, cfd, EV_WRITE, (void *) cb_client, cs);
    else
	event_set(&cs->ev, cfd, EV_READ, (void *) cb_client, cs);
    event_add(&cs->ev, &tv);
}

void
setup_client(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    struct client_state *cs = calloc(1, sizeof(struct client_state));
    cs->state = EV_WRITE;
    cs->pop3_state = POP3_AUTH;
    cs->evb_read = evbuffer_new();
    cs->evb_write = evbuffer_new();

    evbuffer_add_printf(cs->evb_write, "+OK PttBBS POP3 ready\r\n");
    event_set(&cs->ev, fd, EV_WRITE, (void *) cb_client, cs);
    event_add(&cs->ev, &tv);
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
    int ch, sfd, inetd = 0;
    char *iface_ip = "127.0.0.1:5140";

    Signal(SIGPIPE, SIG_IGN);
    while ((ch = getopt(argc, argv, "il:h")) != -1)
	switch (ch) {
	    case 'i':
		inetd = 1;
		break;
	    case 'l':
		iface_ip = optarg;
		break;
	    case 'h':
	    default:
		fprintf(stderr, "usage: bpop3d [-i [interface_ip]:port]\n");
		return 1;
	}

    if (!inetd)
	if ((sfd = tobind(iface_ip)) < 0)
	    return 1;

    setuid(BBSUID);
    setgid(BBSGID);

    srandom(getpid() + time(NULL));
    attach_SHM();

    if (!inetd)
	daemonize(BBSHOME "/run/bpop3d.pid", NULL);

    event_init();
    if (!inetd) {
	event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, cb_listen, &ev_listen);
	event_add(&ev_listen, NULL);
    } else
	setup_client(0);
    event_dispatch();

    return 0;
}
