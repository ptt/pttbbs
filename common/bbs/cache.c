#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "cmsys.h"
#include "cmbbs.h"
#include "common.h"
#include "var.h"

#include "modes.h" // for DEBUGSLEEPING

//////////////////////////////////////////////////////////////////////////
// This is shared by utility library and core BBS,
// so do not put code using currutmp/cuser here.
//////////////////////////////////////////////////////////////////////////

// these cannot be used!
#define currutmp  YOU_FAILED
#define usernum	  YOU_FAILED
#undef  cuser
#undef  cuser_ref
#define cuser     YOU_FAILED
#define cuser_ref YOU_FAILED
#define abort_bbs YOU_FAILED
#define log_usies YOU_FAILED

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
add_to_uhash(int n, const char *id)
{
    int            *p, h = StringHash(id)%(1<<HASH_BITS);
    int             times;
    STRLCPY(SHM->userid[n], id);

    p = &(SHM->hash_head[h]);

    for (times = 0; times < MAX_USERS && *p != -1; ++times)
	p = &(SHM->next_in_hash[*p]);

    if (times >= MAX_USERS)
    {
	// abort_bbs(0);
	fprintf(stderr, "add_to_uhash: exceed max users.\r\n");
	exit(0);
    }

    SHM->next_in_hash[*p = n] = -1;
}

void
remove_from_uhash(int n)
{
/*
 * note: after remove_from_uhash(), you should add_to_uhash() (likely with a
 * different name)
 */
    int             h = StringHash(SHM->userid[n])%(1<<HASH_BITS);
    int            *p = &(SHM->hash_head[h]);
    int             times;

    for (times = 0; times < MAX_USERS && (*p != -1 && *p != n); ++times)
	p = &(SHM->next_in_hash[*p]);

    if (times >= MAX_USERS)
    {
	// abort_bbs(0);
	fprintf(stderr, "remove_from_uhash: current SHM exceed max users.\r\n");
	exit(0);
    }

    if (*p == n)
	*p = SHM->next_in_hash[n];
}

#if !defined(SKIP_HASH_BITS_CHECK) && ((1<<HASH_BITS)*10 < MAX_USERS)
// Changing HASH_BITS will change SHM size so we may skip this value if
// that's already decided.
#warning "Suggest to use bigger HASH_BITS for better searchuser() performance,"
#warning "searchuser() average chaining MAX_USERS/(1<<HASH_BITS) times."
#endif

int
dosearchuser(const char *userid, char *rightid)
{
    int             h, p, times;
    STATINC(STAT_SEARCHUSER);
    h = StringHash(userid)%(1<<HASH_BITS);
    p = SHM->hash_head[h];

    for (times = 0; times < MAX_USERS && p != -1 && p < MAX_USERS ; ++times) {
	if (strcasecmp(SHM->userid[p], userid) == 0) {
	    if(userid[0] && rightid) strcpy(rightid, SHM->userid[p]);
	    return p + 1;
	}
	p = SHM->next_in_hash[p];
    }

    return 0;
}

int
searchuser(const char *userid, char *rightid)
{
    if(userid[0]=='\0')
	return 0;
    return dosearchuser(userid, rightid);
}

char           *
getuserid(int num)
{
    if (--num >= 0 && num < MAX_USERS)
	return ((char *)SHM->userid[num]);
    return NULL;
}

void
setuserid(int num, const char *userid)
{
    if (num > 0 && num <= MAX_USERS) {
/*  Ptt: it may cause problems
	if (num > SHM->number)
	    SHM->number = num;
	else
*/
        remove_from_uhash(num - 1);
	add_to_uhash(num - 1, userid);
    }
}

