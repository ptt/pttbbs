
#include "bbs.h"
#include "daemons.h"
#include <arpa/inet.h>

#ifdef EDITPOST_SMARTMERGE

#include "fnv_hash.h"
#define SMHASHLEN (64/8)

#endif // EDITPOST_SMARTMERGE

#define WHEREAMI_LEVEL	16

#define NEWIDPOST_LIMIT_DAYS (14)

static int recommend(int ent, fileheader_t * fhdr, const char *direct);
static int do_add_recommend(const char *direct, fileheader_t *fhdr,
                            int ent, const char *buf, int type);
static int view_postinfo(int ent, const fileheader_t * fhdr,
                         const char *direct, int crs_ln);

static int bnote_lastbid = -1; // �M�w�O�_�n��ܶi�O�e���� cache

// recommend/comment types
//  most people use recommendation just for one-line reply.
//  so we may change default to (RECTYPE_ARROW)= comment only.
//  however, the traditional behavior (which does not have
//  BRC info for 'new comments available') uses RECTYPE_GOOD.
enum {
    RECTYPE_GOOD,
    RECTYPE_BAD,
    RECTYPE_ARROW,

    RECTYPE_SIZE,
    RECTYPE_MAX     = RECTYPE_SIZE-1,
    RECTYPE_DEFAULT = RECTYPE_GOOD, // match traditional user behavior
};

/**
 * Test if the fhdr looks like really belong to user
 */
int
is_file_owner(const fileheader_t *fhdr, const userec_t *usr) {
    // XXX there are two known issues here:
    //  1. Anonymous post don't work anymore.
    //  2. People cross-posting (^X) very old post can't own it.
    // Since ptt.cc does not have anonymous boards anymore, these issues are in
    // low priority.  Sites using anonymous boards can fix on your own.
    if (strcmp(fhdr->owner, usr->userid) != EQUSTR)
        return 0;

    // deal with the format M.TIMESTAMP, return as 0 if unknown
    if (strlen(fhdr->filename) > 3) {
        time4_t ts = atoi(fhdr->filename + 2);
        if (ts && ts >= usr->firstlogin)
            return 1;
    }
    return 0;
}

int
is_file_owner_id(const fileheader_t *fhdr, const char *userid) {
    userec_t xuser;
    if (getuser(userid, &xuser) <= 0)
        return 0;
    return is_file_owner(fhdr, &xuser);
}

/* query money by fileheader pointer.
 * return <0 if money is not valid.
 */
int
query_file_money(const fileheader_t *pfh)
{
    fileheader_t hdr;

    if(	(currmode & MODE_SELECT) &&
	(pfh->multi.refer.flag) &&
	(pfh->multi.refer.ref > 0)) // really? not sure, copied from other's code
    {
	char genbuf[PATHLEN];

	/* it is assumed that in MODE_SELECT, currboard is selected. */
	setbfile(genbuf, currboard, FN_DIR);
	get_record(genbuf, &hdr, sizeof(hdr), pfh->multi.refer.ref);
	pfh = &hdr;
    }

    if(pfh->filemode & INVALIDMONEY_MODES || pfh->multi.money > MAX_POST_MONEY)
	return -1;

    return pfh->multi.money;
}

// lite weight version to update dir files
static int
modify_dir_lite(
	const char *direct, int ent, const char *fhdr_name, time4_t modified,
        const char *title, const char *owner, const char *date,
        char recommend, void *multi, uint8_t enable_modes, uint8_t disable_modes)
{
    // since we want to do 'modification'...
    int fd;
    off_t sz = dashs(direct);
    fileheader_t fhdr;

    // prevent black holes
    if (sz < (int)sizeof(fileheader_t) * (ent) ||
	    (fd = open(direct, O_RDWR)) < 0 )
	return -1;

    // also check if fhdr->filename is same.
    // let sz = base offset
    sz = (sizeof(fileheader_t) * (ent-1));
    if (lseek(fd, sz, SEEK_SET) < 0 ||
	read(fd, &fhdr, sizeof(fhdr)) != sizeof(fhdr) ||
	strcmp(fhdr.filename, fhdr_name) != 0)
    {
	close(fd);
	return -1;
    }

    // update records
    if (modified > 0)
	fhdr.modified = modified;
    if (enable_modes)
        fhdr.filemode |= enable_modes;
    if (disable_modes)
        fhdr.filemode &= ~disable_modes;
    if (title && *title)
	strlcpy(fhdr.title, title, sizeof(fhdr.title));
    if (owner && *owner)
	strlcpy(fhdr.owner, owner, sizeof(fhdr.owner));
    if (date && *date)
	strlcpy(fhdr.date, date, sizeof(fhdr.date));
    if (multi)
        memcpy(&fhdr.multi, multi, sizeof(fhdr.multi));

    if (recommend) {
	recommend += fhdr.recommend;
	if (recommend > MAX_RECOMMENDS) recommend = MAX_RECOMMENDS;
	else if (recommend < -MAX_RECOMMENDS) recommend = -MAX_RECOMMENDS;
	fhdr.recommend = recommend;
    }


    // PttLock(fd, sz, sizeof(fhdr), F_WRLCK);
    if (lseek(fd, sz, SEEK_SET) >= 0)
	write(fd, &fhdr, sizeof(fhdr));
    // PttLock(fd, sz, sizeof(fhdr), F_UNLCK);

    close(fd);
    return 0;
}

static void
check_locked(fileheader_t *fhdr)
{
    boardheader_t *bp = NULL;

    if (currstat == RMAIL)
	return;
    if (!currboard[0] || currbid <= 0)
	return;
    bp = getbcache(currbid);
    if (!bp)
	return;
    if (!(fhdr->filemode & FILE_SOLVED))
	return;
    if (!(fhdr->filemode & FILE_MARKED))
	return;
    syncnow();
    bp->SRexpire = now;

#ifdef ALERT_M_PLUS_L
    {
        VREFSCR scr;
        static char did_prompt = 0;

        if (did_prompt)
            return;

        scr = vscr_save();
        vs_hdr("���רð���^��");
        move(5, 0);
        outs("  �Ъ`�N�A���\\�� (m+L) �O�u���רòפ�^���v(�T�^�����)�A\n"
             "  �@�������O�u��w�峹(�ܰ�Ū)�v�C"
             "���P�u��w�峹�v�O�u�������i�Ϊ� ^E�C\n\n"
             "  �ҥH�פ�^����ϥΪ̤��i�ק惡�ɡC\n\n"
             "  ���ӧڭ̷|�Ҽ{�O�_�ӭק��� m+L ����w����O�A"
             "���u�������欰���|���ܡC\n\n"
             "  �ФŦA�����������u��w�� bug�v, ����\n\n"
             ANSI_COLOR(1;31)
             "  �t�~�A���\\��ثe���P�B���D�A"
             "�w�b�u�W���O�ͦb���}�ݪO�e�������|�^��/����C\n"
             ANSI_RESET
             );
        did_prompt = 1;
        pressanykey();
        vscr_restore(scr);
    }
#endif
}

/* hack for listing modes */
enum LISTMODES {
    LISTMODE_DATE = 0,
    LISTMODE_MONEY,
};
static char *listmode_desc[] = {
    "�� ��",
    "�� ��",
};
static int currlistmode = LISTMODE_DATE;

#define IS_LISTING_MONEY \
	(currlistmode == LISTMODE_MONEY || \
	 ((currmode & MODE_SELECT) && (currsrmode & RS_MONEY)))

void
anticrosspost(void)
{
    // probably failure here.
    if (HasUserPerm(PERM_ADMIN | PERM_MANAGER)) {
        vmsg("�t�Υi��~�P�z CP, ���קK�X�������_�u");
        exit(0);
    }

    log_filef("etc/illegal_money",  LOG_CREAT,
             ANSI_COLOR(1;33;46) "%s "
             ANSI_COLOR(37;45) "cross post �峹 "
             ANSI_COLOR(37) " %s" ANSI_RESET "\n",
             cuser.userid, Cdatelite(&now));

    post_violatelaw(cuser.userid, BBSMNAME "�t��ĵ��",
                    "Cross-post", "�@��B��");
    pwcuViolateLaw();
    mail_id(cuser.userid, "Cross-Post�@��",
	    "etc/crosspost.txt", BBSMNAME "ĵ���");
    // Delete all references from BN_ALLPOST, if available.
    delete_allpost(cuser.userid);
    kick_all(cuser.userid); // XXX: in2: wait for testing
    u_exit("Cross Post");
    exit(0);
}

/* Heat CharlieL */
int
save_violatelaw(void)
{
    char            buf[128], ok[3];
    int             day;

    setutmpmode(VIOLATELAW);
    clear();
    vs_hdr("ú�@�椤��");

    // XXX reload lots of stuff here?
    pwcuReload();
    if (!(cuser.userlevel & PERM_VIOLATELAW)) {
	vmsg("�A�S���Q�}�@��~~");
	return 0;
    }

    day =  cuser.vl_count*3 - (now - cuser.timeviolatelaw)/DAY_SECONDS;
    if (day > 0) {
        vmsgf("�̷ӹH�W����(%d), �A�ٻݭn�Ϭ� %d �Ѥ~��ú�@��",
              cuser.vl_count, day);
	return 0;
    }
    reload_money();
    if (cuser.money < (int)cuser.vl_count * 1000) {
	snprintf(buf, sizeof(buf),
                 ANSI_COLOR(1;31) "�o�O�A�� %d ���H�ϥ����k�W"
                 "����ú�X %d " MONEYNAME "�F���A�ثe�u�� %d �A�ƶq����!!"
                 ANSI_RESET, (int)cuser.vl_count, (int)cuser.vl_count * 1000,
                 cuser.money);
	mvouts(22, 0, buf);
	pressanykey();
	return 0;
    }
    move(5, 0);
    prints("�o�O�A�� %d ���H�k ����ú�X %d " MONEYNAME "\n\n",
	    cuser.vl_count, cuser.vl_count * 1000);
    outs(ANSI_COLOR(1;37) "�A�O�_�T�w�H�ᤣ�|�A�ǤF�H" ANSI_RESET "\n");

    if (!getdata(10, 0, "�T�w�ܡH[y/N]:", ok, sizeof(ok), LCECHO) ||
	ok[0] != 'y')
    {
	move(15, 0);
	outs(ANSI_COLOR(1;31) "���Q�I�ܡH �٬O�����D�n�� y �H\n"
	     "�оi���ݲM���t�ΰT�����n�ߺD�C\n" ANSI_RESET);
	pressanykey();
	return 0;
    }

    //Ptt:check one more time
    reload_money();
    if (cuser.money < (int)cuser.vl_count * 1000)
    {
	log_filef("log/violation", LOG_CREAT,
		"%s %s pay-violation error: race-conditionn hack?\n",
		Cdate(&now), cuser.userid);
	vmsg(MONEYNAME "��򩿵M�����F�H �չϴ��F�t�γQ�d��N��b���I");
	return 0;
    }

    pay(1000 * (int)cuser.vl_count, "ú�I�@�� (#%d)", cuser.vl_count);
    pwcuSaveViolateLaw();
    log_filef("log/violation", LOG_CREAT,
	    "%s %s pay-violation: $%d complete.\n",
	    Cdate(&now), cuser.userid, (int)cuser.vl_count*1000);

    vmsg("�@��w�I�A�Э��s�n�J�C");
    u_exit("save_violate");
    exit(0);
    return 0;
}

static time4_t  *board_note_time = NULL;

void
set_board(void)
{
    boardheader_t  *bp;

    bp = getbcache(currbid);
    if (!HasBoardPerm(bp)) {
	vmsg("access control violation, exit");
	u_exit("access control violation!");
	exit(-1);
    }

    if (BoardPermNeedsSysopOverride(bp))
 	vmsg("�i�J���g���v�ݪO");

    board_note_time = &bp->bupdate;

    if (!does_board_have_public_bm(bp)) {
	strcpy(currBM, "�x�D��");
    } else {
	/* calculate with other title information */
	int l = 0;

	snprintf(currBM, sizeof(currBM), "�O�D:%s", bp->BM);
	/* title has +7 leading symbols */
	l += strlen(bp->title);
	if(l >= 7)
	    l -= 7;
	else
	    l = 0;
	l += 8 + strlen(currboard); /* trailing stuff */
	l += strlen(bp->brdname);
	l = t_columns - l -strlen(currBM);

#ifdef _DEBUG
	{
	    char buf[PATHLEN];
	    sprintf(buf, "title=%d, brdname=%d, currBM=%d, t_c=%d, l=%d",
		    strlen(bp->title), strlen(bp->brdname),
		    strlen(currBM), t_columns, l);
	    vmsg(buf);
	}
#endif

	if(l < 0 && ((l += strlen(currBM)) > 7))
	{
	    currBM[l] = 0;
	    currBM[l-1] = currBM[l-2] = '.';
	}
    }

    /* init basic perm, but post perm is checked on demand */
    currmode = (currmode & (MODE_DIRTY | MODE_GROUPOP)) | MODE_STARTED;
    if (!HasUserPerm(PERM_NOCITIZEN) &&
        (HasUserPerm(PERM_BOARD) ||
         is_BM_cache(currbid) ||
         (bp->BM[0] <= ' ' && GROUPOP()))) {
	currmode |= MODE_BOARD;
    }
}

#ifdef QUERY_ARTICLE_URL


static int
IsBoardForWeb(const boardheader_t *bp) {
    if (!bp || !IS_OPENBRD(bp))
        return 0;
    if (strcmp(bp->brdname, BN_ALLPOST) == 0)
	return 0;
    return 1;
}

static int
GetWebUrl(const boardheader_t *bp, const fileheader_t *fhdr, char *buf,
          size_t szbuf)
{
    const char *folder = bp->brdname, *fn = fhdr->filename, *ext = ".html";

#ifdef USE_AID_URL
    char aidc[32] = "";
    aidu_t aidu = fn2aidu(fhdr->filename);
    if (!aidu)
	return 0;

    aidu2aidc(aidc, aidu);
    fn = aidc;
    ext = "";
#endif

    if (!fhdr || !*fhdr->filename || *fhdr->filename == 'L' ||
        *fhdr->filename == '.')
	return 0;

    return snprintf(buf, szbuf, URL_PREFIX "/%s/%s%s", folder, fn, ext);
}

#endif // QUERY_ARTICLE_URL

// post �峹��������O
int IsFreeBoardName(const char *brdname)
{
    if (strcmp(brdname, BN_TEST) == 0)
	return 1;
    if (strcmp(brdname, BN_ALLPOST) == 0)
	return 1;
    // since DEFAULT_BOARD is allowed to post without
    // special permission, we should not give rewards.
    if (strcasecmp(brdname, DEFAULT_BOARD) == 0)
	return 1;
    return 0;
}

int
CheckModifyPerm(const char **preason)
{
    static time4_t last_chk_time = 0x0BAD0BB5; /* any magic number */
    static int last_board_index = 0; /* for speed up */
    static const char *last_reason = NULL;
    int valid_index = 0;
    boardheader_t *bp = NULL;

    assert(preason);

    // check if my own permission is changed.
    if (ISNEWPERM(currutmp))
    {
#ifdef DEBUG
	log_filef("log/newperm.log", LOG_CREAT,
		"%-13s: reloaded perm %s\n",
		cuser.userid, Cdate(&now));
#endif
	currmode &= ~MODE_POSTCHECKED;
	pwcuReload();
	// XXX let pwcuReload handle NEWPERM?
	CLEAR_ALERT_NEWPERM(currutmp);
    }

    if (currmode & MODE_POSTCHECKED)
    {
        // if board is different, rebuild cache
        if (currbid != last_board_index) {
#ifdef DEBUG
            log_filef("log/reload_board_perm.log", LOG_CREAT,
                      "%-13s: reload board perm (curr=%d, last=%d)\n",
                      cuser.userid, currbid, last_board_index);
#endif
            currmode &= ~MODE_POSTCHECKED;
            last_board_index = 0;
        }

	/* checked? let's check if perm reloaded or board changed*/
	if (last_board_index < 1 || last_board_index > SHM->Bnumber)
	{
	    /* invalid board index, refetch. */
	    last_board_index = getbnum(currboard);
	    valid_index = 1;
	}
	assert(0<=last_board_index-1 && last_board_index-1<MAX_BOARD);
	bp = getbcache(last_board_index);

	if(bp->perm_reload != last_chk_time)
	    currmode &= ~MODE_POSTCHECKED;
    }

    if (!(currmode & MODE_POSTCHECKED))
    {
	if(!valid_index)
	{
	    last_board_index = getbnum(currboard);
	    bp = getbcache(last_board_index);
	}
	last_chk_time = bp->perm_reload;
	currmode |= MODE_POSTCHECKED;

	// vmsg("reload board postperm");

        last_reason = postperm_msg(currboard);
        *preason = last_reason;

	if (last_reason) {
            currmode &= ~MODE_POST;
            return 0;
        } else {
	    currmode |= MODE_POST;
	    return 1;
	}
    }
    *preason = last_reason;
    return (currmode & MODE_POST);
}

/* check post perm on demand, no double checks in current board
 * currboard MUST be defined!
 * XXX can we replace currboard with currbid ? */
int CheckPostPerm2(const char **preason) {
    const char *garbage = NULL;
    if (!preason)
        preason = &garbage;

    if (currmode & MODE_DIGEST) {
        *preason = "��K�L�k�o��";
	return 0;
    }
    return CheckModifyPerm(preason);
}

int
CheckPostPerm(void)
{
    return CheckPostPerm2(NULL);
}


char* get_board_restriction_reason(int bid, size_t sz_msg, char *msg)
{
    boardheader_t *bp;
    if (HasUserPerm(PERM_SYSOP))
	return NULL;
    assert(0<=bid-1 && bid-1<MAX_BOARD);

    // XXX currmode �O�ثe�ݪO���O bid...
    if (is_BM_cache(bid))
	return NULL;
    bp = getbcache(bid);

    return get_restriction_reason(
        cuser.numlogindays, cuser.badpost,
        bp->post_limit_logins, bp->post_limit_badpost,
        sz_msg, msg);
}

int CheckPostRestriction(int bid) {
    char garbage[256];
    return get_board_restriction_reason(bid, sizeof(garbage), garbage) == NULL;
}


static void
readtitle(void)
{
    boardheader_t  *bp;
    char    *brd_title;
    char     buf[32];

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);

    if(bp->bvote != 2 && bp->bvote)
	brd_title = "���ݪO�i��벼��";
    else
	brd_title = bp->title + 7;

    showtitle(currBM, brd_title);
    outs("[��]���} [��]�\\Ū [Ctrl-P]�o��峹 [d]�R�� [z]��ذ� [i]�ݪO��T/�]�w [h]����\n");
    buf[0] = 0;

#ifdef USE_COOLDOWN
    if (bp->brdattr & BRD_COOLDOWN)
        snprintf(buf, sizeof(buf), "[�R]");
    else
#endif
    {
        // nuser is not real-time updated (maintained by utmpsort), so let's
        // make some calibration here. It's minimal value is one because the
        // user IS reading it.
        int nuser = SHM->bcache[currbid - 1].nuser;
        if (nuser < 1) nuser = 1;
        snprintf(buf, sizeof(buf), "�H��:%d ", nuser);
    }

    vbarf(ANSI_REVERSE "   �s��    %s �@  ��       ��  ��  ��  �D\t%s ",
	    IS_LISTING_MONEY ? listmode_desc[LISTMODE_MONEY] : listmode_desc[currlistmode],
	    buf);
}

static int
is_tn_announce(const char *title)
{
    return strncmp(title, TN_ANNOUNCE, strlen(TN_ANNOUNCE)) == 0;
}

