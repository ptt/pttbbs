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
/* Ptt:�o�̥[�W hash �[����Ū� utmp */
    register int    i;
    register userinfo_t *uentp;
    unsigned int p = StringHash(up->userid) % USHM_SIZE;
    for (i = 0; i < USHM_SIZE; i++, p++) {
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

int
count_logins(int uid, int show)
{
    register int    i = 0, j, start = 0, end = SHM->UTMPnumber - 1, count;
    int *ulist;
    userinfo_t *u;
    if (end == -1)
	return 0;
    ulist = SHM->sorted[SHM->currsorted][7];
    for (i = ((start + end) / 2);; i = (start + end) / 2) {
	u = &SHM->uinfo[ulist[i]];
	j = uid - u->uid;
	if (!j) {
	    for (; i > 0 && uid == SHM->uinfo[ulist[i - 1]].uid; i--);
							/* ����Ĥ@�� */
	    for (count = 0; (ulist[i + count] &&
		    (u = &SHM->uinfo[ulist[i + count]]) &&
		    uid == u->uid); count++) {
		if (show)
		    prints("(%d) �ثe���A��: %-17.16s(�Ӧ� %s)\n",
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

void
setutmpmode(unsigned int mode)
{
    if (currstat != mode)
	currutmp->mode = currstat = mode;
    /* �l�ܨϥΪ� */
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
    substitute_record(fn_board, bp, sizeof(boardheader_t), bid);
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
#ifdef USE_NEW_BAN_SYSTEM
    // static is bad, but this is faster...
    static char ban_msg[STRLEN];
    time4_t expire = is_banned_by_board(bname);
    if (expire > now) {
        sprintf(ban_msg, "�ϥΪ̤��i�o��(�|��%d��)",
                ((expire - now) / DAY_SECONDS) +1);
        return ban_msg;
    }
#else
    char buf[PATHLEN];
    setbfile(buf, bname, fn_water);
    if (file_exist_record(buf, cuser.userid))
        return "�ϥΪ̤��i�o��";
#endif
    return NULL;
}

// TODO move this to board.c
const char *
postperm_msg(const char *bname)
{
    register int    i;
    boardheader_t   *bp = NULL;

    if (!(i = getbnum(bname)))
	return "�ݪO���s�b";

    // system internal read only boards (no matter what attribute/flag set)
    if (is_readonly_board(bname))
	return "�ݪO��Ū";

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

    if (bp->brdattr & BRD_NEEDS_SMS_VERIFICATION)
        if (!is_user_sms_verified())
            return "�ݪO���w�w���ҥx�W�����o/����";

    if (bp->brdattr & BRD_GUESTPOST)
        return NULL;

    // XXX should we enable this?
#if 0
    // always allow post for BM
    if (is_BM_cache(i))
	return NULL;
#endif

    if (!HasUserPerm(PERM_POST))
	return (PERM_POST == PERM_LOGINOK) ? "�������{��" :
            "�L�o���v��";

    /* ���K�ݪO�S�O�B�z */
    if (bp->brdattr & BRD_HIDE)
	return NULL;
    else if (bp->brdattr & BRD_RESTRICTEDPOST &&
	    !is_hidden_board_friend(i, usernum))
	return "�ݪO����o��";

    if (HasUserPerm(PERM_VIOLATELAW))
    {
	// �b�@�檺�Q�׬����O�i�H�o��
	if (bp->level & PERM_VIOLATELAW)
	    return NULL;
	else
	    return "�@�楼ú";
    }

    if (!(bp->level & ~PERM_POST))
	return NULL;
    if (!HasUserPerm(bp->level & ~PERM_POST))
	return "���F�ݪO�n�D�v��";
    return NULL;
}

// TODO move this to board.c
int
haspostperm(const char *bname)
{
    return postperm_msg(bname) == NULL ? 1 : 0;
}

