/* $Id$ */
#include "bbs.h"

#ifdef _BBS_UTIL_C_
#    define log_usies(a, b) ;
#    define abort_bbs(a)    exit(1)
#endif
/*
 * the reason for "safe_sleep" is that we may call sleep during SIGALRM
 * handler routine, while SIGALRM is blocked. if we use the original sleep,
 * we'll never wake up.
 */
unsigned int
safe_sleep(unsigned int seconds)
{
    /* jochang  sleep有問題時用 */
    sigset_t        set, oldset;

    sigemptyset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);
    if (sigismember(&oldset, SIGALRM)) {
	unsigned int    retv;
	log_usies("SAFE_SLEEP ", "avoid hang");
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	retv = sleep(seconds);
	sigprocmask(SIG_BLOCK, &set, NULL);
	return retv;
    }
    return sleep(seconds);
}

/*
 * section - SHM
 */
static void
attach_err(int shmkey, char *name)
{
    fprintf(stderr, "[%s error] key = %x\n", name, shmkey);
    fprintf(stderr, "errno = %d: %s\n", errno, strerror(errno));
    exit(1);
}

void           *
attach_shm(int shmkey, int shmsize)
{
    void           *shmptr = (void *)NULL;
    int             shmid;

    shmid = shmget(shmkey, shmsize, 0);
    if (shmid < 0) {
	// SHM should be created by uhash_loader, NOT mbbsd or other utils
	attach_err(shmkey, "shmget");
    } else {
	shmptr = (void *)shmat(shmid, NULL, 0);
	if (shmptr == (void *)-1)
	    attach_err(shmkey, "shmat");
    }

    return shmptr;
}

void
attach_SHM(void)
{
    SHM = attach_shm(SHM_KEY, sizeof(SHM_t));
    if (!SHM->loaded)		/* (uhash) assume fresh shared memory is
				 * zeroed */
	exit(1);
    if (SHM->Btouchtime == 0)
	SHM->Btouchtime = 1;
    bcache = SHM->bcache;
    numboards = SHM->Bnumber;

    GLOBALVAR = SHM->GLOBALVAR;
    if (SHM->Ptouchtime == 0)
	SHM->Ptouchtime = 1;

    if (SHM->Ftouchtime == 0)
	SHM->Ftouchtime = 1;
}

/* ----------------------------------------------------- */
/* semaphore : for critical section                      */
/* ----------------------------------------------------- */
#define SEM_FLG        0600	/* semaphore mode */

#ifndef __FreeBSD__
/* according to X/OPEN, we have to define it ourselves */
union semun {
    int             val;	/* value for SETVAL */
    struct semid_ds *buf;	/* buffer for IPC_STAT, IPC_SET */
    unsigned short int *array;	/* array for GETALL, SETALL */
    struct seminfo *__buf;	/* buffer for IPC_INFO */
};
#endif

void
sem_init(int semkey, int *semid)
{
    union semun     s;

    s.val = 1;
    *semid = semget(semkey, 1, 0);
    if (*semid == -1) {
	*semid = semget(semkey, 1, IPC_CREAT | SEM_FLG);
	if (*semid == -1)
	    attach_err(semkey, "semget");
	semctl(*semid, 0, SETVAL, s);
    }
}

void
sem_lock(int op, int semid)
{
    struct sembuf   sops;

    sops.sem_num = 0;
    sops.sem_flg = SEM_UNDO;
    sops.sem_op = op;
    if (semop(semid, &sops, 1)) {
	perror("semop");
	exit(1);
    }
}

/*
 * section - user cache(including uhash)
 */
/* uhash ****************************************** */
/*
 * the design is this: we use another stand-alone program to create and load
 * data into the hash. (that program could be run in rc-scripts or something
 * like that) after loading completes, the stand-alone program sets loaded to
 * 1 and exits.
 * 
 * the bbs exits if it can't attach to the shared memory or the hash is not
 * loaded yet.
 */

void
add_to_uhash(int n, char *id)
{
    int            *p, h = StringHash(id);
    int             times;
    strlcpy(SHM->userid[n], id, sizeof(SHM->userid[n]));

    p = &(SHM->hash_head[h]);

    for (times = 0; times < MAX_USERS && *p != -1; ++times)
	p = &(SHM->next_in_hash[*p]);

    if (times == MAX_USERS)
	abort_bbs(0);

    SHM->next_in_hash[*p = n] = -1;
}

