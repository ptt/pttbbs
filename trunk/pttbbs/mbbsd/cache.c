/* $Id: cache.c,v 1.6 2002/04/04 18:02:31 ptt Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#ifdef __FreeBSD__
#include <machine/param.h>
#endif

#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "perm.h"
#include "modes.h"
#include "proto.h"

#ifndef __FreeBSD__
union semun {
    int val;                    /* value for SETVAL */
    struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
    unsigned short int *array;  /* array for GETALL, SETALL */
    struct seminfo *__buf;      /* buffer for IPC_INFO */
};
#endif

int fcache_semid;

/* the reason for "safe_sleep" is that we may call sleep during
   SIGALRM handler routine, while SIGALRM is blocked.
   if we use the original sleep, we'll never wake up. */
unsigned int safe_sleep(unsigned int seconds) {
    /* jochang  sleep有問題時用*/
    sigset_t set,oldset;
    
    sigemptyset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);
    if(sigismember(&oldset, SIGALRM)) {
	unsigned long retv;
#if !defined(_BBS_UTIL_C_)
	log_usies("SAFE_SLEEP ", "avoid hang");
#endif
	sigemptyset(&set);
	sigaddset(&set,SIGALRM);
	sigprocmask(SIG_UNBLOCK,&set,NULL);
	retv=sleep(seconds);
	sigprocmask(SIG_BLOCK,&set,NULL);
	return retv;
    }
    return sleep(seconds);
}

#if defined(_BBS_UTIL_C_)
static void setapath(char *buf, char *boardname) {
    sprintf(buf, "man/boards/%c/%s", boardname[0], boardname);
}

static char *str_dotdir = ".DIR";

static void setadir(char *buf, char *path) {
    sprintf(buf, "%s/%s", path, str_dotdir);
}
#endif

static void attach_err(int shmkey, char *name) {
    fprintf(stderr, "[%s error] key = %x\n", name, shmkey);
    fprintf(stderr, "errno = %d: %s\n", errno, strerror(errno));
    exit(1);
}

static void *attach_shm(int shmkey, int shmsize) {
    void *shmptr;
    int shmid;

    char *empty_addr;
    /* set up one page in-accessible -- jochang */
    {
	int fd = open("/dev/zero",O_RDONLY);
	int size = ((shmsize + 4095) / 4096) * 4096;
	
	munmap(
	    (empty_addr=mmap(0,4096+size,PROT_NONE,MAP_PRIVATE,fd,0))+4096
	    ,size);
	
	close(fd);
    }
    
    shmid = shmget(shmkey, shmsize, 0);
    if(shmid < 0) {
	shmid = shmget(shmkey, shmsize, IPC_CREAT | 0600);
	if(shmid < 0)
	    attach_err(shmkey, "shmget");
	shmptr = (void *)shmat(shmid, NULL, 0);
	if(shmptr == (void *)-1)
	    attach_err(shmkey, "shmat");
    } else {
	shmptr = (void *)shmat(shmid, NULL, 0);
	if(shmptr == (void *)-1)
	    attach_err(shmkey, "shmat");
    }
    
    /* unmap the page -- jochang */
    {
	munmap(empty_addr,4096);		
    }
    return shmptr;
}


#define SEM_FLG        0600    /* semaphore mode */

/* ----------------------------------------------------- */
/* semaphore : for critical section                      */
/* ----------------------------------------------------- */
void sem_init(int semkey,int *semid) {
    union semun s;

    s.val=1;
    *semid = semget(semkey, 1, 0);
    if(*semid == -1) {
	*semid = semget(semkey, 1, IPC_CREAT | SEM_FLG);
	if(*semid == -1)
	    attach_err(semkey, "semget");
	semctl(*semid, 0, SETVAL, s);
    }
}

void sem_lock(int op,int semid) {
    struct sembuf sops;

    sops.sem_num = 0;
    sops.sem_flg = SEM_UNDO;
    sops.sem_op = op;
    semop(semid, &sops, 1);
}