static int
is_tn_allowed(const char *title)
{
#ifdef ALLOW_FREE_TN_ANNOUNCE
    return 1;
#else
    // TN_ANNOUNCE is prohibited for non-BMs
    if ((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP) ||
	HasUserPerm(PERM_ACCOUNTS | PERM_BOARD | PERM_BBSADM |
		    PERM_VIEWSYSOP| PERM_POLICE_MAN))
	return 1;

    // Note: ���� subgroup op ���P�w�ثe�]�O�@���V - �p�ժ��n�q�ۤv������
    // �i�h�~�|�� GROUPOP(). ���L�ϥ��p�ժ���s�ժ����H�S����h�A�N�}��L��
    // always �i�H�ϥ� TN_ANNOUNCE �a�C
    if (HasUserPerm(PERM_SYSSUPERSUBOP) ||
	HasUserPerm(PERM_SYSSUBOP))
	return 1;

    if (is_tn_announce(title))
	return 0;
    return 1;
#endif
}

static void
tn_safe_strip(char *title)
{
    if (is_tn_allowed(title))
	return;
    assert(is_tn_announce(title));
    memmove(title, title + strlen(TN_ANNOUNCE),
	    strlen(title) - strlen(TN_ANNOUNCE)+1);
}

static void
readdoent(int num, fileheader_t * ent)
{
    int  type = ' ', title_type = SUBJECT_NORMAL;
    const char *title;
    char *mark, color, special = 0, isonline = 0, recom[8];
    char *typeattr = "";
    char isunread = 0, oisunread = 0;
    int w = 0;
    int const_title = 0;

#ifdef SAFE_ARTICLE_DELETE
    // TODO maybe we should also check .filename because admin can't change that
    char iscorpse = (ent->owner[0] == '-') && (ent->owner[1] == 0);

    if (iscorpse) {
#ifdef COLORIZED_SAFEDEL
	// quick display, but lack of recommend counter...
	prints("%7d    ", num);
	outs(ANSI_COLOR(1;30));
	prints("%-6.5s", ent->date);
	prints("%-13.12s", ent->owner);
	prints("�� %-.*s" ANSI_RESET "\n",
		t_columns-34, ent->title);
	return;
#else // COLORIZED_SAFEDEL
        oisunread = isunread = 0;
#endif // COLORIZED_SAFEDEL
    } else
#endif // SAFE_ARTICLE_DELETE
    oisunread = isunread =
	brc_unread(currbid, ent->filename, ent->modified);

    // modified tag
    if (isunread == 2)
    {
	// ignore unread, if user doesn't want to show it.
	if (HasUserFlag(UF_NO_MODMARK))
	{
	    oisunread = isunread = 0;
	}
	// if user wants colored marks, use 'read' marks
	else if (HasUserFlag(UF_COLORED_MODMARK))
	{
	    isunread = 0;
	    typeattr = ANSI_COLOR(36);
	}
    }

    // handle 'type'
    type = isunread ? '+' : ' ';
    if (isunread == 2) type = '~';

    if ((currmode & MODE_BOARD) && (ent->filemode & FILE_DIGEST))
    {
	type = (type == ' ') ? '*' : '#';
    }
    else if (ent->filemode & FILE_MARKED) // marks should be visible to everyone.
    {
	if(ent->filemode & FILE_SOLVED)
	    type = '!';
	else if (isunread == 0)
	    type = 'm';
	else if (isunread == 1)
	    type = 'M';
	else if (isunread == 2)
	    type = '=';
    }
    else if (ent->filemode & FILE_SOLVED)
    {
	type = (type == ' ') ? 's': 'S';
    }

    // tag should override everything
    if ((currmode & MODE_BOARD) || HasBasicUserPerm(PERM_LOGINOK))
    {
	if (FindTaggedItem(ent))
	    type = 'D';
    }

    // the only special case: ' ' with isunread == 2,
    // change to '+' with gray attribute.
    if (type == ' ' && oisunread == 2)
    {
	typeattr = ANSI_COLOR(1;30);
	type = '+';
    }

    title =  subject_ex(ent->title, &title_type);

    if (ent->filename[0] == 'L')
        title_type = SUBJECT_LOCKED;

#ifdef SAFE_ARTICLE_DELETE
    if (iscorpse)
	color = '0', mark = "��";
        // color = '0', mark = "��";
    else
#endif
    if (ent->filemode & FILE_VOTE)
	color = '2', mark = "��";
    else switch(title_type) {
        case SUBJECT_REPLY:
            color = '3', mark = "R:";
            break;
        case SUBJECT_FORWARD:
            color = '6', mark = "��";
            break;
        case SUBJECT_LOCKED:
            color = '5', mark = "��";
            break;
        case SUBJECT_NORMAL:
        default:
            color = '1', mark = "��";
            break;
    }

    // TN_ANNOUNCE: [���i]
    if (title[0] == '[' && is_tn_announce(title))
	special = 1;

    isonline = query_online(ent->owner);

    if (title_type == SUBJECT_LOCKED)
        strcpy(recom,"0m--");
    else if(ent->recommend >= MAX_RECOMMENDS)
	  strcpy(recom,"1m�z");
    else if(ent->recommend>9)
	  sprintf(recom,"3m%2d",ent->recommend);
    else if(ent->recommend>0)
	  sprintf(recom,"2m%2d",ent->recommend);
    else if(ent->recommend <= -MAX_RECOMMENDS)
	  sprintf(recom,"0mXX");
    else if(ent->recommend<-10)
	  sprintf(recom,"0mX%d",-ent->recommend);
    else strcpy(recom,"0m  ");

    /* start printing */
    if (ent->filemode & FILE_BOTTOM) {
        if (HasUserFlag(UF_MENU_LIGHTBAR))
            outs("  " ANSI_COLOR(33) "  �� " ANSI_RESET);
        else
            outs("  " ANSI_COLOR(1;33) "  �� " ANSI_RESET);
    }
    else
	/* recently we found that many boards have >10k articles,
	 * so it's better to use 5+2 (2 for cursor marker) here.
	 * XXX if we are in big term, enlarge here.
	 */
	prints("%7d", num);

    if (HasUserFlag(UF_MENU_LIGHTBAR))
        prints(" %s%c" ESC_STR "[0;3%4.4s" ANSI_RESET,
               typeattr, type, recom);
    else
        prints(" %s%c" ESC_STR "[0;1;3%4.4s" ANSI_RESET,
               typeattr, type, recom);

    if(IS_LISTING_MONEY)
    {
	int m = query_file_money(ent);
	if(m < 0)
	    outs(" ---- ");
	else
	    prints("%5d ", m);
    }
    else // LISTMODE_DATE
    {
#ifdef COLORDATE
	prints(ANSI_COLOR(%d) "%-6.5s" ANSI_RESET,
		(ent->date[3] + ent->date[4]) % 7 + 31, ent->date);
#else
	prints("%-6.5s", ent->date);
#endif
    }

    // print author
    if(isonline) {
        if (HasUserFlag(UF_MENU_LIGHTBAR))
            outs(ANSI_COLOR(36));
        else
            outs(ANSI_COLOR(1));
    }
    prints("%-13.12s", ent->owner);
    if(isonline) outs(ANSI_RESET);

    // TODO calculate correct width. �e������ 33 �Ӧr���C */
    w = t_columns - 34; /* 33+1, for trailing one more space */

    // print subject prefix
    ent->title[sizeof(ent->title)-1] = 0;
    if (strcmp(currtitle, title) == 0) {
        if (HasUserFlag(UF_MENU_LIGHTBAR))
            prints(ANSI_COLOR(3%c), color);
        else
            prints(ANSI_COLOR(1;3%c), color);
        outs(mark);
        outc(' ');
        special = 1;
    } else {
        outs(mark);
        outc(' ');
        if (special) {
            int len_announce = strlen(TN_ANNOUNCE);
            if (!HasUserFlag(UF_MENU_LIGHTBAR))
                outs(ANSI_COLOR(1));
            outs(TN_ANNOUNCE);
            outs(ANSI_RESET);
            title += len_announce;
            w -= len_announce;
            special = 0;
        }
    }

    // strip unsafe characters
    if (!const_title)
        strip_nonebig5((unsigned char*)title, INT_MAX);

    // print subject, bounded by w.
    if ((int)strlen(title) > w) {
        if (DBCS_Status(title, w-2) == DBCS_TRAILING)
            w--;
        outns(title, w-2);
        outs("�K");
    } else {
        outs(title);
    }

    if (special)
        outs(ANSI_RESET "\n");
    else
       outc('\n');
}

int
whereami(void)
{
    boardheader_t  *bh, *p[WHEREAMI_LEVEL];
    char category[sizeof(bh->title)] = "", *pcat;
    int             i, j;
    int bid = currbid;
    int total_boards;

    if (!bid)
	return 0;

    move(1, 0);
    clrtobot();
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    bh = getbcache(bid);
    p[0] = bh;
    for (i = 0, total_boards = num_boards();
         i+1 < WHEREAMI_LEVEL && p[i]->parent>1 && p[i]->parent < total_boards;
         i++)
	p[i + 1] = getbcache(p[i]->parent);
    j = i;
    prints("�ڦb��?\n%-40.40s %.13s\n", p[j]->title + 7, p[j]->BM);
    for (j--; j >= 0; j--)
	prints("%*s %-13.13s %-37.37s %.13s\n", (i - j) * 2, "",
	       p[j]->brdname, p[j]->title,
	       p[j]->BM);

    move(b_lines - 2, 0);
    strlcpy(category, p[i]->title + 7, sizeof(category));
    if ((pcat = strchr(category, ' ')) != NULL)
        *pcat = 0;
    prints("��m: ");
    for (j = i; j >= 0; j--)
        prints("%s%s ",
               (j == i) ? category: p[j]->brdname,
               j ? " >": "");

    pressanykey();
    return FULLUPDATE;
}


static int
do_select(void)
{
    char            bname[20];

    setutmpmode(SELECT);
    move(0, 0);
    clrtoeol();
    CompleteBoard(MSG_SELECT_BOARD, bname);

    if(enter_board(bname) < 0)
	return FULLUPDATE;

    move(1, 0);
    clrtoeol();
    return NEWDIRECT;
}

static int
cancelpost(const char *direct, const fileheader_t *fh,
           int not_owned GCC_UNUSED, char *newpath, size_t sznewpath,
           const char *reason) {
    int ret = 0;
    char bakdir[PATHLEN];

#ifdef USE_TIME_CAPSULE
    setbdir(bakdir, currboard);
#else
    const char *brd = not_owned ? BN_DELETED : BN_JUNK;
    setbdir(bakdir, brd);
#endif

    ret = delete_file_content2(direct, fh, bakdir, newpath, sznewpath, reason);
    if (!IS_DELETE_FILE_CONTENT_OK(ret))
        return ret;

#ifdef USE_TIME_CAPSULE
    // do nothing for capsule
#else
    setbtotal(getbnum(brd));
#endif

    return ret;
}

static void
do_deleteCrossPost(const fileheader_t *fh, char bname[])
{
    // TODO FIXME this function is used when some user violates policy (ex,
    // crosspost) and system is trying to delete all the posts in every boards.

    char bdir[PATHLEN], file[PATHLEN];
    fileheader_t newfh;
    boardheader_t  *bp;
    int i, bid;

    if(!bname || !fh) return;
    if(!fh->filename[0]) return;

    bid = getbnum(bname);
    if(bid <= 0) return;

    bp = getbcache(bid);
    if(!bp) return;

    setbdir(bdir, bname);
    setbfile(file, bname, fh->filename);
    memcpy(&newfh, fh, sizeof(fileheader_t));

    // XXX TODO FIXME This (finding file by getindex) sucks. getindex checks
    // only timestamp by binary search, but that is not always true in current
    // system.

    // Ptt: protect original fh
    // because getindex safe_article_delete will change fh in some case
    if( (i=getindex(bdir, &newfh, 0))>0)
    {
#ifdef SAFE_ARTICLE_DELETE
        if(bp && bp->nuser >= SAFE_ARTICLE_DELETE_NUSER)
            safe_article_delete(i, &newfh, bdir, NULL);
        else
#endif
            delete_fileheader(bdir, &newfh, i);
	setbtotal(bid);
    }

    // the getindex is not stable. in order to prevent leaving files,
    // no matter what, delete the file.
    delete_file_content2(bdir, fh, bdir, NULL, 0, "Cross-Post(�t��ĵ��R��)");
    unlink(file);
}

static void
deleteCrossPost(const fileheader_t *fhdr, char *bname)
{
    if(!fhdr || !fhdr->filename[0]) return;

    if(strcmp(bname, BN_ALLPOST) == 0 ||
       strcmp(bname, BN_ALLHIDPOST) == 0 ||
       strcmp(bname, BN_NEWIDPOST) == 0 ||
       strcmp(bname, BN_UNANONYMOUS) == 0 ) {
        // These files (in BN_ALLPOST etc) have '.BOARD�O' or '(BOARD)' refrence
        // in title
        int bnlen;
	char bnbuf[IDLEN + 1] = "", *bn;

        if (*fhdr->title && fhdr->title[strlen(fhdr->title) - 1] == ')') {
            // new format:  (BOARD)
            bn = strrchr(fhdr->title, '(');
            if (!bn)
                return;
            bnlen = strlen(++bn) - 1;
            snprintf(bnbuf, sizeof(bnbuf), "%*.*s", bnlen, bnlen, bn);
        } else {
            // old format:  .BOARD��, which may conflict with board names.
            bn = strrchr(fhdr->title, '.');
            if (!bn)
                return;
            bnlen = strlen(++bn) - 2;
            snprintf(bnbuf, sizeof(bnbuf), "%*.*s", bnlen, bnlen, bn);
        }
	do_deleteCrossPost(fhdr, bnbuf);
    } else {
        // Always delete file content in ALLPOST and keep the header
        // because that will be reset by cron jobs
        // Note in USE_LIVE_ALLPOST, there should be no files.
#ifndef USE_LIVE_ALLPOST
        char file[PATHLEN];
        setbfile(file, BN_ALLPOST, fhdr->filename);
        unlink(file);
#endif
    }
}

void
delete_allpost(const char *userid)
{
    fileheader_t fhdr;
    int     fd, i;
    char    bdir[PATHLEN], file[PATHLEN];

    if(!userid) return;

    setbdir(bdir, BN_ALLPOST);
    if( (fd = open(bdir, O_RDWR)) != -1)
    {
       for(i=0; read(fd, &fhdr, sizeof(fileheader_t)) >0; i++){
           if(strcmp(fhdr.owner, userid))
             continue;
           deleteCrossPost(&fhdr, BN_ALLPOST);
           // No need to touch the file since ALLPOST is cleared weekly.
	   setbfile(file, BN_ALLPOST, fhdr.filename);
	   unlink(file);
           // No need to touch header in ALLPOST.
       }
       close(fd);
    }
}

/* ----------------------------------------------------- */
/* �o��B�^���B�s��B����峹                            */
/* ----------------------------------------------------- */
static int
solveEdFlagByBoard(const char *bn, int flags)
{
    if (
#ifdef BN_BBSMOVIE
	    strcmp(bn, BN_BBSMOVIE) == 0 ||
#endif
#ifdef BN_TEST
	    strcmp(bn, BN_TEST) == 0 ||
#endif
	    0
	)
    {
	flags |= EDITFLAG_UPLOAD | EDITFLAG_ALLOWLARGE;
    }
    return flags;
}

void
do_reply_title(int row, const char *title, const char *prefix,
               char *result, int len) {
    char ans[4];

    snprintf(result, len, "%s %s", prefix, subject(title));
    if (len > TTLEN)
        len = TTLEN;
    result[TTLEN - 1] = '\0';
    DBCS_safe_trim(result);
    mvouts(row++, 0, "����D: "); outs(result);
    getdata(row, 0, "�ĥέ���D[Y/n]? ", ans, 3, LCECHO);
    if (ans[0] == 'n')
	getdata_str(row, 0, "�s���D�G", result, len, DOECHO, result);

}

void
log_crosspost_in_allpost(const char *brd, const fileheader_t *postfile) {
    char genbuf[PATHLEN];
    fileheader_t fh;
    // '�K' appears for t_columns-33.
    int  len = 42-strlen(brd);
    const char *title = subject(postfile->title);
    int bid = getbnum(BN_ALLPOST), brd_id = getbnum(brd);
    if(bid <= 0 || bid > MAX_BOARD)
        return;
    if(brd_id <= 0 || brd_id > MAX_BOARD)
        return;

    // don't log when brd is a hidden board.
    if (!IS_OPENBRD(getbcache(brd_id)))
        return;

    memcpy(&fh, postfile, sizeof(fileheader_t));
    fh.filemode = FILE_LOCAL;
    fh.modified = now;
    strlcpy(fh.owner, cuser.userid, sizeof(fh.owner));
    strlcpy(genbuf, title, len + 1);
    if ((int)strlen(title) > len) {
        genbuf[len-2] = 0;
        DBCS_safe_trim(genbuf);
        strcat(genbuf, "�K");
    }
    snprintf(fh.title, sizeof(fh.title),
             "%s %-*.*s(%s)", str_forward, len, len, genbuf, brd);

    setbdir(genbuf, BN_ALLPOST);
    if (append_record(genbuf, &fh, sizeof(fileheader_t)) != -1) {
	SHM->lastposttime[bid - 1] = now;
	touchbpostnum(bid, 1);
    }
}

void
dbcs_safe_trim_title(char *output, const char *title, int len) {
    if ((int)strlen(title) > len) {
        snprintf(output, len + 1, "%-*.*s", len - 2, len - 2, title);
        DBCS_safe_trim(output);
        strlcat(output, "�K", len + 1);
        while ((int)strlen(output) < len)
            strlcat(output, " ", len + 1);
    } else {
        snprintf(output, len + 1, "%-*.*s", len, len, title);
    }
}

void
do_crosspost(const char *brd, fileheader_t *postfile, const char *fpath)
{
    char            genbuf[200];
    int             len = 42-strlen(currboard);
    fileheader_t    fh;
    int bid = getbnum(brd);
    const char *title, *prefix = "";
    int title_type = SUBJECT_NORMAL;

    if(bid <= 0 || bid > MAX_BOARD) return;
    title = subject_ex(postfile->title, &title_type);
    switch (title_type) {
        case SUBJECT_REPLY:
            prefix = str_reply;
            break;
        case SUBJECT_FORWARD:
            // is this a real case?
            prefix = str_forward;
            break;
    }

    memcpy(&fh, postfile, sizeof(fileheader_t));
    setbfile(genbuf, brd, postfile->filename);

    if(!strcasecmp(brd, BN_UNANONYMOUS))
       strcpy(fh.owner, cuser.userid);

    snprintf(fh.title, sizeof(fh.title), "%s%s", prefix, *prefix ? " " : "");
    dbcs_safe_trim_title(fh.title + strlen(fh.title), title, len);
    snprintf(fh.title + strlen(fh.title), sizeof(fh.title) - strlen(fh.title),
             "(%s)", currboard);
    if (dashs(genbuf) > 0) {
        log_filef("log/conflict.log", LOG_CREAT,
                  "%s %s->%s %s: %s\n", Cdatelite(&now),
                  currboard, brd, fh.filename, fh.title);
    }

#ifdef USE_LIVE_ALLPOST
    if (strcmp(brd, BN_ALLPOST) != 0)
#endif
    {
        Copy(fpath, genbuf);
    }

    fh.filemode = FILE_LOCAL;
    fh.modified = now;
    setbdir(genbuf, brd);
    if (append_record(genbuf, &fh, sizeof(fileheader_t)) != -1) {
	SHM->lastposttime[bid - 1] = now;
	touchbpostnum(bid, 1);
    }
}

