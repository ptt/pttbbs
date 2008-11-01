/* $Id$ */
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <event.h>

#include "bbs.h"

extern void utmplogin(int uid, int index, const int like[MAX_FRIEND], const int hate[MAX_REJECT]);
extern int genfriendlist(int uid, int index, ocfs_t *fs, int maxfs);
extern void utmplogoutall(void);
#ifdef UTMPLOG
FILE *logfp;
#endif

clock_t begin_clock;
time_t begin_time;
int count_flooding, count_login;

#ifdef NOFLOODING
/* 0 ok, 1 delay action, 2 reject */
int action_frequently(int uid)
{
    int i;
    time_t now = time(NULL);
    time_t minute = now/60;
    time_t hour = minute/60;

    static time_t flood_base_minute;
    static time_t flood_base_hour;
    static struct {
	unsigned short lastlogin; // truncated time_t
	unsigned char minute_count;
	unsigned char hour_count;
    } flooding[MAX_USERS];

    if(minute!=flood_base_minute) {
	for(i=0; i<MAX_USERS; i++)
	    flooding[i].minute_count=0;
	flood_base_minute=minute;
    }
    if(hour!=flood_base_hour) {
	for(i=0; i<MAX_USERS; i++)
	    flooding[i].hour_count=0;
	flood_base_hour=hour;
    }

    if(abs(flooding[uid].lastlogin-(unsigned short)now)<=3 ||
	    flooding[uid].minute_count>30 ||
	    flooding[uid].hour_count>60) {
	count_flooding++;
	return 2;
    }

    flooding[uid].minute_count++;
    flooding[uid].hour_count++;
    flooding[uid].lastlogin=now;

    if(flooding[uid].minute_count>5 ||
	    flooding[uid].hour_count>20) {
	count_flooding++;
	return 1;
    }
    return 0;
}
#endif /* NOFLOODING */

void syncutmp(int cfd) {
    int i;
    int like[MAX_FRIEND];
    int hate[MAX_REJECT];

#ifdef UTMPLOG
    int x=-1;
    if(logfp && ftell(logfp)> 500*(1<<20)) {
	fclose(logfp);
	logfp=NULL;
    }
    if(logfp) fwrite(&x, sizeof(x), 1, logfp);
#endif

    printf("logout all\n");
    utmplogoutall();
    fprintf(stderr,"sync begin\n");
    for(i=0; i<USHM_SIZE; i++) {
	int uid;
	if( toread(cfd, &uid, sizeof(uid)) <= 0 ||
		toread(cfd, like, sizeof(like)) <= 0 ||
		toread(cfd, hate, sizeof(hate)) <= 0)
	    break;
#ifdef UTMPLOG
	if(logfp) {
	    fwrite(&uid, sizeof(uid), 1, logfp);
	    fwrite(like, sizeof(like), 1, logfp);
	    fwrite(hate, sizeof(hate), 1, logfp);
	}
#endif

	if(uid != 0)
	    utmplogin(uid, i, like, hate);
    }
    if(i<USHM_SIZE) {
#ifdef UTMPLOG
	int x=-2;
	if(logfp) fwrite(&x, sizeof(x), 1, logfp);
#endif
    }

    fprintf(stderr,"sync end\n");
}

struct client_state {
    struct event ev;
    int state;
    struct evbuffer *evb;
};

void processlogin(struct client_state *cs, int uid, int index)
{
    int like[MAX_FRIEND];
    int hate[MAX_REJECT];
    /* 因為 logout 的時候並不會通知 utmpserver , 可能會查到一些
       已經 logout 的帳號。所以不能只取 MAX_FRIEND 而要多取一些 */
#define MAX_FS   (2 * MAX_FRIEND)
    int res;
    int nfs;
    ocfs_t fs[MAX_FS];

    evbuffer_remove(cs->evb, like, sizeof(like));
    evbuffer_remove(cs->evb, hate, sizeof(hate));
#ifdef UTMPLOG
    if(logfp) {
	int x=-3;
	fwrite(&x, sizeof(x), 1, logfp);
	fwrite(&uid, sizeof(uid), 1, logfp);
	fwrite(&index, sizeof(index), 1, logfp);
	fwrite(like, sizeof(like), 1, logfp);
	fwrite(hate, sizeof(hate), 1, logfp);
    }
#endif

    utmplogin(uid, index, like, hate);
    nfs=genfriendlist(uid, index, fs, MAX_FS);
    res=0;
#ifdef NOFLOODING
    res=action_frequently(uid);
#endif
    evbuffer_drain(cs->evb, 2147483647);
    evbuffer_add(cs->evb, &res, sizeof(res));
    evbuffer_add(cs->evb, &nfs, sizeof(nfs));
    evbuffer_add(cs->evb, fs, sizeof(ocfs_t) * nfs);
}