userinfo_t     *
search_ulist_pid(int pid)
{
    register int    i = 0, j, start = 0, end = SHM->UTMPnumber - 1;
    int *ulist;
    register userinfo_t *u;
    if (end == -1)
	return NULL;
    ulist = SHM->sorted[SHM->currsorted][8];
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
    ulist = SHM->sorted[SHM->currsorted][7];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	u = &SHM->uinfo[ulist[i]];
	j = uid - u->uid;
	if (j == 0) {
	    for (; i > 0 && uid == SHM->uinfo[ulist[i - 1]].uid; --i)
		;/* 指到第一筆 */
	    // piaip Tue Jan  8 09:28:03 CST 2008
	    // many people bugged about that their utmp have invalid
	    // entry on record.
	    // we found them caused by crash process (DEBUGSLEEPING) which
	    // may occupy utmp entries even after process was killed.
	    // because the memory is invalid, it is not safe for those process
	    // to wipe their utmp entry. it should be done by some external
	    // daemon.
	    // however, let's make a little workaround here...
	    for (; unum > 0 && i >= 0 && ulist[i] >= 0 &&
		    SHM->uinfo[ulist[i]].uid == uid; unum--, i++)
	    {
		if (SHM->uinfo[ulist[i]].mode == DEBUGSLEEPING)
		    unum ++;
	    }
	    if (unum == 0 && i > 0 && ulist[i-1] >= 0 &&
		    SHM->uinfo[ulist[i-1]].uid == uid)
		return &SHM->uinfo[ulist[i-1]];
	    /*
	    if ( i + unum - 1 >= 0 &&
		 (ulist[i + unum - 1] >= 0 &&
		  uid == SHM->uinfo[ulist[i + unum - 1]].uid ) )
		return &SHM->uinfo[ulist[i + unum - 1]];
		*/
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
search_ulist_userid(const char *userid)
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
    if (uid <= 0 || uid > MAX_USERS){
	fprintf(stderr, "internal error: deumoney(%d, %d)\r\n", uid, money);
	return -1;
    }

    if (money < 0 && moneyof(uid) < -money)
	return setumoney(uid, 0);
    else
	return setumoney(uid, SHM->money[uid - 1] + money);
}

/*
 * section - board cache
 */
void touchbtotal(int bid) {
    assert(0<=bid-1 && bid-1<MAX_BOARD);
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
    int cmp;

    cmp=strncmp(brd1->title, brd2->title, 4);
    if(cmp!=0) return cmp;
    return strcasecmp(brd1->brdname, brd2->brdname);
}


void
sort_bcache(void)
{
    int             i;
    /* critical section 時常被呼叫  */
    /* 僅新增 或刪除看板 需要呼叫 */
    if (!__sync_bool_compare_and_swap(&SHM->Bbusystate, 0, 1)) {
	sleep(1);
	return;
    }
    for (i = 0; i < SHM->Bnumber; i++) {
	SHM->bsorted[BRD_GROUP_LL_TYPE_NAME][i] = i;
	SHM->bsorted[BRD_GROUP_LL_TYPE_CLASS][i] = i;
    }
    qsort(SHM->bsorted[BRD_GROUP_LL_TYPE_NAME],
	  SHM->Bnumber, sizeof(int), cmpboardname);
    qsort(SHM->bsorted[BRD_GROUP_LL_TYPE_CLASS],
	  SHM->Bnumber, sizeof(int), cmpboardclass);

    for (i = 0; i < SHM->Bnumber; i++) {
	bcache[i].firstchild[BRD_GROUP_LL_TYPE_NAME] = 0;
	bcache[i].firstchild[BRD_GROUP_LL_TYPE_CLASS] = 0;
    }
    SHM->Bbusystate = 0;
}

void
reload_bcache(void)
{
    int     i, fd;
    for (i = 0; i < 10; ++i) {
	if (__sync_bool_compare_and_swap(&SHM->Bbusystate, 0, 1))
	    break;
	fprintf(stderr, "SHM->Bbusystate is currently locked (value: %d). "
	       "please wait... \r\n", SHM->Bbusystate);
	sleep(1);
    }
    if (i == 10)
	return;
    if ((fd = open(FN_BOARD, O_RDONLY)) > 0) {
	SHM->Bnumber =
	    read(fd, bcache, MAX_BOARD * sizeof(boardheader_t)) /
	    sizeof(boardheader_t);
	close(fd);
    }
    memset(SHM->lastposttime, 0, MAX_BOARD * sizeof(time4_t));
    memset(SHM->total, 0, MAX_BOARD * sizeof(int));

    /* 等所有 boards 資料更新後再設定 uptime */
    SHM->Buptime = SHM->Btouchtime;
    // log_usies("CACHE", "reload bcache");
    fprintf(stderr, "cache: reload bcache\r\n");
    SHM->Bbusystate = 0;
    sort_bcache();
}

void resolve_boards(void)
{
    while (time4_lt(SHM->Buptime, SHM->Btouchtime)) {
	reload_bcache();
    }
}

int num_boards(void)
{
    return SHM->Bnumber;
}

void addbrd_touchcache(void)
{
    SHM->Bnumber++;
    reset_board(num_boards());
    sort_bcache();
}

void
reset_board(int bid) /* XXXbid: from 1 */
{				/* Ptt: 這樣就不用老是touch board了 */
    int             fd;
    boardheader_t  *bhdr;

    if (--bid < 0)
	return;
    assert(0<=bid && bid<MAX_BOARD);
    if (SHM->Bbusystate || time4_diff(COMMON_TIME, SHM->busystate_b[bid]) < 10) {
	sleep(1);
    } else {
	SHM->busystate_b[bid] = COMMON_TIME;

	bhdr = bcache;
	bhdr += bid;
	if ((fd = open(FN_BOARD, O_RDONLY)) >= 0) {
	    lseek(fd, (off_t) (bid * sizeof(boardheader_t)), SEEK_SET);
	    read(fd, bhdr, sizeof(boardheader_t));
	    close(fd);
	}
	SHM->busystate_b[bid] = 0;

	buildBMcache(bid + 1); /* XXXbid */
    }
}

void
resolve_board_group(const int gid, const int type)
{
    boardheader_t  *bptr, *currbptr, *parent;
    int             bid, n, childcount = 0;
    int             boardcount;
    assert(0<=type && type<2);
    assert(0<= gid-1 && gid-1<MAX_BOARD);
    currbptr = parent = &bcache[gid - 1];
    boardcount = num_boards();
    assert(0<=boardcount && boardcount<=MAX_BOARD);
    for (n = 0; n < boardcount; ++n) {
	bid = SHM->bsorted[type][n]+1;
	if( bid<=0 || !(bptr = getbcache(bid))
		|| bptr->brdname[0] == '\0' )
	    continue;
	if (bptr->gid == gid) {
	    if (currbptr == parent)
		currbptr->firstchild[type] = bid;
	    else {
		currbptr->next[type] = bid;
		currbptr->parent = gid;
	    }
	    childcount++;
	    currbptr = bptr;
	}
    }
    parent->childcount = childcount;
    if (currbptr == parent) // no child
	currbptr->firstchild[type] = -1;
    else // the last child
	currbptr->next[type] = -1;
}

static uint8_t bottom_migrated[(MAX_BOARD + 7) / 8];

static int
migrate_board_bottom_lazy(int bid, boardheader_t *bh)
{
    if (bid >= 1 && bid <= MAX_BOARD &&
        (bottom_migrated[(bid - 1) >> 3] & (1U << ((bid - 1) & 7))))
        return 0;

    char fn_bot[PATHLEN], fn_dir[PATHLEN], art_path[PATHLEN];
    setbfile(fn_bot, bh->brdname, FN_DIR_BOTTOM);

    int fd_bot = open(fn_bot, O_RDWR);
    if (fd_bot < 0) {
        if (errno == ENOENT && bid >= 1 && bid <= MAX_BOARD)
            bottom_migrated[(bid - 1) >> 3] |= (uint8_t)(1U << ((bid - 1) & 7));
        return 0;
    }

    /* Exclusive lock serializes concurrent processes entering the same board */
    if (flock(fd_bot, LOCK_EX) < 0) {
        close(fd_bot);
        return 0;
    }

    struct stat st_bot;
    if (fstat(fd_bot, &st_bot) < 0 || st_bot.st_nlink == 0) {
        /* Another process already migrated and unlinked .DIR.bottom */
        close(fd_bot);
        int cnt = 0;
        for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
            if (aidu_raw(bh->bottom[i]) != 0)
                cnt++;
        }
        return cnt;
    }

    for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
        if (aidu_raw(bh->bottom[i]) != 0) {
            unlink(fn_bot);
            close(fd_bot);
            int cnt = 0;
            for (int j = 0; j < MAX_BOTTOM_POSTS; j++) {
                if (aidu_raw(bh->bottom[j]) != 0)
                    cnt++;
            }
            return cnt;
        }
    }

    int bot_count = (int)(st_bot.st_size / sizeof(fileheader_t));
    if (bot_count <= 0) {
        unlink(fn_bot);
        close(fd_bot);
        return 0;
    }
    if (bot_count > MAX_BOTTOM_POSTS)
        bot_count = MAX_BOTTOM_POSTS;

    setbfile(fn_dir, bh->brdname, FN_DIR);
    int fd_dir = open(fn_dir, O_RDWR | O_CREAT, DEFAULT_FILE_CREATE_PERM);
    if (fd_dir < 0) {
        close(fd_bot);
        return 0;
    }

    struct stat st_dir;
    int total = (fstat(fd_dir, &st_dir) == 0)
                    ? (int)(st_dir.st_size / sizeof(fileheader_t))
                    : 0;

    aidu_t new_slots[MAX_BOTTOM_POSTS];
    memset(new_slots, 0, sizeof(new_slots));
    int valid_cnt = 0;

    for (int i = 0; i < bot_count && valid_cnt < MAX_BOTTOM_POSTS; i++) {
        fileheader_t bfh;
        if (pread(fd_bot, &bfh, sizeof(bfh), (off_t)i * sizeof(bfh)) !=
            (ssize_t)sizeof(bfh))
            break;

        aidu_t a = fn2aidu(bfh.filename);
        if (aidu_raw(a) == 0)
            continue;

        /* Use legacy multi.refer (bit 31 = flag, bits [30:0] = ref) as O(1) index hint */
        uint32_t raw_ref = (uint32_t)bfh.multi.money;
        if ((raw_ref & 0x80000000U) && (raw_ref & 0x7fffffffU) > 0)
            a = aidu_with_idx(a, (int)(raw_ref & 0x7fffffffU));

        fileheader_t dfh;
        /* Do not ask for out_fh: it may be converted to memory encoding and
         * must not be written back to .DIR. Re-read the raw record instead. */
        int found_idx = search_dir_by_aidu_fd(fd_dir, total, a, 0, NULL);
        if (found_idx > 0) {
            off_t off = (off_t)(found_idx - 1) * sizeof(dfh);
            if (pread(fd_dir, &dfh, sizeof(dfh), off) == (ssize_t)sizeof(dfh) &&
                (dfh.filemode & (FILE_BOTTOM | FILE_MARKED)) !=
                (FILE_BOTTOM | FILE_MARKED)) {
                dfh.filemode |= (FILE_BOTTOM | FILE_MARKED);
                pwrite(fd_dir, &dfh, sizeof(dfh), off);
            }
            new_slots[valid_cnt++] = aidu_with_idx(a, found_idx);
        } else {
            /* Orphan bottom post: restore into .DIR if article file exists */
            setbfile(art_path, bh->brdname, bfh.filename);
            if (dashf(art_path)) {
                dfh = bfh;
                dfh.multi.money = 0;
                dfh.filemode |= (FILE_BOTTOM | FILE_MARKED);
                if (append_record(fn_dir, &dfh, sizeof(dfh)) == 0) {
                    total = get_num_records(fn_dir, sizeof(fileheader_t));
                    new_slots[valid_cnt++] = aidu_with_idx(a, total);
                }
            }
        }
    }
    close(fd_dir);

    memcpy(bh->bottom, new_slots, sizeof(bh->bottom));
    substitute_record(FN_BOARD, bh, sizeof(boardheader_t), bid);
    SHM->n_bottom[bid - 1] = valid_cnt;

    unlink(fn_bot);
    close(fd_bot);
    return valid_cnt;
}