int
does_board_have_public_bm(const boardheader_t *bp) {
    // Usually we can assume SHM->BMcache contains complete BM list; however
    // sometimes boards may contains only private BMs (ex: [ <-space  some_uid])
    // �t�~�٦��H�������F�ۤv�[�F [] �W�h
    // �d�F�b���٬Orevert�^��l�� BM[0] < ' ' �n�F�A
    // ���V���O�٨S�Q���n���@�k�C
    return bp->BM[0] > ' ';
}

#ifdef USE_POSTD
static int PostAddRecord(const char *board, const fileheader_t *fhdr,
                         time4_t ctime)
{
    int s;
    PostAddRequest req = {0};

    req.cb = sizeof(req);
    req.operation = POSTD_REQ_ADD;
    strlcpy(req.key.board, board, sizeof(req.key.board));
    strlcpy(req.key.file, fhdr->filename, sizeof(req.key.file));
    memcpy(&req.header, fhdr, sizeof(req.header));
    req.extra.userref = cuser.firstlogin;
    req.extra.ctime = ctime;
    req.extra.ipv4 = inet_addr(fromhost);
    strlcpy(req.extra.userid, cuser.userid, sizeof(req.extra.userid));

    s = toconnectex(POSTD_ADDR, 10);
    if (s < 0)
        return 1;
    if (towrite(s, &req, sizeof(req)) < 0) {
        close(s);
        return 1;
    }
    close(s);
    return 0;
}
#endif

static int
do_post_article(int edflags)
{
    fileheader_t    postfile;
    char            fpath[PATHLEN], buf[PATHLEN];
    int i, j;
    int             ifuseanony;
    int             money = 0;
    char            genbuf[PATHLEN];
    const char	    *owner;
    char            ctype[8][5] = {"���D", "��ĳ", "�Q��", "�߱o",
				   "����", "�Яq", "����",
				   "���i" // TN_ANNOUNCE
				  };
    boardheader_t  *bp;
    int             posttype=-1;
    char save_title[STRLEN];
    const char *reason = "�L�k�o��";

    save_title[0] = '\0';

    ifuseanony = 0;
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);

    if( !CheckPostPerm2(&reason)
#ifdef FOREIGN_REG
	// ���O�~�y�ϥΪ̦b PttForeign �O
	&& !(HasUserFlag(UF_FOREIGN) &&
	    strcmp(bp->brdname, BN_FOREIGN) == 0)
#endif
	) {
	vmsgf("�L�k�o��: %s", reason);
	return READ_REDRAW;
    }

    if (get_board_restriction_reason(currbid, sizeof(genbuf), genbuf))
    {
	vmsgf("���F�ݪO�o�孭��: %s", genbuf);
	return FULLUPDATE;
    }
#ifdef USE_COOLDOWN
   if(check_cooldown(bp))
       return READ_REDRAW;
#endif
    clear();

#ifdef USE_POST_CAPTCHA_FOR_NOREG
#ifndef USE_REMOTE_CAPTCHA
#error "To use USE_POST_CAPTCHA_FOR_NOREG, you must also set USE_REMOTE_CAPTCHA"
#endif
    if (!HasUserPerm(PERM_LOGINOK)) {
	const char *errmsg = remote_captcha();
	if (errmsg) {
	    vmsg(errmsg);
	    return FULLUPDATE;
	}
	clear();
    }
#endif

    setbfile(genbuf, currboard, FN_POST_NOTE);

    if (more(genbuf, NA) == -1) {
	more("etc/" FN_POST_NOTE, NA);
    }
    move(19, 0);
    prints("%s��i" ANSI_COLOR(33) " %s" ANSI_RESET " �j "
	   ANSI_COLOR(32) "%s" ANSI_RESET " �ݪO\n",
	   "�o��峹",
	   currboard, bp->title + 7);

    if (quote_file[0])
        do_reply_title(20, currtitle, str_reply, save_title,
                       sizeof(save_title));
    else {
	char tmp_title[STRLEN]="";
	move(21,0);
	outs("�����G");
	for(i=0; i<8 && bp->posttype[i*4]; i++)
	    strlcpy(ctype[i],bp->posttype+4*i,5);
	if(i==0) i=8;
	for(j=0; j<i; j++)
	    prints("%d.%4.4s ", j+1, ctype[j]);
	sprintf(buf,"(1-%d�Τ���)",i);

	do {
	    getdata(21, 6+7*i, buf, tmp_title, 3, LCECHO);
	    posttype = tmp_title[0] - '1';
	    if (posttype >= 0 && posttype < i)
	    {
		snprintf(tmp_title, sizeof(tmp_title),
			"[%s] ", ctype[posttype]);
	    } else {
		tmp_title[0] = '\0';
		posttype=-1;
	    }

	    if (!is_tn_allowed(tmp_title))
	    {
		// XXX the only case now is TN_ANNOUNCE.
		mvouts(22, 0, "��p�A" TN_ANNOUNCE " �O�d���O�D�ϥΡC\n");
		continue;
	    }
	    break;
	} while (1);

	do {
            int w = DISP_TTLEN + (t_columns - 80);
            // -4: for prefixes
            if (w >= TTLEN - 4)
                w = TTLEN - 4;
	    getdata_buf(22, 0, "���D�G", tmp_title, w, DOECHO);
	    strip_ansi(tmp_title, tmp_title, STRIP_ALL);

	    if (!is_tn_allowed(tmp_title))
	    {
		// XXX the only case now is TN_ANNOUNCE.
		mvouts(21, 0, "��p�A" TN_ANNOUNCE " �O�d���O�D�ϥΡC\n");
		tn_safe_strip(tmp_title);
		continue;
	    }
	    break;
	} while (1);

	strlcpy(save_title, tmp_title, sizeof(save_title));
    }
    if (save_title[0] == '\0')
	return FULLUPDATE;

    setutmpmode(POSTING);

    /* build filename */
    setbpath(fpath, currboard);
    if (stampfile(fpath, &postfile) < 0) {
        vmsg("�t�ο��~: �L�k�g�J�ɮסC");
        return FULLUPDATE;
    }
    if(posttype!=-1 && ((1<<posttype) & bp->posttype_f)) {
	setbnfile(genbuf, bp->brdname, "postsample", posttype);
	Copy(genbuf, fpath);
    }

    edflags |= EDITFLAG_ALLOWTITLE;
    edflags |= solveEdFlagByBoard(currboard, edflags);
    if (bp->brdattr & BRD_NOSELFDELPOST)
        edflags |= EDITFLAG_WARN_NOSELFDEL;

#if defined(PLAY_ANGEL)
    // XXX �c�d�� code�C
    if (HasUserPerm(PERM_ANGEL) && (currbrdattr & BRD_ANGELANONYMOUS))
    {
	currbrdattr |= BRD_ANONYMOUS;
	currbrdattr |= BRD_DEFAULTANONYMOUS;
    };
#endif

    money = vedit2(fpath, YEA, save_title, edflags);
    if (money == EDIT_ABORTED) {
	unlink(fpath);
	pressanykey();
	return FULLUPDATE;
    }
    /* set owner to Anonymous for Anonymous board */

    // check TN_ANNOUNCE again for non-BMs...
    tn_safe_strip(save_title);

#ifdef HAVE_ANONYMOUS
    /* Ptt and Jaky */
    if ((currbrdattr & BRD_ANONYMOUS) && strcmp(real_name, "r") != 0) {
	strcat(real_name, ".");
	owner = real_name;
	ifuseanony = 1;
    } else
	owner = cuser.userid;
#else
    owner = cuser.userid;
#endif

    // ---- BEGIN OF MONEY VERIFICATION ----

    // money verification
#ifdef MAX_POST_MONEY
    if (money > MAX_POST_MONEY)
	money = MAX_POST_MONEY;
#endif

    // drop money & numposts for free boards, or user without login information
    // (they may post on free boards like SYSOP)
    // including: special boards (e.g. TEST, ALLPOST), bad boards, no BM boards
    if (!HasBasicUserPerm(PERM_LOGINOK) ||
	IsFreeBoardName(currboard) ||
	(currbrdattr & BRD_NOCREDIT) ||
#ifdef USE_HIDDEN_BOARD_NOCREDIT
        (currbrdattr & BRD_HIDE) ||
#endif
        !does_board_have_public_bm(bp))
    {
	money = 0;
    }

    // also drop for anonymos/bid posts
    if(ifuseanony) {
	money = 0;
	postfile.filemode |= FILE_ANONYMOUS;
	postfile.multi.anon_uid = currutmp->uid;
    }
    else
    {
	/* general article */
	postfile.modified = dasht(fpath);
	postfile.multi.money = money;
    }

    // ---- END OF MONEY VERIFICATION ----

    strlcpy(postfile.owner, owner, sizeof(postfile.owner));
    strlcpy(postfile.title, save_title, sizeof(postfile.title));

    setbdir(buf, currboard);

    // Ptt: stamp file again to make it order
    //      fix the bug that search failure in getindex
    //      stampfile_u is used when you don't want to clear other fields
    strcpy(genbuf, fpath);
    setbpath(fpath, currboard);
    stampfile_u(fpath, &postfile);

#ifdef QUERY_ARTICLE_URL
    if (IsBoardForWeb(bp)) {
        char url[STRLEN];
        if (GetWebUrl(bp, &postfile, url, sizeof(url))) {
            log_filef(genbuf, LOG_CREAT,
                      "�� " URL_DISPLAYNAME ": %s\n", url);
        }
    }
#endif

    if (append_record(buf, &postfile, sizeof(postfile)) == -1)
    {
        unlink(genbuf);
    }
    else
    {
        rename(genbuf, fpath);
	setbtotal(currbid);

        LOG_IF(LOG_CONF_POST,
               log_filef("log/post", LOG_CREAT,
                         "%d %s boards/%c/%s/%s %d\n",
                         (int)now, cuser.userid, currboard[0], currboard,
                         postfile.filename, money));

	if( currmode & MODE_SELECT )
	    append_record(currdirect, &postfile, sizeof(postfile));
	brc_addlist(postfile.filename, postfile.modified);

        if (IS_OPENBRD(bp)) {
	        if (cuser.numlogindays < NEWIDPOST_LIMIT_DAYS)
            		do_crosspost(BN_NEWIDPOST, &postfile, fpath);

		if (currbrdattr & BRD_HIDE)
                    do_crosspost(BN_ALLHIDPOST, &postfile, fpath);
                else {
                    do_crosspost(BN_ALLPOST, &postfile, fpath);
                }
	}
	outs("���Q�K�X�G�i�A");

	// Freeboard/BRD_NOCREDIT check was already done.
	if (!ifuseanony)
	{
            if (money > 0)
	    {
                pay(-money, "%s �ݪO�o��Z�S: %s", currboard, postfile.title);
		pwcuIncNumPost();
		prints("�o�O�z���� %d �g���Ĥ峹�A��o�Z�S %d "
                        MONEYNAME "\n", cuser.numposts, money);
		prints("\n (�Y����ۦ�γQ�O�D�R��"
                       "�h������o�����Ĥ峹�ƤνZ�S�i��|�Q�^��)\n\n");
	    } else {
#ifdef USE_HIDDEN_BOARD_NOCREDIT
                if (currbrdattr & BRD_HIDE)
                    outs("�ѩ󥻬ݪO�����O�A���קK�D�ݥΡA�o��L������y�C");
                else
#endif
		// no money, no record.
		outs("���g�峹���C�J�O���A�q�Х]�[�C");
	    }
	}
	else
	{
	    outs("���C�J�O���A�q�Х]�[�C");
	}
	clrtobot();

	/* �^�����@�̫H�c */
        if (edflags & EDITFLAG_KIND_SENDMAIL) {
	    char *str, *msg = NULL;

	    genbuf[0] = 0;
	    // XXX quote_user may contain invalid user, like '-' (deleted).
	    if (is_validuserid(quote_user))
	    {
		sethomepath(genbuf, quote_user);
		if (!dashd(genbuf))
		{
		    genbuf[0] = 0;
		    msg = err_uid;
		} else if(is_rejected(quote_user)) {
                    genbuf[0] = 0;
                    msg = "�@�̩ڦ�";
                }
	    }

	    // now, genbuf[0] = "if user exists".
	    if (genbuf[0])
	    {
                fileheader_t mailfile;
                stampfile(genbuf, &mailfile);
		unlink(genbuf);
		Copy(fpath, genbuf);

		strlcpy(mailfile.owner, cuser.userid, sizeof(mailfile.owner));
		strlcpy(mailfile.title, save_title, sizeof(mailfile.title));
		sethomedir(genbuf, quote_user);
                msg = "�^���ܧ@�̫H�c";
		if (append_record(genbuf, &mailfile, sizeof(mailfile)) == -1)
		    msg = err_uid;
		else
		    sendalert(quote_user, ALERT_NEW_MAIL);
	    } else if ((str = strchr(quote_user, '.'))) {
                LOG_IF(LOG_CONF_INTERNETMAIL,
                       log_filef("log/internet_mail.log", LOG_CREAT,
                                 "%s [%s (%s)] %s -> %s: %s\n",
                                 Cdatelite(&now), __FUNCTION__,
                                 currboard, cuser.userid, str + 1, save_title));
                msg = "�^���ܧ@�̥~���H�c";
		if ( bsmtp(fpath, save_title, str + 1, NULL) < 0)
		    msg = "�@�̵L�k���H";
	    } else {
		// unknown user id
                if (!msg)
                    msg = "�@�̵L�k���H";
	    }
	    outs(msg);
	}

	if (currbrdattr & BRD_ANONYMOUS)
            do_crosspost(BN_UNANONYMOUS, &postfile, fpath);
#ifdef USE_COOLDOWN
        if(bp->nuser>30)
	{
	    if ((time4_t)cooldowntimeof(usernum) < now)
		add_cooldowntime(usernum, 5);
	}
	add_posttimes(usernum, 1);
#endif
#ifdef USE_POSTD
        PostAddRecord(bp->brdname, &postfile, dashc(fpath));
#endif
    }
    pressanykey();
    return FULLUPDATE;
}

int new_post() {
    return do_post(EDITFLAG_KIND_NEWPOST);
}

int
do_post(int edflags)
{
    boardheader_t  *bp;
    STATINC(STAT_DOPOST);
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);
    if (bp->brdattr & BRD_VOTEBOARD)
	return do_voteboard(0);
    else if (!(bp->brdattr & BRD_GROUPBOARD))
	return do_post_article(edflags);
    return 0;
}

int
do_post_vote(void)
{
    return do_voteboard(1);
}

static void
do_generalboardreply(/*const*/ fileheader_t * fhdr)
{
    char            genbuf[3];
    int edflags = EDITFLAG_KIND_REPLYPOST;

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);

    if (!CheckPostRestriction(currbid))
    {
	getdata(b_lines - 1, 0,
		ANSI_COLOR(1;31) "�� �L�k�^���ܬݪO�C " ANSI_RESET
		"��^���� (M)�@�̫H�c (Q)�����H[Q] ",
		genbuf, sizeof(genbuf), LCECHO);
	switch (genbuf[0]) {
	    case 'm':
		mail_reply(0, fhdr, 0);
		break;
	    default:
		break;
	}
    }
    else {
	getdata(b_lines - 1, 0,
		"�� �^���� (F)�ݪO (M)�@�̫H�c (B)�G�̬ҬO (Q)�����H[F] ",
		genbuf, sizeof(genbuf), LCECHO);
	switch (genbuf[0]) {
	    case 'm':
		mail_reply(0, fhdr, 0);
	    case 'q':
		break;

	    case 'b':
                // TODO(piaip) Check if fhdr has valid author.
                edflags |= EDITFLAG_KIND_SENDMAIL;
	    default:
		strlcpy(currtitle, fhdr->title, sizeof(currtitle));
		strlcpy(quote_user, fhdr->owner, sizeof(quote_user));
		do_post(edflags);
	}
    }
    *quote_file = 0;
}

int
b_call_in(int ent GCC_UNUSED, const fileheader_t * fhdr,
          const char *direct GCC_UNUSED)
{
    userinfo_t *u;
    if (!HasBasicUserPerm(PERM_LOGINOK))
        return DONOTHING;

    u = search_ulist(searchuser(fhdr->owner, NULL));
    if (u) {
	int             fri_stat;
	fri_stat = friend_stat(currutmp, u);
	if (isvisible_stat(currutmp, u, fri_stat) && call_in(u, fri_stat))
	    return FULLUPDATE;
    }
    return DONOTHING;
}


static int
do_reply(/*const*/ fileheader_t * fhdr)
{
    boardheader_t  *bp;
    if (!fhdr || !fhdr->filename[0] || fhdr->owner[0] == '-')
	return DONOTHING;

    if (!CheckPostPerm() ) return DONOTHING;
    if (fhdr->filemode &FILE_SOLVED)
     {
       if(fhdr->filemode & FILE_MARKED)
         {
          vmsg("�ܩ�p, ���峹�w���רüаO, ���o�^��.");
          return FULLUPDATE;
         }
       if(vmsg("���g�峹�w����, �O�_�u���n�^��?(y/N)")!='y')
          return FULLUPDATE;
     }

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);
    if (bp->brdattr & BRD_NOREPLY) {
	// try to reply by mail.
	if (vans("�ܩ�p, ���O���}��^�Ф峹�A�n��^�H���@�̶ܡH [y/N]: ") == 'y')
	    return mail_reply(0, fhdr, 0);
	else
	    return FULLUPDATE;
    }

    setbfile(quote_file, bp->brdname, fhdr->filename);
    if (bp->brdattr & BRD_VOTEBOARD || (fhdr->filemode & FILE_VOTE))
	do_voteboardreply(fhdr);
    else
	do_generalboardreply(fhdr);
    *quote_file = 0;
    return FULLUPDATE;
}

static int
reply_post(int ent GCC_UNUSED, fileheader_t * fhdr,
           const char *direct GCC_UNUSED)
{
    return do_reply(fhdr);
}

#ifdef EDITPOST_SMARTMERGE

#define HASHPF_RET_OK (0)

// return: 0 - ok; otherwise - fail.
static int
hash_partial_file( char *path, size_t sz, unsigned char output[SMHASHLEN] )
{
    int fd;
    size_t n;
    unsigned char buf[1024];

    Fnv64_t fnvseed = FNV1_64_INIT;
    assert(SMHASHLEN == sizeof(fnvseed));

    fd = open(path, O_RDONLY);
    if (fd < 0)
	return 1;

    while(  sz > 0 &&
	    (n = read(fd, buf, sizeof(buf))) > 0 )
    {
	if (n > sz) n = sz;
	fnvseed = fnv_64_buf(buf, (int) n, fnvseed);
	sz -= n;
    }
    close(fd);

    if (sz > 0) // file is different
	return 2;

    memcpy(output, (void*) &fnvseed, sizeof(fnvseed));
    return HASHPF_RET_OK;
}