void
remove_from_uhash(int n)
{
/*
 * note: after remove_from_uhash(), you should add_to_uhash() (likely with a
 * different name)
 */
    int             h = StringHash(SHM->userid[n]);
    int            *p = &(SHM->hash_head[h]);
    int             times;

    for (times = 0; times < MAX_USERS && (*p != -1 && *p != n); ++times)
	p = &(SHM->next_in_hash[*p]);

    if (times == MAX_USERS)
	abort_bbs(0);

    if (*p == n)
	*p = SHM->next_in_hash[n];
}

int
searchuser(char *userid)
{
    int             h, p, times;
    h = StringHash(userid);
    p = SHM->hash_head[h];

    for (times = 0; times < MAX_USERS && p != -1 && p < MAX_USERS ; ++times) {
	if (strcasecmp(SHM->userid[p], userid) == 0) {
	    strcpy(userid, SHM->userid[p]);
	    return p + 1;
	}
	p = SHM->next_in_hash[p];
    }

    return 0;
}

int
getuser(char *userid)
{
    int             uid;

    if ((uid = searchuser(userid)))
	passwd_query(uid, &xuser);
    xuser.money = moneyof(uid);
    return uid;
}

char           *
getuserid(int num)
{
    if (--num >= 0 && num < MAX_USERS)
	return ((char *)SHM->userid[num]);
    return NULL;
}

void
setuserid(int num, char *userid)
{
    if (num > 0 && num <= MAX_USERS) {
	if (num > SHM->number)
	    SHM->number = num;
	else
	    remove_from_uhash(num - 1);
	add_to_uhash(num - 1, userid);
    }
}

/* 0 ==> 找過期帳號 */
/* 1 ==> 建立新帳號 */
/* should do it by searching "" in the hash */
int
searchnewuser(int mode)
{
    register int    i, num;

    num = SHM->number;
    i = 0;

    /* 為什麼這邊不用 hash table 去找而要用 linear search? */
    while (i < num) {
	if (!SHM->userid[i++][0])
	    return i;
    }
    if (mode && (num < MAX_USERS))
	return num + 1;
    return 0;
}

#ifndef _BBS_UTIL_C_
char           *
u_namearray(char buf[][IDLEN + 1], int *pnum, char *tag)
{
    register char  *ptr, tmp;
    register int    n, total;
    char            tagbuf[STRLEN];
    int             ch, ch2, num;

    if (*tag == '\0') {
	*pnum = SHM->number;
	return SHM->userid[0];
    }
    for (n = 0; tag[n]; n++)
	tagbuf[n] = chartoupper(tag[n]);
    tagbuf[n] = '\0';
    ch = tagbuf[0];
    ch2 = ch - 'A' + 'a';
    total = SHM->number;
    for (n = num = 0; n < total; n++) {
	ptr = SHM->userid[n];
	tmp = *ptr;
	if (tmp == ch || tmp == ch2) {
	    if (chkstr(tag, tagbuf, ptr))
		strcpy(buf[num++], ptr);
	}
    }
    *pnum = num;
    return buf[0];
}
#endif

void
getnewutmpent(userinfo_t * up)
{
/* Ptt:這裡加上 hash 觀念找空的 utmp */
    register int    i, p;
    register userinfo_t *uentp;
    for (i = 0, p = StringHash(up->userid) % USHM_SIZE; i < USHM_SIZE; i++, p++) {
	if (p == USHM_SIZE)
	    p = 0;
	uentp = &(SHM->uinfo[p]);
	if (!(uentp->pid)) {
	    memcpy(uentp, up, sizeof(userinfo_t));
	    currutmp = uentp;
	    return;
	}
    }
    exit(1);
}

int
apply_ulist(int (*fptr) (userinfo_t *))
{
    register userinfo_t *uentp;
    register int    i, state;

    for (i = 0; i < USHM_SIZE; i++) {
	uentp = &(SHM->uinfo[i]);
	if (uentp->pid && (PERM_HIDE(currutmp) || !PERM_HIDE(uentp)))
	    if ((state = (*fptr) (uentp)))
		return state;
    }
    return 0;
}

