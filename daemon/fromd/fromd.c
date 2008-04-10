// $Id$
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <event.h>

#include "bbs.h"
#include "ip_desc_db.h"

const char * cfgfile = BBSHOME "/etc/domain_name_query.cidr";
struct timeval tv = {5, 0};
static struct event ev_listen, ev_sighup;

static void sighup_cb(int signal, short event, void *arg)
{
    ip_desc_db_reload(cfgfile);
}

static void client_cb(int fd, short event, void *arg)
{
    char buf[32];
    const char *result;
    int len;

    // ignore clients that timeout
    if (event & EV_TIMEOUT)
	goto end;

    if ( (len = read(fd, buf, sizeof(buf) - 1)) <= 0 )
	goto end;

    buf[len] = '\0';

    result = ip_desc_db_lookup(buf);
    write(fd, result, strlen(result));

end:
    // cleanup
    close(fd);
    free(arg);
}

static void listen_cb(int fd, short event, void *arg)
{
    int cfd;

    if ((cfd = accept(fd, NULL, NULL)) < 0 )
	return;

    struct event *ev = malloc(sizeof(struct event));

    event_set(ev, cfd, EV_READ, client_cb, ev);
    event_add(ev, &tv);
}

void daemonize()
{
    pid_t pid;

    if ( (pid = fork()) < 0)
	exit(1);

    if (pid > 0)
	exit(0);

    umask(0);

    if (setsid() < 0)
	exit(-1);

    if (chdir("/") < 0)
	exit(-1);
}

int main(int argc, char *argv[])
{
    int     ch, sfd;
    char   *iface_ip = ":5130";

    Signal(SIGPIPE, SIG_IGN);

    while ( (ch = getopt(argc, argv, "i:h")) != -1 )
	switch( ch ){
	case 'i':
	    iface_ip = optarg;
	    break;
	case 'h':
	default:
	    fprintf(stderr, "usage: %s [-i [interface_ip]:port]\n", argv[0]);
	    return 1;
	}

    if ( (sfd = tobind(iface_ip)) < 0 )
	return 1;

    daemonize();

    ip_desc_db_reload(cfgfile);

    event_init();
    event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, &ev_listen);
    event_add(&ev_listen, NULL);
    signal_set(&ev_sighup, SIGHUP, sighup_cb, &ev_sighup);
    signal_add(&ev_sighup, NULL);
    event_dispatch();

    return 0;
}