int
append_merge_replace(const char *ref_fn, const char *mod_fn, size_t sz_orig) {
    // assumption:
    //  1. ref_fn was sized in sz_orig, and no one has edited it.
    //  2. ref_fn only get appened.
    //  3. ref_fn must be replaced by content from mod_fn
    int fd_src, fd_dest;
    ssize_t sz;
    char buf[1024];
    int ret = 1;

    fd_dest = open(mod_fn, O_APPEND | O_WRONLY);
    if (fd_dest < 0)
        return 0;

    fd_src = open(ref_fn, O_RDONLY);
    if (fd_src < 0) {
	close(fd_dest);
	return 0;
    }
    if ((size_t)dashs(ref_fn) < sz_orig) {
        close(fd_src);
        close(fd_dest);
        return 0;
    }

    // during lock, only record error and never break.
    flock(fd_src, LOCK_EX);
    if (lseek(fd_src, (off_t)sz_orig, SEEK_SET) != (off_t)sz_orig)
        ret = 0;
    while ((sz = read(fd_src, buf, sizeof(buf))) > 0) {
        if (write(fd_dest, buf, sz) != sz)
            ret = 0;
    }
    if (sz != 0)
        ret = 0;
    // close and flush the merged file
    close(fd_dest);

    if (unlink(ref_fn) != 0 ||
        Rename(mod_fn, ref_fn) != 0)
        ret = 0;

    flock(fd_src, LOCK_UN);
    close(fd_src);
    return ret;
}
#endif // EDITPOST_SMARTMERGE

int
edit_post(int ent, fileheader_t * fhdr, const char *direct)
{
    char            fpath[PATHLEN];
    char            genbuf[PATHLEN];
    fileheader_t    postfile;
    boardheader_t  *bp = getbcache(currbid);
    off_t	    oldsz;
    int		    edflags = 0, is_race_condition = 0;
    char save_title[STRLEN];

#ifdef USE_TIME_CAPSULE
    int rev = 0;
    time4_t oldmt = 0;
#endif

#ifdef EDITPOST_SMARTMERGE
    unsigned char oldsum[SMHASHLEN] = {0}, newsum[SMHASHLEN] = {0};
#endif // EDITPOST_SMARTMERGE

#ifdef EXP_EDITPOST_TEXTONLY
    // experimental: "text only" editing
    edflags |= EXP_EDITPOST_TEXTONLY;
#endif

    save_title[0] = '\0';
    assert(0<=currbid-1 && currbid-1<MAX_BOARD && bp);

    // special modes (plus MODE_DIGEST?)
    if( currmode & MODE_SELECT )
	return DONOTHING;

    // board check
    if (is_readonly_board(bp->brdname) ||
	(bp->brdattr & BRD_VOTEBOARD))
	return DONOTHING;

    // file check
    if (fhdr->filemode & FILE_VOTE)
	return DONOTHING;

#ifdef SAFE_ARTICLE_DELETE
    if( fhdr->filename[0] == '.' )
	return DONOTHING;
#endif

    // user and permission check
    // reason 1: BM may alter post restrictions to this board
    // reason 2: this account may be occupied by someone else.
    if (!HasUserPerm(PERM_BASIC) ||	// including guests
	!CheckPostPerm() ||		// edit needs post perm (cannot be changed in digest mode)
	!CheckPostRestriction(currbid)
	)
	return DONOTHING;

    setdirpath(genbuf, direct, fhdr->filename);
    if (!is_file_owner(fhdr, &cuser))
    {
#ifdef USE_SYSOP_EDIT
	if (!HasUserPerm(PERM_SYSOP))
	    return DONOTHING;

	// admin edit!
	log_filef("log/security", LOG_CREAT,
		"%d %s %d %s admin edit (board) file=%s\n",
		(int)now, Cdate(&now), getpid(), cuser.userid, genbuf);
#else
        return DONOTHING;
#endif
    }

    if (!dashf(genbuf)) {
        vmsg("���ɤw�l���A�L�k�s��C�z�i�H�յۧR�����C");
        return FULLUPDATE;
    }

    edflags = EDITFLAG_ALLOWTITLE;
    edflags = solveEdFlagByBoard(bp->brdname, edflags);

    setutmpmode(REEDIT);

    // TODO �ѩ�{�b�ɮ׳��O�����\�^���ɡA
    // �b��ݪO�ؿ��}�w�S���ܤj�N�q�C (�Ĳv�y���@�I)
    // �i�H�Ҽ{��}�b user home dir
    // �n�B�O�ݪO���ɮ׼Ƥ��|�g�����C (when someone crashed)
    // sethomedir(fpath, cuser.userid);
    // XXX �p�G�A���t�Φ��w���ݪO�M�t���ɡA���N���Ω� user home�C
    setbpath(fpath, currboard);

    // XXX �H�{�b���Ҧ��A�o�O�� temp file
    stampfile(fpath, &postfile);

    // copying takes long time, add some visual effect
    grayout(0, b_lines-2, GRAYOUT_DARK);
    move(b_lines-1, 0); clrtoeol();
    outs("���b���J�ɮ�...");
    refresh();

    strlcpy(save_title, fhdr->title, sizeof(save_title));

    Copy(genbuf, fpath);
    // to prevent genbuf being modified after copy, use dashs(fpath) instead.
    oldsz = dashs(fpath);

#ifdef EDITPOST_SMARTMERGE
    if (hash_partial_file(fpath, oldsz, oldsum) != HASHPF_RET_OK) {
        vmsg("�t�ο��~�A�L�k�ǳƽs���ɮסC�Ц�" BN_BUGREPORT "���i");
        unlink(fpath);
        return FULLUPDATE;
    }
#endif

    if (vedit2(fpath, 0, save_title, edflags) == EDIT_ABORTED) {
        unlink(fpath);
        return FULLUPDATE;
    }

#ifdef EDITPOST_SMARTMERGE
    outs("\n\n" ANSI_COLOR(1;30) "���b�ˬd�ɮ׬O�_�Q�ק�L..." ANSI_RESET);
    refresh();

    if (hash_partial_file(genbuf, oldsz, newsum) != HASHPF_RET_OK ||
        memcmp(oldsum, newsum, sizeof(oldsum)) != 0) {
        is_race_condition = 1;
    }
#else
    // without smart merge, simply alert by size and mtime.
    if (dashs(genbuf) != oldsz || dasht(genbuf) > dashc(fpath))
        is_race_condition = 1;
#endif
    if (is_race_condition) {
        // save to ~/buf.0
        setuserfile(genbuf, "buf.0");
        Rename(fpath, genbuf);
        // alert uesr
        outs("\n\n" ANSI_COLOR(1;31) "�ɮפw�Q�䥦�H�ק�L�A�L�k�g�J�C\n"
             "��~�����e�w�s�J�z���Ȧs��[0]�C �Э��s�s��C");
        pressanykey();
        return FULLUPDATE;
    }

    // OK to save file.
#ifdef USE_TIME_CAPSULE
    oldmt = dasht(genbuf);
    rev = timecapsule_add_revision(genbuf);
#endif

#ifdef EDITPOST_SMARTMERGE
    // atomic lock-merge-replace
    append_merge_replace(genbuf, fpath, oldsz);
#else
    // piaip Wed Jan  9 11:11:33 CST 2008
    // in order to prevent calling system 'mv' all the
    // time, it is better to unlink() first, which
    // increased the chance of succesfully using rename().
    // WARNING: if genbuf and fpath are in different directory,
    // you should disable pre-unlinking
    unlink(genbuf);
    Rename(fpath, genbuf);
#endif

    fhdr->modified = dasht(genbuf);
    if (strcmp(save_title, fhdr->title) != 0) {
        LOG_IF(LOG_CONF_EDIT_TITLE,
               log_filef("log/edit_title.log", LOG_CREAT,
                         "%s %s(E) %s(%s) %s => %s\n", Cdatelite(&now),
                         cuser.userid, currboard, fhdr->owner, fhdr->title,
                         save_title));
        strlcpy(fhdr->title, save_title, sizeof(fhdr->title));
    }

    // substitute_ref_record(direct, fhdr, ent);
    modify_dir_lite(direct, ent, fhdr->filename, fhdr->modified, save_title,
                    NULL, NULL, 0, NULL, 0, 0);

    // mark my self as "read this file".
    brc_addlist(fhdr->filename, fhdr->modified);

    // tag revision history file to solve expire issue
#ifdef USE_TIME_CAPSULE
    if (rev > 0) {
        char revfn[PATHLEN];
        timecapsule_get_by_revision(genbuf, rev, revfn, sizeof(revfn));
        log_filef(revfn, 0, "\n�� Last modified: %s", Cdatelite(&oldmt));
    }
#endif

    return FULLUPDATE;
}

int
forward_post(int ent GCC_UNUSED, fileheader_t * fhdr, const char *direct) {
    if (!HasUserPerm(PERM_FORWARD) || fhdr->filename[0] == '.' ||
        fhdr->filename[0] == 'L')
        return DONOTHING;

#ifdef QUERY_ARTICLE_URL
    if (currbid) {
        char buf[STRLEN];
        const boardheader_t *bp = getbcache(currbid);
        if (bp && IsBoardForWeb(bp) &&
            GetWebUrl(bp, fhdr, buf, sizeof(buf))) {
            move(b_lines - 4, 0); clrtobot();
            prints("\n" URL_DISPLAYNAME ": " ANSI_COLOR(1)
                   "%s" ANSI_RESET "\n", buf);
        }
    }
#endif
    forward_file(fhdr, direct);
    return FULLUPDATE;
}

#define UPDATE_USEREC   (currmode |= MODE_DIRTY)

static int
cross_post(int ent, fileheader_t * fhdr, const char *direct)
{
    char            xboard[20], fname[PATHLEN], xfpath[PATHLEN], xtitle[80];
    char            genbuf[200];
    fileheader_t    xfile;
    FILE           *xptr;
    int             xbid;
    boardheader_t  *bp;

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);

    if (bp && (bp->brdattr & BRD_VOTEBOARD) )
	return DONOTHING;

    // if file is SAFE_DELETED, skip it.
    if (fhdr->owner[0] == '-')
	return DONOTHING;

    setbfile(fname, currboard, fhdr->filename);
    if (!dashf(fname))
    {
	vmsg("�ɮפw���s�b�C");
	return FULLUPDATE;
    }

    // XXX TODO ���קK�H�k�ϥΪ̤j�q��ӶD�O���A���w�C���o��q�C
    if (HasUserPerm(PERM_VIOLATELAW))
    {
	static int violatecp = 0;
	if (violatecp++ >= MAX_CROSSNUM)
	    return DONOTHING;
    }

    if (!HasUserPerm(PERM_LOGINOK)) {
	vmsg("�z�L����v���C");
	return FULLUPDATE;
    }

    // prompt user what he's going to do now.
    move(2, 0);
    if (is_BM_cache(currbid)) {
        SOLVE_ANSI_CACHE();
        clrtoeol();
        outs("�ǳƶi��峹����C�O�D�n�m���峹�Ч�� "
                ANSI_COLOR(1;31) "_" ANSI_RESET " (���� "
                ANSI_COLOR(1;36) "Shift" ANSI_RESET " �A�� "
                ANSI_COLOR(1;36) "-" ANSI_RESET
                " )\n");
    }

#ifdef USE_AUTOCPLOG
    // anti-crosspost spammers
    //
    // some spammers try to cross-post to other boards without
    // restriction (see pakkei0712* events on 2007/12)
    // for (1) increase numpost (2) flood target board
    // (3) flood original post
    // You must have post permission on current board
    //
    if( (bp->brdattr & BRD_CPLOG) &&
	(!CheckPostPerm() || !CheckPostRestriction(currbid)))
    {
	vmsg("�ѥ��O����峹�ݦ��o���v��(�i�� i �d�ݭ���)");
	return FULLUPDATE;
    }
#endif // USE_AUTOCPLOG

#ifdef LOCAL_ALERT_CROSSPOST
    LOCAL_ALERT_CROSSPOST();
#endif

    move(1, 0);

    CompleteBoard("������峹��ݪO�G", xboard);
    if (*xboard == '\0')
	return FULLUPDATE;

    if (!haspostperm(xboard))
    {
	vmsg("�ݪO���s�b�θӬݪO�T��z�o��峹�I");
	return FULLUPDATE;
    }

    /* ���n�ɥ��ܼơA�O����S����ʡA�H���V�ê��N������� */
    xbid = getbnum(xboard);
    assert(0<=xbid-1 && xbid-1<MAX_BOARD);

    if (xbid == currbid)
    {
	vmsg("�P�O��������C");
	return FULLUPDATE;
    }

    // check target postperm
    if (get_board_restriction_reason(xbid, sizeof(genbuf), genbuf))
    {
	vmsgf("���F�ݪO�o�孭��: %s", genbuf);
	return FULLUPDATE;
    }

#ifdef USE_COOLDOWN
    if(check_cooldown(getbcache(xbid))) {
	vmsg("�ӬݪO�{�b�L�k����C");
	return FULLUPDATE;
    }
#endif

    do_reply_title(2, fhdr->title, str_forward, xtitle, sizeof(xtitle));
    // FIXME �o�̥i��|���H�����ͥX�O�d���D(�p[���i])
    // ���L��F�A�����H�h�o�ؤH�����K
    // �ϥ����ӴN�u�O�Q�ѨM�u���p�ߡv�άO�u���ˤ��p�ߡv�Ψ쪺���ΡC
    // tn_safe_strip_board(xtitle, xboard);

    getdata(2, 0, "(S/L)�o�� (Q)�����H[Q] ", genbuf, 3, LCECHO);

    if (genbuf[0] != 'l' && genbuf[0] != 's')
        return FULLUPDATE;

#ifdef CROSSPOST_VERIFY_CAPTCHA
    if (!verify_captcha("���F�קK����j�q���\n")) {
        vmsg("�峹������A�Э��աC");
        return FULLUPDATE;
    }
#endif

    {
    // if (genbuf[0] == 'l' || genbuf[0] == 's') {
	int             currmode0 = currmode;
	const char     *save_currboard;

	currmode = 0;
	setbpath(xfpath, xboard);
        if (stampfile(xfpath, &xfile) < 0) {
            vmsg("�t�ο��~: �L�k�g�J�ɮ�");
            return FULLUPDATE;
        }
        strlcpy(xfile.owner, cuser.userid, sizeof(xfile.owner));
	strlcpy(xfile.title, xtitle, sizeof(xfile.title));
	if (genbuf[0] == 'l') {
	    xfile.filemode = FILE_LOCAL;
	}
	setbfile(fname, currboard, fhdr->filename);
	xptr = fopen(xfpath, "w");

	save_currboard = currboard;
	currboard = xboard;
        // TODO(piaip) write_header allows anonymous "if current board is
        // anonymous" - so we probably need to fix that here.
	write_header(xptr, xfile.title);
	currboard = save_currboard;

	if (!IS_OPENBRD(bp))
	{
	    /* invisible board */
	    fprintf(xptr, "�� [��������۬Y���άݪO]\n\n");
	    b_suckinfile_invis(xptr, fname, currboard);
	} else {
	    /* public board */
	    // XXX we should add some string length checks here.
	    // maybe someday we will define the standard aidc length
	    // and helper functions to create aidc string with prefixes.
	    aidu_t aidu = 0;
	    char aidc[32] = {0};

	    aidu = fn2aidu((char *)fhdr->filename);
	    if (aidu > 0) {
		aidc[0] = ' '; aidc[1] = '#';
		aidu2aidc(aidc + strlen(aidc), aidu);
		// add trailing space
		strcat(aidc, " ");
	    }
	    fprintf(xptr, "�� [��������� %s �ݪO%s]\n\n", currboard, aidc);
	    b_suckinfile(xptr, fname);
	}

        addforwardsignature(xptr, NULL);
	fclose(xptr);

        // try to record in ALLPOST
        log_crosspost_in_allpost(xboard, &xfile);

#ifdef USE_AUTOCPLOG
	/* add cp log. bp is currboard now. */
	if(bp->brdattr & BRD_CPLOG)
	{
	    char buf[PATHLEN], tail[STRLEN];
	    char bname[STRLEN] = "";
	    int maxlength = 51 +2 - 6;
	    int bid = getbnum(xboard);

	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    bp = getbcache(bid);
	    if (IS_OPENBRD(bp))
	    {
		sprintf(bname, "�ݪO %s", xboard);
	    } else {
		strcpy(bname, "�Y���άݪO");
	    }

	    maxlength -= (strlen(cuser.userid) + strlen(bname));

#ifdef GUESTRECOMMEND
	    snprintf(tail, sizeof(tail),
		    "%15s %s",
		    FROMHOST, Cdate_md(&now));
#else
	    maxlength += (15 - 6);
	    snprintf(tail, sizeof(tail),
		    " %s",
		    Cdate_mdHM(&now));
#endif
	    snprintf(buf, sizeof(buf),
		    // ANSI_COLOR(32) <- system will add green
		    "�� " ANSI_COLOR(1;32) "%s"
		    ANSI_COLOR(0;32) ":�����"
		    "%s" ANSI_RESET "%*s%s\n" ,
		    cuser.userid, bname, maxlength, "",
		    tail);

            // use do_add_recommend to log forward info and also modify the file
            // record
            do_add_recommend(direct, fhdr,  ent, buf, RECTYPE_ARROW);
	} else
#endif
	{
	    /* now point bp to new bord */
	    // NOTE: xbid should be already fetched before.
	    assert(xbid == getbnum(xboard));
	    assert(0<=xbid-1 && xbid-1<MAX_BOARD);
	    bp = getbcache(xbid);
	}

	/*
	 * Cross fs�����D else { unlink(xfpath); link(fname, xfpath); }
	 */
	setbdir(fname, xboard);
	append_record(fname, &xfile, sizeof(xfile));
#ifdef USE_COOLDOWN
        if(bp->nuser>30)
	{
	    if ((time4_t)cooldowntimeof(usernum) < now)
		add_cooldowntime(usernum, 5);
	}
	add_posttimes(usernum, 1);
#endif
	setbtotal(getbnum(xboard));
	outs("�峹��������C(������W�[�峹�ơA�q�Х]�[)\n\n");

        LOG_IF(LOG_CONF_CROSSPOST,
               log_filef("log/cross_post.log", LOG_CREAT,
                         "%s %s %s #%u %s -> %s %s | %s\n",
                         Cdatelite(&now), cuser.userid, cuser.lasthost,
                         (unsigned) getpid(),
                         currboard, xboard, fhdr->filename, fhdr->title));

	// update crosspost record
	if (is_BM_cache(xbid)) {
#ifdef NOTIFY_BM_CP_IGNORE
	    // ignore BM for cross-posting.
	    outs(ANSI_COLOR(1;32)
                 "���g���O�D����A���۰��ˬdCP(���Фp�߻~Ĳ�H�u���|)\n"
		 ANSI_RESET);
#endif
	}

        clrtobot();
	pressanykey();
	currmode = currmode0;
    }

    return FULLUPDATE;
}

