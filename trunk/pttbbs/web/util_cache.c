/* $Id: util_cache.c,v 1.2 2003/03/24 20:44:09 ptt Exp $ */
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
#include <sys/types.h>
#include <sys/mman.h>
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
	sigemptyset(&set);
	sigaddset(&set,SIGALRM);
	sigprocmask(SIG_UNBLOCK,&set,NULL);
	retv=sleep(seconds);
	sigprocmask(SIG_BLOCK,&set,NULL);
	return retv;
    }
    return sleep(seconds);
}

void setapath(char *buf, char *boardname) {
    sprintf(buf, "man/boards/%c/%s", boardname[0], boardname);
}

static char *str_dotdir = ".DIR";

void setadir(char *buf, char *path) {
    sprintf(buf, "%s/%s", path, str_dotdir);
}

static void attach_err(int shmkey, char *name) {
    fprintf(stderr, "[%s error] key = %x\n", name, shmkey);
    fprintf(stderr, "errno = %d: %s\n", errno, strerror(errno));
    exit(1);
}

void *attach_shm(int shmkey, int shmsize) {
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

#ifndef __FreeBSD__
/* according to X/OPEN we have to define it ourselves */
union semun {
    int val;                    /* value for SETVAL */
    struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
    unsigned short int *array;  /* array for GETALL, SETALL */
    struct seminfo *__buf;      /* buffer for IPC_INFO */
};
#endif

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

SHM_t   *SHM;
int     *GLOBALVAR;
boardheader_t   *bcache;
int     numboards = -1;

void attach_SHM(void)
{
    SHM = attach_shm(SHM_KEY, sizeof(SHM_t));
    if( !SHM->loaded ) /* (uhash) assume fresh shared memory is zeroed */
	exit(1);
    if( SHM->Btouchtime == 0 )
	SHM->Btouchtime = 1;
    bcache = SHM->bcache;
    
    GLOBALVAR = SHM->GLOBALVAR;
    if( SHM->Ptouchtime == 0 )
	SHM->Ptouchtime = 1;

    if( SHM->Ftouchtime == 0 )
	SHM->Ftouchtime = 1;

    bcache = SHM->bcache;
    numboards = SHM->Bnumber;
}

int setumoney(int uid, int money) {
   SHM->money[uid-1]=money;
   passwd_update_money(uid);
   return SHM->money[uid-1];
}

int deumoney(int uid, int money) {
   if(money<0 && SHM->money[uid-1]<-money)
       return setumoney(uid,0);
   else
       return setumoney(uid,SHM->money[uid-1]+money);
}
int moneyof(int uid){   /* ptt 改進金錢處理效率 */
   return SHM->money[uid-1];
}

static unsigned string_hash(unsigned char *s) {
    unsigned int v=0;
    while(*s) {
	v = (v << 8) | (v >> 24);
	v ^= toupper(*s++);	/* note this is case insensitive */
    }
    return (v * 2654435769UL) >> (32 - HASH_BITS);
}

void add_to_uhash(int n, char *id) {
    int *p, h = string_hash(id);
    strcpy(SHM->userid[n], id);
    
    p = &(SHM->hash_head[h]);
			
    while(*p != -1)
	p = &(SHM->next_in_hash[*p]);
				
    SHM->next_in_hash[*p = n] = -1;
}

/* note: after remove_from_uhash(), you should add_to_uhash()
   (likely with a different name) */
void remove_from_uhash(int n) {
    int h = string_hash(SHM->userid[n]);
    int *p = &(SHM->hash_head[h]);
    
    while(*p != -1 && *p != n)
	p = &(SHM->next_in_hash[*p]);
    if(*p == n)
	*p = SHM->next_in_hash[n];
}

int searchuser(char *userid) {
    int h,p;
    
    if(SHM == NULL)
	attach_SHM();	/* for sloopy util programs */
    
    h = string_hash(userid);
    p = SHM->hash_head[h];
	
    while(p != -1) {
	if(strcasecmp(SHM->userid[p],userid) == 0) {
	    strcpy(userid,SHM->userid[p]);
	    return p + 1;
	}
	p = SHM->next_in_hash[p];
    }
    return 0;
}
userec_t xuser;

int getuser(char *userid) {
    int uid;
    if((uid = searchuser(userid)))
        passwd_query(uid, &xuser);
    return uid;
}   

void setuserid(int num, char *userid) {
    if(num > 0 && num <= MAX_USERS) {
	if(num > SHM->number)
	    SHM->number = num;
	else
	    remove_from_uhash(num-1);
	add_to_uhash(num-1,userid);
    }
}

/*-------------------------------------------------------*/
/* .UTMP cache                                           */
/*-------------------------------------------------------*/
void resolve_utmp() {
    if(SHM == NULL) {
	attach_SHM();
	if(SHM->UTMPuptime == 0)
	    SHM->UTMPuptime = SHM->UTMPnumber = 1;
    }
}

userinfo_t *currutmp = NULL;

void getnewutmpent(userinfo_t *up) {
    extern int errno;
    register int i;
    register userinfo_t *uentp;

    resolve_utmp();
    
    for(i = 0; i < USHM_SIZE; i++) {
	uentp = &(SHM->uinfo[i]);
	if(!(uentp->pid)) {
	    memcpy(uentp, up, sizeof(userinfo_t));
	    currutmp = uentp;
	    SHM->number++;
	    return;
	}
    }
    exit(1);
}

int apply_ulist(int (*fptr)(userinfo_t *)) {
    register userinfo_t *uentp;
    register int i, state;

    resolve_utmp();
    for(i = 0; i < USHM_SIZE; i++) {
	uentp = &(SHM->uinfo[i]);
	if(uentp->pid && (PERM_HIDE(currutmp) || !PERM_HIDE(uentp)))
	    if((state = (*fptr) (uentp)))
		return state;
    }
    return 0;
}

userinfo_t *search_ulist(int uid) {
    register int i;
    register userinfo_t *uentp;
    
    resolve_utmp();
    for(i = 0; i < USHM_SIZE; i++) {
	uentp = &(SHM->uinfo[i]);
	if(uid==uentp->uid)
	    return uentp;
    }
    return 0;
}

/*-------------------------------------------------------*/
/* .BOARDS cache                                         */
/*-------------------------------------------------------*/
char *fn_board=BBSHOME"/"FN_BOARD;
static void reload_bcache() {
    if(SHM->Bbusystate) {
	safe_sleep(1);
    }
    else{
	puts("bcache is not loaded? resolve_boards() fail");
	exit(1);
    }
}

void resolve_boards() {
    if(SHM == NULL) {
	attach_SHM();
	if(SHM->Btouchtime == 0)
	    SHM->Btouchtime = 1;
    }

    while(SHM->Buptime < SHM->Btouchtime)
	reload_bcache();
    numboards = SHM->Bnumber;
}

void touch_boards() {
    time(&(SHM->Btouchtime));
    numboards = -1;
    resolve_boards();  
}
void reset_board(int bid)
{
  int fd;
  boardheader_t bh;
  if(--bid<0)return;
  if(SHM->Bbusystate==0)
   {
    SHM->Bbusystate = 1;
    if((fd = open(fn_board, O_RDONLY)) > 0) {
      lseek(fd, (off_t)(bid *  sizeof(boardheader_t)), SEEK_SET);
      read(fd, &bh , sizeof(boardheader_t));
      close(fd);
      if(bh.brdname[0] && !strcmp(bh.brdname,bcache[bid].brdname))
         memcpy(&bcache[bid],&bh, sizeof(boardheader_t));
    }
    SHM->Bbusystate = 0;
   }
}
boardheader_t *getbcache(int bid) { /* Ptt改寫 */
    return bcache + bid - 1;
}

void touchbtotal(int bid) {
    SHM->total[bid - 1] = 0;
    SHM->lastposttime[bid - 1] = 0;
}


int getbnum(char *bname) {
    register int i;
    register boardheader_t *bhdr;
    
    for(i = 0, bhdr = bcache; i++ < numboards; bhdr++)
	if(
	    !strcasecmp(bname, bhdr->brdname))
	    return i;
    return 0;
}

/*-------------------------------------------------------*/
/* PTT  cache                                            */
/*-------------------------------------------------------*/
/* cachefor 動態看版 */
void reload_pttcache(void)
{
    if(SHM->Pbusystate)
	safe_sleep(1);
    else {				/* jochang: temporary workaround */
	fileheader_t item, subitem;
	char pbuf[256], buf[256], *chr;
	FILE *fp, *fp1, *fp2;
	int id, section = 0;

	SHM->Pbusystate = 1;
	SHM->max_film = 0;
	bzero(SHM->notes, sizeof SHM->notes);
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
		    SHM->next_refresh[section] = SHM->n_notes[section] = id;
		    section ++;
		    while(fread(&subitem, sizeof(subitem), 1, fp1)) {
			sprintf(buf,"%s/%s/%s", pbuf, item.filename ,
				subitem.filename);
			if(!(fp2=fopen(buf,"r")))
			    continue;
			fread(SHM->notes[id],sizeof(char), 200*11, fp2);
			SHM->notes[id][200*11 - 1]=0;
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
	SHM->next_refresh[section] = -1;
	SHM->n_notes[section] = SHM->max_film = id-1;
	SHM->max_history = SHM->max_film - 2;
	if(SHM->max_history > MAX_HISTORY - 1)
	    SHM->max_history = MAX_HISTORY - 1;
	if(SHM->max_history <0) SHM->max_history=0;

	fp = fopen("etc/today_is","r");
	if(fp) {
	    fgets(SHM->today_is,15,fp);
	    if((chr = strchr(SHM->today_is,'\n')))
		*chr = 0;
	    SHM->today_is[15] = 0;
	    fclose(fp);
	}
     
	/* 等所有資料更新後再設定 uptime */

	SHM->Puptime = SHM->Ptouchtime ;
	SHM->Pbusystate = 0;
    }
}

void resolve_garbage() {
    int count=0;
    
    if(SHM == NULL) {
	attach_SHM();
	if(SHM->Ptouchtime == 0)
	    SHM->Ptouchtime = 1;
    }
    while(SHM->Puptime < SHM->Ptouchtime) { /* 不用while等 */
	reload_pttcache();
	if(count ++ > 10 && SHM->Pbusystate) {
/* Ptt: 這邊會有問題  load超過10 秒會所有進loop的process都讓 busystate = 0
   這樣會所有prcosee都會在load 動態看板 會造成load大增
   但沒有用這個function的話 萬一load passwd檔的process死了 又沒有人把他
   解開  同樣的問題發生在reload passwd
*/    
	    SHM->Pbusystate = 0;
	}
    }
}

/*-------------------------------------------------------*/
/* PTT's cache                                           */
/*-------------------------------------------------------*/
/* cachefor from host 與最多上線人數 */
static void reload_fcache() {
    if(SHM->Fbusystate)
	safe_sleep(1);
    else {
	FILE *fp;

	SHM->Fbusystate = 1;
	bzero(SHM->domain, sizeof SHM->domain);
	if((fp = fopen("etc/domain_name_query","r"))) {
	    char buf[101],*po;

	    SHM->top=0;
	    while(fgets(buf,100,fp)) {
		if(buf[0] && buf[0] != '#' && buf[0] != ' ' &&
		   buf[0] != '\n') {
		    sscanf(buf,"%s",SHM->domain[SHM->top]);
		    po = buf + strlen(SHM->domain[SHM->top]);
		    while(*po == ' ')
			po++;
		    strncpy(SHM->replace[SHM->top],po,49);
		    SHM->replace[SHM->top]
			[strlen(SHM->replace[SHM->top])-1] = 0;
		    (SHM->top)++;
		}	
	    }
	}

	SHM->max_user=0;

	/* 等所有資料更新後再設定 uptime */
	SHM->Fuptime = SHM->Ftouchtime;
	SHM->Fbusystate = 0;
    }
}

void resolve_fcache() {
    if(SHM == NULL) {
	attach_SHM();
	if(SHM->Ftouchtime == 0)
	    SHM->Ftouchtime = 1;
    }
    while(SHM->Fuptime < SHM->Ftouchtime)
	reload_fcache();
}
