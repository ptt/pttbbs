/* $Id: shmctl.c,v 1.29 2002/11/05 14:18:47 in2 Exp $ */
#include "bbs.h"

extern SHM_t   *SHM;

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
	}
    }
    SHM->UTMPbusystate = 0;
    return 0;
}

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

/* ulistsort */
static int cmputmpuserid(const void *i, const void *j){
    return strcasecmp((*((userinfo_t**)i))->userid,
		      (*((userinfo_t**)j))->userid);
}

static int cmputmpmode(const void *i, const void *j){
    return (*((userinfo_t**)i))->mode-
	(*((userinfo_t**)j))->mode;
} 

static int cmputmpidle(const void *i, const void *j){
    return (*((userinfo_t**)i))->lastact-
	(*((userinfo_t**)j))->lastact;
} 

static int cmputmpfrom(const void *i, const void *j){
  return strcasecmp((*((userinfo_t**)i))->from,
		    (*((userinfo_t**)j))->from);
} 

static int cmputmpfive(const void *i, const void *j){
    int type;
    if((type=(*((userinfo_t**)j))->five_win - (*((userinfo_t**)i))->five_win))
	return type;
    if((type=(*((userinfo_t**)i))->five_lose-(*((userinfo_t**)j))->five_lose))
	return type;
    return (*((userinfo_t**)i))->five_tie-(*((userinfo_t**)j))->five_tie;
} 

#if 0
static int cmputmpsex(const void *i, const void *j){
    static int ladyfirst[]={1,0,1,0,1,0,3,3};
    return ladyfirst[(*((userinfo_t**)i))->sex]-
	ladyfirst[(*((userinfo_t**)j))->sex];
}
#endif

static int cmputmppid(const void *i, const void *j){
    return (*((userinfo_t**)i))->pid-(*((userinfo_t**)j))->pid;
}
static int cmputmpuid(const void *i, const void *j){
    return (*((userinfo_t**)i))->uid-(*((userinfo_t**)j))->uid;
}

int utmpsort(int argc, char **argv)
{
    time_t now=time(NULL);
    int count, i, ns;
    userinfo_t *uentp;
    
    if(now-SHM->UTMPuptime<60 && (now==SHM->UTMPuptime || SHM->UTMPbusystate)){
	puts("lazy sort");
	//return; /* lazy sort */
    }
    SHM->UTMPbusystate=1;
    SHM->UTMPuptime = now;
    ns=(SHM->currsorted?0:1);

    for(uentp = &SHM->uinfo[0], count=0, i=0;
	i< USHM_SIZE; i++,uentp = &SHM->uinfo[i])
	if(uentp->pid){
	    SHM->sorted[ns][0][count++]= uentp;
        }
    SHM->number = count;
    qsort(SHM->sorted[ns][0],count,sizeof(userinfo_t*),cmputmpuserid);
    memcpy(SHM->sorted[ns][1],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(SHM->sorted[ns][2],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(SHM->sorted[ns][3],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(SHM->sorted[ns][4],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(SHM->sorted[ns][5],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(SHM->sorted[ns][6],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(SHM->sorted[ns][7],SHM->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    qsort(SHM->sorted[ns][1], count, sizeof(userinfo_t *), cmputmpmode );
    qsort(SHM->sorted[ns][2], count, sizeof(userinfo_t *), cmputmpidle );
    qsort(SHM->sorted[ns][3], count, sizeof(userinfo_t *), cmputmpfrom );
    qsort(SHM->sorted[ns][4], count, sizeof(userinfo_t *), cmputmpfive );
    //qsort(SHM->sorted[ns][5], count, sizeof(userinfo_t *), cmputmpsex );
    qsort(SHM->sorted[ns][6], count, sizeof(userinfo_t *), cmputmpuid );
    qsort(SHM->sorted[ns][7], count, sizeof(userinfo_t *), cmputmppid );
    SHM->currsorted=ns;
    SHM->UTMPbusystate=0;

    puts("new utmpstate");
    utmpstate(0, NULL);
    return 0;
}
/* end of ulistsort */

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

int showglobal(int argc, char **argv)
{
    int     i;
    for( i = 0 ; i < 10 ; ++i )
	printf("GLOBALVAR[%d] = %d\n", i, SHM->GLOBALVAR[i]);
    return 0;
}

int setglobal(int argc, char **argv)
{
    int     where;
    if( argc != 3 )
	return 1;
    where = atoi(argv[1]);
    if( !(0 <= where && where <= 9) ){
	puts("only GLOBALVAR[0] ~ GLOBALVAR[9]");
	return 1;
    }
    printf("GLOBALVAR[%d] = %d -> ", where, SHM->GLOBALVAR[where]);
    printf("%d\n", SHM->GLOBALVAR[where] = atoi(argv[2]));
    return 0;
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
      {utmpstate,  "utmpstate",  "list utmpstate"},
      {utmpreset,  "utmpreset",  "SHM->busystate=0"},
      {utmpsort,   "utmpsort",   "sort ulist"},
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
	resolve_boards();
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