static int
read_post(int ent, fileheader_t * fhdr, const char *direct)
{
    char            genbuf[PATHLEN];
    int             more_result;

    if (fhdr->owner[0] == '-' || fhdr->filename[0] == 'L' || !fhdr->filename[0])
	return READ_SKIP;

    STATINC(STAT_READPOST);
    setdirpath(genbuf, direct, fhdr->filename);

#ifdef USE_LIVE_ALLPOST
    do {
        static char allpost_base[PATHLEN] = "";
        static int allpost_base_len = 0;
        char bnbuf[IDLEN + 1] = "";
        const char *bn;
        int bnlen;

        if (!*allpost_base) {
            setbpath(allpost_base, BN_ALLPOST);
            strlcat(allpost_base, "/", sizeof(allpost_base));
            allpost_base_len = strlen(allpost_base);
        }

        if (!allpost_base_len)
            break;
        if (strncmp(allpost_base, genbuf, allpost_base_len) != 0)
            break;

        if (*fhdr->title && fhdr->title[strlen(fhdr->title) - 1] == ')') {
            // new format:  (BOARD)
            bn = strrchr(fhdr->title, '(');
            if (!bn)
                break;
            bnlen = strlen(++bn) - 1;
            snprintf(bnbuf, sizeof(bnbuf), "%*.*s", bnlen, bnlen, bn);
        } else {
            // old format:  .BOARD��, which may conflict with board names.
            bn = strrchr(fhdr->title, '.');
            if (!bn)
                break;
            bnlen = strlen(++bn) - 2;
            snprintf(bnbuf, sizeof(bnbuf), "%*.*s", bnlen, bnlen, bn);
        }
        setbfile(genbuf, bnbuf, fhdr->filename);

#ifdef DEBUG
        vmsgf("redirect: %s", genbuf);
#endif
    } while (0);
#endif

    more_result = more(genbuf, YEA);

    LOG_IF(LOG_CONF_CRAWLER, {
           // kcwu: log crawler
           static int read_count = 0;
           extern Fnv32_t client_code;
           ++read_count;
           syncnow();
           if (read_count % 1000 == 0)
               log_filef("log/read_alot", LOG_CREAT,
                        "%d %s %d %s %08x %d\n", (int)now, Cdate(&now), getpid(),
                        cuser.userid, client_code, read_count);
           });

    {
	int posttime=atoi(fhdr->filename+2);
	if(posttime>now-12*3600)
	    STATINC(STAT_READPOST_12HR);
	else if(posttime>now-1*DAY_SECONDS)
	    STATINC(STAT_READPOST_1DAY);
	else if(posttime>now-3*DAY_SECONDS)
	    STATINC(STAT_READPOST_3DAY);
	else if(posttime>now-7*DAY_SECONDS)
	    STATINC(STAT_READPOST_7DAY);
	else
	    STATINC(STAT_READPOST_OLD);
    }
    brc_addlist(fhdr->filename, fhdr->modified);
    strlcpy(currtitle, subject(fhdr->title), sizeof(currtitle));

    switch(more_result)
    {
	case -1:
	    clear();
	    vmsg("���峹�L���e");
	    return FULLUPDATE;
	case RET_DOREPLY:
	case RET_DOREPLYALL:
	    do_reply(fhdr);
            return FULLUPDATE;
	case RET_DORECOMMEND:
            recommend(ent, fhdr, direct);
	    return FULLUPDATE;
	case RET_DOQUERYINFO:
	    view_postinfo(ent, fhdr, direct, b_lines-3);
	    return FULLUPDATE;
        case RET_EDITPOST:
            edit_post(ent, fhdr, direct);
            return FULLUPDATE;
        case RET_EDITTITLE:
            edit_title(ent, fhdr, direct);
            return FULLUPDATE;
    }
    if(more_result)
	return more_result;
    return FULLUPDATE;
}

void
editLimits(unsigned char *plogins,
           unsigned char *pbadpost)
{
    char genbuf[STRLEN];
    int  temp, y;

    // load var
    unsigned char
	logins  = *plogins,
	badpost = *pbadpost;

    // query UI
    y = b_lines - 12;
    move(y, 0); clrtobot();

    y++;
    move(y+2, 0); clrtobot();
    outs(ANSI_COLOR(1;32)
         "�Ъ`�N�u���U�ɶ��v�w�Q����(��" STR_LOGINDAYS "���N)�A�Y�z���쪺�ק�ШD\n"
         "�����ӼƭȡA�Э��s�V���X�ӽЪ̽T�{�C �t�~�Ъ`�N�o�O�@�Ѥ@���p��A���O\n"
         "�ª��W�����ơC  �����O�D�i�ள�ª��W�����ƨӥӽСA�y���ϥΪ̩��M�L�k�o��\n"
         "���O�D�S�H���S��L�C �]�w�e�и�O�D�T�{�L�o���O�ª��]�w�ƭȡC\n"
        ANSI_RESET);

    sprintf(genbuf, "%u", logins*10);
    do {
	getdata_buf(y, 0,
                STR_LOGINDAYS "�U�� (0~2550,�H10�����,�Ӧ�Ʀr�N�۰ʱ˥h)�G",
                genbuf, 5, NUMECHO);
	temp = atoi(genbuf);
    } while (temp < 0 || temp > 2550);
    logins = (unsigned char)(temp / 10);
    move(y+1, 0); clrtobot();

    y++;
    temp = 255 - (unsigned int)badpost;
    sprintf(genbuf, "%u", temp);
    do {
	getdata_buf(y, 0,
		"�h��g�ƤW�� (0~255)�G", genbuf, 5, NUMECHO);
	temp = atoi(genbuf);
    } while (temp < 0 || temp > 255);
    badpost = (unsigned char)(255 - temp);
    vmsgf("editLimits: logins=%u, badpost=%u",
          (unsigned int)logins,
          (unsigned int)badpost);

    // save var
    *plogins  = logins;
    *pbadpost = badpost;
}

int
do_limitedit(int ent, fileheader_t * fhdr, const char *direct)
{
    char buf[STRLEN];
    boardheader_t  *bp = getbcache(currbid);

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    if (!((currmode & MODE_BOARD) || HasUserPerm(PERM_BOARD) ||
		(HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP())))
	return DONOTHING;

    strcpy(buf, "��� ");
    if (HasUserPerm(PERM_BOARD) || (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()))
	strcat(buf, "(A)���O�o���� ");
    strcat(buf, "(B)���O�w�]");
    if (fhdr->filemode & FILE_VOTE)
	strcat(buf, " (C)���g");
    strcat(buf, "�s�p���� (Q)�����H[Q]");
    buf[0] = vans(buf);

    if ((HasUserPerm(PERM_BOARD) || (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP())) && buf[0] == 'a') {

	editLimits(
		&bp->post_limit_logins,
		&bp->post_limit_badpost);

	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	log_usies("SetBoard", bp->brdname);
	vmsg("�ק粒���I");
	return FULLUPDATE;

    } else if (buf[0] == 'b') {

	editLimits(
		&bp->vote_limit_logins,
		&bp->vote_limit_badpost);

	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	log_usies("SetBoard", bp->brdname);
	vmsg("�ק粒���I");
	return FULLUPDATE;

    } else if ((fhdr->filemode & FILE_VOTE) && buf[0] == 'c') {
        if (currmode & MODE_SELECT) {
            vmsg("�аh�X�j�M�Ҧ���A�]�w�C");
            return FULLUPDATE;
        }

	editLimits(
		&fhdr->multi.vote_limits.logins,
		&fhdr->multi.vote_limits.badpost);
        if (modify_dir_lite(direct, ent, fhdr->filename, 0, NULL, NULL, NULL, 0,
                            &fhdr->multi, 0, 0) != 0) {
            vmsg("�ק異�ѡA�Э��s�i�J�ݪO�A�ոաC");
            return FULLUPDATE;
        }

	vmsg("�ק粒���I");
	return FULLUPDATE;
    }

    vmsg("�����ק�");
    return FULLUPDATE;
}

/* ----------------------------------------------------- */
/* �Ķ���ذ�                                            */
/* ----------------------------------------------------- */
static int
b_man(void)
{
    char apath[PATHLEN], backup_path[PATHLEN];

#ifdef USE_TIME_CAPSULE
    setbdir(backup_path, currboard);
#else
    boardheader_t *bp = getbcache(currbid);
    setbdir(backup_path, (bp->brdattr & BRD_HIDE) ? BN_JUNK : BN_DELETED);
#endif
    setapath(apath, currboard);

    if ((currmode & MODE_BOARD) || HasUserPerm(PERM_BOARD)) {
	char rebuild_path[PATHLEN];
	int  fd;

	snprintf(rebuild_path, sizeof(rebuild_path), "%s/.rebuild", apath);
	if ((fd = OpenCreate(rebuild_path, O_RDWR)) > 0)
	    close(fd);
    }

    return a_menu(currboard, apath,HasUserPerm(PERM_BOARD) ? 2 :
		  (currmode & MODE_BOARD ? 1 : 0),
		  currbid, // getbnum(currboard)?
		  NULL, backup_path);
}

static int
cite_post(int ent GCC_UNUSED, const fileheader_t * fhdr,
          const char *direct GCC_UNUSED)
{
    char            fpath[PATHLEN];
    char            title[TTLEN + 1];

    setbfile(fpath, currboard, fhdr->filename);
    strlcpy(title, "�� ", sizeof(title));
    strlcpy(title + 3, fhdr->title, TTLEN - 3);
    title[TTLEN] = '\0';
    a_copyitem(fpath, title, 0, 1);
    b_man();
    return FULLUPDATE;
}

int
edit_title(int ent, fileheader_t * fhdr, const char *direct)
{
    fileheader_t    tmpfhdr;
    char            genbuf[PATHLEN] = "";
    int allow = 0;

    // should we allow edit-title here?
    if (currstat == RMAIL)
	allow = 0;
    else if (HasUserPerm(PERM_BOARD))
	allow = 2;
    else if (strcmp(BN_ALLPOST, currboard) == 0)
        allow = 0;
    else if (currmode & MODE_BOARD || is_file_owner(fhdr, &cuser))
	allow = 1;

    if (!allow || !fhdr)
	return DONOTHING;

    if (currmode & MODE_SELECT) {
        vmsg("�аh�X�j�M�Ҧ���A�]�w�C");
        return READ_REDRAW;
    }
    memcpy(&tmpfhdr, fhdr, sizeof(tmpfhdr));

    if (fhdr && fhdr->title[0])
	strlcpy(genbuf, fhdr->title, TTLEN+1);

    if (getdata_buf(b_lines - 1, 0, "���D�G", tmpfhdr.title, TTLEN, DOECHO)) {
	// check TN_ANNOUNCE again for non-BMs...
	tn_safe_strip(tmpfhdr.title);
    }

    if (allow >= 2) {
        char datebuf[6];
        getdata_buf(b_lines - 1, 0, "�@�̡G", tmpfhdr.owner, IDLEN + 2, DOECHO);
        getdata_str(b_lines - 1, 0, "����G", datebuf, 6, DOECHO, tmpfhdr.date);
        // Normalize date to %.5s
        snprintf(tmpfhdr.date, sizeof(tmpfhdr.date), "%5.5s", datebuf);
    }
    if (memcmp(&tmpfhdr, fhdr, sizeof(tmpfhdr)) == 0)
        return FULLUPDATE;

    if (allow >= 2) {
        // Render in b_lines -2
        move(b_lines - 4, 0); clrtobot();
        prints("\n%11s%s %-*s %s", "", tmpfhdr.date, IDLEN, tmpfhdr.owner,
               tmpfhdr.title);
    }

    getdata(b_lines - 1, 0, "�T�w(Y/N)?[n] ", genbuf, 3, LCECHO);
    if (genbuf[0] != 'y')
        return FULLUPDATE;

    if (modify_dir_lite(direct, ent, fhdr->filename, 0, tmpfhdr.title,
                        tmpfhdr.owner, tmpfhdr.date, 0, NULL, 0, 0) != 0) {
        vmsg("��p�A�t�Φ��L���A�еy��A�աC");
        return FULLUPDATE;
    }
    LOG_IF(LOG_CONF_EDIT_TITLE,
           log_filef("log/edit_title.log", LOG_CREAT,
                     "%s %s(%d) %s %s=>%s %s=>%s %s => %s\n", Cdatelite(&now),
                     cuser.userid, allow, currboard, fhdr->owner, tmpfhdr.owner,
                     fhdr->date, tmpfhdr.date, fhdr->title, tmpfhdr.title));
    // Sync caller.
    memcpy(fhdr, &tmpfhdr, sizeof(*fhdr));
    return FULLUPDATE;
}

static int
do_add_recommend(const char *direct, fileheader_t *fhdr,
		 int ent, const char *buf, int type)
{
    char    path[PATHLEN];
    int     update = 0;
    int fd;
    BEGINSTAT(STAT_DORECOMMEND);

    /*
      race here:
      ���F��� system calls , �{�b�����η�e������� +1 �g�J .DIR ��.
      �y��
      1.�Y�Ӥ��ɦW�Q��������, ����N�g�����ɦW�� (�y�����F��)
      2.�S�����sŪ�@��, �ҥH����ƥi��Q�ֺ�
      3.�Y�����ɭԫe��Q�R, �N�[���媺�����

     */

    // Lock and append, (lock may be caused other add_recommend or edit_post)
    setdirpath(path, direct, fhdr->filename);
    fd = open(path, O_APPEND | O_WRONLY);
    if (fd >= 0) {
#ifdef EDITPOST_SMARTMERGE
        int lock_retry = 5, lock_wait = 1, lock_success = 0;
        while (lock_retry-- > 0) {
            // try several times
            if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
                move(b_lines, 0);
                SOLVE_ANSI_CACHE();
                prints("==> �ɮץ��Q���H�s�褤�A���ݧ���: %d\n", lock_retry+1);
                doupdate();
                sleep(lock_wait);
                // reopen the file because edit_post creates a new file.
                close(fd);
                fd = open(path, O_APPEND | O_WRONLY);
                continue;
            }
            lock_success = 1;
            write(fd, buf, strlen(buf));
            flock(fd, LOCK_UN);
            break;
        }
        close(fd);
        if (!lock_success) {
            vmsg("���~: �ɮץ��Q���H�s�褤�A�L�k�g�J�C");
            goto error;
        }
#else
        write(fd, buf, strlen(buf));
        close(fd);
#endif
    } else {
	vmsg((errno == EROFS) ? "���~: �t�Υثe��Ū���A�L�k�ק�C" :
             "���~: ���ɮפw�Q�R���C �L�k�g�J�C");
	goto error;
    }

    // XXX do lock some day!

    /* This is a solution to avoid most racing (still some), but cost four
     * system calls.                                                        */

    if(type == RECTYPE_GOOD && fhdr->recommend < MAX_RECOMMENDS )
          update = 1;
    else if(type == RECTYPE_BAD && fhdr->recommend > -MAX_RECOMMENDS)
          update = -1;
    fhdr->recommend += update;

    // since we want to do 'modification'...
    fhdr->modified = dasht(path);

    if (fhdr->modified > 0)
    {
	if (modify_dir_lite(direct, ent, fhdr->filename,
		fhdr->modified, NULL, NULL, NULL, update, NULL, 0, 0) < 0)
	    goto error;
	// mark my self as "read this file".
	brc_addlist(fhdr->filename, fhdr->modified);
    }

    ENDSTAT(STAT_DORECOMMEND);
    return 0;

 error:
    ENDSTAT(STAT_DORECOMMEND);
    return -1;
}

int
recommend(int ent, fileheader_t * fhdr, const char *direct)
{
    char            buf[PATHLEN], msg[STRLEN];
    const char	    *myid = cuser.userid;
    char	    aligncmt = 0;
    char	    mynick[IDLEN+1];
#ifndef OLDRECOMMEND
    static const char *ctype[RECTYPE_SIZE] = {
		       "��", "�N", "��",
		   };
    static const char *ctype_attr[RECTYPE_SIZE] = {
		       ANSI_COLOR(1;33),
		       ANSI_COLOR(1;31),
		       ANSI_COLOR(1;37),
		   }, *ctype_long[RECTYPE_SIZE] = {
		       "�ȱo����",
		       "�����N�n",
		       "�u�[������",
		   };
#endif
    int             type, maxlength;
    boardheader_t  *bp;
    static time4_t  lastrecommend = 0;
    static char lastrecommend_fname[FNLEN] = "";
    int isGuest = (strcmp(cuser.userid, STR_GUEST) == EQUSTR);
    int logIP = 0;
    int ymsg = b_lines -1;
    const char *reason = "�v������";

    if (!fhdr || !fhdr->filename[0])
	return DONOTHING;

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);

    if (bp->brdattr & BRD_NORECOMMEND || fhdr->filename[0] == 'L' ||
        ((fhdr->filemode & FILE_MARKED) && (fhdr->filemode & FILE_SOLVED))) {
	vmsg("��p, �T�����");
	return FULLUPDATE;
    }
    if (!CheckPostPerm2(&reason) || isGuest)
    {
	vmsgf("�L�k����: %s", reason); //  "(�i���j�g I �d�ݭ���)"
	return FULLUPDATE;
    }

    // TODO ���ӥi�H�Ҽ{�@�����ﶵ�i�]�w�C
#ifdef BN_ONLY_OP_CAN_ADD_COMMENT
    if (  strcmp(bp->brdname, BN_ONLY_OP_CAN_ADD_COMMENT) == 0 &&
	!((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP|PERM_SYSSUPERSUBOP|PERM_SYSSUBOP)) )
    {
	vmsg("���O���孭�w�޲z�H���ϥΡC");
	return FULLUPDATE;
    }
#endif

#ifdef SAFE_ARTICLE_DELETE
    if (fhdr->filename[0] == '.' || fhdr->owner[0] == '-') {
	vmsg("����w�R��");
	return FULLUPDATE;
    }
#endif

    if ((bp->brdattr & BRD_VOTEBOARD) || (fhdr->filemode & FILE_VOTE))
    {
	do_voteboardreply(fhdr);
	return FULLUPDATE;
    }

#ifndef DEBUG
    if (get_board_restriction_reason(currbid, sizeof(msg), msg))
    {
	vmsgf("���F�ݪO�o�孭��: %s", msg);
	return FULLUPDATE;
    }
#endif

    aligncmt = (bp->brdattr & BRD_ALIGNEDCMT) ? 1 : 0;
    if((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP))
    {
	/* I'm BM or SYSOP. */
    }
    else if (bp->brdattr & BRD_NOFASTRECMD)
    {
	int d = (int)bp->fastrecommend_pause - (now - lastrecommend);
	if (d > 0)
	{
	    vmsgf("���O�T��ֳt�s�����A�ЦA�� %d ��", d);
	    return FULLUPDATE;
	}
    }
    {
	// kcwu
	static unsigned char lastrecommend_minute = 0;
	static unsigned short recommend_in_minute = 0;
	unsigned char now_in_minute = (unsigned char)(now / 60);
	if(now_in_minute != lastrecommend_minute) {
	    recommend_in_minute = 0;
	    lastrecommend_minute = now_in_minute;
	}
	recommend_in_minute++;
	if(recommend_in_minute>60) {
	    vmsg("�t�θT��u�ɶ����j�q����");
	    return FULLUPDATE;
	}
    }
    {
	// kcwu
	char path[PATHLEN];
	off_t size;
	setdirpath(path, direct, fhdr->filename);
	size = dashs(path);
	if (size > 5*1024*1024) {
	    vmsg("�ɮפӤj, �L�k�~�����, �Хt����o��");
	    return FULLUPDATE;
	}

	if (size > 100*1024) {
	    int d = 10 - (now - lastrecommend);
	    if (d > 0) {
		vmsgf("����w�L��, �T��ֳt�s�����, �ЦA�� %d ��", d);
		return FULLUPDATE;
	    }
	}
    }


#ifdef USE_COOLDOWN
       if(check_cooldown(bp))
	  return FULLUPDATE;
#endif

    type = RECTYPE_GOOD;

    // why "recommend == 0" here?
    // some users are complaining that they like to fxck up system
    // with lots of recommend one-line text.
    // since we don't support recognizing update of recommends now,
    // they tend to use the counter to identify whether an arcitle
    // has new recommends or not.
    // so, make them happy here.
#ifndef OLDRECOMMEND
    // no matter it is first time or not.
    if (is_file_owner(fhdr, &cuser))
#else
    // old format is one way counter, so let's relax.
    if (fhdr->recommend == 0 && is_file_owner(fhdr, &cuser))
#endif
    {
	// owner recommend
	type = RECTYPE_ARROW;
	move(ymsg--, 0); clrtoeol();
#ifndef OLDRECOMMEND
	outs("�@�̥��H, �ϥ� �� �[���覡\n");
#else
	outs("�@�̥��H����, �ϥ� �� �[���覡\n");
#endif

    }