int
getbottomtotal(int bid)
{
    if (bid < 1 || bid > MAX_BOARD)
        return 0;
    boardheader_t *bh = getbcache(bid);
    if (!bh->brdname[0])
        return 0;
    int n = 0;
    for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
        if (aidu_raw(bh->bottom[i]) != 0)
            n++;
    }
    if (n == 0)
        n = migrate_board_bottom_lazy(bid, bh);
    return n;
}

int
resolve_board_bottoms(int bid, int32_t out_recs[MAX_BOTTOM_POSTS])
{
    if (bid < 1 || bid > MAX_BOARD)
        return 0;
    boardheader_t *bh = getbcache(bid);
    if (!bh->brdname[0])
        return 0;

    int raw_count = 0;
    for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
        if (aidu_raw(bh->bottom[i]) != 0)
            raw_count++;
    }
    if (raw_count == 0) {
        raw_count = migrate_board_bottom_lazy(bid, bh);
        if (raw_count == 0)
            return 0;
    }

    char fname[PATHLEN];
    setbfile(fname, bh->brdname, FN_DIR);
    int fd = open(fname, O_RDONLY);
    if (fd < 0)
        return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return 0;
    }
    int total = (int)(st.st_size / sizeof(fileheader_t));
    /* bh->bottom lives in SHM and may be changed by pin_post() while we do
     * .DIR I/O; work on a snapshot and only write back if unchanged. */
    aidu_t snap[MAX_BOTTOM_POSTS], new_slots[MAX_BOTTOM_POSTS];
    memcpy(snap, bh->bottom, sizeof(snap));
    memset(new_slots, 0, sizeof(new_slots));
    int valid_cnt = 0;
    int dirty = 0;

    for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
        aidu_t a = snap[i];
        if (aidu_raw(a) == 0)
            continue;
        int old_idx = aidu_idx(a);
        int found_idx = search_dir_by_aidu_fd(fd, total, a, FILE_BOTTOM, NULL);
        if (found_idx > 0) {
            int expected_idx = ((uint32_t)found_idx <= AIDU_IDX_MASK) ? found_idx : 0;
            new_slots[valid_cnt] = aidu_with_idx(a, expected_idx);
            if (out_recs)
                out_recs[valid_cnt] = found_idx;
            if (expected_idx != old_idx || valid_cnt != i)
                dirty = 1;
            valid_cnt++;
        } else {
            /* Original article was deleted from .DIR; drop dead bottom slot */
            dirty = 1;
        }
    }
    close(fd);

    if (dirty && memcmp(snap, bh->bottom, sizeof(snap)) == 0) {
        memcpy(bh->bottom, new_slots, sizeof(bh->bottom));
        substitute_record(FN_BOARD, bh, sizeof(boardheader_t), bid);
    }
    SHM->n_bottom[bid - 1] = valid_cnt;
    return valid_cnt;
}

