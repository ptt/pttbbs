#include <stdio.h>
#include <sys/time.h>

#include "bbs.h"

extern void utmplogin(int uid, int index, const int like[MAX_FRIEND], const int hate[MAX_REJECT]);
extern int genfriendlist(int uid, int index, ocfs_t *fs, int maxfs);
extern void utmplogoutall(void);
#ifdef FAKEDATA
FILE *fp;
#endif
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
#ifdef FAKEDATA
	fread(&uid, sizeof(uid), 1, fp);
	if(uid==-2)
	    break;
	fread(like, sizeof(like), 1, fp);
	fread(hate, sizeof(hate), 1, fp);
#else
	if( toread(cfd, &uid, sizeof(uid)) <= 0 ||
		toread(cfd, like, sizeof(like)) <= 0 ||
		toread(cfd, hate, sizeof(hate)) <= 0)
	    break;
#endif
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

void processlogin(int cfd, int uid, int index)
{
    int like[MAX_FRIEND];
    int hate[MAX_REJECT];
    /* 因為 logout 的時候並不會通知 utmpserver , 可能會查到一些
       已經 logout 的帳號。所以不能只取 MAX_FRIEND 而要多取一些 */
#define MAX_FS   (2 * MAX_FRIEND)
    int res;
    int nfs;
    ocfs_t fs[MAX_FS];

#ifdef FAKEDATA
    fread(like, sizeof(like), 1, fp);
    fread(hate, sizeof(hate), 1, fp);
#else
    if(toread(cfd, like, sizeof(like)) <= 0 ||
	    toread(cfd, hate, sizeof(hate)) <= 0)
	return;
#endif
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
#ifndef FAKEDATA
    res=0;
#ifdef NOFLOODING
    res=action_frequently(uid);
#endif
    towrite(cfd, &res, sizeof(res));
    towrite(cfd, &nfs, sizeof(nfs));
    towrite(cfd, fs, sizeof(ocfs_t) * nfs);
#endif
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

int main(int argc, char *argv[])
{
    struct  sockaddr_in     clientaddr;
    int     ch, port = 5120, sfd, cfd, len;
    char   *iface_ip = NULL;
    int cmd;
    int uid,index;
    int fail;
    int firstsync=0;

#ifdef UTMPLOG
    logfp = fopen("utmp.log","a");
    if(logfp && ftell(logfp)> 500*(1<<20)) {
	fclose(logfp);
	logfp=NULL;
    }
#endif

    Signal(SIGPIPE, SIG_IGN);
    while( (ch = getopt(argc, argv, "p:i:h")) != -1 )
	switch( ch ){
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'i':
	    iface_ip = optarg;
	    break;
	case 'h':
	default:
	    fprintf(stderr, "usage: utmpserver [-i interface_ip] [-p port]\n");
	    return 1;
	}

#ifdef FAKEDATA
    fp=fopen("utmp.data","rb");
#else
    if( (sfd = tobind(iface_ip, port)) < 0 )
	return 1;
#endif
    while(1) {
#ifdef FAKEDATA
	if(fread(&cmd, sizeof(cmd), 1, fp)==0) break;
#else
	len = sizeof(clientaddr);
	if( (cfd = accept(sfd, (struct sockaddr *)&clientaddr, &len)) < 0 ){
	    if( errno != EINTR )
		sleep(1);
	    continue;
	}
	toread(cfd, &cmd, sizeof(cmd));
#endif

	if(cmd==-1) {
	    syncutmp(cfd);
#ifndef FAKEDATA
	    close(cfd);
#endif
	    firstsync=1;
	    continue;
	}
	if(!firstsync) {
	    // don't accept client before first sync, to prevent incorrect friend data
	    close(cfd);
	    continue;
	}

	fail=0;
#ifdef FAKEDATA
	fread(&uid, sizeof(uid), 1, fp);
	fread(&index, sizeof(index), 1, fp);
#else
	if(cmd==-2) {
	    if(toread(cfd, &index, sizeof(index)) <= 0)
		fail=1;
	    if(toread(cfd, &uid, sizeof(uid)) <= 0)
		fail=1;
	} else if(cmd>=0) {
	    // old client
	    fail=1;
	} else {
	    printf("unknown cmd=%d\n",cmd);
	}
#endif
	if(index>=USHM_SIZE) {
	    fprintf(stderr, "bad index=%d\n",index);
	    fail=1;
	}

	if(fail) {
#ifndef FAKEDATA
	    close(cfd);
#endif
	    continue;
	}

	count_login++;
	processlogin(cfd, uid, index);
	if(count_login>=4000 || time(NULL)-begin_time>30*60)
	    showstat();
#ifndef FAKEDATA
	close(cfd);
#endif
    }
#ifdef FAKEDATA
    fclose(fp);
#endif
    return 0;
}