#ifndef DEBUG
    else if (!(currmode & MODE_BOARD) &&
	    (now - lastrecommend) < (
#if 0
	    /* i'm not sure whether this is better or not */
		(bp->brdattr & BRD_NOFASTRECMD) ?
		 bp->fastrecommend_pause :
#endif
		90))
    {
	// too close
	type = RECTYPE_ARROW;
	move(ymsg--, 0); clrtoeol();
	outs("�ɶ��Ӫ�, �ϥ� �� �[���覡\n");
    }
#endif

#ifndef OLDRECOMMEND
    else
    {
	int i;

	move(b_lines, 0); clrtoeol();
	outs(ANSI_COLOR(1)  "�zı�o�o�g�峹 ");

	for (i = 0; i < RECTYPE_SIZE; i++)
	{
	    if (i == RECTYPE_BAD && (bp->brdattr & BRD_NOBOO))
		continue;
	    outs(ctype_attr[i]);
	    prints("%d.", i+1);
	    outs(ctype_long[i]);
	    outc(' ');
	}
	prints(ANSI_RESET "[%d]? ",
		RECTYPE_DEFAULT+1);

	type = vkey();

	if (!isascii(type) || !isdigit(type))
	{
	    type = RECTYPE_DEFAULT;
	} else {
	    type -= '1';
	    if (type < 0 || type > RECTYPE_MAX)
		type = RECTYPE_DEFAULT;
	}

	if( (bp->brdattr & BRD_NOBOO) && (type == RECTYPE_BAD))
	    type = RECTYPE_ARROW;
	assert(type >= 0 && type <= RECTYPE_MAX);

	move(b_lines, 0); clrtoeol();
    }
#endif

    // warn if article is outside post
    if (strchr(fhdr->owner, '.')  != NULL)
    {
	move(ymsg--, 0); clrtoeol();
	outs(ANSI_COLOR(1;31)
	    "���o�g�峹�Ӧ۰ΦW�O�Υ~����H�O�A��@�̥i��L�k�ݨ����C"
	    ANSI_RESET "\n");
    }

    // warn if in non-standard mode
    {
	char *p = strrchr(direct, '/');
	// allow .DIR or .DIR.bottom
	if (!p || strncmp(p+1, FN_DIR, strlen(FN_DIR)) != 0)
	{
	    ymsg --;
	    move(ymsg--, 0); clrtoeol();
	    outs(ANSI_COLOR(1;33)
	    "���z���b�j�M(���D�B�@��...)�Ψ䥦�S��C��Ҧ��A"
	    "����p�ƻP�ק�O���N�|���}�p��C"
	    ANSI_RESET "\n"
	    "  �Y�Q���`�p�ƽХ�����h�^���`�C��Ҧ��C\n");
	}
    }

    if(type >  RECTYPE_MAX || type < 0)
	type = RECTYPE_ARROW;

    maxlength = 78 -
	3 /* lead */ -
	6 /* date */ -
	1 /* space */ -
	6 /* time */;

    if (bp->brdattr & BRD_IPLOGRECMD || isGuest)
    {
	maxlength -= 15 /* IP */;
	logIP = 1;
    }

#if defined(PLAY_ANGEL)
    if (HasUserPerm(PERM_ANGEL) && (bp->brdattr & BRD_ANGELANONYMOUS) &&
	vans("�n�ϥΤp�ѨϰΦW����ܡH [Y/n]: ") != 'n')
    {
	// angel push
	mynick[0] = 0;
	angel_load_my_fullnick(mynick, sizeof(mynick));
	myid = mynick;
    }
#endif

    if (aligncmt)
    {
	// left align, voted by LydiaWu and LadyNotorious
	snprintf(buf, sizeof(buf), "%-*s", IDLEN, myid);
	strlcpy(mynick, buf, sizeof(mynick));
	myid = mynick;
    }

#ifdef OLDRECOMMEND
    maxlength -= 2; /* '��' */
    maxlength -= strlen(myid);
    sprintf(buf, "%s %s:", "��" , myid);

#else // !OLDRECOMMEND
    maxlength -= strlen(myid);
    sprintf(buf, "%s%s%s %s:",
	    ctype_attr[type], ctype[type], ANSI_RESET, myid);
#endif // !OLDRECOMMEND

    move(b_lines, 0);
    clrtoeol();

    if (!getdata(b_lines, 0, buf, msg, maxlength, DOECHO))
	return FULLUPDATE;

    // make sure to do modification
    {
        // to hold ':wq', ':q!' 'ZZ'
	char ans[2];
	sprintf(buf+strlen(buf),
		ANSI_REVERSE "%-*s" ANSI_RESET " �T�w[y/N]:",
		maxlength, msg);
	move(b_lines, 0);
	clrtoeol();
	if(!getdata(b_lines, 0, buf, ans, sizeof(ans), LCECHO))
            return FULLUPDATE;
        if (ans[0] == 'y' ||
            strncmp(ans, ":w", 2) == 0 ||
            strcmp(ans, "zz") == 0) {
            // success!
        } else
            return FULLUPDATE;
    }
    STATINC(STAT_RECOMMEND);
    LOG_IF(LOG_CONF_PUSH, log_filef("log/push", LOG_CREAT,
                                    "%s %d %s %s %s\n", cuser.userid,
                                    (int)now, currboard, fhdr->filename, msg));
#ifdef USE_COMMENTD
    if (CommentsAddRecord(bp->brdname, fhdr->filename, type, msg)) {
        vmsg("���~: ��Ʈw�s�u���`�A�L�k�g�J�C�еy�ԦA�աC");
        return FULLUPDATE;
    }
#endif

    {
	/* build tail first. */
	char tail[STRLEN];

	if(logIP)
	{
	    snprintf(tail, sizeof(tail),
		    "%15s %s",
		    FROMHOST,
		    Cdate_mdHM(&now));
	} else {
	    snprintf(tail, sizeof(tail),
		    " %s",
		    Cdate_mdHM(&now));
	}

        FormatCommentString(buf, sizeof(buf), type,
                            myid, maxlength, msg, tail);
    }

    if (do_add_recommend(direct, fhdr,  ent, buf, type) < 0)
        return DIRCHANGED;

    lastrecommend = now;
    strlcpy(lastrecommend_fname, fhdr->filename, sizeof(lastrecommend_fname));
    return FULLUPDATE;
}

int
del_range(int ent GCC_UNUSED, const fileheader_t *fhdr GCC_UNUSED,
          const char *direct, const char *backup_direct)
{
    char            numstr[8];
    int             num1, num2, num, cdeleted = 0;
    fileheader_t *recs = NULL;
    int ret = 0;
    int check_mark = 1, check_digest = 0;

    /* ���T�ر��p�|�i�o��, �H��, �ݪO, ��ذ� */
    // �ˬd����k�O�� *direct:
    //  'b' = �ݪO boards/%c/%s/*
    //  'm' = ��ذ� man/.../* or man/boards/%c/%s/.../*
    //  'h' = �H�c home/%c/%s/* �ΫH�c��ذ� home/%c/%s/man/.../*
    int is_board = (*direct == 'b'),
        // is_home = (*direct == 'h'),
        is_man = (*direct == 'm');

#ifdef SAFE_ARTICLE_DELETE
    int use_safe_delete = 0;

    if (is_board) {
        boardheader_t  *bp = getbcache(currbid);
        if(!(currmode & MODE_DIGEST) &&
                bp->nuser >= SAFE_ARTICLE_DELETE_NUSER)
            use_safe_delete = 1;
    }
#endif

    /* rocker.011018: �걵�Ҧ��U�٬O�����\�R������n */
    if (currmode & MODE_SELECT) {
	vmsg("�Х��^�쥿�`�Ҧ���A�i��R��...");
	return FULLUPDATE;
    }

    // let's do a full screen delete.
    clear();
    vs_hdr("�R���d��");

    getdata(2, 0, "�_�I: ", numstr, 7, DOECHO);
    num1 = atoi(numstr);
    if (num1 <= 0) {
        vmsg("�_�I���~");
        return FULLUPDATE;
    }
    getdata(3, 0, "���I: ",numstr, 7, DOECHO);
    num2 = atoi(numstr);
    if (num2 < num1) {
        vmsg("���I���~");
        return FULLUPDATE;
    }
    num = num2 - num1 + 1;
    if (num > 1000) {
        vmsg("�ФŤ@���R���W�L 1000 �g�C");
        return FULLUPDATE;
    }

    // verify the results
    // TODO kcwu suggested to check only first/end and compare
    // timestamp. that's a good idea and more efficient.
    recs = (fileheader_t*) malloc ( num * sizeof(fileheader_t));
    if (!recs ||
        get_records(direct, recs, sizeof(fileheader_t), num1, num) != num) {
        free(recs);
        vmsg("�L�k���o���w�d�򪺸�T�A�аh�X��y�ԦA��");
        return FULLUPDATE;
    }
    mvprints(5, 0, "#%06d %-*s %s\n"
                   " . . .\n"
                   "#%06d %-*s %s\n",
                   num1, IDLEN, recs[0].owner, recs[0].title,
                   num2, IDLEN, recs[num-1].owner, recs[num-1].title);

    // HACK: warn if target is man.
    if (is_man) {
        outs("(�d�򤺪��l�ؿ��|�Q�۰ʸ��L�A�Хt��Τp d �R��)\n");
        // do not check mark in man
        check_mark = 0;
    } else if (is_board) {
        check_digest = 1;
    }

    getdata(10, 0, msg_sure_ny, numstr, 3, LCECHO);
    if (*numstr != 'y') {
        free(recs);
        return FULLUPDATE;
    }

    // ready to start.
    outmsg("�B�z��,�еy��...");
    refresh();
    ret = 0;
    LOG_IF((LOG_CONF_MASS_DELETE && (num >= 50)),
           log_filef("log/del_range.log", LOG_CREAT,
                     "%s range %d->%d %s [%s]\n",
                     cuser.userid, num1, num2,
                     Cdate(&now), direct));
    do {
        int id = num1, i;
        for (i = 0; ret == 0 && i < num; i++) {
            // first, check if the record is ready for being deleted.
            fileheader_t *fh = recs + i;
            const char *bypass = NULL;

            if (check_mark && fh->filemode & FILE_MARKED) {
                bypass = "�аO�� m ������";
            } else if (check_digest && fh->filemode & FILE_DIGEST) {
                /* ��K , FILE_DIGEST is used as REPLIED in mail menu.*/
                bypass = "��K";
            } else {
                char xfpath[PATHLEN];
                setdirpath(xfpath, direct, fh->filename);
                if (dashd(xfpath)) {
                    bypass = "�l�ؿ�";
                }
            }

            if (bypass) {
                id++;
                mvprints(b_lines-1, 0, "���L%s: %s\n", bypass, fh->title);
                doupdate();
                continue;
            }

#ifdef SAFE_ARTICLE_DELETE
            if (use_safe_delete &&
                safe_article_delete(id, fh, direct, NULL) == 0) {
                id++;
            }
            else
#endif
            if (delete_fileheader(direct, fh, id) == 0) {
                // no need to add id
            } else {
                ret = -1;
                break;
            }
            if (!IS_DELETE_FILE_CONTENT_OK(
                        delete_file_content(direct, fh,
                                            backup_direct, NULL, 0))) {
                ret = -1;
                break;
            }
            cdeleted++;
        }
    } while (0);

    // clean up
    free(recs);
    fixkeep(direct, num1);

    if (ret < 0) {
        clear();
        vs_hdr("�����R������");
        prints("\n\n�w�R���F %d ���ɮסA���L�k�R���䥦�ɮסC\n", cdeleted);
        outs( "�i��O�P�ɦ��䥦�H�]�b�i��R���C�аh�X���ؿ���A���աC\n\n"
              "�Y�����~����o�͡A�е����@�p�ɫ�A���աC\n\n"
              "�Y��ɤ��L�k�R���A�Ш� " BN_BUGREPORT " �ݪO���i�C\n");
        vmsg("�L�k�R���C�i�঳�䥦�H���b�P�ɧR���C");
    }

    return (ret < 0 || cdeleted > 0) ? DIRCHANGED : FULLUPDATE;
}

static int
del_range_post(int ent, fileheader_t * fhdr, char *direct)
{
    int ret = 0;
   if (!(currmode & MODE_BOARD))
        return DONOTHING;

    if (currbid) {
        boardheader_t *bp = getbcache(currbid);
        assert(bp);
        if (is_readonly_board(bp->brdname))
            return DONOTHING;
    }

    ret = del_range(ent, fhdr, direct, direct);
    if (ret == DIRCHANGED) {
        setbtotal(currbid);
    }
    return ret;
}

static int
del_post(int ent, fileheader_t * fhdr, char *direct)
{
    char	    reason[PROPER_TITLE_LEN] = "";
    char            genbuf[100], newpath[PATHLEN] = "";
    int             not_owned, is_anon, tusernum, del_ret = 0, as_badpost = 0;
    boardheader_t  *bp;

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    bp = getbcache(currbid);

    if (is_readonly_board(bp->brdname))
	return DONOTHING;

    /* TODO recursive lookup */
    if (currmode & MODE_SELECT) {
        vmsg("�Ц^��@��Ҧ��A�R���峹");
        return DONOTHING;
    }

    /* DIGEST is not visible to users... */
    if ((fhdr->filemode & FILE_BOTTOM) ||
        (fhdr->filemode & FILE_MARKED) || (fhdr->filemode & FILE_DIGEST)) {
        vmsg("�峹�Q�аO�θm���Φ��J��K�A�L�k�R���A�Ь��O�D");
        return DONOTHING;
    }

    if (fhdr->owner[0] == '-')
        return DONOTHING;

    is_anon = (fhdr->filemode & FILE_ANONYMOUS);
    if(is_anon)
	/* When the file is anonymous posted, fhdr->multi.anon_uid is author.
	 * see do_post_article() */
        tusernum = fhdr->multi.anon_uid;
    else
        tusernum = searchuser(fhdr->owner, NULL);

    not_owned = (tusernum == usernum ? 0: 1);
    if (!(currmode & MODE_BOARD) &&
        (not_owned || !is_file_owner(fhdr, &cuser)))
            return DONOTHING;

    if (((bp->brdattr & BRD_VOTEBOARD) && !HasUserPerm(PERM_BOARD)) ||
	!strcmp(cuser.userid, STR_GUEST))
	return DONOTHING;

    // user and permission check
    // reason 1: BM may alter post restrictions to this board
    // reason 2: this account may be occupied by someone else.
    if (!HasUserPerm(PERM_BASIC) ||	// including guests
	!( (currmode & MODE_DIGEST) ? (currmode & MODE_BOARD) : CheckPostPerm() ) || // allow BM to delete posts in digest mode
	!CheckPostRestriction(currbid)
	)
	return DONOTHING;

    if ((bp->brdattr & BRD_NOSELFDELPOST) && !(currmode & MODE_BOARD)) {
        vmsg("��p�A���ݪO�ثe�T��ۧR�峹�C");
	return DONOTHING;
    }

    if (fhdr->filename[0]=='L') fhdr->filename[0]='M';

#ifdef SAFE_ARTICLE_DELETE
    // query if user really wants to delete it
    if (not_owned && !is_anon && fhdr->owner[0])
    {
	// manager (bm, sysop, police)
	do {
	    getdata(1, 0, "�нT�w�R��(Y/N/R�[���z��)?[N]", genbuf, 3, LCECHO);

	    // for y/n, skip.
	    if (genbuf[0] != 'r')
		break;

	    // build reason string (based on STR_SAFEDEL_TITLE)
	    snprintf(reason, sizeof(reason), "(�w�Q%s�R��) <%s>",
		    cuser.userid, fhdr->owner);
	    move(3, 0); clrtoeol();
	    getdata_str(2, 0, " >> �п�J�R����n��ܪ����D: �� ",
		    reason, sizeof(reason), DOECHO, reason);

	    if (!reason[0])
	    {
		vmsg("����J�z�ѡA���R���C");
		genbuf[0] = 'n';
		break;
	    }

	    // confirm again!
	    move(4, 0); clrtoeol();
	    getdata(3, 0, "�ЦA���T�w�O�_�n�ΤW�z�z�ѧR��(Y/N)?[N]",
		    genbuf, 3, LCECHO);

	    // since the default y/n is same to msg_del_ny, we reuse the genbuf[0] here.
	} while (0);
    } else
#endif
    {
	getdata(1, 0, msg_del_ny, genbuf, 3, LCECHO);
    }

    if (genbuf[0] == 'y') {

        // Must have enough money.
        // ����e����峹�����d�M��
        int del_fee = fhdr->multi.money;
        // freebn/brd_bad: should be done before, but let's make it safer.
        // new rule: only articles with money need updating
        // numpost (to solve deleting cross-posts).
        // DIGEST mode ���κ�
        // INVALIDMONEY_MODES (FILE_BID, FILE_ANONYMOUS, ...) �]�����Φ�
        // also check MAX_POST_MONEY in case any error made bad money...
        if (del_fee < 0 ||
            IsFreeBoardName(currboard) ||
            (currbrdattr & BRD_NOCREDIT) ||
            (currmode & MODE_DIGEST) ||
            (fhdr->filemode & INVALIDMONEY_MODES) ||
            del_fee > MAX_POST_MONEY ||
            0)
            del_fee = 0;

        if (!not_owned) {
            reload_money();
            if (cuser.money < del_fee) {
                vmsgf("�z���W��" MONEYNAME "�����A�R������n %d "
                      MONEYNAME, del_fee);
                return FULLUPDATE;
            }
            // after this, we cannot delay in !not_owned mode otherwise we'll get
            // a exploit by race condition.
        }

	if(
#ifdef SAFE_ARTICLE_DELETE
	   ((reason[0] || bp->nuser >= SAFE_ARTICLE_DELETE_NUSER) &&
            !(currmode & MODE_DIGEST) &&
            !safe_article_delete(ent, fhdr, direct, reason[0] ? reason : NULL)) ||
#endif
            !delete_fileheader(direct, fhdr, ent)) {
            // do this immediately after .DIR change in case connection
            // was closed.
	    setbtotal(currbid);

            del_ret = cancelpost(direct, fhdr, not_owned,
                                 newpath, sizeof(newpath), reason);
            // delete the file reference in BN_ALLPOST, or delete all related
            // posts if we (SYSOP) are working in a special board like
            // BN_NEWIDPOST.
            deleteCrossPost(fhdr, bp->brdname);

            move(b_lines - 10, 0); clrtobot();
            prints("\n���b�R���峹: %s\n", fhdr->title);

            // check delete_file_content for del_ret
            if (!IS_DELETE_FILE_CONTENT_OK(del_ret)) {
                outs("�ɮץi��w�Q���H�R���εo�Ϳ��~�A"
                     "�Y����o�ͽЦV" BN_BUGREPORT "���i\n");
            }
            if (del_ret == DELETE_FILE_CONTENT_BACKUP_FAILED) {
                outs(" " ANSI_COLOR(1;31) "* �ɮ׳ƥ����ѡA�Ц� "
                     BN_BUGREPORT "���i" ANSI_RESET "\n");
            }
#ifdef ASSESS
	    // badpost assignment

	    if (!not_owned || tusernum <= 0 || (currmode & MODE_DIGEST) ) {
                // case one, self-owned, invalid author, or digest mode - should not give bad posts
	    } else if (!IS_DELETE_FILE_CONTENT_OK(del_ret) || !*newpath) {
                // case 2, got error in file deletion (already deleted, also skip badpost)
                outs("�h��]�w: �w�R�ΧR�����~ (���L)\n");
	    } else if (now - atoi(fhdr->filename + 2) > 7 * 24 * 60 * 60) {
                // case 3, post older than one week (TODO use macro for the duration)
		outs("�h��]�w: �峹�W�L�@�g (���L)\n");
	    } else {
                // case 4, can assign badpost
		move_ansi(1, 40); clrtoeol();
		// TODO not_owned �ɤ]�n���� numpost?
                outs("�c�h�峹?(y/N) ");
                // FIXME ���O�D�|�b�o�̤��p���_���s�u�ҥH�n�p��...
                // ���n���Ƴ̦n�b�e���@���C
                vgets(genbuf, 3, VGET_LOWERCASE);

		if (genbuf[0]=='y') {
                    as_badpost = 1;
		    assign_badpost(getuserid(tusernum), fhdr, newpath, NULL);
                }
	    }
#endif // ASSESS
            if (*newpath && *reason) {
                log_filef(newpath, LOG_CREAT, "�� Delete Reason: %s\n", reason);
            }

            if (del_fee <= 0)
            {
                // no need to change user record
            }
	    else if (not_owned)
	    {
		// not owner case
		if (tusernum)
		{
		    userec_t xuser;
		    assert(tusernum != usernum);
		    // TODO we're doing redundant i/o here... merge and refine someday
		    if (passwd_sync_query(tusernum, &xuser) == 0) {
			if (xuser.numposts > 0)
			    xuser.numposts--;
			passwd_sync_update(tusernum, &xuser);
		    }

                    pay_as_uid(tusernum, del_fee,
                            "%s �ݪO �峹�u%s�v�Q%s�A�����Z�S%s %s",
                            currboard,  fhdr->title,
                            as_badpost ? "�h�^" : "�R��",
                            reason[0] ? "�C��]:" : "", reason);
                    sendalert_uid(tusernum, ALERT_PWD_PERM);
#ifdef USE_COOLDOWN
		    if (bp->brdattr & BRD_COOLDOWN)
			add_cooldowntime(tusernum, 15);
#endif
		}
	    }
	    else
	    {
		// owner case
		pwcuDecNumPost();
                pay(del_fee, "%s �ݪO �峹�ۧR�M��O: %s",
                        currboard, fhdr->title);
		sendalert(cuser.userid, ALERT_PWD_PERM);
		prints("�z���峹� %d �g�A��I�M��O %d " MONEYNAME "\n",
                        cuser.numposts, del_fee);
	    }
            pressanykey();
	    return DIRCHANGED;
	} else { // delete_fileheader
            vmsg("�L�k�R���ɮװO���C�еy�ԦA�աC");
            return DIRCHANGED;
        }
    } // genbuf[0] == 'y'
    return FULLUPDATE;
}