void
setbottomtotal(int bid)
{
    (void)resolve_board_bottoms(bid, NULL);
}

void
setbtotal(int bid)
{
    boardheader_t  *bh = getbcache(bid);
    struct stat     st;
    char            genbuf[PATHLEN];
    int             num, fd;

    assert(0<=bid-1 && bid-1<MAX_BOARD);
    setbfile(genbuf, bh->brdname, FN_DIR);
    if ((fd = open(genbuf, O_RDONLY)) < 0)
	return;			/* .DIR掛了 */
    fstat(fd, &st);
    num = st.st_size / sizeof(fileheader_t);
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    SHM->total[bid - 1] = num;

    if (num > 0) {
	lseek(fd, (off_t) (num - 1) * sizeof(fileheader_t), SEEK_SET);
	if (read(fd, genbuf, FNLEN) >= 0) {
#ifdef FN_SAFEDEL_PREFIX_LEN
            if (strncmp(genbuf, FN_SAFEDEL, FN_SAFEDEL_PREFIX_LEN) == 0)
                SHM->lastposttime[bid - 1] = 0;
            else
#endif
	    SHM->lastposttime[bid - 1] = get_fhdr_stamp_ts(genbuf);
	}
    } else
	SHM->lastposttime[bid - 1] = 0;
    close(fd);
}

