/* $Id$ */
#include "bbs.h"
#include <sys/wait.h>
#include <string.h>

extern SHM_t   *SHM;

/* utmpfix ----------------------------------------------------------------- */
int logout_friend_online(userinfo_t *utmp)
{
    int i, j, k;
    int offset=(int) (utmp - &SHM->uinfo[0]);
    userinfo_t *ui;
    while(utmp->friendtotal){
	if( !(0 <= utmp->friendtotal && utmp->friendtotal < MAX_FRIEND) )
	    return 1;
	i = utmp->friendtotal-1;
	j = (utmp->friend_online[i] & 0xFFFFFF);
	if( !(0 <= j && j < MAX_ACTIVE) )
	    printf("\tonline friend error(%d)\n", j);
	else{
	    utmp->friend_online[i]=0;
	    ui = &SHM->uinfo[j]; 
	    if(ui->pid && ui!=utmp){
		if(ui->friendtotal > MAX_FRIEND)
		    printf("\tfriend(%d) has too many(%d) friends\n", j, ui->friendtotal);
		for(k=0; k<ui->friendtotal && k<MAX_FRIEND &&
			(int)(ui->friend_online[k] & 0xFFFFFF) !=offset; k++);
		if( k < ui->friendtotal && k < MAX_FRIEND ){
		    ui->friendtotal--;
		    ui->friend_online[k]=ui->friend_online[ui->friendtotal];
		    ui->friend_online[ui->friendtotal]=0;
		}
	    }
	}
	utmp->friendtotal--;
	utmp->friend_online[utmp->friendtotal]=0;
    }
    return 0;
}

void purge_utmp(userinfo_t *uentp)
{
    logout_friend_online(uentp);
    //memset(uentp, 0, sizeof(userinfo_t));
}

typedef struct {
    int     index;
    int     idle;
} IDLE_t;

int sfIDLE(const void *a, const void *b)
{
    return ((IDLE_t *)b)->idle - ((IDLE_t *)a)->idle;
}

int utmpfix(int argc, char **argv)
{
    int     i, fast = 0, lowerbound = 100, nownum = SHM->UTMPnumber;
    int     which, nactive = 0, dofork = 1;
    time_t  now, timeout = -1;
    char    *clean, buf[1024], ch;
    IDLE_t  idle[USHM_SIZE];
    char    changeflag = 0;

    while( (ch = getopt(argc, argv, "nt:l:F")) != -1 )
	switch( ch ){
	case 'n':
	    fast = 1;
	    break;
	case 't':
	    timeout = atoi(optarg);
	    break;
	case 'l':
	    lowerbound = atoi(optarg);
	    break;
	case 'F':
	    dofork = 0;
	    break;
	default:
	    printf("usage:\tshmctl\tutmpfix [-n] [-t timeout] [-F]\n");
	    return 1;
	}

    for( i = 0 ; i < 5 ; ++i )
	if( !SHM->UTMPbusystate )
	    break;
	else{
	    puts("utmpshm is busy....");
	    sleep(1);
	}
    SHM->UTMPbusystate = 1;

    if( dofork ){
	int     times, status;
	pid_t   pid;
	printf("forking mode\n");
	for( times = 0 ; times < 100 ; ++times ){
	    switch( (pid = fork()) ){
	    case -1:
		perror("fork()");
		sleep(1);
		break;
	    case 0:
		goto DoUtmpfix;
		break;
	    default:
		waitpid(pid, &status, 0);
		printf("status: %d\n", status);
		if( WIFEXITED(status) )
		    return 0;
		changeflag = 1;
	    }
	}
    }

 DoUtmpfix:
    printf("starting scaning... %s \n", (fast ? "(fast mode)" : ""));
#ifdef OUTTA_TIMER
    now = SHM->GV2.e.now;
#else
    time(&now);
#endif
    for( i = 0, nactive = 0 ; i < USHM_SIZE ; ++i )
	if( SHM->uinfo[i].pid ){
	    idle[nactive].index = i;
	    idle[nactive].idle = now - SHM->uinfo[i].lastact;
	    ++nactive;
	}
    if( !fast )
	qsort(idle, nactive, sizeof(IDLE_t), sfIDLE);

    for( i = 0 ; i < nactive ; ++i ){
	which = idle[i].index;
	clean = NULL;
	if( !isalpha(SHM->uinfo[which].userid[0]) )
	    clean = "userid error";
	else if( memchr(SHM->uinfo[which].userid, '\0', IDLEN+1)==NULL)
	    clean = "userid without z";
	else if( SHM->uinfo[which].friendtotal > MAX_FRIEND)
	    clean = "too many friend";
	else if( kill(SHM->uinfo[which].pid, 0) < 0 ){
	    clean = "process error";
	    purge_utmp(&SHM->uinfo[which]);
	}
	else if( searchuser(SHM->uinfo[which].userid) == 0 ){
	    clean = "user not exist";
	}
#ifdef DOTIMEOUT
	else if( !fast ){
	    if( nownum > lowerbound &&
		idle[i].idle > 
		(timeout == -1 ? IDLE_TIMEOUT : timeout) ){
		sprintf(buf, "timeout(%s",
			ctime(&SHM->uinfo[which].lastact));
		buf[strlen(buf) - 1] = 0;
		strcat(buf, ")");
		clean = buf;
		kill(SHM->uinfo[which].pid, SIGHUP);
		printf("%s\n", buf);
		--nownum;
		continue;
	    }
	}
#endif
	
	if( clean ){
	    printf("clean %06d(%s), userid: %s\n",
		   i, clean, SHM->uinfo[which].userid);
	    memset(&SHM->uinfo[which], 0, sizeof(userinfo_t));
	    --nownum;
	    changeflag = 1;
	}
    }
    SHM->UTMPbusystate = 0;
    if( changeflag )
	SHM->UTMPneedsort = 1;
    return 0;
}
/* end of utmpfix ---------------------------------------------------------- */