userinfo_t     *
search_ulist_pid(int pid)
{
    register int    i = 0, j, start = 0, end = SHM->UTMPnumber - 1;
    int *ulist;
    register userinfo_t *u;
    if (end == -1)
	return NULL;
    ulist = SHM->sorted[SHM->currsorted][7];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	u = &SHM->uinfo[ulist[i]];
	j = pid - u->pid;
	if (!j) {
	    return u;
	}
	if (end == start) {
	    break;
	} else if (i == start) {
	    i = end;
	    start = end;
	} else if (j > 0)
	    start = i;
	else
	    end = i;
    }
    return 0;
}

userinfo_t     *
search_ulistn(int uid, int unum)
{
    register int    i = 0, j, start = 0, end = SHM->UTMPnumber - 1;
    int *ulist;
    register userinfo_t *u;
    if (end == -1)
	return NULL;
    ulist = SHM->sorted[SHM->currsorted][6];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	u = &SHM->uinfo[ulist[i]];
	j = uid - u->uid;
	if (j == 0) {
	    for (; i > 0 && uid == SHM->uinfo[ulist[i - 1]].uid; --i)
		;/* 指到第一筆 */
	    if ( i + unum - 1 >= 0 &&
		 (ulist[i + unum - 1] >= 0 &&
		  uid == SHM->uinfo[ulist[i + unum - 1]].uid ) )
		return &SHM->uinfo[ulist[i + unum - 1]];
	    break;		/* 超過範圍 */
	}
	if (end == start) {
	    break;
	} else if (i == start) {
	    i = end;
	    start = end;
	} else if (j > 0)
	    start = i;
	else
	    end = i;
    }
    return 0;
}

userinfo_t     *
search_ulist_userid(char *userid)
{
    register int    i = 0, j, start = 0, end = SHM->UTMPnumber - 1;
    int *ulist;
    register userinfo_t * u;
    if (end == -1)
	return NULL;
    ulist = SHM->sorted[SHM->currsorted][0];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	u = &SHM->uinfo[ulist[i]];
	j = strcasecmp(userid, u->userid);
	if (!j) {
	    return u;
	}
	if (end == start) {
	    break;
	} else if (i == start) {
	    i = end;
	    start = end;
	} else if (j > 0)
	    start = i;
	else
	    end = i;
    }
    return 0;
}

#ifndef _BBS_UTIL_C_
int
count_logins(int uid, int show)
{
    register int    i = 0, j, start = 0, end = SHM->UTMPnumber - 1, count;
    int *ulist;
    userinfo_t *u; 
    if (end == -1)
	return 0;
    ulist = SHM->sorted[SHM->currsorted][6];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	u = &SHM->uinfo[ulist[i]];
	j = uid - u->uid;
	if (!j) {
	    for (; i > 0 && uid == SHM->uinfo[ulist[i - 1]].uid; i--);
							/* 指到第一筆 */
	    for (count = 0; (ulist[i + count] &&
		    (u = &SHM->uinfo[ulist[i + count]]) &&
		    uid == u->uid); count++) {
		if (show)
		    prints("(%d) 目前狀態為: %-17.16s(來自 %s)\n",
			   count + 1, modestring(u, 0),
			   u->from);
	    }
	    return count;
	}
	if (end == start) {
	    break;
	} else if (i == start) {
	    i = end;
	    start = end;
	} else if (j > 0)
	    start = i;
	else
	    end = i;
    }
    return 0;
}

void
purge_utmp(userinfo_t * uentp)
{
    logout_friend_online(uentp);
    memset(uentp, 0, sizeof(userinfo_t));
    SHM->UTMPneedsort = 1;
}
#endif

/*
 * section - money cache
 */
int
setumoney(int uid, int money)
{
    SHM->money[uid - 1] = money;
    passwd_update_money(uid);
    return SHM->money[uid - 1];
}

int
deumoney(int uid, int money)
{
    if (money < 0 && moneyof(uid) < -money)
	return setumoney(uid, 0);
    else
	return setumoney(uid, SHM->money[uid - 1] + money);
}