/* uhash *******************************************/
/* the design is this:
   we use another stand-alone program to create and load data into the hash.
   (that program could be run in rc-scripts or something like that)
   after loading completes, the stand-alone program sets loaded to 1 and exits.
   
   the bbs exits if it can't attach to the shared memory or 
   the hash is not loaded yet.
*/
uhash_t *uhash;

/* attach_uhash should be called before using uhash */
void attach_uhash() {
    uhash = attach_shm(UHASH_KEY, sizeof(*uhash));
    if(!uhash->loaded)	/* assume fresh shared memory is zeroed */
	exit(1);
}

void add_to_uhash(int n, char *id) {
    int *p, h = StringHash(id);
    strcpy(uhash->userid[n], id);
    
    p = &(uhash->hash_head[h]);
			
    while(*p != -1)
	p = &(uhash->next_in_hash[*p]);
				
    uhash->next_in_hash[*p = n] = -1;
}

/* note: after remove_from_uhash(), you should add_to_uhash()
   (likely with a different name) */
void remove_from_uhash(int n) {
    int h = StringHash(uhash->userid[n]);
    int *p = &(uhash->hash_head[h]);
    
    while(*p != -1 && *p != n)
	p = &(uhash->next_in_hash[*p]);
    if(*p == n)
	*p = uhash->next_in_hash[n];
}

int setumoney(int uid, int money) {
   uhash->money[uid-1]=money;
   passwd_update_money(uid);
   return uhash->money[uid-1];
}

int deumoney(int uid, int money) {
   if(money<0 && uhash->money[uid-1]<-money)
       return setumoney(uid,0); 
   else
       return setumoney(uid,uhash->money[uid-1]+money);
}
int demoney(int money) {
  extern int usernum;
  return deumoney(usernum,money);
} 
int moneyof(int uid){   /* ptt 改進金錢處理效率 */
   return uhash->money[uid-1]; 
}
int searchuser(char *userid) {
    int h,p;
    
    h = StringHash(userid);
    p = uhash->hash_head[h];
	
    while(p != -1) {
	if(strcasecmp(uhash->userid[p],userid) == 0) {
	    strcpy(userid,uhash->userid[p]);
	    return p + 1;
	}
	p = uhash->next_in_hash[p];
    }
    return 0;
}

#if !defined(_BBS_UTIL_C_)
extern userec_t xuser;

int getuser(char *userid) {
    int uid;
    
    if((uid = searchuser(userid)))
	passwd_query(uid, &xuser);
    return uid;
}

char *getuserid(int num) {
    if(--num >= 0 && num < MAX_USERS)
	return ((char *) uhash->userid[num]);
    return NULL;
}

void setuserid(int num, char *userid) {
    if(num > 0 && num <= MAX_USERS) {
	if(num > uhash->number)
	    uhash->number = num;
	else
	    remove_from_uhash(num-1);
	add_to_uhash(num-1,userid);
    }
}

/* 0 ==> 找過期帳號 */
/* 1 ==> 建立新帳號 */
/* should do it by searching "" in the hash */
int searchnewuser(int mode) {
    register int i, num;

    num = uhash->number;
    i = 0;

    /* 為什麼這邊不用 hash table 去找而要用 linear search? */
    while(i < num) {
	if(!uhash->userid[i++][0])
	    return i;
    }
    if(mode && (num < MAX_USERS))
	return num + 1;
    return 0;
}