/* utmpsortd --------------------------------------------------------------- */
static int
cmputmpuserid(const void *i, const void *j)
{
    return strcasecmp((*((userinfo_t **) i))->userid, (*((userinfo_t **) j))->userid);
}

static int
cmputmpmode(const void *i, const void *j)
{
    return (*((userinfo_t **) i))->mode - (*((userinfo_t **) j))->mode;
}

static int
cmputmpidle(const void *i, const void *j)
{
    return (*((userinfo_t **) i))->lastact - (*((userinfo_t **) j))->lastact;
}

static int
cmputmpfrom(const void *i, const void *j)
{
    return strcasecmp((*((userinfo_t **) i))->from, (*((userinfo_t **) j))->from);
}

static int
cmputmpfive(const void *i, const void *j)
{
    userinfo_t *a=(*((userinfo_t **) i)),*b=(*((userinfo_t **) j));
    int played_a=(a->five_win+a->five_lose+a->five_lose)!=0;
    int played_b=(b->five_win+b->five_lose+b->five_lose)!=0;
    int             type;

    if ((type = played_b - played_a))
	return type;
    if (played_a == 0)
	return 0;
    if ((type = b->five_win - a->five_win))
	return type;
    if ((type = a->five_lose - b->five_lose))
	return type;
    return a->five_tie - b->five_tie;
}

static int
cmputmpchc(const void *i, const void *j)
{
    userinfo_t *a=(*((userinfo_t **) i)),*b=(*((userinfo_t **) j));
    int played_a=(a->chc_win+a->chc_lose+a->chc_lose)!=0;
    int played_b=(b->chc_win+b->chc_lose+b->chc_lose)!=0;
    int             type;

    if ((type = played_b - played_a))
	return type;
    if (played_a == 0)
	return 0;
    if ((type = b->chc_win - a->chc_win))
        return type;
    if ((type = a->chc_lose - b->chc_lose))
	return type;
    return a->chc_tie - b->chc_tie;
}

static int
cmputmppid(const void *i, const void *j)
{
    return (*((userinfo_t **) i))->pid - (*((userinfo_t **) j))->pid;
}

static int
cmputmpuid(const void *i, const void *j)
{
    return (*((userinfo_t **) i))->uid - (*((userinfo_t **) j))->uid;
}

