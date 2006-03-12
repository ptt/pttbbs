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


#ifdef NOFLOODING
#define MAXWAIT 1024
#define FLUSHTIME (3600*6)

struct {
    time_t  lasttime;
    int     count;
} flooding[MAX_USERS];

int nWaits, lastflushtime;
struct {
    int     uid;
    int     fd;
    int     index;
} waitqueue[MAXWAIT];

void processlogin(int cfd, int uid, int index);
void flushwaitqueue(void)
{
    int     i;
    for( i = 0 ; i < nWaits ; ++i ) {
	processlogin(waitqueue[i].fd, waitqueue[i].uid, waitqueue[i].index);
	close(waitqueue[i].fd);
    }
    lastflushtime = time(NULL);
    nWaits = 0;
    memset(flooding, 0, sizeof(flooding));
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
    towrite(cfd, fs, sizeof(ocfs_t) * nfs);
#endif
}

int main(int argc, char *argv[])
{
    struct  sockaddr_in     clientaddr;
    int     ch, port = 5120, sfd, cfd, len;
    char   *iface_ip = NULL;
    int cmd;
    int uid,index;
    int fail;

#ifdef UTMPLOG
    logfp = fopen("utmp.log","a");
    if(logfp && ftell(logfp)> 500*(1<<20)) {
	fclose(logfp);
	logfp=NULL;
    }
#endif

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
#ifdef NOFLOODING
    lastflushtime = time(NULL);
#endif
    while(1) {
#ifdef NOFLOODING
	if( lastflushtime < (time(NULL) - 1800) )
	    flushwaitqueue();
#endif

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
	    continue;
	}

	fail=0;
#ifdef FAKEDATA
	fread(&uid, sizeof(uid), 1, fp);
	fread(&index, sizeof(index), 1, fp);
#else
	index=cmd; // bad protocol
	if(toread(cfd, &uid, sizeof(uid)) <= 0)
	    fail=1;
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

#ifdef NOFLOODING
	if( (time(NULL) - flooding[uid].lasttime) < 20 )
	    ++flooding[uid].count;
	if( flooding[uid].count > 10 ){
	    if( nWaits == MAXWAIT )
		flushwaitqueue();
	    waitqueue[nWaits].uid = uid;
	    waitqueue[nWaits].index = index;
	    waitqueue[nWaits].fd = cfd;
	    ++nWaits;

	    continue;
	}
	flooding[uid].lasttime = time(NULL);
#endif

	processlogin(cfd, uid, index);
#ifndef FAKEDATA
	close(cfd);
#endif
    }
#ifdef FAKEDATA
    fclose(fp);
#endif
    return 0;
}