static int  // Ptt: �ץ��Y��
show_filename(int ent GCC_UNUSED, const fileheader_t * fhdr,
              const char *direct GCC_UNUSED)
{
    if(!HasUserPerm(PERM_SYSOP)) return DONOTHING;
    vmsgf("�ɮצW��: %s ", fhdr->filename);
    return PART_REDRAW;
}

static int
lock_post(int ent, fileheader_t * fhdr, const char *direct)
{
    char fn1[PATHLEN];
    char fnx[PATHLEN];
    char genbuf[PATHLEN] = "";
    int i;
    boardheader_t *bp = NULL;

    if (currstat == RMAIL)
	return DONOTHING;

    // SYSOP/POLICE can lock, BM can unlock
    if (!(currmode & MODE_BOARD) && !HasUserPerm(PERM_SYSOP | PERM_POLICE))
	return DONOTHING;

    bp = getbcache(currbid);
    assert(bp);

#ifdef USE_LIVE_ALLPOST
    // In case idiots do this in ALLPOST...
    if (strcmp(bp->brdname, BN_ALLPOST) == 0) {
        vmsgf("�Цܭ�ݪO��w�C%s �|�۰ʧ�s�C", BN_ALLPOST);
        return FULLUPDATE;
    }
#endif

    if (fhdr->filename[0]=='M') {
	if (!HasUserPerm(PERM_SYSOP | PERM_POLICE)) {
            vmsg("�����ίS��޲z�H���~�i�i����w�C�O�D�u��Ѱ���w�C");
	    return FULLUPDATE;
        }

	getdata(b_lines - 1, 0, "�п�J��w�z�ѡG", genbuf, 50, DOECHO);

	if (vans("�n�N�峹��w��(y/N)?") != 'y')
	    return FULLUPDATE;

        setbfile(fn1, currboard, fhdr->filename);

	for (i = 0; i < MAX_BMs && SHM->BMcache[currbid-1][i] > 0; i++)
            mail_id(SHM->userid[SHM->BMcache[currbid-1][i] - 1], genbuf, fn1,
                    "[���q��]");

        post_policelog2(currboard, fhdr->title, "���", genbuf, 1, fn1);

        // Rename the file to be secure, introducing new X-dot file here.
        // We can not use L-dot prefix since there may be some code that
        // doesn't recognize locked files and end up in reading the file.
        fhdr->filename[0] = 'X';
        setbfile(fnx, currboard, fhdr->filename);
        Rename(fn1, fnx);
        fhdr->filename[0] = 'L';
    } else if (fhdr->filename[0]=='L') {
	if (vans("�n�N�峹��w�Ѱ���(y/N)?") != 'y')
	    return FULLUPDATE;
        fhdr->filename[0] = 'X';
        setbfile(fnx, currboard, fhdr->filename);
        fhdr->filename[0] = 'M';
        setbfile(fn1, currboard, fhdr->filename);
        // Rename it back, no check for errors because old locked files
        // are not renamed.
        if (access(fnx, 0) == 0)
            Rename(fnx, fn1);
        post_policelog(currboard, fhdr->title, "���", genbuf, 0);
    } else {
        vmsg("�L�k�i����w�θѰ��C");
        return FULLUPDATE;
    }
    // TODO fix race condition here.
    substitute_ref_record(direct, fhdr, ent);
    syncnow();
    bp->SRexpire = now;
    return FULLUPDATE;
}

static int
change_post_mode(int ent, fileheader_t *fhdr, const char *direct,
                 int mode_mask)
{
    int ret = 0;
    char dirpath[PATHLEN];

    if (!(currmode & MODE_BOARD))
        return DONOTHING;

    if (currmode & MODE_SELECT) {
        if (!fhdr->multi.refer.flag) {
            vmsg("�аh�X�j�M�Ҧ��A�i�榹���]�w�C");
            return READ_REDRAW;
        }
        // Try to solve entx and direct. Note fhdr does not need to be changed.
        // Instead of updating original "direct", we simply wait for SRexpire to
        // change before return.
        ent = fhdr->multi.refer.ref;
        setdirpath(dirpath, direct, FN_DIR);
        direct = dirpath;
    }
    do {
        if (fhdr->filemode & mode_mask) {
            // clear
            if ((ret = modify_dir_lite(direct, ent, fhdr->filename, 0, NULL,
                                       NULL, NULL, 0, NULL, 0, mode_mask)) != 0)
                break;
            fhdr->filemode &= ~mode_mask;
        } else {
            // set
            if ((ret = modify_dir_lite(direct, ent, fhdr->filename, 0, NULL,
                                       NULL, NULL, 0, NULL, mode_mask, 0)) != 0)
                break;
            fhdr->filemode |= mode_mask;
        }
    } while (0);

    if (ret < 0) {
        vmsg("�]�w���ѡA�Э��i�ݪO��A�դ@���C");
        return FULLUPDATE;
    } else {
        boardheader_t *bp = getbcache(currbid);
        if (bp)
            bp->SRexpire = now;
    }

    check_locked(fhdr);
    return PART_REDRAW;
}

static int
solve_post(int ent, fileheader_t * fhdr, const char *direct)
{
    return change_post_mode(ent, fhdr, direct, FILE_SOLVED);
}

static int
mark_post(int ent, fileheader_t * fhdr, const char *direct)
{
    return change_post_mode(ent, fhdr, direct, FILE_MARKED);
}

static int
recommend_cancel(int ent, fileheader_t * fhdr, const char *direct)
{
    char yn[5];
    char fn[PATHLEN];

    if (!(currmode & MODE_BOARD))
	return DONOTHING;

    getdata(b_lines - 1, 0, "�T�w�n�����k�s[y/N]? ", yn, 3, LCECHO);
    if (yn[0] != 'y')
	return FULLUPDATE;
    fhdr->recommend = 0;
    // TODO fix race condition here.
    substitute_ref_record(direct, fhdr, ent);
    setdirpath(fn, direct, fhdr->filename);
    if (dashf(fn))
        log_filef(fn, LOG_CREAT, "��%s �� %s �N���˭��k�s\n", cuser.userid,
                  Cdatelite(&now));
    return FULLUPDATE;
}


static int
view_postinfo(int ent GCC_UNUSED, const fileheader_t * fhdr,
              const char *direct GCC_UNUSED, int crs_ln)
{
    aidu_t aidu = 0;
    int l = crs_ln + 3;  /* line of cursor */
    int area_l = l + 1;
#ifdef QUERY_ARTICLE_URL
    const int area_lines = 5;
#else
    const int area_lines = 4;
#endif

    if(!fhdr || fhdr->filename[0] == '.' || !fhdr->filename[0])
      return DONOTHING;

    if((area_l + area_lines > b_lines) ||  /* �U���񤣤U */
       (l >= (b_lines  * 2 / 3)))  /* ���W�L�e�� 2/3 */
      area_l -= (area_lines + 1);

    grayout(0, MIN(l - 1, area_l)-1, GRAYOUT_DARK);
    grayout(MAX(l + 1 + 1, area_l + area_lines), b_lines-1, GRAYOUT_DARK);
    grayout(l, l, GRAYOUT_BOLD);

    /* �M���峹���e�@��Ϋ�@�� */
    if(area_l > l)
      move(l - 1, 0);
    else
      move(l + 1, 0);
    clrtoeol();

    move(area_l-(area_l < l), 0);
    clrtoln(area_l -(area_l < l) + area_lines+1);
    outc(' '); outs(ANSI_CLRTOEND);
    move(area_l -(area_l < l) + area_lines, 0);
    outc(' '); outs(ANSI_CLRTOEND);
    move(area_l, 0);

    // TODO XXX support wide terminal someday.

    prints("�z�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�{\n");

    aidu = fn2aidu((char *)fhdr->filename);
    if(aidu > 0)
    {
      char aidc[10];
      int y, x;

      aidu2aidc(aidc, aidu);
      prints("�x " AID_DISPLAYNAME ": "
	  ANSI_COLOR(1) "#%s" ANSI_RESET " (%s) [%s] ",
	  aidc, currboard && currboard[0] ? currboard : "����",
	  AID_HOSTNAME);
      getyx_ansi(&y, &x);
      x = 75 - x;
      if (x > 1)
	  prints("%.*s ", x, fhdr->title);
      outs("\n");
    }
    else
    {
      prints("�x\n");
    }

#ifdef QUERY_ARTICLE_URL
    if (currbid) {
	boardheader_t *bp = getbcache(currbid);
	char url[STRLEN];

	if (!bp) {
	    prints("�x\n");
	} else if (!IsBoardForWeb(bp)) {
	    prints("�x ���ݪO�ثe������" URL_DISPLAYNAME " \n");
	} else if (!GetWebUrl(bp, fhdr, url, sizeof(url))) {
	    prints("�x ���峹������" URL_DISPLAYNAME " \n");
        } else {
	    prints("�x " URL_DISPLAYNAME ": " ANSI_COLOR(1) "%s" ANSI_RESET
		    "\n", url);
	}
    }
#endif

    if(fhdr->filemode & FILE_ANONYMOUS) {
        /* Note in MODE_SELECT, the multi may be fucked by ref number. */
	/* When the file is anonymous posted, fhdr->multi.anon_uid is author.
	 * see do_post_article() */
	prints("�x �ΦW�޲z�s��: %u (�P�@�H���X�|�@��)",
	   (unsigned int)fhdr->multi.anon_uid + (unsigned int)currutmp->pid);
    } else if (fhdr->filemode & FILE_VOTE) {
        uint32_t bp = fhdr->multi.vote_limits.badpost,
                 lgn = fhdr->multi.vote_limits.logins;
        prints("�x �벼����(�ݥ������o�孭��): ");
        if (bp)
            prints("�h�� %d �g�H�U ", 255 - bp);
        if (lgn)
            prints(STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY "�H�W ", lgn * 10);
        if (!bp && !lgn)
            prints("�L ");
    } else {
	int m = query_file_money(fhdr);

	if(m < 0)
	    prints("�x �S��峹�A�L����O��");
	else
	    prints("�x �o�@�g�峹�� %d " MONEYNAME, m);

    }
    prints("\n");
    prints("�|�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�}\n");

    /* �L��ܮت��k��� */
    {
      int i;

      for(i = 1; i < area_lines - 1; i ++)
      {
        move_ansi(area_l + i , 76);
        prints("�x");
      }
    }
    {
        int r = pressanykey();
        /* TODO: �h�[�@�� LISTMODE_AID�H */
	/* QQ: enable money listing mode */
	if (r == 'Q')
	{
	    currlistmode = (currlistmode == LISTMODE_MONEY) ?
		LISTMODE_DATE : LISTMODE_MONEY;
	    vmsg((currlistmode == LISTMODE_MONEY) ?
		    "�}�Ҥ峹����C��Ҧ�" : "����C�X�峹����");
	}
     }

    return FULLUPDATE;
}

#ifdef USE_TIME_CAPSULE
static int
view_posthistory(int ent GCC_UNUSED, const fileheader_t * fhdr, const char *direct)
{
    char fpath[PATHLEN];
    const char *err_no_history = "���g�峹�ȵL�s����v�O���C"
                                 "�n�i�귽�^�����ЦA���@�� ~";
    int maxrev = 0;
    int current_as_base = 1;

    // TODO allow author?
    if (!((currmode & MODE_BOARD) ||
          (HasUserPerm(PERM_SYSSUPERSUBOP | PERM_SYSSUBOP) && GROUPOP())))
        return DONOTHING;

    if (!fhdr || !fhdr->filename[0]) {
        if (vmsg(err_no_history) == '~')
            psb_recycle_bin(direct, currboard);
        return FULLUPDATE;
    }

    // assert(FN_SAFEDEL[0] == '.')
    if ((fhdr->filename[0] == '.')) {
        if (
#ifdef FN_SAFEDEL_PREFIX_LEN
            (strncmp(fhdr->filename, FN_SAFEDEL, FN_SAFEDEL_PREFIX_LEN) != 0)
#else
            (strcmp(fhdr->filename, FN_SAFEDEL) != 0)
#endif
           ) {
            if (vmsg(err_no_history) == '~')
                psb_recycle_bin(direct, currboard);
            return FULLUPDATE;
        }
        current_as_base = 0;
    }

    setbfile(fpath, currboard, fhdr->filename);
    if (!current_as_base) {
        char *prefix = strrchr(fpath, '/');
        assert(prefix);
        // hard-coded file name conversion
        *++prefix = 'M';
        *++prefix = '.';
    }
    maxrev = timecapsule_get_max_revision_number(fpath);
    if (maxrev < 1) {
        if (vmsg(err_no_history) == '~')
            psb_recycle_bin(direct, currboard);
        return FULLUPDATE;
    }

    if (RET_RECYCLEBIN ==
        psb_view_edit_history(fpath, fhdr->title, maxrev, current_as_base))
            psb_recycle_bin(direct, currboard);
    return FULLUPDATE;
}

#else // USE_TIME_CAPSULE

static int
view_posthistory(int ent, const fileheader_t * fhdr, const char *direct) {
    return DONOTHING;
}

#endif // USE_TIME_CAPSULE

/* �ݪO�ƥ� */
static int
tar_addqueue(void)
{
#if defined(OUTJOBSPOOL) && defined(TARQUEUE_SENDURL)
    char            email[60], qfn[80], ans[2];
    FILE           *fp;
    char            bakboard, bakman;

    if (!((currmode & MODE_BOARD) || HasUserPerm(PERM_BOARD)))
        return DONOTHING;

    snprintf(qfn, sizeof(qfn), BBSHOME "/jobspool/tarqueue.%s", currboard);
    if (access(qfn, 0) == 0) {
        vmsg("�w�g�Ʃw��{, �|��C��" TARQUEUE_TIME_STR "�̧Ƕi��ƥ�");
	return FULLUPDATE;
    }

    if (vansf("�T�w�n��ݪO %s �i��ƥ��ܡH[y/N] ", currboard) != 'y')
        return FULLUPDATE;

    vs_hdr2(" �ݪO�ƥ� ", currboard);

    if (!getdata_str(4, 0, "�п�J�q���H�c: ", email, sizeof(email), DOECHO,
                     cuser.userid))
	return FULLUPDATE;
    if (strstr(email, "@") == NULL)
    {
	strlcat(email, ".bbs@", sizeof(email));
	strlcat(email, MYHOSTNAME, sizeof(email));
    }
    move(4,0); clrtoeol();
    outs(email);

    /* check email -.-"" */
    if (!is_valid_email(email)) {
	vmsg("�z���w���H�c�����T! ");
	return FULLUPDATE;
    }

    getdata(6, 0, "�n�ƥ��ݪO���e��(Y/N)?[Y]", ans, sizeof(ans), LCECHO);
    bakboard = (ans[0] == 'n') ? 0 : 1;
    getdata(7, 0, "�n�ƥ���ذϤ��e��(Y/N)?[N]", ans, sizeof(ans), LCECHO);
    bakman = (ans[0] == 'y') ? 1 : 0;
    if (!bakboard && !bakman) {
	move(8, 0);
	outs("�i�O�ڭ̥u��ƥ��ݪO�κ�ذϪ��C ^^\"\"\"");
	pressanykey();
	return FULLUPDATE;
    }
    fp = fopen(qfn, "w");
    fprintf(fp, "%s\n", cuser.userid);
    fprintf(fp, "%s\n", email);
    fprintf(fp, "%d,%d\n", bakboard, bakman);
    fclose(fp);

    move(10, 0);
    outs("�t�Τw�g�N�z���ƥ��ƤJ��{, \n");
    outs("�y��N�|�b�t�έt�����C���ɭԱN��ƱH���z~ :) ");
    pressanykey();
    return FULLUPDATE;
#else
    return DONOTHING;
#endif
}

/* ----------------------------------------------------- */
/* �ݪO�i�O�e���B��K�B��ذ�                              */
/* ----------------------------------------------------- */
int
b_note_edit_bname(int bid)
{
    char            buf[PATHLEN];
    int             aborted;
    boardheader_t  *fh = getbcache(bid);
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    setbfile(buf, fh->brdname, fn_notes);
    aborted = veditfile(buf);
    if (aborted == -1) {
	clear();
	outs(msg_cancel);
	pressanykey();
    } else {
       // alert user our new b_note policy.
       char msg[STRLEN];
       clear();
       vs_hdr("�i�O�e����ܳ]�w");
       outs("\n"
       "\t�ШM�w�O�_�n�b�ϥΪ̭����i�J�ݪO����ܭ��x�s���i�O�e���C\n\n"
       "\t�Ъ`�N�Y�ϥΪ̳s�򭫽ƶi�X�P�@�ӬݪO�ɡA�i�O�e���u�|��ܤ@���C\n"
       "\t�����t�γ]�w�A�ëD�]�w���~�C\n\n"
       "\t(�ϥΪ��H�ɥi�� b �θg�Ѷi�X���P�ݪO�ӭ��s��ܶi�O�e��)\n");

       // �]�w������ĪG���ܦ��N���|�ʤF,�ҥH�ޱ�
       snprintf(msg, sizeof(msg),
	       "�n�b�����i�J�ݪO����ܶi�O�e���ܡH (y/n) [%c]: ",
	       fh->bupdate ? 'Y' : 'N');
       getdata(10, 0, msg, buf, 3, LCECHO);

       switch(buf[0])
       {
           case 'y':
               // assign a large, max date.
               fh->bupdate = INT_MAX -1;
               break;
           case 'n':
               fh->bupdate = 0;
               break;
           default:
	       // do nothing
               break;
       }
       // expire BM's lastbid to help him verify settings.
       bnote_lastbid = -1;

       assert(0<=bid-1 && bid-1<MAX_BOARD);
       substitute_record(fn_board, fh, sizeof(boardheader_t), bid);
    }
    return 0;
}