inline void utmpsort(int sortall)
{
    userinfo_t     *uentp;
    int             count, i, ns;
    short           nusers[MAX_BOARD];


    SHM->UTMPbusystate = 1;
#ifdef OUTTA_TIMER
    SHM->UTMPuptime = SHM->GV2.e.now;
#else
    SHM->UTMPuptime = time(NULL);
#endif
    ns = (SHM->currsorted ? 0 : 1);

    for (uentp = &SHM->uinfo[0], count = i = 0;
	 i < USHM_SIZE;
	 ++i, uentp = &SHM->uinfo[i]) {
	if (uentp->pid) {
	    if (uentp->sex < 0 || uentp->sex > 7)
		purge_utmp(uentp);
	    else
		SHM->sorted[ns][0][count++] = uentp;
	}
    }
    SHM->UTMPnumber = count;
    qsort(SHM->sorted[ns][0], count, sizeof(userinfo_t *), cmputmpuserid);
    memcpy(SHM->sorted[ns][6],
	   SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][7],
	   SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    qsort(SHM->sorted[ns][6], count, sizeof(userinfo_t *), cmputmpuid);
    qsort(SHM->sorted[ns][7], count, sizeof(userinfo_t *), cmputmppid);
    if( sortall ){
	memcpy(SHM->sorted[ns][1],
	       SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
	memcpy(SHM->sorted[ns][2],
	       SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
	memcpy(SHM->sorted[ns][3],
	       SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
	memcpy(SHM->sorted[ns][4],
	       SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
	memcpy(SHM->sorted[ns][5],
	       SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
	qsort(SHM->sorted[ns][1], count, sizeof(userinfo_t *), cmputmpmode);
	qsort(SHM->sorted[ns][2], count, sizeof(userinfo_t *), cmputmpidle);
	qsort(SHM->sorted[ns][3], count, sizeof(userinfo_t *), cmputmpfrom);
	qsort(SHM->sorted[ns][4], count, sizeof(userinfo_t *), cmputmpfive);
	qsort(SHM->sorted[ns][5], count, sizeof(userinfo_t *), cmputmpchc);
	memset(nusers, 0, sizeof(nusers));
	for (i = 0; i < count; ++i) {
	    uentp = SHM->sorted[ns][0][i];
	    if (uentp && uentp->pid &&
		0 < uentp->brc_id && uentp->brc_id < MAX_BOARD)
		++nusers[uentp->brc_id - 1];
	}
	{
#if HOTBOARDCACHE
	    int     k, r, last = 0, top = 0;
	    boardheader_t *HBcache[HOTBOARDCACHE];
#endif
	    for (i = 0; i < SHM->Bnumber; ++i)
		if (SHM->bcache[i].brdname[0] != 0){
		    SHM->bcache[i].nuser = nusers[i];
#if HOTBOARDCACHE
		    if( nusers[i] > 8                             &&
			(top < HOTBOARDCACHE || nusers[i] > last) &&
			IS_BOARD(&SHM->bcache[i])                 &&
			IS_OPENBRD(&SHM->bcache[i]) ){
			for( k = top - 1 ; k >= 0 ; --k )
			    if( nusers[i] < HBcache[k]->nuser )
				break;
			if( top < HOTBOARDCACHE )
			    ++top;
			for( r = top - 1 ; r > (k + 1) ; --r )
			    HBcache[r] = HBcache[r - 1];
			HBcache[k + 1] = &SHM->bcache[i];
			last = nusers[(HBcache[top - 1] - SHM->bcache)];
		    }
#endif
		}
#if HOTBOARDCACHE
	    memcpy(SHM->HBcache, HBcache, sizeof(HBcache));
	    SHM->nHOTs = top;
#endif
	}
    }

    SHM->currsorted = ns;
    SHM->UTMPbusystate = 0;
}

int utmpsortd(int argc, char **argv)
{
    pid_t   pid;
    int     interval; // sleep interval in microsecond(1/10**6)
    int     sortall, counter = 0;

    if( fork() > 0 ){
	puts("sortutmpd daemonized...");
	return 0;
    }

    setproctitle("shmctl utmpsortd");
    if( argc < 2 || (interval = atoi(argv[1])) < 500000 )
	interval = 1000000; // default to 1 sec
    sortall = ((argc < 3) ? 1 : atoi(argv[2]));

    while( 1 ){
	if( (pid = fork()) != 0 ){
	    int     s;
	    waitpid(pid, &s, 0);
	}
	else{
	    while( 1 ){
		int     i;
		for( i = 0 ; SHM->UTMPbusystate && i < 5 ; ++i )
		    usleep(300000);

		if( SHM->UTMPneedsort ){
		    if( ++counter == sortall ){
			utmpsort(1);
			counter = 0;
		    }
		    else
			utmpsort(0);
		}

		usleep(interval);
	    }
	}
    }
}
/* end of utmpsortd -------------------------------------------------------- */

char *CTIMEx(char *buf, time_t t)
{
    strcpy(buf, ctime(&t));
    buf[strlen(buf) - 1] = 0;
    return buf;
}

int utmpstatus(int argc, char **argv)
{
    time_t  now;
    char    upbuf[64], nowbuf[64];
#ifdef OUTTA_TIMER
    now = SHM->GV2.e.now;
#else
    now = time(NULL);
#endif
    CTIMEx(upbuf,  SHM->UTMPuptime);
    CTIMEx(nowbuf, now);
    printf("now:        %s\n", nowbuf);
    printf("currsorted: %d\n", SHM->currsorted);
    printf("uptime:     %s\n", upbuf);
    printf("number:     %d\n", SHM->UTMPnumber);
    printf("busystate:  %d\n", SHM->UTMPbusystate);
    return 0;
}

int utmpreset(int argc, char **argv)
{
    SHM->UTMPbusystate=0;
    utmpstatus(0, NULL);
    return 0;
}

#define TIMES	10
int utmpwatch(int argc, char **argv)
{
    int     i;
    while( 1 ){
	for( i = 0 ; i < TIMES ; ++i ){
	    usleep(300);
	    if( !SHM->UTMPbusystate )
		break;
	    puts("busying...");
	}
	if( i == TIMES ){
	    puts("reset!");
	    SHM->UTMPbusystate = 0;
	}
    }
    return 0;
}

int utmpnum(int argc, char **argv)
{
    printf("%d.0\n", SHM->UTMPnumber);
    return 0;
}

char    *GV2str[] = {"dymaxactive", "toomanyusers",
		     "noonlineuser", NULL};
int showglobal(int argc, char **argv)
{
    int     i;
    for( i = 0 ; i < 10 ; ++i )
	printf("GLOBALVAR[%d] = %d\n", i, SHM->GLOBALVAR[i]);
    for( i = 0 ; GV2str[i] != NULL ; ++i )
	printf("GV2.%s = %d\n", GV2str[i], SHM->GV2.v[i]);
    return 0;
}

int setglobal(int argc, char **argv)
{
    int     where, value;
    if( argc != 3 ){
	puts("usage: shmctl setglobal ([0-9]|GV2) newvalue");
	return 1;
    }
    where = argv[1][0] - '0';
    value = atoi(argv[2]);

    if( 0 <= where && where <= 9 ){
	printf("GLOBALVAR[%d] = %d -> ", where, SHM->GLOBALVAR[where]);
	printf("%d\n", SHM->GLOBALVAR[where] = value);
	return 0;
    }
    else{
	for( where = 0 ; GV2str[where] != NULL ; ++where )
	    if( strcmp(GV2str[where], argv[1]) == 0 ){
		printf("GV2.%s = %d -> ", GV2str[where], SHM->GV2.v[where]);
		printf("%d\n", SHM->GV2.v[where] = value);
		return 0;
	    }
    }
    printf("GLOBALVAR %s not found\n", argv[1]);

    return 1;
}

int listpid(int argc, char **argv)
{
    int     i;
    for( i = 0 ; i < USHM_SIZE ; ++i )
	if( SHM->uinfo[i].pid > 0 )
	    printf("%d\n", SHM->uinfo[i].pid);
    return 0;
}

int listbrd(int argc, char **argv)
{
    int     i, ch;
    int     noHidden = 0;

    while( (ch = getopt(argc, argv, "hn")) != -1 )
	switch( ch ){
	case 'n':
	    noHidden = 1;
	    break;
	case 'h':
	default:
	    printf("usage:\tshmctl\tlistbrd [-n]\n"
		   "\t-n no hidden board\n");
	    return 0;
	}

    for( i = 0 ; i < MAX_BOARD ; ++i )
	if( bcache[i].brdname[0] && !(bcache[i].brdattr & BRD_GROUPBOARD) &&
	    (!noHidden ||
	     !((bcache[i].brdattr & BRD_HIDE) ||
	       (bcache[i].level && !(bcache[i].brdattr & BRD_POSTMASK) &&
		(bcache[i].level & 
		 ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))))) )
	    printf("%s\n", bcache[i].brdname);
    return 0;
}

#ifdef OUTTA_TIMER
int timed(int argc, char **argv)
{
    pid_t   pid;
    if( (pid = fork()) < 0 )
	perror("fork()");
    if( pid != 0 )
	return 0;
    setproctitle("shmctl timed");
    while( 1 ){
	SHM->GV2.e.now = time(NULL);
	sleep(1);
    }
}
#endif

#if 0
void buildclass(int bid, int level)
{
    boardheader_t  *bptr;
    if( level == 20 ){ /* for safty */
	printf("is there something wrong? class level: %d\n", level);
	return;
    }
    bptr = &bcache[bid];
    if (bptr->firstchild[0] == NULL || bptr->childcount <= 0)
        load_uidofgid(bid + 1, 1); /* 因為這邊 bid從 0開始, 所以再 +1 回來 */
    if (bptr->firstchild[1] == NULL || bptr->childcount <= 0)
        load_uidofgid(bid + 1, 1); /* 因為這邊 bid從 0開始, 所以再 +1 回來 */

    for (bptr = bptr->firstchild[0]; bptr != NULL ; bptr = bptr->next[0]) {
	if( bptr->brdattr & BRD_GROUPBOARD )
            buildclass(bptr - bcache, level + 1);
    }
}
#endif

int bBMC(int argc, char **argv)
{
    int     i;
    for( i = 0 ; i < MAX_BOARD ; ++i )
	if( bcache[i].brdname[0] )
	    buildBMcache(i + 1); /* XXXbid */
    return 0;
}

#ifdef NOKILLWATERBALL
int nkwbd(int argc, char **argv)
{
    int     ch, sleeptime = 5, timeout = 5;
    while( (ch = getopt(argc, argv, "s:t:h")) != -1 )
	switch( ch ){
	case 's':
	    if( (sleeptime = atoi(optarg)) <= 0 ){
		fprintf(stderr, "sleeptime <= 0? set to 5");
		sleeptime = 5;
	    }
	    break;

	case 't':
	    if( (timeout = atoi(optarg)) <= 0 ){
		fprintf(stderr, "timeout <= 0? set to 5");
		timeout = 20;
	    }
	    break;

	default:
	    fprintf(stderr, "usage: shmctl nkwbd [-s sleeptime] [-t timeout]\n");
	    return 0;
	}

    setproctitle("shmctl nkwbd(sleep%d,timeout%d)", sleeptime, timeout);

    switch( fork() ){
    case -1:
	perror("fork()");
	return 0;
	break;

    case 0:  /* child */
	while( 1 ){
	    int     i;
	    time_t  t = SHM->GV2.e.now - timeout;

	    for( i = 0 ; i < MAX_ACTIVE ; ++i )
		if( SHM->uinfo[i].pid        &&
		    SHM->uinfo[i].wbtime     &&
		    SHM->uinfo[i].wbtime < t    ){
		    kill(SHM->uinfo[i].pid, SIGUSR2);
		}
	    sleep(sleeptime);
	}
	break;

    default: /* parent */
	fprintf(stderr, "nkwbd\n");
	return 0;
    }
    return 0;
}
#endif

int SHMinit(int argc, char **argv)
{
    int     ch;
    int     no_uhash_loader = 0;
    while( (ch = getopt(argc, argv, "n")) != -1 )
	switch( ch ){
	case 'n':
	    no_uhash_loader = 1;
	    break;
	default:
	    printf("usage:\tshmctl\tSHMinit\n");
	    return 0;
	}

    puts("loading uhash...");
    system("bin/uhash_loader");

    attach_SHM();

    puts("loading bcache...");
    reload_bcache();

    puts("building BMcache...");
    bBMC(1, NULL);

#if 0
    puts("building class...");
    buildclass(0, 0);
#endif

    if( !no_uhash_loader ){
	puts("utmpsortd...");
	utmpsortd(1, NULL);
    }

#ifdef OUTTA_TIMER
    puts("timed...");
    timed(1, NULL);
#endif

#ifdef NOKILLWATERBALL
    puts("nkwbd...");
    nkwbd(1, NULL);
#endif

    return 0;
}

int hotboard(int argc, char **argv)
{
#define isvisiableboard(bptr)                                              \
        ((bptr)->brdname[0] &&                                             \
         !((bptr)->brdattr & BRD_GROUPBOARD) &&                            \
	 !(((bptr)->brdattr & (BRD_HIDE | BRD_TOP)) ||                     \
	   ((bptr)->level && !((bptr)->brdattr & BRD_POSTMASK) &&          \
	    ((bptr)->level &                                               \
	     ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK)))))

    int     ch, topn = 20, i, nbrds, j, k, nusers;
    struct bs {
	int     nusers;
	boardheader_t *b;
    } *brd;

    while( (ch = getopt(argc, argv, "t:h")) != -1 )
	switch( ch ){
	case 't':
	    topn = atoi(optarg);
	    if( topn <= 0 ){
		goto hotboardusage;
		return 1;
	    }
	    break;
	case 'h':
	default:
	hotboardusage:
	    fprintf(stderr, "usage: shmctl hotboard [-t topn]\n");
	    return 1;
	}

    brd = (struct bs *)malloc(sizeof(struct bs) * topn);
    brd[0].b = &SHM->bcache[0];
    brd[0].nusers = brd[0].b->brdname[0] ? brd[0].b->nuser : 0;
    nbrds = 1;

    for( i = 1 ; i < MAX_BOARD ; ++i )
	if( (isvisiableboard(&SHM->bcache[i])) &&
	    (nbrds != topn ||
	     SHM->bcache[i].nuser > brd[nbrds - 1].nusers) ){

	    nusers = SHM->bcache[i].nuser; // no race ?
	    for( k = nbrds - 2 ; k >= 0 ; --k )
		if( brd[k].nusers > nusers )
		    break;

	    if( (k + 1) < nbrds && (k + 2) < topn )
		for( j = nbrds - 1 ; j >= k + 1 ; --j )
		    brd[j] = brd[j - 1];
	    brd[k + 1].nusers = nusers;
	    brd[k + 1].b = &SHM->bcache[i];

	    if( nbrds < topn )
		++nbrds;
	}

    for( i = 0 ; i < nbrds ; ++i )
	printf("%05d|%-12s|%s\n",
	       brd[i].nusers, brd[i].b->brdname, brd[i].b->title);
    return 0;
}

int usermode(int argc, char **argv)
{
    int     i, modes[MAX_MODES];
    memset(modes, 0, sizeof(modes));
    for( i = 0 ; i < MAX_ACTIVE ; ++i )
	if( SHM->uinfo[i].userid[0] )
	    ++modes[ (int)SHM->uinfo[i].mode ];

    for( i = 0 ; i < MAX_MODES ; ++i )
	printf("%03d|%05d|%s\n", i, modes[i], ModeTypeTable[i]);
    return 0;
}

int torb(int argc, char **argv)
{
    reload_bcache();
    puts("bcache reloaded");
    return 0;
}

int rlfcache(int argc, char **argv)
{
    reload_fcache();
    puts("fcache reloaded");
    return 0;
}

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmd[] =
    { {utmpfix,    "utmpfix",    "clear dead userlist entry"},
      {utmpsortd,  "utmpsortd",  "utmp sorting daemon"},
      {utmpstatus, "utmpstatus", "list utmpstatus"},
      {utmpreset,  "utmpreset",  "SHM->busystate=0"},
      {utmpwatch,  "utmpwatch",  "to see if busystate is always 1 then fix it"},
      {utmpnum,    "utmpnum",    "print SHM->number for snmpd"},
      {showglobal, "showglobal", "show GLOBALVAR[]"},
      {setglobal,  "setglobal",  "set GLOBALVAR[]"},
      {listpid,    "listpid",    "list all pids of mbbsd"},
      {listbrd,    "listbrd",    "list board info in SHM"},
#ifdef OUTTA_TIMER
      {timed,      "timed",      "time daemon for OUTTA_TIMER"},
#endif
#ifdef NOKILLWATERBALL
      {nkwbd,      "nkwbd",      "NOKillWaterBall daemon"},
#endif
      {bBMC,       "bBMC",       "build BM cache"},
      {SHMinit,    "SHMinit",    "initialize SHM (including uhash_loader)"},
      {hotboard,   "hotboard",   "list boards of most bfriends"},
      {usermode,   "usermode",   "list #users in the same mode"},
      {torb,       "reloadbcache", "reload bcache"},
      {rlfcache,   "reloadfcache", "reload fcache"},
      {NULL, NULL, NULL} };

extern char ** environ;

int main(int argc, char **argv)
{
    int     i = 0;
	
    chdir(BBSHOME);
    initsetproctitle(argc, argv, environ);
    if( argc >= 2 ){
	if( strcmp(argv[1], "init") == 0 ){
	    /* in this case, do NOT run attach_SHM here.
	       because uhash_loader is not run yet.      */
	    return SHMinit(argc - 1, &argv[1]);
	}

	attach_SHM();
	/* shmctl doesn't need resolve_boards() first */
	//resolve_boards();
	resolve_garbage();
	resolve_fcache();
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    if( strcmp(cmd[i].cmd, argv[1]) == 0 ){
		return cmd[i].func(argc - 1, &argv[1]);
	    }
    }
    if( argc == 1 || cmd[i].func == NULL ){
	/* usage */
	printf("usage: shmctl [command] [options]\n");
	printf("commands:\n");
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    printf("\t%-15s%s\n", cmd[i].cmd, cmd[i].descript);
    }
    return 0;
}