/*
 * section - utmp
 */
#if !defined(_BBS_UTIL_C_) /* _BBS_UTIL_C_ 不會有 utmp */
void
setutmpmode(unsigned int mode)
{
    if (currstat != mode)
	currutmp->mode = currstat = mode;
    /* 追蹤使用者 */
    if (HAS_PERM(PERM_LOGUSER)) {
	log_user("setutmpmode to %s(%d)\n", modestring(currutmp, 0), mode);
    }
}
#endif

/*
 * section - board cache
 */
void touchbtotal(int bid) {
    SHM->total[bid - 1] = 0;
    SHM->lastposttime[bid - 1] = 0;
}


/**
 * qsort comparison function - 照板名排序
 */
static int
cmpboardname(const void * i, const void * j)
{
    return strcasecmp(bcache[*(int*)i].brdname, bcache[*(int*)j].brdname);
}

/**
 * qsort comparison function - 先照群組排序、同一個群組內依板名排
 */
static int
cmpboardclass(const void * i, const void * j)
{
    boardheader_t *brd1 = &bcache[*(int*)i], *brd2 = &bcache[*(int*)j];
    return (strncmp(brd1->title, brd2->title, 4) << 8) + 
	    strcasecmp(brd1->brdname, brd2->brdname);
}


void
sort_bcache(void)
{
    int             i;
    /* critical section 盡量不要呼叫  */
    /* 只有新增 或移除看板 需要呼叫到 */
    if(SHM->Bbusystate) {
	sleep(1);
	return;
    }
    SHM->Bbusystate = 1;
    for (i = 0; i < SHM->Bnumber; i++) {
	SHM->bsorted[0][i] = SHM->bsorted[1][i] = i;
    }
    qsort(SHM->bsorted[0], SHM->Bnumber, sizeof(int), cmpboardname);
    qsort(SHM->bsorted[1], SHM->Bnumber, sizeof(int), cmpboardclass);

    for (i = 0; i < SHM->Bnumber; i++) {
	bcache[i].firstchild[0] = 0;
	bcache[i].firstchild[1] = 0;
    }
    SHM->Bbusystate = 0;
}

#ifdef _BBS_UTIL_C_
void
reload_bcache(void)
{
    if (SHM->Bbusystate) {
	safe_sleep(1);
    }
    else {
	int             fd;

	SHM->Bbusystate = 1;
	if ((fd = open(fn_board, O_RDONLY)) > 0) {
	    SHM->Bnumber =
		read(fd, bcache, MAX_BOARD * sizeof(boardheader_t)) /
		sizeof(boardheader_t);
	    close(fd);
	}
	memset(SHM->lastposttime, 0, MAX_BOARD * sizeof(time_t));
	memset(SHM->total, 0, MAX_BOARD * sizeof(int));
	/* 等所有 boards 資料更新後再設定 uptime */
	SHM->Buptime = SHM->Btouchtime;
	log_usies("CACHE", "reload bcache");
	SHM->Bbusystate = 0;
	sort_bcache();
    }
}

void resolve_boards(void)
{
    while (SHM->Buptime < SHM->Btouchtime) {
	reload_bcache();
    }
    numboards = SHM->Bnumber;
}
#endif /* defined(_BBS_UTIL_C_)*/

#if 0
/* Unused */
void touch_boards(void)
{
    SHM->Btouchtime = COMMON_TIME;
    numboards = -1;
    resolve_boards();
}
#endif

void addbrd_touchcache(void)
{
    SHM->Bnumber++;
    numboards = SHM->Bnumber;
    reset_board(numboards);
    sort_bcache();
}

void
reset_board(int bid) /* XXXbid: from 1 */
{				/* Ptt: 這樣就不用老是touch board了 */
    int             fd, nuser;
    boardheader_t  *bhdr;

    if (--bid < 0)
	return;
    if (SHM->Bbusystate || COMMON_TIME - SHM->busystate_b[bid] < 10) {
	safe_sleep(1);
    } else {
	SHM->busystate_b[bid] = COMMON_TIME;
	nuser = bcache[bid].nuser;

	bhdr = bcache;
	bhdr += bid;
	if ((fd = open(fn_board, O_RDONLY)) > 0) {
	    lseek(fd, (off_t) (bid * sizeof(boardheader_t)), SEEK_SET);
	    read(fd, bhdr, sizeof(boardheader_t));
	    close(fd);
	}
	SHM->busystate_b[bid] = 0;

	buildBMcache(bid + 1); /* XXXbid */
    }
}

