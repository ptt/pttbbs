#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include "config.h"
#include "pttstruct.h"
#include "util.h"

extern struct utmpfile_t *utmpshm;

int utmpfix(int argc, char **argv)
{
    int     i;
    char    buf[1024], *clean;
    struct  stat    st;
    if( utmpshm->busystate ){
	puts("utmpshm is busy");
	return 0;
    }
    utmpshm->busystate = 1;
    for( i = 0 ; i < USHM_SIZE ; ++i )
	if( utmpshm->uinfo[i].pid ){
	    clean = NULL;
	    if( !isalpha(utmpshm->uinfo[i].userid[0]) )
		clean = "userid error";
	    else{
		sprintf(buf, "/proc/%d", utmpshm->uinfo[i].pid);
		if( stat(buf, &st) < 0 ) 
		    clean = "process not exist";
		else{
		    sprintf(buf, "home/%c/%s",
			    utmpshm->uinfo[i].userid[0],
			    utmpshm->uinfo[i].userid);
		    if( stat(buf, &st) < 0 )
			clean = "user not exist";
		}
	    }
	    
	    if( clean ){
		printf("clean %06d(%s), userid: %s\n",
		       i, clean, utmpshm->uinfo[i].userid);
		memset(&utmpshm->uinfo[i], 0, sizeof(userinfo_t));
	    }
	    else if ( utmpshm->uinfo[i].mind > 40 ){
		printf("mind fix: %06d, userid: %s, mind: %d\n",
		       i, utmpshm->uinfo[i].userid, utmpshm->uinfo[i].mind);
		utmpshm->uinfo[i].mind %= 40;
	    }
	}
    utmpshm->busystate = 0;
    return 0;
}

char *CTIME(char *buf, time_t t)
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
    CTIME(upbuf,  utmpshm->uptime);
    CTIME(nowbuf, time(NULL));
    printf("now:        %s\n", nowbuf);
    printf("currsorted: %d\n", utmpshm->currsorted);
    printf("uptime:     %s\n", upbuf);
    printf("number:     %d\n", utmpshm->number);
    printf("busystate:  %d\n", utmpshm->busystate);
    return 0;
}

int utmpreset(int argc, char **argv)
{
    utmpshm->busystate=0;
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

static int cmputmpsex(const void *i, const void *j){
    static int ladyfirst[]={1,0,1,0,1,0,3,3};
    return ladyfirst[(*((userinfo_t**)i))->sex]-
	ladyfirst[(*((userinfo_t**)j))->sex];
}
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
    
    if(now-utmpshm->uptime<60 && (now==utmpshm->uptime || utmpshm->busystate)){
	puts("lazy sort");
	//return; /* lazy sort */
    }
    utmpshm->busystate=1;
    utmpshm->uptime = now;
    ns=(utmpshm->currsorted?0:1);

    for(uentp = &utmpshm->uinfo[0], count=0, i=0;
	i< USHM_SIZE; i++,uentp = &utmpshm->uinfo[i])
	if(uentp->pid){
	    utmpshm->sorted[ns][0][count++]= uentp;
        }
    utmpshm->number = count;
    qsort(utmpshm->sorted[ns][0],count,sizeof(userinfo_t*),cmputmpuserid);
    memcpy(utmpshm->sorted[ns][1],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(utmpshm->sorted[ns][2],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(utmpshm->sorted[ns][3],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(utmpshm->sorted[ns][4],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(utmpshm->sorted[ns][5],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(utmpshm->sorted[ns][6],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    memcpy(utmpshm->sorted[ns][7],utmpshm->sorted[ns][0],
						 sizeof(userinfo_t *)*count);
    qsort(utmpshm->sorted[ns][1], count, sizeof(userinfo_t *), cmputmpmode );
    qsort(utmpshm->sorted[ns][2], count, sizeof(userinfo_t *), cmputmpidle );
    qsort(utmpshm->sorted[ns][3], count, sizeof(userinfo_t *), cmputmpfrom );
    qsort(utmpshm->sorted[ns][4], count, sizeof(userinfo_t *), cmputmpfive );
    //qsort(utmpshm->sorted[ns][5], count, sizeof(userinfo_t *), cmputmpsex );
    qsort(utmpshm->sorted[ns][6], count, sizeof(userinfo_t *), cmputmpuid );
    qsort(utmpshm->sorted[ns][7], count, sizeof(userinfo_t *), cmputmppid );
    utmpshm->currsorted=ns;
    utmpshm->busystate=0;

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
	    if( !utmpshm->busystate )
		break;
	    puts("busying...");
	}
	if( i == TIMES ){
	    puts("reset!");
	    utmpshm->busystate = 0;
	}
    }
    return 0;
}

int utmpnum(int argc, char **argv)
{
    printf("%d.0\n", utmpshm->number);
    return 0;
}

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmd[] =
    { {utmpfix,   "utmpfix",   "clear dead userlist entry"},
      {utmpstate, "utmpstate", "list utmpstate"},
      {utmpreset, "utmpreset", "utmpshm->busystate=0"},
      {utmpsort,  "utmpsort",  "sort ulist"},
      {utmpwatch, "utmpwatch", "to see if busystate is always 1 then fix it"},
      {utmpnum,   "utmpnum",   "print utmpshm->number for snmpd"},
      {NULL, NULL, NULL} };

int main(int argc, char **argv)
{
    int     i = 0;
	
    if( argc >= 2 ){
	/* shmctl shouldn't create shm itself */
	int     shmid = shmget(UHASH_KEY, sizeof(*utmpshm), 0);
	if( shmid < 0 ){
	    perror("attach utmpshm");
	    return 1;
	}
	chdir(BBSHOME);
	resolve_utmp();
	resolve_boards();
	//resolve_garbage();
	resolve_fcache();
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    if( strcmp(cmd[i].cmd, argv[1]) == 0 ){
		return cmd[i].func(argc - 2, &argv[2]);
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