static int
b_notes(void)
{
    char            buf[PATHLEN];
    int mr = 0;

    setbfile(buf, currboard, fn_notes);
    mr = more(buf, NA);

    if (mr == -1)
    {
	clear();
	move(4, 20);
	outs("���ݪO�|�L�i�O�e���C");
    }
    if(mr != READ_NEXT)
	    pressanykey();
    return FULLUPDATE;
}

int
board_select(void)
{
    currmode &= ~MODE_SELECT;
    currsrmode = 0;
    if (currstat == RMAIL)
	sethomedir(currdirect, cuser.userid);
    else
	setbdir(currdirect, currboard);
    return NEWDIRECT;
}

int
board_digest(void)
{
    if (currmode & MODE_SELECT)
	board_select();
    currmode ^= MODE_DIGEST;

    // MODE_POST may be changed if board is modified.
    // do not change post perm here. use other instead.

    setbdir(currdirect, currboard);
    return NEWDIRECT;
}


static int
pin_post(int ent, fileheader_t *old_fhdr, const char *direct)
{
    int num;
    fileheader_t fhdr;
    char buf[PATHLEN];

    if ((currmode & MODE_DIGEST) || !(currmode & MODE_BOARD)
        || old_fhdr->filename[0]=='L')
        return DONOTHING;

    setbottomtotal(currbid);  // <- Ptt : will be remove when stable
    num = getbottomtotal(currbid);
    if (!(old_fhdr->filemode & FILE_BOTTOM))
    {
	move(b_lines-1, 0); clrtoeol();
	outs(ANSI_COLOR(1;33) "�����z�m���P���ثe�����s���A�R�����]�|�ɭP�m�������C" ANSI_RESET);
    }
    if( vans(old_fhdr->filemode & FILE_BOTTOM ?
	       "�����m�����i?(y/N)":
	       "�[�J�m�����i?(y/N)") != 'y' )
	return FULLUPDATE;

    // Don't change original fhdr.
    memcpy(&fhdr, old_fhdr, sizeof(fhdr));

    if(!(fhdr.filemode & FILE_BOTTOM) ){
          snprintf(buf, sizeof(buf), "%s.bottom", direct);
          if(num >= 5){
              vmsg("���o�W�L 5 �g���n���i �к�²!");
              return FULLUPDATE;
	  }
	  fhdr.filemode ^= FILE_BOTTOM;
	  fhdr.multi.refer.flag = 1;
          fhdr.multi.refer.ref = ent;
          append_record(buf, &fhdr, sizeof(fileheader_t));
          // make sure original one won't be deleted... add 'm'.
          if (!(old_fhdr->filemode & FILE_MARKED))
              mark_post(ent, old_fhdr, direct);
    }
    else{
        fhdr.filemode ^= FILE_BOTTOM;
	num = delete_fileheader(direct, &fhdr, ent);
    }
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    setbottomtotal(currbid);
    return DIRCHANGED;
}

static int
good_post(int ent, fileheader_t * fhdr, const char *direct)
{
    char            genbuf[200];
    char            genbuf2[200];

    if ((currmode & MODE_DIGEST) || !(currmode & MODE_BOARD))
	return DONOTHING;

    if(vans(fhdr->filemode & FILE_DIGEST ?
              "�����ݪO��K?(Y/n)" : "���J�ݪO��K?(Y/n)") == 'n')
	return READ_REDRAW;

    if (fhdr->filemode & FILE_DIGEST) {
	fhdr->filemode = (fhdr->filemode & ~FILE_DIGEST);
	if (!strcmp(currboard, BN_NOTE) ||
	    !strcmp(currboard, BN_LAW)
	    )
	{
            int unum = searchuser(fhdr->owner, NULL);
            if (unum > 0) {
                pay_as_uid(unum, 1000, "���� %s �ݪO��K", currboard);
            }
	    if (!(currmode & MODE_SELECT))
		fhdr->multi.money -= 1000;
	}
    } else {
	fileheader_t    digest;
	char           *ptr, buf[PATHLEN];

	memcpy(&digest, fhdr, sizeof(digest));
	digest.filename[0] = 'G';
	strlcpy(buf, direct, sizeof(buf));
	ptr = strrchr(buf, '/');
	assert(ptr);
	ptr++;
	ptr[0] = '\0';
	snprintf(genbuf, sizeof(genbuf), "%s%s", buf, digest.filename);

	if (dashf(genbuf))
	    unlink(genbuf);

	digest.filemode = 0;
	snprintf(genbuf2, sizeof(genbuf2), "%s%s", buf, fhdr->filename);
	Copy(genbuf2, genbuf);
	strcpy(ptr, fn_mandex);
	append_record(buf, &digest, sizeof(digest));

	fhdr->filemode = (fhdr->filemode & ~FILE_MARKED) | FILE_DIGEST;
	if (!strcmp(currboard, BN_NOTE) ||
	    !strcmp(currboard, BN_LAW)
	    )
	{
            int unum = searchuser(fhdr->owner, NULL);
            if (unum > 0) {
                pay_as_uid(unum, -1000, "�Q��J %s �ݪO��K", currboard);
            }
	    if (!(currmode & MODE_SELECT))
		fhdr->multi.money += 1000;
	}
    }
    // TODO fix race condition here.
    substitute_ref_record(direct, fhdr, ent);
    return FULLUPDATE;
}

static int
b_help(void)
{
    show_helpfile(fn_boardhelp);
    return FULLUPDATE;
}

static int
b_mark_read_unread(int ent GCC_UNUSED, const fileheader_t * fhdr,
                   const char *direct GCC_UNUSED) {
    char ans[3];
    time4_t curr;
    move(b_lines-4, 0); clrtobot();
    outs("\n�]�w�wŪ��Ū�O�� (�`�N: �峹�]���wŪ�ᤣ�|�A�X�{�ק�O�� '~')\n");
    getdata(b_lines-1, 0,
            "�]�w�Ҧ��峹 (U)��Ū (V)�wŪ (W)�e�wŪ�᥼Ū (Q)�����H[Q] ",
            ans, sizeof(ans), LCECHO);

    switch(*ans) {
        case 'u':
            brc_toggle_all_read(currbid, 0);
            break;
        case 'v':
            brc_toggle_all_read(currbid, 1);
            break;
        case 'w':
            // XXX dirty hack: file name timestamp in [2]
            curr = atoi(fhdr->filename + 2);
            if (curr > 1 && curr <= now) {
                brc_toggle_read(currbid, curr);
            } else {
                vmsg("�Ч�Ψ䥦�峹�]�w��Ѧ��I");
            }
            break;
        default:
            break;
    }
    return FULLUPDATE;
}

#ifdef USE_COOLDOWN

int check_cooldown(boardheader_t *bp)
{
    int diff = cooldowntimeof(usernum) - now;
    int i, limit[8] = {4000,1,2000,2,1000,3,-1,10};

    if(diff<0)
	SHM->cooldowntime[usernum - 1] &= 0xFFFFFFF0;
    else if( !((currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP)))
    {
      if( bp->brdattr & BRD_COOLDOWN )
       {
  	 vmsgf("�N�R�@�U�a�I (���� %d �� %d ��)", diff/60, diff%60);
	 return 1;
       }
      else if(posttimesof(usernum)==0xf)
      {
	 vmsgf("�藍�_�A�z�Q�]�h��I (���� %d �� %d ��)", diff/60, diff%60);
	 return 1;
      }
#ifdef REJECT_FLOOD_POST
      else
      {
        for(i=0; i<4; i++)
          if(bp->nuser>limit[i*2] && posttimesof(usernum)>=limit[i*2+1])
          {
	    vmsgf("�藍�_�A�z���峹�α��嶡�j�Ӫ��o�I (���� %d �� %d ��)",
		  diff/60, diff%60);
	    return 1;
          }
      }
#endif // REJECT_FLOOD_POST
   }
   return 0;
}
#endif

static int
mask_post_content(int ent GCC_UNUSED, fileheader_t * fhdr, const char *direct) {
#ifndef USE_TIME_CAPSULE
    vmsg("���\\�ॼ�}�ҡA�Ь������C");
    return FULLUPDATE;
#else
    char pattern[STRLEN];
    char reason[15];
    char buf[ANSILINELEN], buf2[ANSILINELEN];
    char fpath[PATHLEN], revpath[PATHLEN];
    char ans[3];
    FILE *fp, *fpw;
    int i, rev, found = 0;
    boardheader_t *bp;

    if (currstat == RMAIL)
	return DONOTHING;

    bp = getbcache(currbid);
    assert(bp);

    if (!(bp->brdattr & BRD_BM_MASK_CONTENT)) {
        vmsg("�n�}�Ҧ����\\��Ь��s�ժ��C");
        return FULLUPDATE;
    }

    vs_hdr2(" �R���S�w��r ", fhdr->title);
    if (!getdata(1, 0, "�R����]: ", reason, sizeof(reason), DOECHO))
        return FULLUPDATE;
    mvouts(3, 0, "�п�J�n�R������r (�X�{�ɷ|���Q[�H�W���e]���N, �̤֨�Ӧr��)\n");
    if (!getdata(2, 0, "�R����r: ", pattern, TTLEN, DOECHO) || strlen(pattern) < 2)
        return FULLUPDATE;

    // try to render and build.
    setdirpath(fpath, direct, fhdr->filename);
    fp = fopen(fpath, "rt");
    if (!fp) {
        vmsg("�峹�w�Q�R������w�C");
        return FULLUPDATE;
    }
    mvouts(3, 0, ANSI_COLOR(1;31) "�N�R���U�C��r:" ANSI_RESET "\n");
    i = 4;
    while (fgets(buf, sizeof(buf), fp)) {
        strip_ansi(buf, buf, STRIP_ALL);
        if (strstr(buf, pattern)) {
            found++;
            mvouts(i, 0, ANSI_RESET);
            outs(buf);
            outs(ANSI_RESET);
            if (++i >= b_lines) {
                if (tolower(vmsg("�� q ���A�άO���N���~��: ")) == 'q') {
                    found = 0;
                    break;
                }
                i = 4;
                move(i, 0);
                clrtobot();
            }
        }
    }
    fclose(fp);

    if (!found) {
        vmsg("���ק��ɮסC");
        return FULLUPDATE;
    }

    /* XXX race condition here... */
    getdata(b_lines, 0, "�T�w�n�R���o�Ǥ��e��? [y/N]: ", ans, sizeof(ans), LCECHO);
    if (*ans != 'y') {
        vmsg("���ק��ɮסC");
        return FULLUPDATE;
    }

    rev = timecapsule_add_revision(fpath);
    if (!timecapsule_get_by_revision(fpath, rev, revpath, sizeof(revpath))) {
        vmsg("�t�ο��~�A�L�k�ק�C");
        return FULLUPDATE;
    }
    // Enforce building a new file.
    unlink(fpath);

    fp = fopen(revpath, "rt");
    fpw = fopen(fpath, "wt");
    while (fgets(buf, sizeof(buf), fp)) {
        strip_ansi(buf2, buf, STRIP_ALL);
        if (strstr(buf2, pattern)) {
            fputs("�� [�����H�W��r�w�R��]\n", fpw);
            continue;
        }
        fputs(buf, fpw);
    }
    fclose(fp);
    fclose(fpw);
    log_filef(fpath, LOG_CREAT, "�� %s �� %s �R�������H�W��r,��]: %s\n", cuser.userid,
              Cdatelite(&now), reason);
    log_filef(revpath, LOG_CREAT,
              "�� %s �� %s �R�������H�W��r,��]: %s\n"
              "�� �H�W��r�˦�: %s\n",
              cuser.userid, Cdatelite(&now), reason, pattern);

    // �z�פW�n�� fhdr->modified, ���L�b�ثe�@�ζê��P�B����U�A�h�@�h���C
    vmsg("�H�W��r�w�R���C");
    return FULLUPDATE;
#endif
}

static int
b_moved_to_config()
{
    if (currmode & MODE_BOARD) {
	vmsg("�o�ӥ\\��w���J�ݪO�]�w (i) �h�F�I");
	return FULLUPDATE;
    }
    return DONOTHING;
}

static int
moved_to_ctrl_e()
{
    if (currmode & MODE_BOARD) {
	vmsg("�o�ӥ\\��w���J�峹�޲z (Ctrl-E) �h�F�I");
	return FULLUPDATE;
    }
    return DONOTHING;
}

static int
manage_post(int ent, fileheader_t * fhdr, const char *direct) {
    int ans;
    const char *prompt = "[Y]�����k�s [E]��w/�Ѱ� [M]�R�S�w��r"
#ifdef USE_COMMENTD
        " [V](����)����޲z"
#endif
        ":";

    if (currstat == RMAIL)
        return DONOTHING;

    if (!(currmode & MODE_BOARD)) {
        if (HasUserPerm(PERM_POLICE))
            return lock_post(ent, fhdr, direct);
        else
            return DONOTHING;
    }

    ans = vmsg(prompt);
    if (!isascii(ans))
        return FULLUPDATE;

    switch(tolower(ans)) {
        case 'y':
            recommend_cancel(ent, fhdr, direct);
            break;

        case 'e':
            lock_post(ent, fhdr, direct);
            break;

        case 'm':
            mask_post_content(ent, fhdr, direct);
            break;

#ifdef USE_COMMENTD
        case 'v':
            if (currbrdattr & (BRD_ANGELANONYMOUS | BRD_ANONYMOUS)) {
                vmsg("��p�A�Ȯɤ��䴩�ΦW�O�C");
            } else  {
                psb_comment_manager(currboard, fhdr->filename);
            }
            break;
#endif
    }
    return FULLUPDATE;
}

/* ----------------------------------------------------- */
/* �ݪO�\���                                            */
/* ----------------------------------------------------- */
/* onekey_size was defined in ../include/pttstruct.h, as ((int)'z') */
const onekey_t read_comms[] = {
    { 1, show_filename }, // Ctrl('A')
    { 0, NULL }, // Ctrl('B')
    { 0, NULL }, // Ctrl('C')
    { 0, NULL }, // Ctrl('D')
    { 1, manage_post }, // Ctrl('E')
    { 0, NULL }, // Ctrl('F')
    { 0, hold_gamble }, // Ctrl('G')
    { 0, NULL }, // Ctrl('H')
    { 0, board_digest }, // Ctrl('I') KEY_TAB 9
    { 0, NULL }, // Ctrl('J')
    { 0, NULL }, // Ctrl('K')
    { 0, NULL }, // Ctrl('L')
    { 0, NULL }, // Ctrl('M')
    { 0, NULL }, // Ctrl('N')
    { 0, NULL }, // Ctrl('O') // BETTER NOT USE ^O - UNIX not work
    { 0, new_post }, // Ctrl('P')
    { 0, NULL }, // Ctrl('Q')
    { 0, NULL }, // Ctrl('R')
    { 0, NULL }, // Ctrl('S')
    { 0, NULL }, // Ctrl('T')
    { 0, NULL }, // Ctrl('U')
    { 0, do_post_vote }, // Ctrl('V')
    { 0, whereami }, // Ctrl('W')
    { 1, cross_post }, // Ctrl('X')
    { 0, NULL }, // Ctrl('Y')
    { 0, NULL }, // Ctrl('Z') 26 // �{�b�� ZA �ΡC
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 1, recommend }, // '%' (m3itoc style)
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, // 'A' 65
    { 0, NULL }, // 'B'
    { 1, do_limitedit }, // 'C'
    { 1, del_range_post }, // 'D'
    { 1, edit_post }, // 'E'
    { 1, forward_post }, // 'F'
    { 0, NULL }, // 'G'
    { 0, NULL }, // 'H'
    { 0, b_config }, // 'I'
    { 0, b_moved_to_config }, // 'J'
    { 0, NULL }, // 'K'
    { 1, solve_post }, // 'L'
    { 0, NULL }, // 'M'
    { 0, NULL }, // 'N'
    { 0, NULL }, // 'O'
    { 0, NULL }, // 'P'
    { 1, view_postinfo }, // 'Q'
    { 0, b_results }, // 'R'
    { 0, NULL }, // 'S'
    { 1, edit_title }, // 'T'
    { 1, b_quick_acl }, // 'U'
    { 0, b_vote }, // 'V'
    { 0, b_moved_to_config }, // 'W'
    { 1, recommend }, // 'X'
    { 0, moved_to_ctrl_e }, // 'Y'
    { 0, NULL }, // 'Z' 90
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 1, pin_post }, // '_' 95
    { 0, NULL },
    { 0, NULL }, // 'a' 97
    { 0, b_notes }, // 'b'
    { 1, cite_post }, // 'c'
    { 1, del_post }, // 'd'
    { 0, NULL }, // 'e'
    { 0, join_gamble }, // 'f'
    { 1, good_post }, // 'g'
    { 0, b_help }, // 'h'
    { 0, b_config }, // 'i'
    { 0, NULL }, // 'j'
    { 0, NULL }, // 'k'
    { 0, NULL }, // 'l'
    { 1, mark_post }, // 'm'
    { 0, NULL }, // 'n'
    { 0, NULL }, // 'o'
    { 0, NULL }, // 'p'
    { 0, NULL }, // 'q'
    { 1, read_post }, // 'r'
    { 0, do_select }, // 's'
    { 0, NULL }, // 't'
    { 0, tar_addqueue }, // 'u'
    { 1, b_mark_read_unread }, // 'v'
    { 1, b_call_in }, // 'w'
    { 0, NULL }, // 'x'
    { 1, reply_post }, // 'y'
    { 0, b_man }, // 'z' 122
    { 0, NULL }, // '{' 123
    { 0, NULL }, // '|' 124
    { 0, NULL }, // '}' 125
    { 1, view_posthistory }, // '~' 126
};

int
Read(void)
{
    int             mode0 = currutmp->mode;
    int             stat0 = currstat, tmpbid = currutmp->brc_id;
    char            buf[PATHLEN];

    const char *bname = currboard[0] ? currboard : DEFAULT_BOARD;
    if (enter_board(bname) < 0)
	return 0;

    setutmpmode(READING);

    if (currbid != bnote_lastbid &&
	board_note_time && *board_note_time) {
	int mr;

	setbfile(buf, currboard, fn_notes);
	mr = more(buf, NA);
	if(mr == -1)
            *board_note_time=0;
	else if (mr != READ_NEXT)
	    pressanykey();
    }
    bnote_lastbid = currbid;
    i_read(READING, currdirect, readtitle, readdoent, read_comms,
	   currbid);
    currmode &= ~MODE_POSTCHECKED;
    brc_update();
    setutmpbid(tmpbid);
    currutmp->mode = mode0;
    currstat = stat0;
    return 0;
}

int
ReadSelect(void)
{
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;
    int		    changed = 0;

    currstat = SELECT;
    if (do_select() == NEWDIRECT)
    {
	Read();
	changed = 1;
    }
    // need to set utmpbid here...
    // because Read() just restores settings.
    // and 's' directly calls here.
    // so if we don't reset then utmpbid will be out-of-sync.
    // fix someday...
    setutmpbid(0);
    currutmp->mode = mode0;
    currstat = stat0;
    return changed;
}

int
Select(void)
{
    return do_select();
}