#ifndef _BBS_UTIL_C_ /* because of HasPerm() in board.c */
int
apply_boards(int (*func) (boardheader_t *))
{
    register int    i;
    register boardheader_t *bhdr;

    for (i = 0, bhdr = bcache; i < numboards; i++, bhdr++) {
	if (!(bhdr->brdattr & BRD_GROUPBOARD) && HasPerm(bhdr) &&
	    (*func) (bhdr) == QUIT)
	    return QUIT;
    }
    return 0;
}
#endif

void
setbottomtotal(int bid)
{
    boardheader_t  *bh = getbcache(bid);
    char            genbuf[256];
    int             n;

    if(!bh->brdname[0]) return;
    setbfile(genbuf, bh->brdname, ".DIR.bottom");
    n = get_num_records(genbuf, sizeof(fileheader_t));
    if(n>5)
      {
#ifdef DEBUG_BOTTOM
        log_file("fix_bottom", LOG_CREAT | LOG_VF, "%s n:%d\n", genbuf, n);
#endif
        unlink(genbuf);
        SHM->n_bottom[bid-1]=0;
      }
    else
        SHM->n_bottom[bid-1]=n;
}
void
setbtotal(int bid)
{
    boardheader_t  *bh = getbcache(bid);
    struct stat     st;
    char            genbuf[256];
    int             num, fd;

    setbfile(genbuf, bh->brdname, ".DIR");
    if ((fd = open(genbuf, O_RDWR)) < 0)
	return;			/* .DIR掛了 */
    fstat(fd, &st);
    num = st.st_size / sizeof(fileheader_t);
    SHM->total[bid - 1] = num;

    if (num > 0) {
	lseek(fd, (off_t) (num - 1) * sizeof(fileheader_t), SEEK_SET);
	if (read(fd, genbuf, FNLEN) >= 0) {
	    SHM->lastposttime[bid - 1] = (time_t) atoi(&genbuf[2]);
	}
    } else
	SHM->lastposttime[bid - 1] = 0;
    close(fd);
}

void
touchbpostnum(int bid, int delta)
{
    int            *total = &SHM->total[bid - 1];
    if (*total)
	*total += delta;
}

int
getbnum(const char *bname)
{
    register int    i = 0, j, start = 0, end = SHM->Bnumber - 1;
    int *blist = SHM->bsorted[0];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	if (!(j = strcasecmp(bname, bcache[blist[i]].brdname)))
	    return (int)(blist[i] + 1);
	if (end == start) {
	    break;
	} else if (i == start) {
	    i = end;
	    start = end;
	} else if (j > 0)
	    start = i;
	else
	    end = i;
    }
    return 0;
}

int
haspostperm(char *bname)
{
    register int    i;
    char            buf[200];

    setbfile(buf, bname, fn_water);
    if (belong(buf, cuser.userid))
	return 0;

    if (!strcasecmp(bname, DEFAULT_BOARD))
	return 1;

    if (!strcasecmp(bname, "PttLaw"))
	return 1;

    if (!HAS_PERM(PERM_POST))
	return 0;

    if (!(i = getbnum(bname)))
	return 0;

    /* 秘密看板特別處理 */
    if (bcache[i - 1].brdattr & BRD_HIDE)
	return 1;

    i = bcache[i - 1].level;

    if (HAS_PERM(PERM_VIOLATELAW) && (i & PERM_VIOLATELAW))
	return 1;
    else if (HAS_PERM(PERM_VIOLATELAW))
	return 0;

    return HAS_PERM(i & ~PERM_POST);
}

