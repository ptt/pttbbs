/* $Id$ */
#include "bbs.h"
#include <sys/wait.h>
#include <string.h>

extern SHM_t   *SHM;

/* utmpfix ----------------------------------------------------------------- */
/* TODO merge with mbbsd/talk.c logout_friend_online() */
int logout_friend_online(userinfo_t *utmp)
{
    int my_friend_idx, thefriend;
    int k;
    int offset=(int) (utmp - &SHM->uinfo[0]);
    userinfo_t *ui;
    for(; utmp->friendtotal>0; utmp->friendtotal--) {
	if( !(0 <= utmp->friendtotal && utmp->friendtotal < MAX_FRIEND) )
	    return 1;
	my_friend_idx=utmp->friendtotal-1;
	thefriend = (utmp->friend_online[my_friend_idx] & 0xFFFFFF);
	utmp->friend_online[my_friend_idx]=0;

	if( !(0 <= thefriend && thefriend < USHM_SIZE) ) {
	    printf("\tonline friend error(%d)\n", thefriend);
	    continue;
	}

	ui = &SHM->uinfo[thefriend]; 

	if(ui->pid==0 || ui==utmp)
	    continue;
	if(ui->friendtotal > MAX_FRIEND || ui->friendtotal<0) {
	    printf("\tfriend(%d) has too many/less(%d) friends\n", thefriend, ui->friendtotal);
	    continue;
	}

	for(k=0; k<ui->friendtotal && k<MAX_FRIEND &&
		(int)(ui->friend_online[k] & 0xFFFFFF) !=offset; k++);
	if( k < ui->friendtotal && k < MAX_FRIEND ){
	    ui->friendtotal--;
	    ui->friend_online[k]=ui->friend_online[ui->friendtotal];
	    ui->friend_online[ui->friendtotal]=0;
	}
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
    int     i, fast = 0, nownum = SHM->UTMPnumber;
    int     which, nactive = 0, dofork = 1, daemonsleep = 0;
    time_t  now;
    char    *clean, buf[1024];
    IDLE_t  idle[USHM_SIZE];
    char    changeflag = 0;
    time_t  idletimeout = IDLE_TIMEOUT;
    int     lowerbound = 100, upperbound = 0;
    char    ch;

    int     killtop = 0;
    struct {
	pid_t   pid;
	int     where;
    } killlist[USHM_SIZE];

    while( (ch = getopt(argc, argv, "nt:l:FD:u:")) != -1 )
	switch( ch ){
	case 'n':
	    fast = 1;
	    break;
	case 't':
	    idletimeout = atoi(optarg);
	    break;
	case 'l':
	    lowerbound = atoi(optarg);
	    break;
	case 'F':
	    dofork = 0;
	    break;
	case 'D':
	    daemonsleep = atoi(optarg);
	    break;
	case 'u':
	    upperbound = atoi(optarg);
	    break;
	default:
	    printf("usage:\tshmctl\tutmpfix [-n] [-t timeout] [-F] [-D sleep]\n");
	    return 1;
	}

    if( daemonsleep )
	switch( fork() ){
	case -1:
	    perror("fork()");
	    return 0;
	case 0:
	    break;
	default:
	    return 0;
	}

    if( daemonsleep || dofork ){
	int     times = 1000, status;
	pid_t   pid;
	while( daemonsleep ? 1 : times-- )
	    switch( pid = fork() ){
	    case -1:
		sleep(1);
		break;
	    case 0:
#ifndef VALGRIND
		setproctitle("utmpfix");
#endif
		goto DoUtmpfix;
	    default:
#ifndef VALGRIND
		setproctitle(daemonsleep ? "utmpfixd(wait for %d)" :
			     "utmpfix(wait for %d)", (int)pid);
#endif
		waitpid(pid, &status, 0);
		if( WIFEXITED(status) && !daemonsleep )
		    return 0;
		if( !WIFEXITED(status) ){
		    /* last utmpfix fails, so SHM->UTMPbusystate is holded */
		    SHM->UTMPbusystate = 0;
		}
	    }
	return 0; // never reach
    }

 DoUtmpfix:
    killtop=0;
    changeflag=0;
    for( i = 0 ; i < 5 ; ++i )
	if( !SHM->UTMPbusystate )
	    break;
	else{
	    puts("utmpshm is busy....");
	    sleep(1);
	}
    SHM->UTMPbusystate = 1;

    printf("starting scaning... %s \n", (fast ? "(fast mode)" : ""));
    nownum = SHM->UTMPnumber;
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

    #define addkilllist(a)			\
        do {					\
	    pid_t pid=SHM->uinfo[(a)].pid;	\
	    if(pid > 0) {			\
		killlist[killtop].where = (a);	\
		killlist[killtop++].pid = pid;	\
	    }					\
        } while( 0 )
    for( i = 0 ; i < nactive ; ++i ){
	which = idle[i].index;
	clean = NULL;
	if( !isalpha(SHM->uinfo[which].userid[0]) ){
	    clean = "userid error";
	    addkilllist(which);
	}
	else if( memchr(SHM->uinfo[which].userid, '\0', IDLEN + 1) == NULL ){
	    clean = "userid without z";
	    addkilllist(which);
	}
	else if( SHM->uinfo[which].friendtotal > MAX_FRIEND || SHM->uinfo[which].friendtotal<0 ){
	    clean = "too many/less friend";
	    addkilllist(which);
	}
	else if( searchuser(SHM->uinfo[which].userid, NULL) == 0 ){
	    clean = "user not exist";
	    addkilllist(which);
	}
	else if( kill(SHM->uinfo[which].pid, 0) < 0 ){
	    /* 此條件應放最後; 其他欄位沒問題但 process 不存在才 purge_utmp */
	    clean = "process error";
	    purge_utmp(&SHM->uinfo[which]);
	}
#ifdef DOTIMEOUT
	else if( (strcasecmp(SHM->uinfo[which].userid, STR_GUEST)==0 &&
	      idle[i].idle > 60*15) ||
	    (!fast && nownum > lowerbound && 
	     idle[i].idle > idletimeout ) ) {
	  sprintf(buf, "timeout(%s",
	      ctime4(&SHM->uinfo[which].lastact));
	  buf[strlen(buf) - 1] = 0;
	  strcat(buf, ")");
	  clean = buf;
	  addkilllist(which);
	  purge_utmp(&SHM->uinfo[which]);
	  printf("%s\n", buf);
	  --nownum;
	  continue;
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
    for( i = 0 ; i < killtop ; ++i ){
	printf("sending SIGHUP to %d\n", (int)killlist[i].pid);
	kill(killlist[i].pid, SIGHUP);
    }
    sleep(3);
    for( i = 0 ; i < killtop ; ++i )
	// FIXME 前面已經 memset 把 SHM->uinfo[which] 清掉了, 此處檢查 pid 無用
	if( SHM->uinfo[killlist[i].where].pid == killlist[i].pid &&
	    kill(killlist[i].pid, 0) == 0 ){ // still alive
	    printf("sending SIGKILL to %d\n", (int)killlist[i].pid);
	    kill(killlist[i].pid, SIGKILL);
	    purge_utmp(&SHM->uinfo[killlist[i].where]);
	}
    SHM->UTMPbusystate = 0;
    if( changeflag )
	SHM->UTMPneedsort = 1;

    if( daemonsleep ){
	do{
	    sleep(daemonsleep);
	} while( upperbound && SHM->UTMPnumber < upperbound );
	goto DoUtmpfix; /* XXX: goto */
    }
    return 0;
}
/* end of utmpfix ---------------------------------------------------------- */

/* utmpsortd --------------------------------------------------------------- */
static int
cmputmpuserid(const void * i, const void * j)
{
    return strncasecmp(SHM->uinfo[*(int*)i].userid, SHM->uinfo[*(int*)j].userid, IDLEN);
}

static int
cmputmpmode(const void * i, const void * j)
{
    return SHM->uinfo[*(int*)i].mode - SHM->uinfo[*(int*)j].mode;
}

static int
cmputmpidle(const void * i, const void * j)
{
    return SHM->uinfo[*(int*)i].lastact - SHM->uinfo[*(int*)j].lastact;
}

static int
cmputmpfrom(const void * i, const void * j)
{
    return strncmp(SHM->uinfo[*(int*)i].from, SHM->uinfo[*(int*)j].from, sizeof(SHM->uinfo[0].from));
}

static int
cmputmpfive(const void * i, const void * j)
{
    userinfo_t *a=&SHM->uinfo[*(int*)i],*b=&SHM->uinfo[*(int*)j];
    int played_a=(a->five_win+a->five_lose+a->five_tie)!=0;
    int played_b=(b->five_win+b->five_lose+b->five_tie)!=0;
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
cmputmpchc(const void * i, const void * j)
{
    userinfo_t *a=&SHM->uinfo[*(int*)i],*b=&SHM->uinfo[*(int*)j];
    int total_a=a->chc_win+a->chc_lose+a->chc_tie;
    int total_b=b->chc_win+b->chc_lose+b->chc_tie;
    int played_a=(total_a!=0);
    int played_b=(total_b!=0);
    int             type;

    // NOTE: 目前 "別找我下棋" 不影響排序
    /* 1. "找我下棋" 排最前面 */
    if ((a->withme&WITHME_CHESS)!=(b->withme&WITHME_CHESS))
	return (a->withme&WITHME_CHESS)?-1:1;
#ifdef CHC_SORTBY_RATING
    /* 2. 下超過十盤棋用等級分排序 */
    if ((total_a>=10)!=(total_b>=10))
	return (total_a>=10)?-1:1;
    if (total_a>=10 && total_b>=10) {
	if (a->chess_elo_rating!=b->chess_elo_rating)
	    return b->chess_elo_rating-a->chess_elo_rating;
    }
#endif
    /* 3. 有下過棋的在沒下過的前面 */
    if ((type = played_b - played_a))
	return type;
    if (played_a == 0)
	return 0;
    /* 4. 剩下(下不超過十盤或等級分相同, 或不用等級分排序)的人以勝負和排 */
    if ((type = b->chc_win - a->chc_win))
        return type;
    if ((type = a->chc_lose - b->chc_lose))
	return type;
    return a->chc_tie - b->chc_tie;
}

static int
cmputmppid(const void * i, const void * j)
{
    return SHM->uinfo[*(int*)i].pid - SHM->uinfo[*(int*)j].pid;
}

static int
cmputmpuid(const void * i, const void * j)
{
    return SHM->uinfo[*(int*)i].uid - SHM->uinfo[*(int*)j].uid;
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
		SHM->sorted[ns][0][count++] = i;
	}
    }
    SHM->UTMPnumber = count;
    qsort(SHM->sorted[ns][0], count, sizeof(int), cmputmpuserid);
    memcpy(SHM->sorted[ns][6],
	   SHM->sorted[ns][0], sizeof(int) * count);
    memcpy(SHM->sorted[ns][7],
	   SHM->sorted[ns][0], sizeof(int) * count);
    qsort(SHM->sorted[ns][6], count, sizeof(int), cmputmpuid);
    qsort(SHM->sorted[ns][7], count, sizeof(int), cmputmppid);
    if( sortall ){
	memcpy(SHM->sorted[ns][1],
	       SHM->sorted[ns][0], sizeof(int) * count);
	memcpy(SHM->sorted[ns][2],
	       SHM->sorted[ns][0], sizeof(int) * count);
	memcpy(SHM->sorted[ns][3],
	       SHM->sorted[ns][0], sizeof(int) * count);
	memcpy(SHM->sorted[ns][4],
	       SHM->sorted[ns][0], sizeof(int) * count);
	memcpy(SHM->sorted[ns][5],
	       SHM->sorted[ns][0], sizeof(int) * count);
	qsort(SHM->sorted[ns][1], count, sizeof(int), cmputmpmode);
	qsort(SHM->sorted[ns][2], count, sizeof(int), cmputmpidle);
	qsort(SHM->sorted[ns][3], count, sizeof(int), cmputmpfrom);
	qsort(SHM->sorted[ns][4], count, sizeof(int), cmputmpfive);
	qsort(SHM->sorted[ns][5], count, sizeof(int), cmputmpchc);
	memset(nusers, 0, sizeof(nusers));
	for (i = 0; i < count; ++i) {
	    uentp = &SHM->uinfo[SHM->sorted[ns][0][i]];
	    if (uentp && uentp->pid &&
		0 < uentp->brc_id && uentp->brc_id < MAX_BOARD)
		++nusers[uentp->brc_id - 1];
	}
	{
#if HOTBOARDCACHE
	    int     k, r, last = 0, top = 0;
	    int     HBcache[HOTBOARDCACHE];
	    for (i = 0; i < HOTBOARDCACHE; i++)  HBcache[i]=-1;
#endif
	    for (i = 0; i < SHM->Bnumber; i++)
		if (SHM->bcache[i].brdname[0] != 0){
		    SHM->bcache[i].nuser = nusers[i];
#if HOTBOARDCACHE
		    if( nusers[i] > 8                             &&
			(top < HOTBOARDCACHE || nusers[i] > last) &&
			IS_BOARD(&SHM->bcache[i])                 &&
#ifdef USE_COOLDOWN
			!(SHM->bcache[i].brdattr & BRD_COOLDOWN)  &&
#endif
			IS_OPENBRD(&SHM->bcache[i]) ){
			for( k = top - 1 ; k >= 0 ; --k )
			    if(HBcache[k]>=0 &&
                                nusers[i] < SHM->bcache[HBcache[k]].nuser )
				break;
			if( top < HOTBOARDCACHE )
			    ++top;
			for( r = top - 1 ; r > (k + 1) ; --r )
			    HBcache[r] = HBcache[r - 1];
			HBcache[k + 1] = i;
			last = nusers[HBcache[top - 1]];
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

    if( argc < 2 || (interval = atoi(argv[1])) < 500000 )
	interval = 1000000; // default to 1 sec
    sortall = ((argc < 3) ? 1 : atoi(argv[2]));

#ifndef VALGRIND
    setproctitle("shmctl utmpsortd");
#endif

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

char *CTIMEx(char *buf, time4_t t)
{
    strcpy(buf, ctime4(&t));
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
    for( i = 0 ; GV2str[i] != NULL ; ++i )
	printf("GV2.%s = %d\n", GV2str[i], SHM->GV2.v[i]);
    return 0;
}

int setglobal(int argc, char **argv)
{
    int     where, value;
    if( argc != 3 ){
	puts("usage: shmctl setglobal (GV2) newvalue");
	return 1;
    }
    value = atoi(argv[2]);

    for( where = 0 ; GV2str[where] != NULL ; ++where )
	if( strcmp(GV2str[where], argv[1]) == 0 ){
	    printf("GV2.%s = %d -> ", GV2str[where], SHM->GV2.v[where]);
	    printf("%d\n", SHM->GV2.v[where] = value);
	    return 0;
	}
    printf("SHM global variable %s not found\n", argv[1]);

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
    int     i = 0;

    if (argc == 2) 
	i = atoi(argv[1]);

    if(i > 0 && i < MAX_BOARD) 
    {
	int di = i;

	/* print details */
	boardheader_t b = bcache[di-1];
	printf("brdname(bid):\t%s\n", b.brdname);
	printf("title:\t%s\n", b.title);
	printf("BM:\t%s\n", b.BM);
	printf("brdattr:\t%08x ", b.brdattr);

#define SHOWBRDATTR(x) if(b.brdattr & x) printf(#x " ");

	SHOWBRDATTR(BRD_NOZAP);
	SHOWBRDATTR(BRD_NOCOUNT);
	SHOWBRDATTR(BRD_NOTRAN);
	SHOWBRDATTR(BRD_GROUPBOARD);
	SHOWBRDATTR(BRD_HIDE);
	SHOWBRDATTR(BRD_POSTMASK);
	SHOWBRDATTR(BRD_ANONYMOUS);
	SHOWBRDATTR(BRD_DEFAULTANONYMOUS);
	SHOWBRDATTR(BRD_BAD);
	SHOWBRDATTR(BRD_VOTEBOARD);
	SHOWBRDATTR(BRD_WARNEL);
	SHOWBRDATTR(BRD_TOP);
	SHOWBRDATTR(BRD_NORECOMMEND);
	SHOWBRDATTR(BRD_BLOG);
	SHOWBRDATTR(BRD_BMCOUNT);
	SHOWBRDATTR(BRD_SYMBOLIC);
	SHOWBRDATTR(BRD_NOBOO);
	SHOWBRDATTR(BRD_LOCALSAVE);
	SHOWBRDATTR(BRD_RESTRICTEDPOST);
	SHOWBRDATTR(BRD_GUESTPOST);
	SHOWBRDATTR(BRD_COOLDOWN);
	SHOWBRDATTR(BRD_CPLOG);
	SHOWBRDATTR(BRD_NOFASTRECMD);

	printf("\n");

        printf("post_limit_posts:\t%d\n", b.post_limit_posts);
        printf("post_limit_logins:\t%d\n", b.post_limit_logins);
        printf("post_limit_regtime:\t%d\n", b.post_limit_regtime);
        printf("level:\t%d\n", b.level);
        printf("gid:\t%d\n", b.gid);
        printf("parent:\t%d\n", b.parent);
        printf("childcount:\t%d\n", b.childcount);
        printf("nuser:\t%d\n", b.nuser);

        printf("next[0]:\t%d\n", b.next[0]);
        printf("next[1]:\t%d\n", b.next[1]);
        printf("firstchild[0]:\t%d\n", b.firstchild[0]);
	printf("firstchild[1]:\t%d\n", b.firstchild[1]);
        printf("---- children: ---- \n");
        for (i = 0; i < MAX_BOARD; i++)
        {
            if(bcache[i].gid == di && bcache[i].brdname[0])
                printf("%4d %-13s%-25.25s%s\n",
                        i+1, bcache[i].brdname,
                        bcache[i].BM, bcache[i].title);
        }
    } else
    for( i = 0 ; i < MAX_BOARD ; ++i )
    {
	if(bcache[i].brdname[0])
	    printf("%03d %-13s%-25.25s%s\n", i+1, bcache[i].brdname, bcache[i].BM, bcache[i].title);
    }
    return 0;
}

static void update_brd(int i) {
    if(substitute_record(BBSHOME "/" FN_BOARD, &bcache[i],sizeof(boardheader_t),i+1) < 0) {
	printf("\n! CANNOT WRITE: " BBSHOME "/" FN_BOARD "\n");
	exit(0);
    }
}

int fixbrd(int argc, char **argv)
{
    int     i = 0;

    for( i = 0 ; i < MAX_BOARD ; ++i )
    {
	if(!bcache[i].brdname[0])
	    continue;
	/* do whatever you wanna fix here. */

#if 0
	/* upgrade from old NOFASTRECMD (default pause) to new format
	 * (BM config) */
	if(bcache[i].brdattr & BRD_NOFASTRECMD)
	{
	    printf("board with no fastrecmd: #%d [%s]\n",
		    i+1, bcache[i].brdname);
	    bcache[i].fastrecommend_pause = 90;
	    update_brd(i);
	}
#endif

#if 0
	/* fix parent, hope so */
	if(bcache[i].parent > MAX_BOARD) {
	    printf("parent: #%d [%s] *%d\n", i+1, bcache[i].brdname, bcache[i].parent);
	    bcache[i].parent = 0;
	    update_brd(i);
	}
#endif

#if 0
	/* alert wrong gid */
	if(bcache[i].gid < 1) {
	    printf("gid: #%d [%s] *%d\n", i+1, bcache[i].brdname, bcache[i].gid);
	}
#endif
    }
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
#ifndef VALGRIND
    setproctitle("shmctl timed");
#endif
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

#ifndef VALGRIND
    setproctitle("shmctl nkwbd(sleep%d,timeout%d)", sleeptime, timeout);
#endif

    switch( fork() ){
    case -1:
	perror("fork()");
	return 0;
	break;

    case 0:  /* child */
	while( 1 ){
	    int     i;
	    time_t  t = SHM->GV2.e.now - timeout;

	    for( i = 0 ; i < USHM_SIZE ; ++i )
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

#ifdef OUTTA_TIMER
    puts("timed...");
    timed(1, argv);
#endif

    puts("loading bcache...");
    reload_bcache();

    puts("building BMcache...");
    bBMC(1, argv);

#if 0
    puts("building class...");
    buildclass(0, 0);
#endif

    if( !no_uhash_loader ){
	puts("utmpsortd...");
	utmpsortd(1, argv);
    }

#ifdef NOKILLWATERBALL
    puts("nkwbd...");
    nkwbd(1, argv);
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
    for( i = 0 ; i < USHM_SIZE ; ++i )
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

int iszero(void *addr, int size)
{
    char *a=(char*)addr;
    int i;
    for(i=0;i<size;i++)
	if(a[i]!=0) return 0;
    return 1;
}

#define TESTZERO(x,i) do { if(!iszero((x), sizeof(x))) printf("%s is dirty(i=%d)\n",#x,i); } while(0);
int testgap(int argc, char *argv[])
{
    int i;
    TESTZERO(SHM->gap_1,0);
    TESTZERO(SHM->gap_2,0);
    TESTZERO(SHM->gap_3,0);
    TESTZERO(SHM->gap_4,0);
    TESTZERO(SHM->gap_5,0);
    TESTZERO(SHM->gap_6,0);
    TESTZERO(SHM->gap_7,0);
    TESTZERO(SHM->gap_8,0);
    TESTZERO(SHM->gap_9,0);
    TESTZERO(SHM->gap_10,0);
    TESTZERO(SHM->gap_11,0);
    TESTZERO(SHM->gap_12,0);
    TESTZERO(SHM->gap_13,0);
    TESTZERO(SHM->gap_14,0);
    TESTZERO(SHM->gap_15,0);
    TESTZERO(SHM->gap_16,0);
    TESTZERO(SHM->gap_17,0);
    TESTZERO(SHM->gap_18,0);
    TESTZERO(SHM->gap_19,0);
    for(i=0; i<USHM_SIZE; i++) {
	TESTZERO(SHM->uinfo[i].gap_1,i);
	TESTZERO(SHM->uinfo[i].gap_2,i);
	TESTZERO(SHM->uinfo[i].gap_3,i);
	TESTZERO(SHM->uinfo[i].gap_4,i);
    }
    return 0;
}

int showstat(int argc, char *argv[])
{
    int i;
    int flag_clear=0;
    char *stat_desc[]={
	"STAT_LOGIN",
	"STAT_SHELLLOGIN",
	"STAT_VEDIT",
	"STAT_TALKREQUEST",
	"STAT_WRITEREQUEST",
	"STAT_MORE",
	"STAT_SYSWRITESOCKET",
	"STAT_SYSSELECT",
	"STAT_SYSREADSOCKET",
	"STAT_DOSEND",
	"STAT_SEARCHUSER",
	"STAT_THREAD",
	"STAT_SELECTREAD",
	"STAT_QUERY",
	"STAT_DOTALK",
	"STAT_FRIENDDESC",
	"STAT_FRIENDDESC_FILE",
	"STAT_PICKMYFRIEND",
	"STAT_PICKBFRIEND",
	"STAT_GAMBLE",
	"STAT_DOPOST",
	"STAT_READPOST",
	"STAT_RECOMMEND",
	"STAT_TODAYLOGIN_MIN",
	"STAT_TODAYLOGIN_MAX",
	"STAT_SIGINT",
	"STAT_SIGQUIT",
	"STAT_SIGILL",
	"STAT_SIGABRT",
	"STAT_SIGFPE",
	"STAT_SIGBUS",
	"STAT_SIGSEGV",
	"STAT_READPOST_12HR",
	"STAT_READPOST_1DAY",
	"STAT_READPOST_3DAY",
	"STAT_READPOST_7DAY",
	"STAT_READPOST_OLD",
    };

    if(argv[1] && strcmp(argv[1],"-c")==0)
	flag_clear=1;
    for(i=0; i<STAT_NUM; i++) {
	char *desc= i*sizeof(char*)<sizeof(stat_desc)?stat_desc[i]:"?";
	printf("%s:\t%d\n", desc, SHM->statistic[i]);
    }
    if(flag_clear)
	memset(SHM->statistic, 0, sizeof(SHM->statistic));
    return 0;
}

int dummy(int argc, char *argv[])
{
    return 0;
}

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmd[] = { 
    {dummy,      "\b\b\b\bStart daemon:", ""},
    {utmpsortd,  "utmpsortd",  "utmp sorting daemon"},
#ifdef OUTTA_TIMER
    {timed,      "timed",      "time daemon for OUTTA_TIMER"},
#endif
#ifdef NOKILLWATERBALL
    {nkwbd,      "nkwbd",      "NOKillWaterBall daemon"},
#endif

    {dummy,      "\b\b\b\bBuild cache/fix tool:", ""},
    {torb,       "reloadbcache", "reload bcache"},
    {rlfcache,   "reloadfcache", "reload fcache"},
    {bBMC,       "bBMC",       "build BM cache"},
    {utmpfix,    "utmpfix",    "clear dead userlist entry & kick idle user"},
    {utmpreset,  "utmpreset",  "SHM->busystate=0"},
    {utmpwatch,  "utmpwatch",  "to see if busystate is always 1 then fix it"},

    {dummy,      "\b\b\b\bShow info:", ""},
    {utmpnum,    "utmpnum",    "print SHM->number for snmpd"},
    {utmpstatus, "utmpstatus", "list utmpstatus"},
    {listpid,    "listpid",    "list all pids of mbbsd"},
    {listbrd,    "listbrd",    "list board info in SHM"},
    {fixbrd,     "fixbrd",     "fix board info in SHM"},
    {hotboard,   "hotboard",   "list boards of most bfriends"},
    {usermode,   "usermode",   "list #users in the same mode"},
    {showstat,   "showstat",   "show statistics"},
    {testgap,    "testgap",    "test SHM->gap zeroness"},

    {dummy,      "\b\b\b\bMisc:", ""},
    {showglobal, "showglobal", "show GLOBALVAR[]"},
    {setglobal,  "setglobal",  "set GLOBALVAR[]"},
    {SHMinit,    "SHMinit",    "initialize SHM (including uhash_loader)"},
    {NULL, NULL, NULL}
};

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
