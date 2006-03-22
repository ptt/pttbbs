/* $Id$ */
#include "bbs.h"
#include <err.h>

struct {
    int     uid;
    int     nFriends, nRejects;
    int     friend[MAX_FRIEND];
    int     reject[MAX_REJECT];
} utmp[USHM_SIZE];

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
#endif /* NOFLOODING */

inline int countarray(int *s, int max)
{
    int     i;
    for( i = 0 ; i < max && s[i] ; ++i )
	;
    return i;
}

int
reverse_friend_stat(int stat)
{
    int             stat1 = 0;
    if (stat & IFH)
        stat1 |= HFM;
    if (stat & IRH)
        stat1 |= HRM;
    if (stat & HFM)
        stat1 |= IFH;
    if (stat & HRM)
        stat1 |= IRH;
    if (stat & IBH)
        stat1 |= IBH;
    return stat1;
}

int set_friend_bit(int me, int ui)
{
    int     hit = 0;
    /* 判斷對方是否為我的朋友 ? */
    if( intbsearch(utmp[ui].uid, utmp[me].friend, utmp[me].nFriends) )
        hit = IFH;

    /* 判斷我是否為對方的朋友 ? */
    if( intbsearch(utmp[me].uid, utmp[ui].friend, utmp[ui].nFriends) )
        hit |= HFM;

    /* 判斷對方是否為我的仇人 ? */
    if( intbsearch(utmp[ui].uid, utmp[me].reject, utmp[me].nRejects) )
	hit |= IRH;

    /* 判斷我是否為對方的仇人 ? */
    if( intbsearch(utmp[me].uid, utmp[ui].reject, utmp[ui].nRejects) )
	hit |= HRM;

    return hit;
}

void initdata(int index)
{
    utmp[index].nFriends = countarray(utmp[index].friend, MAX_FRIEND);
    utmp[index].nRejects = countarray(utmp[index].reject, MAX_REJECT);
    if( utmp[index].nFriends > 0 )
	qsort(utmp[index].friend, utmp[index].nFriends,
	      sizeof(int), qsort_intcompar);
    if( utmp[index].nRejects > 0 )
	qsort(utmp[index].reject, utmp[index].nRejects,
	      sizeof(int), qsort_intcompar);
}

inline void syncutmp(int cfd)
{
    int     nSynced = 0, i;
    for( i = 0 ; i < USHM_SIZE ; ++i, ++nSynced )
	if( toread(cfd, &utmp[i].uid, sizeof(utmp[i].uid)) > 0      &&
	    toread(cfd, utmp[i].friend, sizeof(utmp[i].friend)) > 0 &&
	    toread(cfd, utmp[i].reject, sizeof(utmp[i].reject)) > 0 ){
	    if( utmp[i].uid )
		initdata(i);
	}
	else
	    for( ; i < USHM_SIZE ; ++i )
		utmp[i].uid = 0;
    close(cfd);
    fprintf(stderr, "%d users synced\n", nSynced);
}

void processlogin(int cfd, int uid, int index)
{
    if( toread(cfd, utmp[index].friend, sizeof(utmp[index].friend)) > 0 &&
	toread(cfd, utmp[index].reject, sizeof(utmp[index].reject)) > 0 ){
	/* 因為 logout 的時候並不會通知 utmpserver , 可能會查到一些
	   已經 logout 的帳號。所以不能只取 MAX_FRIEND 而要多取一些 */
#define MAX_FS   (2 * MAX_FRIEND)
	int     iu, nFrs, stat, rstat;
	ocfs_t  fs[MAX_FS];

	utmp[index].uid = uid;
	initdata(index);

	for( nFrs = iu = 0 ; iu < USHM_SIZE && nFrs < MAX_FS ; ++iu )
	    if( iu != index && utmp[iu].uid ){
		if( (stat = set_friend_bit(index, iu)) ){
		    rstat = reverse_friend_stat(stat);
		    fs[nFrs].index = iu;
		    fs[nFrs].uid = utmp[iu].uid;
		    fs[nFrs].friendstat = (stat << 24) + iu;
		    fs[nFrs].rfriendstat = (rstat << 24) + index;
		    ++nFrs;
		}
	    }
	
	towrite(cfd, &fs, sizeof(ocfs_t) * nFrs);
    }
    close(cfd);
}

#ifdef NOFLOODING
void flushwaitqueue(void)
{
    int     i;
    for( i = 0 ; i < nWaits ; ++i )
	processlogin(waitqueue[i].fd, waitqueue[i].uid, waitqueue[i].index);
    lastflushtime = time(NULL);
    nWaits = 0;
    memset(flooding, 0, sizeof(flooding));
}
#endif

/* XXX 具有被 DoS 的可能, 請用 firewall 之類擋起來 */
int main(int argc, char **argv)
{
    struct  sockaddr_in     clientaddr;
    int     ch, port = 5120, sfd, cfd, len, index, uid;
    char   *iface_ip = NULL;

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

    if( (sfd = tobind(iface_ip, port)) < 0 )
	return 1;

#ifdef NOFLOODING
    lastflushtime = time(NULL);
#endif
    while( 1 ){
#ifdef NOFLOODING
	if( lastflushtime < (time(NULL) - 1800) )
	    flushwaitqueue();
#endif

	len = sizeof(clientaddr);
	if( (cfd = accept(sfd, (struct sockaddr *)&clientaddr, &len)) < 0 ){
	    if( errno != EINTR )
		sleep(1);
	    continue;
	}
	toread(cfd, &index, sizeof(index));
	if( index == -1 ){
	    syncutmp(cfd);
	    continue;
	}

	if( toread(cfd, &uid, sizeof(uid)) > 0 ){
	    if( !(0 < uid || uid > MAX_USERS) ){ /* for safety */
		close(cfd);
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

	    /* cfd will be closed in processlogin() */
	    processlogin(cfd, uid, index);
	} else {
	    close(cfd);
	}
    }
    return 0;
}