void buildBMcache(int bid) /* bid starts from 1 */
{
    char    s[IDLEN * 3 + 3], *ptr;
    int     i, uid;
    --bid;

    strncpy(s, bcache[bid].BM, sizeof(s));
    for( i = 0 ; s[i] != 0 ; ++i )
	if( !isalpha((int)s[i]) && !isdigit((int)s[i]) )
            s[i] = ' ';

    for( ptr = strtok(s, " "), i = 0 ;
	 i < MAX_BMs && ptr != NULL  ;
	 ptr = strtok(NULL, " "), ++i  )
	if( (uid = searchuser(ptr)) != 0 )
	    SHM->BMcache[bid][i] = uid;
    for( ; i < MAX_BMs ; ++i )
	SHM->BMcache[bid][i] = -1;
}

int is_BM_cache(int bid) /* bid starts from 1 */
{
    --bid;
    if( currutmp->uid == SHM->BMcache[bid][0] ||
	currutmp->uid == SHM->BMcache[bid][1] ||
	currutmp->uid == SHM->BMcache[bid][2] ||
	currutmp->uid == SHM->BMcache[bid][3]    ){
	cuser.userlevel |= PERM_BM;
	return 1;
    }
    return 0;
}

/*-------------------------------------------------------*/
/* PTT  cache                                            */
/*-------------------------------------------------------*/
/* cache for 動態看板 */
void
reload_pttcache(void)
{
    if (SHM->Pbusystate)
	safe_sleep(1);
    else {			/* jochang: temporary workaround */
	fileheader_t    item, subitem;
	char            pbuf[256], buf[256], *chr;
	FILE           *fp, *fp1, *fp2;
	int             id, section = 0;

	SHM->Pbusystate = 1;
	SHM->max_film = 0;
	bzero(SHM->notes, sizeof(SHM->notes));
	setapath(pbuf, "Note");
	setadir(buf, pbuf);
	id = 0;
	if ((fp = fopen(buf, "r"))) {
	    while (fread(&item, sizeof(item), 1, fp)) {
		if (item.title[3] == '<' && item.title[8] == '>') {
		    snprintf(buf, sizeof(buf), "%s/%s", pbuf, item.filename);
		    setadir(buf, buf);
		    if (!(fp1 = fopen(buf, "r")))
			continue;
		    SHM->next_refresh[section] = SHM->n_notes[section] = id;
		    section++;
		    while (fread(&subitem, sizeof(subitem), 1, fp1)) {
			snprintf(buf, sizeof(buf),
				 "%s/%s/%s", pbuf, item.filename,
				 subitem.filename);
			if (!(fp2 = fopen(buf, "r")))
			    continue;
			fread(SHM->notes[id], sizeof(char), 200 * 11, fp2);
			SHM->notes[id][200 * 11 - 1] = 0;
			id++;
			fclose(fp2);
			if (id >= MAX_MOVIE)
			    break;
		    }
		    fclose(fp1);
		    if (id >= MAX_MOVIE || section >= MAX_MOVIE_SECTION)
			break;
		}
	    }
	    fclose(fp);
	}
	SHM->next_refresh[section] = -1;
	SHM->n_notes[section] = SHM->max_film = id - 1;
	SHM->max_history = SHM->max_film - 2;
	if (SHM->max_history > MAX_HISTORY - 1)
	    SHM->max_history = MAX_HISTORY - 1;
	if (SHM->max_history < 0)
	    SHM->max_history = 0;

	fp = fopen("etc/today_is", "r");
	if (fp) {
	    fgets(SHM->today_is, 15, fp);
	    if ((chr = strchr(SHM->today_is, '\n')))
		*chr = 0;
	    SHM->today_is[15] = 0;
	    fclose(fp);
	}
	/* 等所有資料更新後再設定 uptime */

	SHM->Puptime = SHM->Ptouchtime;
	log_usies("CACHE", "reload pttcache");
	SHM->Pbusystate = 0;
    }
}

void
resolve_garbage(void)
{
    int             count = 0;

    while (SHM->Puptime < SHM->Ptouchtime) {	/* 不用while等 */
	reload_pttcache();
	if (count++ > 10 && SHM->Pbusystate) {
	    /*
	     * Ptt: 這邊會有問題  load超過10 秒會所有進loop的process tate = 0
	     * 這樣會所有prcosee都會在load 動態看板 會造成load大增
	     * 但沒有用這個function的話 萬一load passwd檔的process死了
	     * 又沒有人把他 解開  同樣的問題發生在reload passwd
	     */
	    SHM->Pbusystate = 0;
#ifndef _BBS_UTIL_C_
	    log_usies("CACHE", "refork Ptt dead lock");
#endif
	}
    }
}

