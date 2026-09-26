#include "bbs.h"

#ifdef _BBS_UTIL_C_
#error sorry, mbbsd/cache.c does not support utility mode anymore. please use libcmbbs instead.
#endif

/*
 * section - user & utmp cache
 */

int
getuser(const char *userid, userec_t *xuser)
{
    int             uid;

    if ((uid = searchuser(userid, NULL))) {
	passwd_sync_query(uid, xuser);
	xuser->money = moneyof(uid);
    }
    return uid;
}

void
getnewutmpent(const userinfo_t * up)
{
/* Ptt:這裡加上 hash 觀念找空的 utmp */
    register int    i;
    register userinfo_t *uentp;
    unsigned int p = StringHash(up->userid) % USHM_SIZE;
    for (i = 0; i < USHM_SIZE; i++, p++) {
	if (p == USHM_SIZE)
	    p = 0;
	uentp = &(SHM->uinfo[p]);
	pid_t zero = 0;
	if (__atomic_compare_exchange_n(&uentp->pid, &zero, up->pid,
	                                false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
	    memcpy(uentp, up, sizeof(userinfo_t));
	    uentp->pid = up->pid;
	    currutmp = uentp;
	    add_to_utmp_user(p, uentp->uid);
	    return;
	}
    }
    exit(1);
}

int
apply_ulist(int (*fptr) (const userinfo_t *))
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

typedef struct {
    int show;
    int count;
} count_logins_ctx_t;

static int
_count_logins_cb(userinfo_t *u, void *arg)
{
    count_logins_ctx_t *ctx = (count_logins_ctx_t *)arg;
    if (u->mode == DEBUGSLEEPING)
        return 0;
    if (ctx->show) {
        prints("(%d) 目前狀態為: %-17.16s(來自 %s)\n",
               ctx->count + 1, modestring(u, 0), u->from);
    }
    ctx->count++;
    return 0;
}

int
count_logins(int uid, int show)
{
    count_logins_ctx_t ctx = { show, 0 };
    utmp_apply_user(uid, _count_logins_cb, &ctx);
    return ctx.count;
}

void
purge_utmp(userinfo_t * uentp)
{
    if (!uentp)
        return;
    logout_friend_online(uentp);
    int uslot = get_utmp_slot(uentp);
    int uid = uentp->uid;
    if (uslot >= 0 && uid > 0)
        remove_from_utmp_user(uslot, uid);
    memset(uentp, 0, sizeof(userinfo_t));
    SHM->UTMPneedsort = 1;
}

void
setutmpmode(unsigned int mode)
{
    if (currstat != mode)
	currutmp->mode = currstat = mode;
    /* 追蹤使用者 */
    if (HasUserPerm(PERM_LOGUSER)) {
	log_user("setutmpmode to %s(%d)\n", modestring(currutmp, 0), mode);
    }
}

unsigned int
getutmpmode(void)
{
    if (currutmp)
	return currutmp->mode;
    return currstat;
}

/*
 * section - board
 */

void
invalid_board_permission_cache(const char *board) {
    boardheader_t *bp = NULL;
    int bid = getbnum(board);

    assert(0<=bid-1 && bid-1<MAX_BOARD);
    bp = getbcache(bid);
    bp->perm_reload = now;
    substitute_record(FN_BOARD, bp, sizeof(boardheader_t), bid);
}


/* HasBoardPerm() in board.c... */
int
apply_boards(int (*func) (boardheader_t *))
{
    register int    i;
    register boardheader_t *bhdr;

    for (i = num_boards(), bhdr = bcache; i > 0; i--, bhdr++) {
	if (!(bhdr->brdattr & BRD_GROUPBOARD) && HasBoardPerm(bhdr) &&
	    (*func) (bhdr) == QUIT)
	    return QUIT;
    }
    return 0;
}

int
is_BM_cache(int bid) /* bid starts from 1 */
{
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    int *pbm = SHM->BMcache[bid-1];

    // XXX potential issue: (thanks for mtdas@ptt)
    //  buildBMcache use -1 as "none".
    //  some function may call is_BM_cache early
    //  without having currutmp->uid (maybe?)
    //  and may get BM permission accidentally.
    // quick check
    if (!HasUserPerm(PERM_BASIC) || !currutmp->uid || currutmp->uid == -1)
	return 0;
    // reject user who haven't complete registration.
    if (!HasBasicUserPerm(PERM_LOGINOK))
	return 0;
    // XXX hard coded MAX_BMs=4
    if( currutmp->uid == pbm[0] ||
	currutmp->uid == pbm[1] ||
	currutmp->uid == pbm[2] ||
	currutmp->uid == pbm[3] )
    {
	// auto enable BM permission
	if (!HasUserPerm(PERM_BM))
	    pwcuBitEnableLevel(PERM_BM);
	return 1;
    }
    return 0;
}

// TODO move this to board.c
const char *
banned_msg(const char *bname)
{
    // static is bad, but this is faster...
    static char ban_msg[40];
    time4_t expire = is_banned_by_board(bname);
    if (time4_gt(expire, now)) {
        SNPRINTF(ban_msg, "使用者不可發言(尚有%d天)",
                time4_days_remaining(expire, now));
        return ban_msg;
    }
    return NULL;
}

// TODO move this to board.c
const char *
postperm_msg(const char *bname)
{
    register int    i;
    boardheader_t   *bp = NULL;

    if (!(i = getbnum(bname)))
	return "看板不存在";

    // system internal read only boards (no matter what attribute/flag set)
    if (is_readonly_board(bname))
	return "看板唯讀";

    if (HasUserPerm(PERM_SYSOP))
	return NULL;

    do {
        const char *msg;
        if ((msg = banned_msg(bname)) != NULL)
            return msg;
    } while (0);

    if (!strcasecmp(bname, DEFAULT_BOARD))
	return NULL;

    assert(0<=i-1 && i-1<MAX_BOARD);
    bp = getbcache(i);

    if (bp->brdattr & BRD_GUESTPOST)
        return NULL;

    if (!HasUserPerm(PERM_POST))
	return (PERM_POST == PERM_LOGINOK) ? "未完成認證" :
            "無發文權限";

    /* 秘密看板特別處理 */
    if (bp->brdattr & BRD_HIDE)
	return NULL;
    else if (bp->brdattr & BRD_RESTRICTEDPOST &&
	    !is_hidden_board_friend(i, usernum))
	return "看板限制發文";

    if (HasUserPerm(PERM_VIOLATELAW))
    {
	// 在罰單的討論相關板可以發文
	if (bp->level & PERM_VIOLATELAW)
	    return NULL;
	else
	    return "罰單未繳";
    }

    if (!(bp->level & ~PERM_POST))
	return NULL;
    if (!HasUserPerm(bp->level & ~PERM_POST))
	return "未達看板要求權限";
    return NULL;
}

// TODO move this to board.c
int
haspostperm(const char *bname)
{
    return postperm_msg(bname) == NULL ? 1 : 0;
}