void showstat(void)
{
    clock_t now_clock=clock();
    time_t now_time=time(0);

    time_t used_time=now_time-begin_time;
    clock_t used_clock=now_clock-begin_clock;

    printf("%.24s : real %.0f cpu %.2f : %d login %d flood, %.2f login/sec, %.2f%% load.\n", 
	    ctime(&now_time), (double)used_time, (double)used_clock/CLOCKS_PER_SEC, 
	    count_login, count_flooding,
	    (double)count_login/used_time, (double)used_clock/CLOCKS_PER_SEC/used_time*100);

    begin_time=now_time;
    begin_clock=now_clock;
    count_login=0;
    count_flooding=0;
}

enum {
    FSM_ENTER,
    FSM_SYNC,
    FSM_LOGIN,
    FSM_WRITEBACK,
    FSM_EXIT
};

static int firstsync=0;

struct timeval tv = {5, 0};
struct event ev;
int clients = 0;

#define READ_BLOCK 1024
#define MAX_CLIENTS 10

void connection_client(int cfd, short event, void *arg)
{
    struct client_state *cs = arg;
    int cmd, break_out = 0;
    int uid = 0, index = 0;

    // ignore clients that timeout
    if (event & EV_TIMEOUT)
	cs->state = FSM_EXIT;

    if (event & EV_READ) {
	if (cs->state != FSM_ENTER) {
	    if (evbuffer_read(cs->evb, cfd, READ_BLOCK) <= 0)
		cs->state = FSM_EXIT;
	}
	else {
	    if (evbuffer_read(cs->evb, cfd, 4) <= 0)
		cs->state = FSM_EXIT;
	}
    }

    while (!break_out) {
	switch (cs->state) {
	    case FSM_ENTER:
		if (EVBUFFER_LENGTH(cs->evb) < sizeof(int)) {
		    break_out = 1;
		    break;
		}
		evbuffer_remove(cs->evb, &cmd, sizeof(cmd));
		if (cmd == -1)
		    cs->state = FSM_SYNC;
		else if (cmd == -2) {
		    fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
		    cs->state = FSM_LOGIN;
		}
		else if (cmd >= 0)
		    cs->state = FSM_EXIT;
		else {
		    printf("unknown cmd=%d\n",cmd);
		    cs->state = FSM_EXIT;
		}
		break;
	    case FSM_SYNC:
		syncutmp(cfd);
		firstsync = 1;
		cs->state = FSM_EXIT;
		break;
	    case FSM_LOGIN:
		if (firstsync) {
		    if (EVBUFFER_LENGTH(cs->evb) < (2 + MAX_FRIEND + MAX_REJECT) * sizeof(int)) {
			break_out = 1;
			break;
		    }
		    evbuffer_remove(cs->evb, &index, sizeof(index));
		    evbuffer_remove(cs->evb, &uid, sizeof(uid));
		    if (index >= USHM_SIZE) {
			fprintf(stderr, "bad index=%d\n", index);
			cs->state = FSM_EXIT;
			break;
		    }
		    count_login++;
		    processlogin(cs, uid, index);
		    if (count_login >= 4000 || (time(NULL) - begin_time) > 30*60)
			showstat();
		    cs->state = FSM_WRITEBACK;
		    break_out = 1;
		}
		else
		    cs->state = FSM_EXIT;
		break;
	    case FSM_WRITEBACK:
		if (event & EV_WRITE)
		    if (evbuffer_write(cs->evb, cfd) <= 0 && EVBUFFER_LENGTH(cs->evb) > 0)
			break_out = 1;
		if (EVBUFFER_LENGTH(cs->evb) == 0)
		    cs->state = FSM_EXIT;
		break;
	    case FSM_EXIT:
		if (clients == MAX_CLIENTS)
		    event_add(&ev, NULL);
		close(cfd);
		evbuffer_free(cs->evb);
		free(cs);
		clients--;
		return;
		break;
	}
    }
    if (cs->state == FSM_WRITEBACK)
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

    struct client_state *cs = calloc(1, sizeof(struct client_state));
    cs->state = FSM_ENTER;
    cs->evb = evbuffer_new();

    event_set(&cs->ev, cfd, EV_READ, (void *) connection_client, cs);
    event_add(&cs->ev, &tv);
    clients++;

    if (clients >= MAX_CLIENTS) {
	event_del(&ev);
    }
}

int main(int argc, char *argv[])
{
    int     ch, sfd;
    char   *iface_ip = ":5120";

#ifdef UTMPLOG
    logfp = fopen("utmp.log","a");
    if(logfp && ftell(logfp)> 500*(1<<20)) {
	fclose(logfp);
	logfp=NULL;
    }
#endif

    Signal(SIGPIPE, SIG_IGN);
    while( (ch = getopt(argc, argv, "i:h")) != -1 )
	switch( ch ){
	case 'i':
	    iface_ip = optarg;
	    break;
	case 'h':
	default:
	    fprintf(stderr, "usage: utmpserver [-i [interface_ip]:port]\n");
	    return 1;
	}

    if( (sfd = tobind(iface_ip)) < 0 )
	return 1;

    event_init();
    event_set(&ev, sfd, EV_READ | EV_PERSIST, connection_accept, &ev);
    event_add(&ev, NULL);
    event_dispatch();
    return 0;
}