/*-------------------------------------------------------*/
/* PTT's cache                                           */
/*-------------------------------------------------------*/
/* cache for from host 與最多上線人數 */
void
reload_fcache(void)
{
    if (SHM->Fbusystate)
	safe_sleep(1);
    else {
	FILE           *fp;

	SHM->Fbusystate = 1;
	bzero(SHM->home_ip, sizeof(SHM->home_ip));
	if ((fp = fopen("etc/domain_name_query.cidr", "r"))) {
	    char            buf[256], *ip, *mask;

	    SHM->home_num = 0;
	    while (fgets(buf, sizeof(buf), fp)) {
		if (!buf[0] || buf[0] == '#' || buf[0] == ' ' || buf[0] == '\n')
		    continue;

		if (buf[0] == '@') {
		    SHM->home_ip[0] = 0;
		    SHM->home_mask[0] = 0xFFFFFFFF;
		    SHM->home_num++;
		    continue;
		}

		ip = strtok(buf, " \t");
		if ((mask = strchr(ip, '/')) != NULL) {
		    int shift = 32 - atoi(mask + 1);
		    SHM->home_ip[SHM->home_num] = ipstr2int(ip);
		    SHM->home_mask[SHM->home_num] = (0xFFFFFFFF >> shift ) << shift;
		}
		else {
		    SHM->home_ip[SHM->home_num] = ipstr2int(ip);
		    SHM->home_mask[SHM->home_num] = 0xFFFFFFFF;
		}
		ip = strtok(NULL, " \t");
		if (ip == NULL) {
		    strncpy(SHM->home_desc[SHM->home_num], "雲深不知處",
			    sizeof(SHM->home_desc[SHM->home_num]));
		}
		else {
		    strncpy(SHM->home_desc[SHM->home_num], ip,
			    sizeof(SHM->home_desc[SHM->home_num]));
    		    SHM->home_desc[SHM->home_num][strlen(SHM->home_desc[SHM->home_num]) - 1] = 0;
		}
		(SHM->home_num)++;
		if (SHM->home_num == MAX_FROM)
		    break;
	    }
	    fclose(fp);
	}
	SHM->max_user = 0;

	/* 等所有資料更新後再設定 uptime */
	SHM->Fuptime = SHM->Ftouchtime;
#if !defined(_BBS_UTIL_C_)
	log_usies("CACHE", "reload fcache");
#endif
	SHM->Fbusystate = 0;
    }
}

void
resolve_fcache(void)
{
    while (SHM->Fuptime < SHM->Ftouchtime)
	reload_fcache();
}

/*
 * section - hbfl (hidden board friend list)
 */
void
hbflreload(int bid)
{
    int             hbfl[MAX_FRIEND + 1], i, num, uid;
    char            buf[128];
    FILE           *fp;

    memset(hbfl, 0, sizeof(hbfl));
    setbfile(buf, bcache[bid - 1].brdname, fn_visable);
    if ((fp = fopen(buf, "r")) != NULL) {
	for (num = 1; num <= MAX_FRIEND; ++num) {
	    if (fgets(buf, sizeof(buf), fp) == NULL)
		break;
	    for (i = 0; buf[i] != 0; ++i)
		if (buf[i] == ' ') {
		    buf[i] = 0;
		    break;
		}
	    if (strcasecmp("guest", buf) == 0 ||
		(uid = searchuser(buf)) == 0) {
		--num;
		continue;
	    }
	    hbfl[num] = uid;
	}
	fclose(fp);
    }
    hbfl[0] = COMMON_TIME;
    memcpy(SHM->hbfl[bid], hbfl, sizeof(hbfl));
}

int
hbflcheck(int bid, int uid)
{
    int             i;

    if (SHM->hbfl[bid][0] < login_start_time - HBFLexpire)
	hbflreload(bid);
    for (i = 1; SHM->hbfl[bid][i] != 0 && i <= MAX_FRIEND; ++i) {
	if (SHM->hbfl[bid][i] == uid)
	    return 0;
    }
    return 1;
}