void
touchbpostnum(int bid, int delta)
{
    int            *total = &SHM->total[bid - 1];
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    if (*total)
	*total += delta;
}

int
getbnum(const char *bname)
{
    register int    i = 0, j, start = 0, end = SHM->Bnumber - 1;
    int *blist = SHM->bsorted[0];
    if(SHM->Bbusystate)
	sleep(1);
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
parseBMlist(const char *input, int uids[MAX_BMs]) {
    int i, uid;
    char *ptr, *strtok_pos;
    char s[(IDLEN + 1) * MAX_BMs];

    STRLCPY(s, input);
    // reset BM list
    for (i = 0; i < MAX_BMs; i++)
        uids[i] = -1;

    for (i = 0 ; s[i] != 0 ; ++i)
	if (!isalpha((int)s[i]) && !isdigit((int)s[i]))
            s[i] = ' ';
    for (ptr = strtok_r(s, " ", &strtok_pos), i = 0;
	 i < MAX_BMs && ptr != NULL;
         ptr = strtok_r(NULL, " ", &strtok_pos))
        if((uid = searchuser(ptr, NULL)) != 0)
            uids[i++] = uid;
    return i;
}


void
buildBMcache(int bid) /* bid starts from 1 */
{
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    parseBMlist(getbcache(bid)->BM, SHM->BMcache[bid - 1]);
}

/*
 * section - PTT cache (adbanner cache?)
 * 動態看板與其它
 */
int 
filter_aggressive(const char*s GCC_UNUSED)
{
    if (
	/*
	mbs_strstr(s, "此處放較不適當的爭議性字句") != NULL ||
	*/
	0
	)
	return 1;
    return 0;
}

int 
filter_dirtywords(const char*s)
{
    if (
	mbs_strstr(s, "幹你娘") != NULL ||
	0)
	return 1;
    return 0;
}

#define AGGRESSIVE_FN ".aggressive"
static char drop_aggressive = 0;

void 
load_aggressive_state()
{
    if (dashf(AGGRESSIVE_FN))
	drop_aggressive = 1;
    else
	drop_aggressive = 0;
}

void 
set_aggressive_state(int s)
{
    FILE *fp = NULL;
    if (s)
    {
	fp = fopen(AGGRESSIVE_FN, "wb");
	fclose(fp);
    } else {
	remove(AGGRESSIVE_FN);
    }
}

/* cache for 動態看板 */
void
reload_pttcache(void)
{
    if (SHM->Pbusystate)
	sleep(1);
    else {			/* jochang: temporary workaround */
	fileheader_t    item, subitem;
	char            pbuf[256], buf[256];
	FILE           *fp, *fp1, *fp2;
	int             id, aggid, rawid;

	SHM->Pbusystate = 1;
	SHM->last_film = 0;
	bzero(SHM->notes, sizeof(SHM->notes));
	setapath(pbuf, BN_NOTE);
	setadir(buf, pbuf);

	load_aggressive_state();
	id = aggid = rawid = 0; // effective count, aggressive count, total (raw) count

	if ((fp = fopen(buf, "r"))) {

	    // .DIR loop
	    while (fread(&item, sizeof(item), 1, fp)) {

		int chkagg = 0; // should we check aggressive?
		int is_ordersong_dir = 0;

		if (item.title[3] != '<' || item.title[8] != '>')
		    continue;

#define ORDERSONG_FOLDERNAME	"<點歌>"
		if (strncmp(item.title+3, ORDERSONG_FOLDERNAME, strlen(ORDERSONG_FOLDERNAME)) == 0)
		    is_ordersong_dir = 1;

#ifdef BN_NOTE_AGGCHKDIR
		// TODO aggressive: only count '<點歌>' section
		if (strncmp(item.title+3, BN_NOTE_AGGCHKDIR, strlen(BN_NOTE_AGGCHKDIR)) == 0)
		    chkagg = 1;
#endif
		SNPRINTF(buf, "%s/%s/" FN_DIR,
			pbuf, item.filename);

		if (!(fp1 = fopen(buf, "r")))
		    continue;

		// file loop
		while (fread(&subitem, sizeof(subitem), 1, fp1)) {

		    SNPRINTF(buf, "%s/%s/%s", pbuf, item.filename,
			    subitem.filename);

		    if (!(fp2 = fopen(buf, "r")))
			continue;

		    fread(SHM->notes[id], sizeof(char), sizeof(SHM->notes[0]), fp2);
		    SHM->notes[id][sizeof(SHM->notes[0]) - 1] = 0;
		    rawid ++;

		    // filtering
		    if (filter_dirtywords(SHM->notes[id]))
		    {
			memset(SHM->notes[id], 0, sizeof(SHM->notes[0]));
			rawid --;
		    }
		    else if (chkagg && filter_aggressive(SHM->notes[id]))
		    {
			aggid++;
			// handle aggressive notes by last detemined state
			if (drop_aggressive)
			    memset(SHM->notes[id], 0, sizeof(SHM->notes[0]));
			else
			    id++;
			// Debug purpose
			// fprintf(stderr, "found aggressive: %s\r\n", buf);
		    } 
		    else 
		    {
			id++;
		    }

		    fclose(fp2);
		    if (id >= MAX_ADBANNER)
			break;

		} // end of file loop
		fclose(fp1);

		if (is_ordersong_dir)
		    SHM->last_usong = id - 1;

		if (id >= MAX_ADBANNER)
		    break;

	    } // end of .DIR loop
	    fclose(fp);

	    // decide next aggressive state
	    if (rawid && aggid*3 >= rawid) // if aggressive exceed 1/3
		set_aggressive_state(1);
	    else
		set_aggressive_state(0);

	    // fprintf(stderr, "id(%d)/agg(%d)/raw(%d)\r\n",
	    //	    id, aggid, rawid);
	}
	SHM->last_film = id - 1;

	/* 等所有資料更新後再設定 uptime */

	SHM->Puptime = SHM->Ptouchtime;
	// log_usies("CACHE", "reload pttcache");
	fprintf(stderr, "cache: reload pttcache\r\n");
	SHM->Pbusystate = 0;
    }
}

void
resolve_garbage(void)
{
    int             count = 0;

    while (time4_lt(SHM->Puptime, SHM->Ptouchtime)) {	/* 不用while等 */
	reload_pttcache();
	if (count++ > 10 && SHM->Pbusystate) {
	    /*
	     * Ptt: 這邊會有問題  load超過10 秒會所有進loop的process tate = 0
	     * 這樣會所有prcosee都會在load 動態看板 會造成load大增
	     * 但沒有用這個function的話 萬一load passwd檔的process死了
	     * 又沒有人把他 解開  同樣的問題發生在reload passwd
	     */
	    SHM->Pbusystate = 0;
	    // log_usies("CACHE", "refork Ptt dead lock");
	    fprintf(stderr, "cache: refork Ptt dead lock\r\n");
	}
    }
}

/*
 * section - from host (deprecated by fromd / logind?)
 * cache for from host 與最多上線人數 
 */
void
reload_fcache(void)
{
    if (SHM->Fbusystate)
	sleep(1);
    else {
	SHM->Fbusystate = 1;
	SHM->max_user = 0;

	/* 等所有資料更新後再設定 uptime */
	SHM->Fuptime = SHM->Ftouchtime;
	// log_usies("CACHE", "reload fcache");
	fprintf(stderr, "cache: reload from cache\r\n");
	SHM->Fbusystate = 0;
    }
}

void
resolve_fcache(void)
{
    while (time4_lt(SHM->Fuptime, SHM->Ftouchtime))
	reload_fcache();
}

/*
 * section - hbfl (hidden board friend list)
 */
void
hbflreload(int bid)
{
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    /* Legacy binaries still read SHM->hbfl during the compat window; zero the
     * load time so they reload from the visable file instead of using a stale
     * list. */
    if (COMMON_TIME < FRIEND_LEGACY_COMPAT_CUTOFF) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	SHM->deprecated_hbfl[bid - 1][0] = 0;
#pragma GCC diagnostic pop
    }
    friend_svc_hbfl_reload(bid);
}

