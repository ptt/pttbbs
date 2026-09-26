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
    if (!SHM || n < 0 || n >= MAX_USERS || !id)
        return;

    int h = StringHash(id) % (1 << HASH_BITS);
    STRLCPY(SHM->userid[n], id);

    int old_head;
    do {
        old_head = __atomic_load_n(&SHM->hash_head[h], __ATOMIC_ACQUIRE);
        __atomic_store_n(&SHM->next_in_hash[n], old_head, __ATOMIC_RELEASE);
    } while (!__atomic_compare_exchange_n(&SHM->hash_head[h], &old_head, n,
                                          false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));
}

void
remove_from_uhash(int n)
{
/*
 * note: after remove_from_uhash(), you should add_to_uhash() (likely with a
 * different name)
 */
    if (!SHM || n < 0 || n >= MAX_USERS)
        return;

    int h = StringHash(SHM->userid[n]) % (1 << HASH_BITS);

    while (1) {
        int head = __atomic_load_n(&SHM->hash_head[h], __ATOMIC_ACQUIRE);
        if (head == -1 || head >= MAX_USERS || head < 0)
            return;

        if (head == n) {
            int next = __atomic_load_n(&SHM->next_in_hash[n], __ATOMIC_ACQUIRE);
            if (__atomic_compare_exchange_n(&SHM->hash_head[h], &head, next,
                                            false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
                return;
            continue;
        }

        int prev = head;
        int restart = 0;
        for (int times = 0; times < MAX_USERS && prev != -1 && prev >= 0 && prev < MAX_USERS; ++times) {
            int next = __atomic_load_n(&SHM->next_in_hash[prev], __ATOMIC_ACQUIRE);
            if (next == n) {
                int succ = __atomic_load_n(&SHM->next_in_hash[n], __ATOMIC_ACQUIRE);
                if (!__atomic_compare_exchange_n(&SHM->next_in_hash[prev], &next, succ,
                                                 false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
                    restart = 1;
                    break;
                }
                return;
            }
            prev = next;
        }
        if (!restart)
            return;
    }
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
    h = StringHash(userid) % (1 << HASH_BITS);
    p = __atomic_load_n(&SHM->hash_head[h], __ATOMIC_ACQUIRE);

    for (times = 0; times < MAX_USERS && p != -1 && p >= 0 && p < MAX_USERS; ++times) {
	if (strcasecmp(SHM->userid[p], userid) == 0) {
	    if (userid[0] && rightid) strlcpy(rightid, SHM->userid[p], IDLEN + 1);
	    return p + 1;
	}
	p = __atomic_load_n(&SHM->next_in_hash[p], __ATOMIC_ACQUIRE);
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


/* utmp user direct mapping functions (Lock-Free Harris CAS) */

#define UTMP_DELETED_BIT (1U << 31)
#define UTMP_ENCODE_NEXT(slot) ((slot) + 1)
#define UTMP_DECODE_SLOT(v) ((int)(((unsigned int)(v) & ~UTMP_DELETED_BIT) - 1))
#define UTMP_IS_DELETED(v) ((((unsigned int)(v)) & UTMP_DELETED_BIT) != 0)
#define UTMP_MARK_DELETED(v) ((int)(((unsigned int)(v)) | UTMP_DELETED_BIT))

static void
clean_marked_nodes(int unum)
{
    while (1) {
        int head = __atomic_load_n(&SHM->utmp_user.user_head[unum], __ATOMIC_ACQUIRE);
        if (!VALID_USHM_ENTRY(head))
            return;

        int head_next = __atomic_load_n(&SHM->utmp_user.next_session[head], __ATOMIC_ACQUIRE);
        if (UTMP_IS_DELETED(head_next)) {
            int next = UTMP_DECODE_SLOT(head_next);
            __atomic_compare_exchange_n(&SHM->utmp_user.user_head[unum], &head, next,
                                        false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
            continue;
        }

        int prev = head;
        int restart = 0;
        for (int count = 0; count < USHM_SIZE && VALID_USHM_ENTRY(prev); count++) {
            int prev_next = __atomic_load_n(&SHM->utmp_user.next_session[prev], __ATOMIC_ACQUIRE);
            if (UTMP_IS_DELETED(prev_next)) {
                restart = 1;
                break;
            }
            int curr = UTMP_DECODE_SLOT(prev_next);
            if (!VALID_USHM_ENTRY(curr))
                break;

            int curr_next = __atomic_load_n(&SHM->utmp_user.next_session[curr], __ATOMIC_ACQUIRE);
            if (UTMP_IS_DELETED(curr_next)) {
                int succ = UTMP_DECODE_SLOT(curr_next);
                int expected = UTMP_ENCODE_NEXT(curr);
                if (!__atomic_compare_exchange_n(&SHM->utmp_user.next_session[prev], &expected,
                                                 UTMP_ENCODE_NEXT(succ),
                                                 false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
                    restart = 1;
                    break;
                }
                continue;
            }
            prev = curr;
        }
        if (!restart)
            break;
    }
}

void
init_utmp_user(void)
{
    if (!SHM)
        return;
    memset(SHM->utmp_user.user_head, 0xff, sizeof(SHM->utmp_user.user_head));
    memset(SHM->utmp_user.next_session, 0, sizeof(SHM->utmp_user.next_session));
    memset(SHM->utmp_user.session_user, 0, sizeof(SHM->utmp_user.session_user));
    for (int i = 0; i < USHM_SIZE; i++) {
        if (SHM->uinfo[i].pid > 0 && SHM->uinfo[i].uid > 0) {
            add_to_utmp_user(i, SHM->uinfo[i].uid);
        }
    }
}

void
remove_from_utmp_user(int uslot, int unum)
{
    if (!SHM || !VALID_USHM_ENTRY(uslot) || unum <= 0 || unum > MAX_USERS)
        return;

    __atomic_store_n(&SHM->utmp_user.session_user[uslot], 0, __ATOMIC_RELEASE);

    int next_val;
    do {
        next_val = __atomic_load_n(&SHM->utmp_user.next_session[uslot], __ATOMIC_ACQUIRE);
        if (UTMP_IS_DELETED(next_val))
            break;
    } while (!__atomic_compare_exchange_n(&SHM->utmp_user.next_session[uslot],
                                          &next_val,
                                          UTMP_MARK_DELETED(next_val),
                                          false,
                                          __ATOMIC_RELEASE,
                                          __ATOMIC_ACQUIRE));

    clean_marked_nodes(unum);
}

void
add_to_utmp_user(int uslot, int unum)
{
    if (!SHM || !VALID_USHM_ENTRY(uslot) || unum <= 0 || unum > MAX_USERS)
        return;

    int old_uid = __atomic_load_n(&SHM->utmp_user.session_user[uslot], __ATOMIC_ACQUIRE);
    if (old_uid > 0 && old_uid <= MAX_USERS) {
        remove_from_utmp_user(uslot, old_uid);
    }

    __atomic_store_n(&SHM->utmp_user.session_user[uslot], unum, __ATOMIC_RELEASE);
    clean_marked_nodes(unum);

    int old_head;
    do {
        old_head = __atomic_load_n(&SHM->utmp_user.user_head[unum], __ATOMIC_ACQUIRE);
        __atomic_store_n(&SHM->utmp_user.next_session[uslot], UTMP_ENCODE_NEXT(old_head), __ATOMIC_RELEASE);
    } while (!__atomic_compare_exchange_n(&SHM->utmp_user.user_head[unum],
                                          &old_head,
                                          uslot,
                                          false,
                                          __ATOMIC_RELEASE,
                                          __ATOMIC_ACQUIRE));
}

int
utmp_apply_user(int unum, int (*callback)(userinfo_t *uentp, void *arg), void *arg)
{
    if (!SHM || unum <= 0 || unum > MAX_USERS)
        return 0;
    int uslot = __atomic_load_n(&SHM->utmp_user.user_head[unum], __ATOMIC_ACQUIRE);
    for (int count = 0; count < USHM_SIZE && VALID_USHM_ENTRY(uslot); count++) {
        int next_val = __atomic_load_n(&SHM->utmp_user.next_session[uslot], __ATOMIC_ACQUIRE);
        int next_slot = UTMP_DECODE_SLOT(next_val);
        if (!UTMP_IS_DELETED(next_val)) {
            userinfo_t *uentp = &SHM->uinfo[uslot];
            if (uentp->uid == unum && uentp->pid > 0) {
                int ret = callback(uentp, arg);
                if (ret != 0)
                    return ret;
            }
        }
        uslot = next_slot;
    }
    return 0;
}
static int
_find_nth_cb(userinfo_t *uentp, void *arg)
{
    struct {
        int n;
        userinfo_t *res;
    } *ctx = arg;
    if (uentp->mode == DEBUGSLEEPING)
        return 0;
    if (--ctx->n == 0) {
        ctx->res = uentp;
        return 1;
    }
    return 0;
}

userinfo_t *
search_ulistn(int uid, int unum)
{
    if (unum <= 0)
        return NULL;
    struct {
        int n;
        userinfo_t *res;
    } ctx = { unum, NULL };
    utmp_apply_user(uid, _find_nth_cb, &ctx);
    return ctx.res;
}

userinfo_t *
utmp_find(int unum)
{
    return search_ulistn(unum, 1);
}

userinfo_t *
search_ulist_userid(const char *userid)
{
    if (!userid || !*userid)
        return NULL;
    int unum = searchuser(userid, NULL);
    if (unum <= 0)
        return NULL;
    return search_ulist(unum);
}

/*
 * section - utmp snapshot
 */
static int
cmputmp_snapshot_userid(const void *i, const void *j)
{
    int uslot_a = *(const int *)i;
    int uslot_b = *(const int *)j;
    return strcasecmp(SHM->uinfo[uslot_a].userid, SHM->uinfo[uslot_b].userid);
}

static int
cmputmp_snapshot_from(const void *i, const void *j)
{
    int uslot_a = *(const int *)i;
    int uslot_b = *(const int *)j;
    return memcmp(&(SHM->uinfo[uslot_a].from_ip), &(SHM->uinfo[uslot_b].from_ip),
                  sizeof(SHM->uinfo[0].from_ip));
}

int
refresh_utmp_snapshot(int *slots)
{
    if (!slots || !SHM)
        return 0;
    int count = 0;
    for (int i = 0; i < USHM_SIZE; i++) {
        if (SHM->uinfo[i].pid > 0 && SHM->uinfo[i].userid[0])
            slots[count++] = i;
    }
    return count;
}

int *
get_utmp_snapshot(int *out_count)
{
    int *slots = (int *)mmap(NULL, sizeof(int) * USHM_SIZE,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (slots == MAP_FAILED) {
        if (out_count)
            *out_count = 0;
        return NULL;
    }
    int count = refresh_utmp_snapshot(slots);
    if (out_count)
        *out_count = count;
    return slots;
}

void
sort_utmp_snapshot(int *slots, int count, int sort_type)
{
    if (!slots || count <= 1 || !SHM)
        return;
    if (sort_type == UTMP_SORT_USERID)
        qsort(slots, count, sizeof(int), cmputmp_snapshot_userid);
    else if (sort_type == UTMP_SORT_FROM)
        qsort(slots, count, sizeof(int), cmputmp_snapshot_from);
}

void
free_utmp_snapshot(int *slots)
{
    if (slots)
        munmap(slots, sizeof(int) * USHM_SIZE);
}

/*
 * section - money cache
 */
int
setumoney(int uid, int money)
{
    if (uid <= 0 || uid > MAX_USERS)
        return -1;
    __atomic_store_n(&SHM->money[uid - 1], money, __ATOMIC_RELEASE);
    passwd_update_money(uid);
    return money;
}

int
deumoney(int uid, int money)
{
    if (uid <= 0 || uid > MAX_USERS) {
	fprintf(stderr, "internal error: deumoney(%d, %d)\r\n", uid, money);
	return -1;
    }

    int old_m, new_m;
    do {
        old_m = __atomic_load_n(&SHM->money[uid - 1], __ATOMIC_ACQUIRE);
        if (money < 0 && old_m < -money)
            new_m = 0;
        else
            new_m = old_m + money;
    } while (!__atomic_compare_exchange_n(&SHM->money[uid - 1], &old_m, new_m,
                                          false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));

    passwd_update_money(uid);
    return new_m;
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
    for (i = 0; i < MAX_BOARD; ++i)
        setbottomtotal(i + 1);

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

    if (--bid < 0 || bid >= MAX_BOARD)
	return;
    if (SHM->Bbusystate)
	return;

    time4_t zero = 0;
    time4_t cur_time = COMMON_TIME;
    if (!__atomic_compare_exchange_n(&SHM->busystate_b[bid], &zero, cur_time,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (time4_diff(cur_time, zero) < 10)
            return;
        if (!__atomic_compare_exchange_n(&SHM->busystate_b[bid], &zero, cur_time,
                                         false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }

    bhdr = bcache;
    bhdr += bid;
    if ((fd = open(FN_BOARD, O_RDONLY)) >= 0) {
	lseek(fd, (off_t) (bid * sizeof(boardheader_t)), SEEK_SET);
	read(fd, bhdr, sizeof(boardheader_t));
	close(fd);
    }
    __atomic_store_n(&SHM->busystate_b[bid], 0, __ATOMIC_RELEASE);

    buildBMcache(bid + 1); /* XXXbid */
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

int
getbottomtotal(int bid)
{
    if (bid < 1 || bid > MAX_BOARD)
        return 0;
    return SHM->n_bottom[bid - 1];
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
    if (raw_count == 0)
        return 0;

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
    if (bid < 1 || bid > MAX_BOARD)
        return;
    boardheader_t *bh = getbcache(bid);
    if (!bh->brdname[0])
        return;
    int n = 0;
    for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
        if (aidu_raw(bh->bottom[i]) != 0)
            n++;
    }
    SHM->n_bottom[bid - 1] = n;
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
    if (bid <= 0 || bid > MAX_BOARD)
        return;
    assert(0 <= bid - 1 && bid - 1 < MAX_BOARD);
    int old_val;
    do {
        old_val = __atomic_load_n(&SHM->total[bid - 1], __ATOMIC_ACQUIRE);
        if (old_val <= 0)
            return;
        int new_val = old_val + delta;
        if (new_val < 0)
            new_val = 0;
        if (__atomic_compare_exchange_n(&SHM->total[bid - 1], &old_val, new_val,
                                        false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
            break;
    } while (1);
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
    int zero = 0;
    int my_pid = getpid();
    if (my_pid <= 0)
        my_pid = 1;

    if (!__atomic_compare_exchange_n(&SHM->Pbusystate, &zero, my_pid,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (zero > 0 && kill(zero, 0) == -1 && errno == ESRCH) {
            if (!__atomic_compare_exchange_n(&SHM->Pbusystate, &zero, my_pid,
                                             false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
        } else {
            return;
        }
    }

    fileheader_t    item, subitem;
    char            pbuf[256], buf[256];
    FILE           *fp, *fp1, *fp2;
    int             id, aggid, rawid;
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
	__atomic_store_n(&SHM->Pbusystate, 0, __ATOMIC_RELEASE);
}

void
resolve_garbage(void)
{
    if (time4_lt(SHM->Puptime, SHM->Ptouchtime)) {
	reload_pttcache();
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