char *u_namearray(char buf[][IDLEN + 1], int *pnum, char *tag) {
    register struct uhash_t *reg_ushm = uhash;
    register char *ptr, tmp;
    register int n, total;
    char tagbuf[STRLEN];
    int ch, ch2, num;

    if(*tag == '\0') {
	*pnum = reg_ushm->number;
	return reg_ushm->userid[0];
    }
    for(n = 0; tag[n]; n++)
	tagbuf[n] = chartoupper(tag[n]);
    tagbuf[n] = '\0';
    ch = tagbuf[0];
    ch2 = ch - 'A' + 'a';
    total = reg_ushm->number;
    for(n = num = 0; n < total; n++) {
	ptr = reg_ushm->userid[n];
	tmp = *ptr;
	if(tmp == ch || tmp == ch2) {
	    if(chkstr(tag, tagbuf, ptr))
		strcpy(buf[num++], ptr);
	}
    }
    *pnum = num;
    return buf[0];
}
#endif

/*-------------------------------------------------------*/
/* .UTMP cache                                           */
/*-------------------------------------------------------*/
struct utmpfile_t *utmpshm=NULL;

void resolve_utmp() {
    if(utmpshm == NULL) {
	utmpshm = attach_shm(UTMPSHM_KEY, sizeof(*utmpshm));
    }
}

userinfo_t *currutmp = NULL;

#if !defined(_BBS_UTIL_C_)
extern unsigned int currstat;
extern userec_t cuser;

void setutmpmode(int mode) {
    if(currstat != mode)
	currutmp->mode = currstat = mode;
    
    /* 追蹤使用者 */
    if(HAS_PERM(PERM_LOGUSER)) {
	time_t now = time(NULL);
	char msg[200];
	sprintf(msg, "%s setutmpmode to %s(%d) at %s",
		cuser.userid, modestring(currutmp, 0), mode, Cdate(&now));
	log_user(msg);
    }
}
#endif
static int cmputmpuserid(const void *i, const void *j){
  return strcasecmp((*((userinfo_t**)i))->userid, (*((userinfo_t**)j))->userid);
}

static int cmputmpmode(const void *i, const void *j){
  return (*((userinfo_t**)i))->mode-(*((userinfo_t**)j))->mode;
} 

static int cmputmpidle(const void *i, const void *j){
  return (*((userinfo_t**)i))->lastact-(*((userinfo_t**)j))->lastact;
} 

static int cmputmpfrom(const void *i, const void *j){
  return strcasecmp((*((userinfo_t**)i))->from, (*((userinfo_t**)j))->from);
} 

static int cmputmpfive(const void *i, const void *j){
  int type;
  if((type=(*((userinfo_t**)j))->five_win - (*((userinfo_t**)i))->five_win))
    return type;
  if((type=(*((userinfo_t**)i))->five_lose - (*((userinfo_t**)j))->five_lose))
     return type;
  return (*((userinfo_t**)i))->five_tie-(*((userinfo_t**)j))->five_tie;
} 

