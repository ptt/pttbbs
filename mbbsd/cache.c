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
    SHM->UTMPneedupdate = 1;
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

// Writes the board header of slot bid (.BRD and the board cache) through
// BoF, which also tells the backend. 0, or -1.
int
save_board_header(int bid, const boardheader_t *bh)
{
    bof_actor_t me = mbbsd_user_actor();
    bof_edit_community_t req = { .bbs_id = bid, .bbs_header = bh };
    return bof_edit_community(&me, &req) == BOF_OK ? 0 : -1;
}

// The same for a new board: into the free slot bid, or with bid 0 after
// the last slot. 0, or -1.
int
create_board_header(int bid, const boardheader_t *bh)
{
    bof_actor_t me = mbbsd_user_actor();
    bof_create_community_t req = { .bbs_id = bid, .bbs_header = bh };
    return bof_create_community(&me, &req, NULL) == BOF_OK ? 0 : -1;
}

void
invalid_board_permission_cache(const char *board) {
    boardheader_t *bp = NULL;
    int bid = getbnum(board);

    assert(0<=bid-1 && bid-1<MAX_BOARD);
    bp = getbcache(bid);
    bp->perm_reload = now;
    save_board_header(bid, bp);
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

/* Whether board bid's moderators include the user. */
int
is_board_moderator(int bid) /* bid starts from 1 */
{
    assert(0<=bid-1 && bid-1<MAX_BOARD);

    // quick check
    if (!HasUserPerm(PERM_BASIC) || !currutmp->uid || currutmp->uid == -1)
	return 0;
    // reject user who haven't complete registration.
    if (!HasBasicUserPerm(PERM_LOGINOK))
	return 0;
    const boardheader_t *bp = getbcache(bid);
    if (bp) {
        for (int m = 0; m < MAX_BMs; m++) {
            if (bp->moderators[m].unum > 0 && bp->moderators[m].unum == currutmp->uid) {
                if (bp->moderators[m].firstlogin == 0 || bp->moderators[m].firstlogin == cuser.firstlogin) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

// TODO move this to board.c
const char *
muted_msg(const char *bname)
{
    // static is bad, but this is faster...
    static char mute_msg[40];
    time4_t expire = is_muted_by_board(bname);
    if (time4_gt(expire, now)) {
        SNPRINTF(mute_msg, "使用者不可發言(尚有%d天)",
                time4_days_remaining(expire, now));
        return mute_msg;
    }
    return NULL;
}

// Why the current user may not post in board bname (NULL if they may),
// by the rules bof keeps for any user interface.
const char *
postperm_msg(const char *bname)
{
    bof_person_t me = mbbsd_person();
    bof_community_t c;
    bof_bbs_rule_t rule;

    if (bof_get_community(bname, &c) != BOF_OK)
	return "看板不存在";
    if (bof_bbs_post_permission(&me, &c, &rule) == BOF_OK)
        return NULL;
    switch (rule) {
    case BOF_BBS_RULE_READ_ONLY:
	return "看板唯讀";
    case BOF_BBS_RULE_MUTED: {
        const char *msg = muted_msg(bname);
        return msg ? msg : "使用者不可發言";
    }
    case BOF_BBS_RULE_NOT_VERIFIED:
	return (PERM_POST == PERM_LOGINOK) ? "未完成認證" : "無發文權限";
    case BOF_BBS_RULE_RESTRICTED:
	return "看板限制發文";
    case BOF_BBS_RULE_UNPAID_TICKET:
	return "罰單未繳";
    case BOF_BBS_RULE_LEVEL:
	return "未達看板要求權限";
    default:
	return "看板不存在";
    }
}

// TODO move this to board.c
int
haspostperm(const char *bname)
{
    return postperm_msg(bname) == NULL ? 1 : 0;
}

