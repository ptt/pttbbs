/* $Id$ */
#include "bbs.h"
#include <err.h>

struct {
    int     uid;
    int     nFriends, nRejects;
    int     friend[MAX_FRIEND];
    int     reject[MAX_REJECT];
} utmp[USHM_SIZE];

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

/* XXX 具有被 DoS 的可能, 請用 firewall 之類擋起來 */
int main(int argc, char **argv)
{
    struct  sockaddr_in     clientaddr;
    int     ch, port = 5120, sfd, cfd, len, index, i, uid;
    char    iface_ip[16] = {NULL};

    while( (ch = getopt(argc, argv, "p:i:h")) != -1 )
	switch( ch ){
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'i':
	    host = strncpy(iface_ip, optarg, 16);
	    host[15] = 0;
	    break;
	case 'h':
	default:
	    fprintf(stderr, "usage: utmpserver [-i interface_ip] [-p port]\n");
	    return 1;
	}

    if( (sfd = tobind(iface_ip, port)) < 0 )
	return 1;

    while( 1 ){
	len = sizeof(clientaddr);
	if( (cfd = accept(sfd, (struct sockaddr *)&clientaddr, &len)) < 0 ){
	    if( errno != EINTR )
		sleep(1);
	    continue;
	}
	toread(cfd, &index, sizeof(index));
	if( index == -1 ){
	    int     nSynced = 0;
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
	    continue;
	}

	if( toread(cfd, &uid, sizeof(uid)) > 0                              &&
	    toread(cfd, utmp[index].friend, sizeof(utmp[index].friend)) > 0 &&
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
    return 0;
}