/* 是否通過板友測試. 如果在板友名單中的話傳回 1, 否則為 0 */
int
is_hidden_board_friend(int bid, int uid)
{
    int             i;
    char            buf[PATHLEN];
    const char     *userid;

    assert(0<=bid-1 && bid-1<MAX_BOARD);
    if (uid <= 0)
	return 0;
    i = friend_svc_is_hidden_board_friend(bid, uid);
    if (i >= 0)
	return i;
    userid = getuserid(uid);
    if (!userid || !*userid || strcasecmp(STR_GUEST, userid) == 0)
	return 0;
    setbfile(buf, bcache[bid - 1].brdname, FN_VISABLE);
    return file_exist_entry(buf, userid) ? 1 : 0;
}

/*
 * section - cooldown
 */
#ifdef USE_COOLDOWN

void add_cooldowntime(int uid, int min)
{
    // Ptt: I will use the number below 15 seconds.
    time4_t cd = SHM->cooldowntime[uid - 1];
    time4_t base = time4_gt(now, cd) ? now: cd;
    base += min*60;
    base &= 0xFFFFFFF0;

    SHM->cooldowntime[uid - 1] = base;
}
void add_posttimes(int uid, int times)
{
  if((SHM->cooldowntime[uid - 1] & 0xF) + times <0xF)
       SHM->cooldowntime[uid - 1] += times;
  else
       SHM->cooldowntime[uid - 1] |= 0xF;
}

#endif
