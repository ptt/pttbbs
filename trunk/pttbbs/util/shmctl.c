/* $Id: shmctl.c,v 1.38 2003/04/08 09:53:21 in2 Exp $ */
#include "bbs.h"
#include <sys/wait.h>

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
		for(k=0; k<ui->friendtotal && 
			(int)(ui->friend_online[k] & 0xFFFFFF) !=offset; k++);
		if(k<ui->friendtotal){
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
    int     which, nactive = 0;
    time_t  now, timeout = -1;
    char    *clean, buf[1024], ch;
    IDLE_t  idle[USHM_SIZE];
    char    changeflag = 0;

    while( (ch = getopt(argc, argv, "nt:l:")) != -1 )
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
	default:
	    printf("usage:\tshmctl\tutmpfix [-n] [-t timeout]\n");
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
    printf("starting scaning... %s \n", (fast ? "(fast mode)" : ""));
    time(&now);
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
    int             type;
    if ((type = (*((userinfo_t **) j))->five_win - (*((userinfo_t **) i))->five_win))
	return type;
    if ((type = (*((userinfo_t **) i))->five_lose - (*((userinfo_t **) j))->five_lose))
	return type;
    return (*((userinfo_t **) i))->five_tie - (*((userinfo_t **) j))->five_tie;
}

static int
cmputmpchc(const void *i, const void *j)
{
    int             type;
    if ((type = (*((userinfo_t **) j))->chc_win - (*((userinfo_t **) i))->chc_win))
	return type;
    if ((type = (*((userinfo_t **) i))->chc_lose - (*((userinfo_t **) j))->chc_lose))
	return type;
    return (*((userinfo_t **) i))->chc_tie - (*((userinfo_t **) j))->chc_tie;
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

inline void utmpsort(void)
{
    userinfo_t     *uentp;
    int             count, i, ns;
    short           nusers[MAX_BOARD];

    SHM->UTMPbusystate = 1;
    SHM->UTMPuptime = time(NULL);
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
    for (i = 0; i < count; ++i)
	((userinfo_t *) SHM->sorted[ns][0][i])->idoffset = i;
    memcpy(SHM->sorted[ns][1], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][2], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][3], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][4], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][5], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][6], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    memcpy(SHM->sorted[ns][7], SHM->sorted[ns][0], sizeof(userinfo_t *) * count);
    qsort(SHM->sorted[ns][1], count, sizeof(userinfo_t *), cmputmpmode);
    qsort(SHM->sorted[ns][2], count, sizeof(userinfo_t *), cmputmpidle);
    qsort(SHM->sorted[ns][3], count, sizeof(userinfo_t *), cmputmpfrom);
    qsort(SHM->sorted[ns][4], count, sizeof(userinfo_t *), cmputmpfive);
    qsort(SHM->sorted[ns][5], count, sizeof(userinfo_t *), cmputmpchc);
    qsort(SHM->sorted[ns][6], count, sizeof(userinfo_t *), cmputmpuid);
    qsort(SHM->sorted[ns][7], count, sizeof(userinfo_t *), cmputmppid);
    SHM->currsorted = ns;
    SHM->UTMPbusystate = 0;

    memset(nusers, 0, sizeof(nusers));
    for (i = 0; i < count; ++i) {
	uentp = SHM->sorted[ns][0][i];
	if (uentp && uentp->pid &&
	    0 < uentp->brc_id && uentp->brc_id < MAX_BOARD)
	    ++nusers[uentp->brc_id - 1];
    }
    for (i = 0; i < SHM->Bnumber; ++i)
	if (SHM->bcache[i].brdname[0] != 0)
	    SHM->bcache[i].nuser = nusers[i];
}

int utmpsortd(int argc, char **argv)
{
    pid_t   pid;
    if( fork() > 0 ){
	puts("sortutmpd daemonized...");
	return 0;
    }

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

		if( SHM->UTMPneedsort )
		    utmpsort();

		sleep(1);
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

int utmpstate(int argc, char **argv)
{
    time_t  now;
    char    upbuf[64], nowbuf[64];
    now = time(NULL);
    CTIMEx(upbuf,  SHM->UTMPuptime);
    CTIMEx(nowbuf, time(NULL));
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
    utmpstate(0, NULL);
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

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmd[] =
    { {utmpfix,    "utmpfix",    "clear dead userlist entry"},
      {utmpsortd,  "utmpsortd",  "utmp sorting daemon"},
      {utmpstate,  "utmpstate",  "list utmpstate"},
      {utmpreset,  "utmpreset",  "SHM->busystate=0"},
      {utmpwatch,  "utmpwatch",  "to see if busystate is always 1 then fix it"},
      {utmpnum,    "utmpnum",    "print SHM->number for snmpd"},
      {showglobal, "showglobal", "show GLOBALVAR[]"},
      {setglobal,  "setglobal",  "set GLOBALVAR[]"},
      {listpid,    "listpid",    "list all pids of mbbsd"},
      {NULL, NULL, NULL} };

int main(int argc, char **argv)
{
    int     i = 0;
	
    if( argc >= 2 ){
	/* shmctl shouldn't create shm itself */
      	int     shmid = shmget(SHM_KEY, sizeof(SHM_t), 0);
	if( shmid < 0 ){
	  printf("%d\n", errno);
	    perror("attach utmpshm");
	    return 1;
	}
	chdir(BBSHOME);
	resolve_utmp();
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
	printf("usage: bbsctl [command] [options]\n");
	printf("commands:\n");
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    printf("\t%-15s%s\n", cmd[i].cmd, cmd[i].descript);
    }
    return 0;
}
