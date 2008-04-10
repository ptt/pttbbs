/* $Id$ */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <event.h>

#include "bbs.h"

struct timeval tv = {5, 0};
struct event ev;
int clients = 0;

#define READ_BLOCK 256
#define MAX_CLIENTS 10

#define AUTH_PASSWDS BBSHOME "/.AUTH_PASSWDS"
#define ENTRY_SIZE 64

struct client_state {
    struct event ev;
    int state;
    int uid;
    int len;
    int response;
    struct evbuffer *evb;
};

enum {
    FSM_ENTER,
    FSM_AUTH,
    FSM_SETPASSWD,
    FSM_RESPOND,
    FSM_EXIT
};

/**
 * 檢查密碼
 * @return 1 - 密碼錯誤, 0 - 密碼正確
 */
int check_passwd(int uid, char *passwd)
{
    char buf[ENTRY_SIZE];
    int i, result = 1;

    if ((i = open(AUTH_PASSWDS, O_WRONLY)) < 0)
	return result;

    if (lseek(i, uid * ENTRY_SIZE, SEEK_SET) < 0)
	goto end;

    if (read(i, buf, ENTRY_SIZE) < ENTRY_SIZE)
	goto end;

    if (!strcmp(buf, crypt(passwd, buf)))
	result = 0;
end:
    memset(buf, 0, ENTRY_SIZE);
    close(i);
    return result;
}

/**
 * 設定新密碼
 * 先用 DES crypt 就好
 */
void set_passwd(int uid, char *passwd)
{
    char buf[ENTRY_SIZE];
    char saltc[3], c;
    int i;

    i = 9 * random();
    saltc[0] = i & 077;
    saltc[1] = (i >> 6) & 077;

    for (i = 0; i < 2; i++) {
	c = saltc[i] + '.';
	if (c > '9')
	    c += 7;
	if (c > 'Z')
	    c += 6;
	saltc[i] = c;
    }
    saltc[2] = '\0';

    memset(buf, 0, sizeof(buf));
    strlcpy(buf, crypt(passwd, saltc), sizeof(buf));

    if ((i = open(AUTH_PASSWDS, O_WRONLY)) < 0)
	return;

    if (lseek(i, uid * ENTRY_SIZE, SEEK_SET) < 0)
	goto close;
    write(i, buf, ENTRY_SIZE);
close:
    close(i);
}

void connection_client(int cfd, short event, void *arg)
{
    struct client_state *cs = arg;
    int cmd;
    static char buf[128];

    // ignore clients that timeout
    if (event & EV_TIMEOUT)
	cs->state = FSM_EXIT;

    if (event & EV_READ) {
	if (evbuffer_read(cs->evb, cfd, READ_BLOCK) < 0)
	    cs->state = FSM_EXIT;
    }

    while (1) {
	switch (cs->state) {
	    case FSM_ENTER:
		if (EVBUFFER_LENGTH(cs->evb) < sizeof(int))
		    goto break_out;
		evbuffer_remove(cs->evb, &cmd, sizeof(cmd));
		cs->state = FSM_AUTH;

	    case FSM_AUTH:
		if (EVBUFFER_LENGTH(cs->evb) < sizeof(int) * 2)
		    goto break_out;

		evbuffer_remove(cs->evb, &cs->uid, sizeof(cs->uid));
		evbuffer_remove(cs->evb, &cs->len, sizeof(cs->len));
		if (EVBUFFER_LENGTH(cs->evb) < cs->len)
		    goto break_out;

		memset(buf, 0, sizeof(buf));
		evbuffer_remove(cs->evb, buf, cs->len);
		cs->response = check_passwd(cs->uid, buf);
		memset(buf, 0, sizeof(buf));

		if (cmd == 2)
		    cs->state = FSM_SETPASSWD;
		else {
		    cs->state = FSM_RESPOND;
		    goto break_out;
		}

	    case FSM_SETPASSWD:
		if (EVBUFFER_LENGTH(cs->evb) < sizeof(int))
		    goto break_out;

		evbuffer_remove(cs->evb, &cs->len, sizeof(cs->len));
		if (EVBUFFER_LENGTH(cs->evb) < cs->len)
		    goto break_out;

		memset(buf, 0, sizeof(buf));
		evbuffer_remove(cs->evb, buf, cs->len);
		set_passwd(cs->uid, buf);
		memset(buf, 0, sizeof(buf));

		cs->state = FSM_RESPOND;
		goto break_out;

	    case FSM_RESPOND:
		write(cfd, &cs->response, sizeof(cs->response));
		cs->state = FSM_EXIT;

	    case FSM_EXIT:
		if (clients == MAX_CLIENTS)
		    event_add(&ev, NULL);
		close(cfd);
		evbuffer_free(cs->evb);
		free(cs);
		clients--;
		return;
	}
    }

break_out:
    if (cs->state == FSM_RESPOND)
	event_set(&cs->ev, cfd, EV_WRITE, (void *) connection_client, cs);
    event_add(&cs->ev, &tv);
}

void connection_accept(int fd, short event, void *arg)
{
    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);
    int cfd;

    if ((cfd = accept(fd, (struct sockaddr *)&clientaddr, &len)) < 0 )
	return;

    fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);

    struct client_state *cs = calloc(1, sizeof(struct client_state));
    cs->state = 0;
    cs->evb = evbuffer_new();

    event_set(&cs->ev, cfd, EV_READ, (void *) connection_client, cs);
    event_add(&cs->ev, &tv);
    clients++;

    if (clients > MAX_CLIENTS)
	event_del(&ev);
}

int main(int argc, char *argv[])
{
    int     ch, sfd;
    char   *iface_ip = ":5121";

    Signal(SIGPIPE, SIG_IGN);
    while( (ch = getopt(argc, argv, "i:h")) != -1 )
	switch( ch ){
	case 'i':
	    iface_ip = optarg;
	    break;
	case 'h':
	default:
	    fprintf(stderr, "usage: authserver [-i [interface_ip]:port]\n");
	    return 1;
	}

    if( (sfd = tobind(iface_ip, port)) < 0 )
	return 1;

    srandom(getpid() + time(NULL));
    event_init();
    event_set(&ev, sfd, EV_READ | EV_PERSIST, connection_accept, &ev);
    event_add(&ev, NULL);
    event_dispatch();
    return 0;
}