static int cmputmpsex(userinfo_t ** i, userinfo_t ** j){
 static int ladyfirst[]={1,0,1,0,1,0,3,3};
 return ladyfirst[(*i)->sex&07]-ladyfirst[(*j)->sex&07];
}
static int cmputmppid(const void *i, const void *j){
 return (*((userinfo_t**)i))->pid-(*((userinfo_t**)j))->pid;
}
static int cmputmpuid(const void *i, const void *j){
 return (*((userinfo_t**)i))->uid-(*((userinfo_t**)j))->uid;
}
void sort_utmp()
{
    time_t now=time(NULL);
    int count, i, ns;
    userinfo_t *uentp;

    if(now-utmpshm->uptime<60 && (now==utmpshm->uptime || utmpshm->busystate))
           return; /* lazy sort */
    utmpshm->busystate=1;
    utmpshm->uptime = now;
    ns=(utmpshm->currsorted?0:1);

    for(uentp = &utmpshm->uinfo[0], count=0, i=0;
            i< USHM_SIZE; i++,uentp = &utmpshm->uinfo[i])
     {
      if(uentp->pid) 
        {
         if(uentp->sex<0 || uentp->sex>7)
           memset(uentp, 0, sizeof(userinfo_t));
         else
           utmpshm->sorted[ns][0][count++]= uentp;
        }
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
    qsort(utmpshm->sorted[ns][5], count, sizeof(userinfo_t *), cmputmpsex );
    qsort(utmpshm->sorted[ns][6], count, sizeof(userinfo_t *), cmputmpuid );
    qsort(utmpshm->sorted[ns][7], count, sizeof(userinfo_t *), cmputmppid );
    utmpshm->currsorted=ns;
    utmpshm->busystate=0;
}
// Ptt:這邊加入hash觀念 找空的utmp
void getnewutmpent(userinfo_t *up) {
    extern int errno;
    register int i, p ;
    register userinfo_t *uentp;
    for(i = 0, p=StringHash(up->userid)%USHM_SIZE; i < USHM_SIZE; i++, p++) {
	if(p==USHM_SIZE) p=0;
	uentp = &(utmpshm->uinfo[p]);
	if(!(uentp->pid)) {
	    memcpy(uentp, up, sizeof(userinfo_t));
	    currutmp = uentp;
	    sort_utmp();
	    return;
	}
    }
    exit(1);
}

int apply_ulist(int (*fptr)(userinfo_t *)) {
    register userinfo_t *uentp;
    register int i, state;

    for(i = 0; i < USHM_SIZE; i++) {
	uentp = &(utmpshm->uinfo[i]);
	if(uentp->pid && (PERM_HIDE(currutmp) || !PERM_HIDE(uentp)))
	    if((state = (*fptr) (uentp)))
		return state;
    }
    return 0;
}

userinfo_t *search_ulist(int uid) {
    return search_ulistn(uid,1);
}

#if !defined(_BBS_UTIL_C_)           
extern int usernum;

userinfo_t *search_ulist_pid(int pid) {
    register int i=0, j, start = 0, end = utmpshm->number - 1;
    register userinfo_t **ulist;
    ulist=utmpshm->sorted[utmpshm->currsorted][7];
    for(i=((start+end)/2);  ;i=(start+end)/2)
     {
         j=pid-ulist[i]->pid;
         if(!j)
           {
              return (userinfo_t *) (ulist[i]);
           }
         if(end==start)
           {
               break;
           }
	else if(i==start)
	   {
	     i=end;
	     start=end;
	   }
	else if(j>0) start = i;
	else    end   = i;
     }
    return 0;
}
userinfo_t *search_ulistn(int uid, int unum) {
    register int i=0, j, start = 0, end = utmpshm->number - 1;
    register userinfo_t **ulist;
    ulist=utmpshm->sorted[utmpshm->currsorted][6];
    for(i=((start+end)/2);  ;i=(start+end)/2)
     {
         j= uid - ulist[i]->uid;
         if(!j)
           {
                 for(;i>0 && uid==ulist[i-1]->uid;i--);/* 指到第一筆 */
                 if(uid==ulist[i+unum-1]->uid) 
                    return (userinfo_t *) (ulist[i+unum-1]);
                 break; /* 超過範圍 */
           }
         if(end==start)
           {
               break;
           }       
	else if(i==start)
	   {
	     i=end;
	     start=end;
	   }
	else if(j>0) start = i;
	else    end   = i;
     }
    return 0;
}

int count_logins(int uid, int show) {
    register int i=0, j, start = 0, end = utmpshm->number - 1, count;
    register userinfo_t **ulist;
    ulist=utmpshm->sorted[utmpshm->currsorted][6];
    for(i=((start+end)/2);  ;i=(start+end)/2)
     {
         j = uid-ulist[i]->uid;
         if(!j)
           {
                 for(;i>0 && uid==ulist[i-1]->uid;i--);/* 指到第一筆 */
                 for(count=0;uid==ulist[i+count]->uid;count++)
                     {
			if(show)
			 prints("(%d) 目前狀態為: %-17.16s(來自 %s)\n",
			         count+1, modestring(ulist[i+count], 0),
                                 ulist[i+count]->from);
		     }  
                 return count;
           }
         if(end==start)
           {
               break;
           }       
	else if(i==start)
	   {
	     i=end;
	     start=end;
	   }
	else if(j>0) start = i;
	else    end   = i;
     }
    return 0;
}


void purge_utmp(userinfo_t *uentp) {
    logout_friend_online();
    memset(uentp, 0, sizeof(userinfo_t));
}

#endif

/*-------------------------------------------------------*/
/* .BOARDS cache                                         */
/*-------------------------------------------------------*/
extern char *fn_board;
extern int currbid;
bcache_t *brdshm;
boardheader_t *bcache;

void touchdircache(int bid)
{
    int *i= (int *)&brdshm->dircache[bid - 1][0].filename[0];
    *i=0;
}       
 
void load_fileheader_cache(int bid, char *direct)
{
    int num=getbtotal(bid);
    int n = num-DIRCACHESIZE+1;
    if (brdshm->busystate!=1 && brdshm->busystate_b[bid-1]!=1)
     {
       brdshm->busystate_b[bid-1] = 1;
       get_records(direct, brdshm->dircache[bid - 1] ,
                sizeof(fileheader_t),n<1?1:n, DIRCACHESIZE);
       brdshm->busystate_b[bid-1] = 0;
     }         
    else 
     {safe_sleep(1);}
}

int get_fileheader_cache(int bid,  char *direct, fileheader_t *headers, 
			 int recbase, int nlines)
{
    int ret, n,num;

    num=getbtotal(bid);

    ret = num-recbase+1,
    n = (num - DIRCACHESIZE+1); 


    if(brdshm->dircache[bid - 1][0].filename[0]=='\0')
	load_fileheader_cache(bid, direct);	
    if (n<1)
	n=recbase-1;
    else
	n=recbase-n;
    if(n<0) n=0;
    if (ret>nlines) ret=nlines; 
    memcpy(headers, &(brdshm->dircache[bid - 1][n]),sizeof(fileheader_t)*ret);
    return ret;
}
static int cmpboardname(boardheader_t **brd, boardheader_t **tmp) {
    return strcasecmp((*brd)->brdname, (*tmp)->brdname);
}              
static int cmpboardclass(boardheader_t **brd, boardheader_t **tmp) {
     return (strncmp((*brd)->title, (*tmp)->title, 4)<<8)+
           strcasecmp((*brd)->brdname, (*tmp)->brdname); 
}
static void sort_bcache(){
       int i;/*critical section 不能單獨呼叫 呼叫reload_bcache or reset_board */
       for(i=0;i<brdshm->number;i++)
         {
           brdshm->sorted[1][i]=brdshm->sorted[0][i]=&bcache[i];
         }
       qsort(brdshm->sorted[0], brdshm->number, sizeof(boardheader_t *),
             (QCAST)cmpboardname);   
       qsort(brdshm->sorted[1], brdshm->number, sizeof(boardheader_t *),
             (QCAST)cmpboardclass);
}
static void reload_bcache() {
    if(brdshm->busystate) {
	safe_sleep(1);
    }
#if !defined(_BBS_UTIL_C_)
    else {
	int fd,i;

	brdshm->busystate = 1;
	if((fd = open(fn_board, O_RDONLY)) > 0) {
	    brdshm->number =
		read(fd, bcache, MAX_BOARD * sizeof(boardheader_t)) /
		sizeof(boardheader_t);
	    close(fd);
	}
	memset(brdshm->lastposttime, 0, MAX_BOARD * sizeof(time_t));
	/* 等所有 boards 資料更新後再設定 uptime */
	brdshm->uptime = brdshm->touchtime;
	log_usies("CACHE", "reload bcache");
        sort_bcache();
        for(i=0;i<brdshm->number;i++)
           {
             bcache[i].firstchild[0]=NULL;
             bcache[i].firstchild[1]=NULL;
           }
	brdshm->busystate = 0;
    }
#endif
}

int numboards = -1;

void resolve_boards() {
    if(brdshm == NULL) {
	brdshm = attach_shm(BRDSHM_KEY, sizeof(*brdshm));
	if(brdshm->touchtime == 0)
	    brdshm->touchtime = 1;
	bcache = brdshm->bcache;
    }

    while(brdshm->uptime < brdshm->touchtime)
	{reload_bcache();}
    numboards = brdshm->number;
}

void touch_boards() {
    time(&(brdshm->touchtime));
    numboards = -1;
    resolve_boards();  
}
void addbrd_touchcache()
{
      brdshm->number++;
      numboards=brdshm->number;     
      reset_board(numboards); 
}
#if !defined(_BBS_UTIL_C_)
void reset_board(int bid) { /* Ptt: 這樣就不用老是touch board了 */
    int fd,i;
    boardheader_t *bhdr;
    
    if(--bid < 0)
	return;
    if(brdshm->busystate || brdshm->busystate_b[bid-1]) {
	safe_sleep(1);
    } else {
	brdshm->busystate_b[bid-1] = 1;
	bhdr = bcache;
	bhdr += bid;       
	if((fd = open(fn_board, O_RDONLY)) > 0) {
	    lseek(fd, (off_t)(bid *  sizeof(boardheader_t)), SEEK_SET);
	    read(fd, bhdr, sizeof(boardheader_t));
	    close(fd);
	}
        sort_bcache();
        for(i=0;i<brdshm->number;i++)
           {
             bcache[i].firstchild[0]=NULL;
             bcache[i].firstchild[1]=NULL;
           }
	brdshm->busystate_b[bid-1] = 0;
    }
}   

int apply_boards(int (*func)(boardheader_t *)) {
    register int i;
    register boardheader_t *bhdr;
    
    for(i = 0, bhdr = bcache; i < numboards; i++, bhdr++) {
	if(!(bhdr->brdattr & BRD_GROUPBOARD) && Ben_Perm(bhdr) && 
	   (*func)(bhdr) == QUIT)
	    return QUIT;
    }
    return 0;
}
#endif

boardheader_t *getbcache(int bid) { /* Ptt改寫 */
    return bcache + bid - 1;
}
int getbtotal(int bid)
{
  return brdshm->total[bid - 1];
}
void setbtotal(int bid) {
    boardheader_t *bh = getbcache(bid);
    struct stat st;
    char genbuf[256];
    int num,fd;

    sprintf(genbuf, "boards/%c/%s/.DIR", bh->brdname[0], bh->brdname);

    if((fd = open(genbuf, O_RDWR)) < 0)
            return; /* .DIR掛了 */
    fstat(fd, &st); 
    num =  st.st_size / sizeof(fileheader_t);
    brdshm->total[bid - 1] = num;

    if(num>0)
     {
       lseek(fd, (off_t) (num - 1) * sizeof(fileheader_t), SEEK_SET);
       if(read(fd, genbuf, FNLEN)>=0)
	{
	  brdshm->lastposttime[bid - 1]=(time_t) atoi(&genbuf[2]);
	}
     }
    else
        brdshm->lastposttime[bid - 1] = 0; 
    close(fd);
    if(num)
       touchdircache(bid);
}

void
touchbpostnum(int bid, int delta) {
  int *total = &brdshm->total[bid - 1];
  if (*total) *total += delta;
}


int getbnum(char *bname) {
    register int i=0, j, start = 0, end = brdshm->number - 1;
    register boardheader_t **bhdr;
    bhdr=brdshm->sorted[0]; 
    for(i=((start+end)/2);  ;i=(start+end)/2)
     {
         if(! (j=strcasecmp(bname,bhdr[i]->brdname)))
		 return (int) (bhdr[i] - bcache +1);
         if(end==start)
           {
               break;
           }
         else if(i==start)
           {
             i=end;
             start=end;
           }
         else if(j>0) start = i;
         else    end   = i;
     } 
    return 0;
}

#if !defined(_BBS_UTIL_C_)
extern char *fn_water;
extern char *str_sysop;

int haspostperm(char *bname) {
    register int i;
    char buf[200];

    setbfile(buf, bname, fn_water);
    if(belong(buf, cuser.userid))
	return 0;

    if(!strcasecmp(bname, DEFAULT_BOARD))
	return 1;

    if (!strcasecmp(bname, "PttLaw"))
        return 1;

    if(!HAS_PERM(PERM_POST))
	return 0;
    
    if(!(i = getbnum(bname)))
	return 0;

    /* 秘密看板特別處理 */
    if(bcache[i - 1].brdattr & BRD_HIDE)
	return 1;

    i = bcache[i - 1].level;

    if (HAS_PERM(PERM_VIOLATELAW) && (i & PERM_VIOLATELAW))
        return 1;
    else if (HAS_PERM(PERM_VIOLATELAW))
        return 0;

    return HAS_PERM(i & ~PERM_POST);
}
#endif

/*-------------------------------------------------------*/
/* PTT  cache                                            */
/*-------------------------------------------------------*/
/* cachefor 動態看版 */
struct pttcache_t *ptt;

static void reload_pttcache() {
    if(ptt->busystate)
	safe_sleep(1);
    else {				/* jochang: temporary workaround */
	fileheader_t item, subitem;
	char pbuf[256], buf[256], *chr;
	FILE *fp, *fp1, *fp2;
	int id, section = 0;

	ptt->busystate = 1;
	ptt->max_film = 0;
	bzero(ptt->notes, sizeof ptt->notes);
	setapath(pbuf, "Note");
	setadir(buf, pbuf);
	id = 0;
	if((fp = fopen(buf, "r"))) {
	    while(fread(&item, sizeof(item), 1, fp)) {
		if(item.title[3]=='<' && item.title[8]=='>') {
		    sprintf(buf,"%s/%s", pbuf, item.filename);
		    setadir(buf, buf);
		    if(!(fp1 = fopen(buf, "r")))
			continue;
		    ptt->next_refresh[section] = ptt->n_notes[section] = id;
		    section ++;
		    while(fread(&subitem, sizeof(subitem), 1, fp1)) {
			sprintf(buf,"%s/%s/%s", pbuf, item.filename ,
				subitem.filename);
			if(!(fp2=fopen(buf,"r")))
			    continue;
			fread(ptt->notes[id],sizeof(char), 200*11, fp2);
			ptt->notes[id][200*11 - 1]=0;
			id++;
			fclose(fp2);
			if(id >= MAX_MOVIE)
			    break;  
		    }
		    fclose(fp1);	   
		    if(id >= MAX_MOVIE || section >= MAX_MOVIE_SECTION)
			break;	  
		}
	    }
	    fclose(fp);
	}
	ptt->next_refresh[section] = -1;
	ptt->n_notes[section] = ptt->max_film = id-1;
	ptt->max_history = ptt->max_film - 2;
	if(ptt->max_history > MAX_HISTORY - 1)
	    ptt->max_history = MAX_HISTORY - 1;
	if(ptt->max_history <0) ptt->max_history=0;

	fp = fopen("etc/today_is","r");
	if(fp) {
	    fgets(ptt->today_is,15,fp);
	    if((chr = strchr(ptt->today_is,'\n')))
		*chr = 0;
	    ptt->today_is[15] = 0;
	    fclose(fp);
	}
     
	/* 等所有資料更新後再設定 uptime */

	ptt->uptime = ptt->touchtime ;
#if !defined(_BBS_UTIL_C_)
	log_usies("CACHE", "reload pttcache");
#endif
	ptt->busystate = 0;
    }
}

void resolve_garbage() {
    int count=0;
    
    if(ptt == NULL) {
	ptt = attach_shm(PTTSHM_KEY, sizeof(*ptt));
	if(ptt->touchtime == 0)
	    ptt->touchtime = 1;
    }
    while(ptt->uptime < ptt->touchtime) { /* 不用while等 */
	reload_pttcache();
	if(count ++ > 10 && ptt->busystate) {
/* Ptt: 這邊會有問題  load超過10 秒會所有進loop的process都讓 busystate = 0
   這樣會所有prcosee都會在load 動態看板 會造成load大增
   但沒有用這個function的話 萬一load passwd檔的process死了 又沒有人把他
   解開  同樣的問題發生在reload passwd
*/    
	    ptt->busystate = 0;
#ifndef _BBS_UTIL_C_
	    log_usies("CACHE", "refork Ptt dead lock");
#endif
	}
    }
}

/*-------------------------------------------------------*/
/* PTT's cache                                           */
/*-------------------------------------------------------*/
/* cachefor from host 與最多上線人數 */
struct fromcache_t *fcache;

static void reload_fcache() {
    if(fcache->busystate)
	safe_sleep(1);
    else {
	FILE *fp;

	fcache->busystate = 1;
	bzero(fcache->domain, sizeof fcache->domain);
	if((fp = fopen("etc/domain_name_query","r"))) {
	    char buf[256],*po;

	    fcache->top=0;
	    while(fgets(buf, sizeof(buf),fp)) {
		if(buf[0] && buf[0] != '#' && buf[0] != ' ' &&
		   buf[0] != '\n') {
		    sscanf(buf,"%s",fcache->domain[fcache->top]);
		    po = buf + strlen(fcache->domain[fcache->top]);
		    while(*po == ' ')
			po++;
		    strncpy(fcache->replace[fcache->top],po,49);
		    fcache->replace[fcache->top]
			[strlen(fcache->replace[fcache->top])-1] = 0;
		    (fcache->top)++;
		    if(fcache->top == MAX_FROM)
			break;
		}	
	    }
	}

	fcache->max_user=0;

	/* 等所有資料更新後再設定 uptime */
	fcache->uptime = fcache->touchtime;
#if !defined(_BBS_UTIL_C_)
	log_usies("CACHE", "reload fcache");
#endif
	fcache->busystate = 0;
    }
}

void resolve_fcache() {
    if(fcache == NULL) {
	fcache = attach_shm(FROMSHM_KEY, sizeof(*fcache));
	if(fcache->touchtime == 0)
	    fcache->touchtime = 1;
    }
    while(fcache->uptime < fcache->touchtime)
	reload_fcache();
}

extern time_t login_start_time;
extern char *fn_visable;
FILE   *DEBUG = NULL;

void hbflreload(int bid)
{
    int     hbfl[MAX_FRIEND + 1], i, num, uid;
    char    buf[128];
    FILE    *fp;

    memset(hbfl, 0, sizeof(hbfl));
    setbfile(buf, bcache[bid-1].brdname, fn_visable);
    if( (fp = fopen(buf, "r")) != NULL ){
	for( num = 1 ; num <= MAX_FRIEND ; ++num ){
	    if( fgets(buf, sizeof(buf), fp) == NULL )
		break;
	    for( i = 0 ; buf[i] != 0 ; ++i )
		if( buf[i] == ' ' ){
		    buf[i] = 0;
		    break;
		}
	    if( strcasecmp("guest", buf) == 0 ||
		(uid = searchuser(buf))  == 0     ) {
		--num;
		continue;
	    }
	    hbfl[num] = uid;
	}
	fclose(fp);
    }
    hbfl[0] = time(NULL);
    memcpy(brdshm->hbfl[bid], hbfl, sizeof(hbfl));
}

int hbflcheck(int bid, int uid)
{
    int     i;

    if( brdshm->hbfl[bid][0] < login_start_time - HBFLexpire )
	hbflreload(bid);
    for( i = 1 ; brdshm->hbfl[bid][i] != 0 && i <= MAX_FRIEND ; ++i ){
	if( brdshm->hbfl[bid][i] == uid )
	    return 0;
    }
    return 1;
}
