#include "bbs.h"

/* personal board state
 * �۹��ݪO�� attr (BRD_* in ../include/pttstruct.h),
 * �o�ǬO�Φb user interface �� flag */
#define NBRD_FAV    	 1
#define NBRD_BOARD	 2
#define NBRD_LINE   	 4
#define NBRD_FOLDER	 8
#define NBRD_TAG	16
#define NBRD_UNREAD     32
#define NBRD_SYMBOLIC   64

#define TITLE_MATCH(bptr, key)	((key)[0] && !strcasestr((bptr)->title, (key)))

#define B_TOTAL(bptr)        (SHM->total[(bptr)->bid - 1])
#define B_LASTPOSTTIME(bptr) (SHM->lastposttime[(bptr)->bid - 1])
#define B_BH(bptr)           (&bcache[(bptr)->bid - 1])

#define HasFavEditPerm() HasUserPerm(PERM_BASIC)

typedef struct {
    int             bid;
    unsigned char   myattr;
} __attribute__ ((packed)) boardstat_t;

/**
 * class_bid ���N�q
 *   class_bid < 0   �����ݪO
 *   class_bid = 0   �ڪ��̷R
 *   class_bid = 1   �����ݪO
 *   class_bid > 1   ��L�ؿ�
 */
#define IN_HOTBOARD()	(class_bid < 0)
#define IN_FAVORITE()	(class_bid == 0)
#define IN_CLASSROOT()	(class_bid == 1)
#define IN_SUBCLASS()	(class_bid > 1)
#define IN_CLASS()	(class_bid > 0)
static int      class_bid = 0;

static int nbrdsize = 0;
static boardstat_t *nbrd = NULL;
static char	choose_board_depth = 0;
static int      brdnum;
static char     yank_flag = 1;

static time4_t   last_save_fav_and_brc;

/* These are all the states yank_flag may be. */
// XXX IS_LISTING_FAV() does not mean we are in favorite.
// That is controlled by IN_FAVORITE().
#define LIST_FAV()         (yank_flag = 0)
#define LIST_BRD()         (yank_flag = 1)
#define IS_LISTING_FAV()   (yank_flag == 0)
#define IS_LISTING_BRD()   (yank_flag == 1)

inline int getbid(const boardheader_t *fh)
{
    return (fh - bcache);
}
inline boardheader_t *getparent(const boardheader_t *fh)
{
    if(fh->parent>0)
	return getbcache(fh->parent);
    else
	return NULL;
}

// �{�������S�O�γ~�ҥH���o�ۦ� post / �ק諸�ݪO
int
is_readonly_board(const char *bname)
{
    return (strcasecmp(bname, BN_SECURITY) == 0 ||
	    strcasecmp(bname, BN_ALLPOST ) == 0 );
}

/**
 * @param[in]	boardname	board name, case insensitive
 * @return	0	if success
 * 		-1	if not found
 * 		-2	permission denied
 * 		-3	error
 * @note enter board:
 * 	1. setup brc (currbid, currboard, currbrdattr)
 * 	2. set currbid, currBM, currmode, currdirect
 * 	3. utmp brc_id
 */
int enter_board(const char *boardname)
{
    boardheader_t  *bh;
    int bid;
    char bname[IDLEN+1];
    char bpath[60];
    struct stat     st;

    /* checking ... */
    if (boardname[0] == '\0' || !(bid = getbnum(boardname)))
	return -1;
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    bh = getbcache(bid);
    if (!HasBoardPerm(bh))
	return -2;
    if (IS_GROUP(bh))
	return -1;

    strlcpy(bname, bh->brdname, sizeof(bname));
    if (bname[0] == '\0')
	return -3;

    setbpath(bpath, bname);
    // Mkdir(bpath);
    if (stat(bpath, &st) == -1) {
	return -3;
    }

    /* really enter board */
    brc_update();
    brc_initial_board(bname);
    setutmpbid(currbid);

    set_board();
    setbdir(currdirect, currboard);

    return 0;
}


static void imovefav(int old)
{
    char buf[5];
    int new;

    getdata(b_lines - 1, 0, "�п�J�s����:", buf, sizeof(buf), DOECHO);
    new = atoi(buf) - 1;
    if (new < 0 || brdnum <= new){
	vmsg("��J�d�򦳻~!");
	return;
    }
    move_in_current_folder(old, new);
}

void
init_brdbuf(void)
{
    if (brc_initialize())
	return;
}

void
save_brdbuf(void)
{
    fav_save();
    fav_free();
}

static inline int
HasBoardPermNormally(boardheader_t *bptr)
{
    register int    level, brdattr;

    level = bptr->level;
    brdattr = bptr->brdattr;

    // allow POLICE to enter BM boards
    if ((level & PERM_BM) &&
	(HasUserPerm(PERM_POLICE) || HasUserPerm(PERM_POLICE_MAN)))
	return 1;

    /* �O�D */
    if( is_BM_cache(bptr - bcache + 1) ) /* XXXbid */
	return 1;

    /* ���K�ݪO�G�ֹﭺ�u�O�D���n�ͦW�� */
    if (brdattr & BRD_HIDE) {	/* ���� */
	if (!is_hidden_board_friend((int)(bptr - bcache) + 1, currutmp->uid)) {
	    if (brdattr & BRD_POSTMASK)
		return 0;
	    else
		return 2;  // What's this?
	} else
	    return 1;
    }

    // TODO Change this to a query on demand.
    /* �Q�K�T�ݪO */
    if( (brdattr & BRD_OVER18) && !cuser.over_18 )
	return 0;

    /* ����\Ū�v�� */
    if (level && !(brdattr & BRD_POSTMASK) && !HasUserPerm(level))
	return 0;

    return 1;
}

int
HasBoardPerm(boardheader_t *bptr)
{
    if (HasUserPerm(PERM_SYSOP))
	return 1;

    return HasBoardPermNormally(bptr);
}

int
BoardPermNeedsSysopOverride(boardheader_t *bptr)
{
    return HasUserPerm(PERM_SYSOP) && !HasBoardPermNormally(bptr);
}

// board configuration utilities

static int
b_post_note(void)
{
    char            buf[PATHLEN], yn[3];

    // if(!(currmode & MODE_BOARD)) return DONOTHING;
    vs_hdr("�ۭq�`�N�ƶ�");

    setbfile(buf, currboard, FN_POST_NOTE);
    move(b_lines-2, 0); clrtobot();

    if (more(buf, NA) == -1)
	more("etc/" FN_POST_NOTE, NA);

    getdata(b_lines - 2, 0, "�ۭq�o��`�N�ƶ�: (y)�s��/(d)�R��/(n)����? [y/d/N]:",
	    yn, sizeof(yn), LCECHO);
    if (yn[0] == 'y')
	veditfile(buf);
    else if (yn[0] == 'd')
	unlink(buf);

    return FULLUPDATE;
}

static int
b_posttype()
{
   boardheader_t  *bp;
   int i, modified = 0, types = 0;
   char filepath[PATHLEN], genbuf[60];
   char posttype_f, posttype[sizeof(bp->posttype)]="", *p;

   assert(0<=currbid-1 && currbid-1<MAX_BOARD);
   bp = getbcache(currbid);
   posttype_f = bp->posttype_f;
   memcpy(posttype, bp->posttype, sizeof(bp->posttype));

   vs_hdr("�]�w�峹���O");

   do {
       move(2, 0);
       clrtobot();
       for (i = 0, p = posttype; *p && i < 8; i++, p += 4) {
           strlcpy(genbuf, p, 5);
           prints(" %d. %s %s\n", i + 1, genbuf,
                  posttype_f & (1 << i) ? "(���d��)": "");
           // Workaround broken items
           if (strlen(p) < 4) {
               memset(p + strlen(p), ' ', 4 - strlen(p));
           }
       }
       types = i;
       if (!getdata(15, 0,
                    "�п�J�n�]�w�����ؽs���A�� c �]�w�`��,�� ENTER ���}:",
                    genbuf, 3, LCECHO))
           break;

       if (genbuf[0] == 'c') {
           getdata(15, 0, "�n�O�d�X�����O�O�H [0-8�� ENTER ���}]: ", genbuf, 3,
                   NUMECHO);
           if (!isdigit(genbuf[0]))
               continue;
           i = atoi(genbuf);
           if (i < 0 || i > 8)
               continue;
           while (i > types++)
               strlcat(posttype, "    ", sizeof(posttype));
           posttype[i * 4] = 0;
           continue;
       }

       i = atoi(genbuf) - 1;
       if (i < 0 || i >= 8)
           continue;
       strlcpy(genbuf, posttype + i * 4, 5);
       if(getdata_str(16, 0, "���O�W��: ", genbuf, 5, DOECHO, genbuf)) {
           char tmp[5];
           snprintf(tmp, sizeof(tmp), "%-4.4s", genbuf);
           memcpy(posttype + (i * 4), tmp, 4);
       }
       getdata(17, 0, "�n�ϥνd����? [y/n/K(������)]: ", genbuf, 2, LCECHO);
       if (genbuf[0] == 'y')
           posttype_f |= 1 << i;
       else if (genbuf[0] == 'n') {
           posttype_f &= ~(1 << i);
           continue;
       }
       getdata(18, 0, "�n�s��d���ɮ׶�? [y/N]: ", genbuf, 2, LCECHO);
       if (genbuf[0] == 'y') {
           setbnfile(filepath, bp->brdname, "postsample", i);
           veditfile(filepath);
       }
   } while (1);

   // TODO last chance to confirm.
   assert(0<=currbid-1 && currbid-1<MAX_BOARD);
   if (bp->posttype_f != posttype_f) {
       bp->posttype_f = posttype_f;
       modified = 1;
   }
   if (strcmp(bp->posttype, posttype) != 0) {
       /* �o�����ӭn��race condition */
       strlcpy(bp->posttype, posttype, sizeof(bp->posttype));
       modified = 1;
   }
   if (modified) {
       substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
       vmsg("��Ƥw��s�C");
   }
   return FULLUPDATE;
}

static int
b_notes_edit(void)
{
    if (currmode & MODE_BOARD) {
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	b_note_edit_bname(currbid);
	return FULLUPDATE;
    }
    return 0;
}

// integrated board config
int
b_config(void)
{
    boardheader_t   *bp=NULL;
    int touched = 0, finished = 0, check_mod = 1;
    int i = 0, attr = 0, ipostres;
    char isBM = (currmode & MODE_BOARD) || HasUserPerm(PERM_SYSOP);
    char isPolice = HasUserPerm(PERM_POLICE);
    char isSysGroupOP = (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP());
    // perm cache
    char hasres = 0,
	 cachePostPerm = CheckPostPerm(),
	 cachePostRes  = CheckPostRestriction(currbid);
    char canpost = (cachePostPerm && cachePostRes);

#define LNBOARDINFO (18)
#define LNPOSTRES   (12)
#define COLPOSTRES  (48)

    int ytitle = b_lines - LNBOARDINFO;

    bp = getbcache(currbid);

#ifdef OLDRECOMMEND
    ytitle ++;
#endif  // OLDRECOMMEND
#ifdef USE_COOLDOWN
    ytitle--;
#endif // USE_COOLDOWN
#ifdef USE_AUTOCPLOG
    ytitle--;
#endif

    grayout(0, ytitle-2, GRAYOUT_DARK);

    // available hotkeys yet:
    // a d p q z
    // 2 3 4 5 6 7 9
    // better not: 0

#define CANTPOSTMSG ANSI_COLOR(1;31) "(�z���F����)" ANSI_RESET

    while (!finished) {
	// limits
	uint8_t llogin = bp->post_limit_logins,
		lbp    = bp->post_limit_badpost;
        char ansk;

	move(ytitle-1, 0);
	clrtobot();

	// outs(MSG_SEPARATOR); // deprecated by grayout
	outs("\n" ANSI_REVERSE); // now (ytitle, 0);
	vbarf(" �m%s�n�ݪO�]�w", bp->brdname);

	move(ytitle + 2, 0);

	prints(" "ANSI_COLOR(1;36) "b" ANSI_RESET " - ����ԭz: %s\n", bp->title);
	prints("     �O�D�W��: %s\n", does_board_have_public_bm(bp) ? bp->BM : "(�L)");
	prints( " " ANSI_COLOR(1;36) "h" ANSI_RESET
		" - ���}���A(�O�_����): %s " ANSI_RESET "\n",
		(bp->brdattr & BRD_HIDE) ?
		ANSI_COLOR(1;31)"����":"���}");

	prints( " " ANSI_COLOR(1;36) "g" ANSI_RESET
		" - ���O�� %s �i�J�Q�j�Ʀ�]" ANSI_RESET "\n",
		(bp->brdattr & BRD_BMCOUNT) ?
		ANSI_COLOR(1)"�i�H" ANSI_RESET: "���i");

	prints( " " ANSI_COLOR(1;36) "e" ANSI_RESET
		" - %s "ANSI_RESET "�D�ݪO�|���o��\n",
		(bp->brdattr & BRD_RESTRICTEDPOST) ?
		ANSI_COLOR(1)"���}��" : "�}��"
		);

	prints( " " ANSI_COLOR(1;36) "y" ANSI_RESET
		" - %s" ANSI_RESET
		" �^���峹\n",
		(bp->brdattr & BRD_NOREPLY) ?
		ANSI_COLOR(1)"���}��" : "�}��"
		);

	prints( " " ANSI_COLOR(1;36) "d" ANSI_RESET
		" - %s" ANSI_RESET
		" �ۧR�峹\n",
		(bp->brdattr & BRD_NOSELFDELPOST) ?
		ANSI_COLOR(1)"���}��" : "�}��"
		);

	prints( " " ANSI_COLOR(1;36) "r" ANSI_RESET
		" - %s " ANSI_RESET "���ˤ峹\n",
		(bp->brdattr & BRD_NORECOMMEND) ?
		ANSI_COLOR(1)"���}��":"�}��"
		);

#ifndef OLDRECOMMEND
	prints( " " ANSI_COLOR(1;36) "s" ANSI_RESET
	        " - %s " ANSI_RESET "�N��\n",
		((bp->brdattr & BRD_NORECOMMEND) || (bp->brdattr & BRD_NOBOO))
		? ANSI_COLOR(1)"���}��":"�}��");
#endif
	{
	    int d = 0;

	    if(bp->brdattr & BRD_NORECOMMEND)
	    {
		d = -1;
	    } else {
		if ((bp->brdattr & BRD_NOFASTRECMD) &&
		    (bp->fastrecommend_pause > 0))
		    d = bp->fastrecommend_pause;
	    }

	    prints( " " ANSI_COLOR(1;36) "f" ANSI_RESET
		    " - %s " ANSI_RESET "�ֳt�s���峹",
		    d != 0 ?
		     ANSI_COLOR(1)"����": "�}��");
	    if(d > 0)
		prints(", �̧C���j�ɶ�: %d ��", d);
	    outs("\n");
	}

	prints( " " ANSI_COLOR(1;36) "i" ANSI_RESET
		" - ����� %s" ANSI_RESET " �O���ӷ� IP\n",
		(bp->brdattr & BRD_IPLOGRECMD) ?
		ANSI_COLOR(1)"�۰�":"���|");

	prints( " " ANSI_COLOR(1;36) "a" ANSI_RESET
		" - ����� %s" ANSI_RESET " �}�Y\n",
		(bp->brdattr & BRD_ALIGNEDCMT) ?
		ANSI_COLOR(1)"���":"���ι��");

	prints( " " ANSI_COLOR(1;36) "k" ANSI_RESET
		" - �O�D %s" ANSI_RESET
		" �R�������H�W��r\n",
		(bp->brdattr & BRD_BM_MASK_CONTENT) ?
		ANSI_COLOR(1)"�i" : "�L�k"
		);

#ifdef USE_AUTOCPLOG
	prints( " " ANSI_COLOR(1;36) "x" ANSI_RESET
		" - ����峹 %s " ANSI_RESET "�۰ʰO���A�B %s "
		ANSI_RESET "�o���v��\n",
		(bp->brdattr & BRD_CPLOG) ?
		ANSI_COLOR(1)"�|" : "���|" ,
		(bp->brdattr & BRD_CPLOG) ?
		ANSI_COLOR(1)"�ݭn" : "����"
		);
#endif
#ifdef USE_COOLDOWN
	prints( " " ANSI_COLOR(1;36) "j" ANSI_RESET
		" - %s �]���N�R�Ҧ�\n",
		(bp->brdattr & BRD_COOLDOWN) ?
		ANSI_COLOR(1)"�w"ANSI_RESET : "��");
#endif

	// use '8' instead of '1', to prevent 'l'/'1' confusion
	prints( " " ANSI_COLOR(1;36) "8" ANSI_RESET
		" - %s" ANSI_RESET "�����Q�K���i�J\n",
		(bp->brdattr & BRD_OVER18) ?
		ANSI_COLOR(1) "�T�� " : "���\\ " );

	if (!canpost)
	    outs(ANSI_COLOR(1;31)"  �� �z�b���ݪO�L�o��α����v���A"
		"�Բӭ�]�аѦҤW����ܬ�����Φ� * �����ءC"ANSI_RESET"\n");

	ipostres = b_lines - LNPOSTRES;
	move_ansi(ipostres++, COLPOSTRES-2);

	if (cachePostPerm && cachePostRes)
	    outs(ANSI_COLOR(1;32));
	else
	    outs(ANSI_COLOR(31));

	if (bp->brdattr & BRD_VOTEBOARD)
	    outs("���X�s�p����:" ANSI_RESET);
	else
	    outs("�o��P���孭��:" ANSI_RESET);

#define POSTRESTRICTION(msg,utag) \
	prints(msg, attr ? ANSI_COLOR(1) : "", i, attr ? ANSI_RESET : "")

	if (bp->brdattr & BRD_VOTEBOARD)
	{
	    llogin = bp->vote_limit_logins;
	    lbp    = bp->vote_limit_badpost;
	}

	if (llogin)
	{
	    move_ansi(ipostres++, COLPOSTRES);
	    i = (int)llogin * 10;
	    attr = ((int)cuser.numlogindays < i) ? 1 : 0;
	    if (attr) outs(ANSI_COLOR(1;31) "*");
	    prints(STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY "�H�W", i);
	    if (attr) outs(ANSI_RESET);
	    hasres = 1;
	}

	if (lbp)
	{
	    move_ansi(ipostres++, COLPOSTRES);
	    i = 255 - lbp;
	    attr = (cuser.badpost > i) ? 1 : 0;
	    if (attr) outs(ANSI_COLOR(1;31) "*");
	    prints("�h��g�� %d �g�H�U", i);
	    if (attr) outs(ANSI_RESET);
	    hasres = 1;
	}

	if (!cachePostPerm)
	{
	    const char *msg = postperm_msg(bp->brdname);
	    if (msg) // some reasons
	    {
		move_ansi(ipostres++, COLPOSTRES);
		outs(ANSI_COLOR(1;31) "*");
		outs(msg);
		outs(ANSI_RESET);
	    }
	}

	if (!hasres && cachePostPerm)
	{
	    move_ansi(ipostres++, COLPOSTRES);
	    outs("�L�S�O����");
	}

	// show BM commands
	{
	    const char *aCat = ANSI_COLOR(1;32);
	    const char *aHot = ANSI_COLOR(1;36);
	    const char *aRst = ANSI_RESET;

	    if (!isBM)
	    {
		aCat = ANSI_COLOR(1;30;40);
		aHot = "";
		aRst = "";
	    }

	    ipostres ++;
	    move_ansi(ipostres++, COLPOSTRES-2);
	    outs(aCat);
	    outs("�W��s��P�䥦:");
	    if (!isBM) outs(" (�ݪO�D�v��)");
	    outs(aRst);
	    move_ansi(ipostres++, COLPOSTRES);
	    prints("%sw%s)�]�w���� %sv%s)�i���|���W�� ",
		    aHot, aRst, aHot, aRst);
	    move_ansi(ipostres++, COLPOSTRES);
	    prints("%sm%s)�|��벼 %so%s)�벼�W�� ",
		    aHot, aRst, aHot, aRst);
	    move_ansi(ipostres++, COLPOSTRES);
	    prints("%sc%s)�峹���O %sn%s)�o��`�N�ƶ� ",
		    aHot, aRst, aHot, aRst);
	    move_ansi(ipostres++, COLPOSTRES);
	    prints("%sp%s)�i�O�e��",
		    aHot, aRst);
	    outs(ANSI_RESET);

            if (GROUPOP()) {
                move_ansi(++ipostres, COLPOSTRES);
                prints(ANSI_COLOR(1;32)
                       "�z�ثe�����ݪO���s�պ޲z�v"
                       ANSI_RESET);
            }

	}

        // 'J' need police perm, which is very stupid.
	if (!isBM && !isPolice && !isSysGroupOP)
	{
	    pressanykey();
	    return FULLUPDATE;
	}

        if (check_mod) {
            if (vmsg("�Y�n�i��ק�Ы� Ctrl-P�A�䥦�䪽�����}�C") != Ctrl('P'))
                return FULLUPDATE;
            check_mod = 0;
        }

        ansk = vans("�п�J�n���ܪ��]�w, �䥦�䵲��: ");
        if (isascii(ansk))
            ansk = tolower(ansk);

        // Now let's try to restrict the stupid perms.
        if (!isBM) {
            const char *sysgroupkeys = "ykd";
            const char *policekeys = "j";

            if (!(isSysGroupOP && strchr(sysgroupkeys, ansk)) &&
                !(isPolice && strchr(policekeys, ansk)))
                return FULLUPDATE;
        }

	switch(ansk)
	{
#ifdef USE_AUTOCPLOG
	    case 'x':
		bp->brdattr ^= BRD_CPLOG;
		touched = 1;
		break;
#endif
	    case 'a':
		bp->brdattr ^= BRD_ALIGNEDCMT;
		touched = 1;
		break;

	    case 'b':
		{
		    char genbuf[BTLEN+1];
		    move(b_lines, 0); clrtoeol();
		    SOLVE_ANSI_CACHE();
		    outs("�п�J�ݪO�s����ԭz: ");
		    vgetstr(genbuf, BTLEN-16, 0, bp->title + 7);
		    if (!genbuf[0] || strcmp(genbuf, bp->title+7) == 0)
			break;
		    touched = 1;
		    strip_ansi(genbuf, genbuf, STRIP_ALL);
		    strlcpy(bp->title + 7, genbuf, sizeof(bp->title) - 7);
		    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
		    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
		    log_usies("SetBoard", currboard);
		}
		break;

	    case 'e':
		if(HasUserPerm(PERM_SYSOP))
		{
		    bp->brdattr ^= BRD_RESTRICTEDPOST;
		    touched = 1;
		} else {
		    vmsg("�����]�w�ݭn�����v��");
		}
		break;

	    case 'h':
#ifndef BMCHS
		if (!HasUserPerm(PERM_SYSOP))
		{
		    vmsg("�����]�w�ݭn�����v��");
		    break;
		}
#endif
		{
		    char ans[2];
		    move(b_lines-2, 0); clrtobot();
		    if (getdata(b_lines-1, 0, (bp->brdattr & BRD_HIDE) ?
			    ANSI_COLOR(1;32) " +++ �T�w�n�Ѱ��ݪO���ζ�?" ANSI_RESET " [y/N]: ":
			    ANSI_COLOR(1;31) " --- �T�w�n���άݪO��?" ANSI_RESET " [y/N]: ",
			    ans, sizeof(ans), LCECHO) < 1 ||
			    ans[0] != 'y')
			break;
		}

		if(bp->brdattr & BRD_HIDE)
		{
		    bp->brdattr &= ~BRD_HIDE;
		    bp->brdattr &= ~BRD_POSTMASK;
		    hbflreload(currbid);
		} else {
		    bp->brdattr |= BRD_HIDE;
		    bp->brdattr |= BRD_POSTMASK;
		}
		bp->perm_reload = now;
		touched = 1;
		vmsg((bp->brdattr & BRD_HIDE) ?
			" �`�N: �ݪO�w����" :
			" �`�N: �ݪO�w�Ѱ�����");
		break;

		// ii�s���N�|�~Ĳ�A�ҥH�A�T�{�@�U
	    case 'i':
		{
		    char ans[2];
		    move(b_lines-2, 0); clrtobot();
		    if (getdata(b_lines-1, 0, (bp->brdattr & BRD_IPLOGRECMD) ?
			    ANSI_COLOR(1;32) " --- �T�w�n����O������ IP ��?" ANSI_RESET " [y/N]: " :
			    ANSI_COLOR(1;31) " +++ �T�w�n�O������ IP ��?" ANSI_RESET " [y/N]: ",
			    ans, sizeof(ans), LCECHO) < 1 ||
			    ans[0] != 'y')
			break;
		}
		bp->brdattr ^= BRD_IPLOGRECMD;
		touched = 1;
		vmsg((bp->brdattr & BRD_IPLOGRECMD) ?
			" �`�N: �}�l�O������IP" :
			" �`�N: �w����O������IP");
		break;

#ifdef USE_COOLDOWN
            case 'j':
                if (!(HasUserPerm(PERM_SYSOP | PERM_POLICE) ||
                      (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()))) {
		    vmsg("�����]�w�ݭn�����άݪOĵ��θs�ժ��v��");
                    break;
                }
                {
                    char ans[50];
                    getdata(b_lines - 1, 0, "�п�J�z��(�ťթ��]�w):", ans, sizeof(ans), DOECHO);
                    if (!*ans) {
                        vmsg("����J�z�ѡA���]�w�C");
                        break;
                    }
                    bp->brdattr ^= BRD_COOLDOWN;
                    post_policelog(bp->brdname, NULL, "�N�R", ans, (bp->brdattr & BRD_COOLDOWN));
                    touched = 1;
                }
                break;
#endif

	    case 'g':
#ifndef BMCHS
		if (!HasUserPerm(PERM_SYSOP))
		{
		    vmsg("�����]�w�ݭn�����v��");
		    break;
		}
#endif
		bp->brdattr ^= BRD_BMCOUNT;
		touched = 1;
		break;

	    case 'r':
		bp->brdattr ^= BRD_NORECOMMEND;
		touched = 1;
		break;

	    case 'f':
		bp->brdattr &= ~BRD_NORECOMMEND;
		bp->brdattr ^= BRD_NOFASTRECMD;
		touched = 1;

		if(bp->brdattr & BRD_NOFASTRECMD)
		{
		    char buf[8] = "";

		    if(bp->fastrecommend_pause > 0)
			sprintf(buf, "%d", bp->fastrecommend_pause);
		    getdata_str(b_lines-1, 0,
			    "�п�J�s���ɶ�����(���: ��) [5~240]: ",
			    buf, 4, NUMECHO, buf);
		    if(buf[0] >= '0' && buf[0] <= '9')
			bp->fastrecommend_pause = atoi(buf);

		    if( bp->fastrecommend_pause < 5 ||
			bp->fastrecommend_pause > 240)
		    {
			if(buf[0])
			{
			    vmsg("��J�ɶ��L�ġA�Шϥ� 5~240 �������Ʀr�C");
			}
			bp->fastrecommend_pause = 0;
			bp->brdattr &= ~BRD_NOFASTRECMD;
		    }
		}
		break;
#ifndef OLDRECOMMEND
	    case 's':
		if(bp->brdattr & BRD_NORECOMMEND)
		    bp->brdattr |= BRD_NOBOO;
		bp->brdattr ^= BRD_NOBOO;
		touched = 1;
		if (!(bp->brdattr & BRD_NOBOO))
		    bp->brdattr &= ~BRD_NORECOMMEND;
		break;
#endif
	    case '8':
		if (!cuser.over_18)
		{
		    vmsg("�O�D�������� 18 ���C");
		} else {
		    bp->brdattr ^= BRD_OVER18;
		    touched = 1;
		}
		break;

	    case 'v':
		clear();
		friend_edit(BOARD_VISABLE);
		assert(0<=currbid-1 && currbid-1<MAX_BOARD);
		hbflreload(currbid);
		clear();
		break;

	    case 'w':
		clear();
#ifdef USE_NEW_BAN_SYSTEM
                edit_banned_list_for_board(currboard);
#else
		friend_edit(BOARD_WATER);
#endif
		clear();
		break;

	    case 'o':
		clear();
		friend_edit(FRIEND_CANVOTE);
		clear();

	    case 'm':
		clear();
		b_vote_maintain();
		clear();
		break;

	    case 'n':
		clear();
		b_post_note();
		clear();
		break;

            case 'p':
                clear();
                b_notes_edit();
                clear();
                break;

	    case 'c':
		clear();
		b_posttype();
		clear();
		break;

	    case 'y':
		if (!(HasUserPerm(PERM_SYSOP) || (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()) ) ) {
		    vmsg("�����]�w�ݭn�s�ժ��ί����v��");
		    break;
		}
		bp->brdattr ^= BRD_NOREPLY;
		touched = 1;
		break;

	    case 'k':
		if (!(HasUserPerm(PERM_SYSOP) || (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()) ) ) {
		    vmsg("�����]�w�ݭn�s�ժ��ί����v��");
		    break;
		}
		bp->brdattr ^= BRD_BM_MASK_CONTENT;
		touched = 1;
		break;

	    case 'd':
#ifndef ALLOW_BM_SET_NOSELFDELPOST
		if (!(HasUserPerm(PERM_SYSOP) || (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()) ) ) {
		    vmsg("�����]�w�ݭn�s�ժ��ί����v��");
		    break;
		}
#endif
		bp->brdattr ^= BRD_NOSELFDELPOST;
		touched = 1;
		break;

	    default:
		finished = 1;
		break;
	}
    }
    if(touched)
    {
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	log_usies("SetBoard", bp->brdname);
	vmsg("�w�x�s�s�]�w");
    }
    else
	vmsg("�����ܥ���]�w");

    return FULLUPDATE;
}

int
b_quick_acl(int ent GCC_UNUSED, fileheader_t *fhdr,
            const char *direct GCC_UNUSED)
{
#ifdef USE_NEW_BAN_SYSTEM
    const boardheader_t *bp = getbcache(currbid);
    if (!bp)
        return FULLUPDATE;

    if (!(currmode & MODE_BOARD) && !HasUserPerm(PERM_SYSOP))
        return FULLUPDATE;

    char uid[IDLEN+2];
    strncpy(uid, fhdr->owner, sizeof(uid));
#ifdef STR_SAFEDEL_TITLE
    if (fhdr->filename[0] == '.' &&
        strncmp(fhdr->title, STR_SAFEDEL_TITLE,
                strlen(STR_SAFEDEL_TITLE)) == 0) {
        char *ps = &fhdr->title[strlen(STR_SAFEDEL_TITLE)];
        if (ps[0] == ' ' && ps[1] == '[') {
            ps += 2;
            char *pe = strchr(ps, ']');
            if (pe) {
                strncpy(uid, ps, pe - ps);
                uid[pe - ps] = '\0';
            }
        }
    }
#endif // STR_SAFEDEL_TITLE
    if (!uid[0] || !is_validuserid(uid) || !searchuser(uid, uid)) {
        vmsg("�ӨϥΪ̱b�����s�b");
        return FULLUPDATE;
    }

    edit_user_acl_for_board(uid, bp->brdname);

    return FULLUPDATE;
#else // !USE_NEW_BAN_SYSTEM
    return DONOTHING;
#endif // USE_NEW_BAN_SYSTEM
}

static int
check_newpost(boardstat_t * ptr)
{				/* Ptt �� */
    time4_t         ftime;

    ptr->myattr &= ~NBRD_UNREAD;
    if (B_BH(ptr)->brdattr & (BRD_GROUPBOARD | BRD_SYMBOLIC) ||
	    ptr->myattr & (NBRD_LINE | NBRD_FOLDER))
	return 0;

    if (B_TOTAL(ptr) == 0) {
        setbtotal(ptr->bid);
        setbottomtotal(ptr->bid);
    }
    if (B_TOTAL(ptr) == 0)
	return 0;

    // Note: �H�e�o�̦� if(ftime > now [+10]) �N���] ftime �� code;
    // ����ڪ����p�O���˲��ͪ� lastposttime �|�P�ݪO�����峹���P�A
    // �y����Ū�Ÿ��û����|�����C �S�A�b�o�䪺 now ���O�D�P�B���A
    // �ҥH�Y���H�b�i�J�o�̭��d�ܤ[(ex, system overload)���N�|�D�`�e��
    // �ܦ� ftime > now.
    ftime = B_LASTPOSTTIME(ptr);

    if (brc_unread_time(ptr->bid, ftime, 0))
	ptr->myattr |= NBRD_UNREAD;

    return 1;
}

static boardstat_t *
addnewbrdstat(int n, int state)
{
    boardstat_t    *ptr;

    // ptt 2 local modification
    // XXX maybe some developer discovered signed short issue?
    assert(n != -32769);

    assert(0<=n && n<MAX_BOARD);
    assert(0<=brdnum && brdnum<nbrdsize);
    ptr = &nbrd[brdnum++];
    //boardheader_t  *bptr = &bcache[n];
    //ptr->total = &(SHM->total[n]);
    //ptr->lastposttime = &(SHM->lastposttime[n]);

    ptr->bid = n + 1;
    ptr->myattr = state;
    if ((B_BH(ptr)->brdattr & BRD_HIDE) && state == NBRD_BOARD)
	B_BH(ptr)->brdattr |= BRD_POSTMASK;
    if (!IS_LISTING_FAV())
	ptr->myattr &= ~NBRD_FAV;
    check_newpost(ptr);
    return ptr;
}

#if !HOTBOARDCACHE
static int
cmpboardfriends(const void *brd, const void *tmp)
{
#ifdef USE_COOLDOWN
    if ((B_BH((boardstat_t*)tmp)->brdattr & BRD_COOLDOWN) &&
	    (B_BH((boardstat_t*)brd)->brdattr & BRD_COOLDOWN))
	return 0;
    else if ( B_BH((boardstat_t*)tmp)->brdattr & BRD_COOLDOWN ) {
	if (B_BH((boardstat_t*)brd)->nuser == 0)
	    return 0;
	else
	    return 1;
    }
    else if ( B_BH((boardstat_t*)brd)->brdattr & BRD_COOLDOWN ) {
	if (B_BH((boardstat_t*)tmp)->nuser == 0)
	    return 0;
	else
	    return -1;
    }
#endif
    return ((B_BH((boardstat_t*)tmp)->nuser) -
	    (B_BH((boardstat_t*)brd)->nuser));
}
#endif

static void
load_boards(char *key)
{
    int             type = (HasUserFlag(UF_BRDSORT)) ? 1 : 0;
    int             i;
    int             state;

    // override type in class root, because usually we don't need to sort
    // class root; and there may be out-of-sync in that mode.
    if (IN_CLASSROOT())
	type = 1;

    brdnum = 0;
    if (nbrd) {
        free(nbrd);
	nbrdsize = 0;
	nbrd = NULL;
    }
    if (!IN_CLASS()) {
	if(IS_LISTING_FAV()){
            fav_t   *fav = get_current_fav();
            int     nfav;

	    // XXX TODO �ܦh�H���b�o�̡A���ڤ��T�w�L�̬O fav �{���a���٬O�ӥä[�ץ�
	    // workaround ���ӬO nfav = fav ? get_data_number(fav) : 0;
            if (!get_current_fav()) {
                vmsgf("�ڪ��̷R�t�ο��~�A�Ш�" BN_BUGREPORT "���i�z���e�i��F���ǰʧ@�A����");
                refresh();
                assert(get_current_fav());
                exit(-1);
            }

            nfav = get_data_number(fav);
	    if( nfav == 0 ) {
		nbrdsize = 1;
		nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * 1);
		addnewbrdstat(0, 0); // dummy
    		return;
	    }
	    nbrdsize = nfav;
	    nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * nfav);
            for( i = 0 ; i < fav->DataTail; ++i ){
		int state;
		if (!(fav->favh[i].attr & FAVH_FAV))
		    continue;

		if ( !key[0] ){
		    if (get_item_type(&fav->favh[i]) == FAVT_LINE )
			state = NBRD_LINE;
		    else if (get_item_type(&fav->favh[i]) == FAVT_FOLDER )
			state = NBRD_FOLDER;
		    else {
			state = NBRD_BOARD;
			if (is_set_attr(&fav->favh[i], FAVH_UNREAD))
			    state |= NBRD_UNREAD;
		    }
		} else {
		    if (get_item_type(&fav->favh[i]) == FAVT_LINE )
			continue;
		    else if (get_item_type(&fav->favh[i]) == FAVT_FOLDER ){
			if( strcasestr(
			    get_folder_title(fav_getid(&fav->favh[i])),
			    key)
			)
			    state = NBRD_FOLDER;
			else
			    continue;
		    }else{
			boardheader_t *bptr = getbcache(fav_getid(&fav->favh[i]));
			assert(0<=fav_getid(&fav->favh[i])-1 && fav_getid(&fav->favh[i])-1<MAX_BOARD);
			if (strcasestr(bptr->title, key))
			    state = NBRD_BOARD;
			else
			    continue;
			if (is_set_attr(&fav->favh[i], FAVH_UNREAD))
			    state |= NBRD_UNREAD;
		    }
		}

		if (is_set_attr(&fav->favh[i], FAVH_TAG))
		    state |= NBRD_TAG;
		if (is_set_attr(&fav->favh[i], FAVH_ADM_TAG))
		    state |= NBRD_TAG;
		// ���ǤH �Y�� bid < 0 Orzz // ptt2 local modification
		if (fav_getid(&fav->favh[i]) < 1)
		    continue;
		addnewbrdstat(fav_getid(&fav->favh[i]) - 1, NBRD_FAV | state);
	    }
	}
#if HOTBOARDCACHE
	else if(IN_HOTBOARD()){
	    nbrdsize = SHM->nHOTs;
	    if(nbrdsize == 0) {
		nbrdsize = 1;
		nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * 1);
		addnewbrdstat(0, 0); // dummy
		return;
	    }
	    assert(0<nbrdsize);
	    nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * nbrdsize);
	    for( i = 0 ; i < nbrdsize; ++i ) {
		if(SHM->HBcache[i] == -1)
		    continue;
		addnewbrdstat(SHM->HBcache[i], HasBoardPerm(&bcache[SHM->HBcache[i]]));
	    }
	}
#endif
	else { // general case
	    nbrdsize = num_boards();
	    assert(0<nbrdsize && nbrdsize<=MAX_BOARD);
	    nbrd = (boardstat_t *) malloc(sizeof(boardstat_t) * nbrdsize);
	    for (i = 0; i < nbrdsize; i++) {
		int n = SHM->bsorted[type][i];
		boardheader_t *bptr;
		if (n < 0)
		    continue;
		bptr = &bcache[n];
		if (bptr == NULL)
		    continue;
		if (!bptr->brdname[0] ||
		    (bptr->brdattr & (BRD_GROUPBOARD | BRD_SYMBOLIC)) ||
		    !((state = HasBoardPerm(bptr)) || GROUPOP()) ||
		    TITLE_MATCH(bptr, key)
#if ! HOTBOARDCACHE
		    || (IN_HOTBOARD() && bptr->nuser < 5)
#endif
		    )
		    continue;
		addnewbrdstat(n, state);
	    }
	}
#if ! HOTBOARDCACHE
	if (IN_HOTBOARD())
	    qsort(nbrd, brdnum, sizeof(boardstat_t), cmpboardfriends);
#endif
    } else { /* load boards of a subclass */
	boardheader_t  *bptr = getbcache(class_bid);
	int childcount;
	int bid;

	assert(0<=class_bid-1 && class_bid-1<MAX_BOARD);
	if (bptr->firstchild[type] == 0 || bptr->childcount==0)
	    resolve_board_group(class_bid, type);

        childcount = bptr->childcount;  // Ptt: child count after resolve_board_group

	nbrdsize = childcount + 5;
	nbrd = (boardstat_t *) malloc((childcount+5) * sizeof(boardstat_t));
        // �w�d��ӥH�K�j�q�}�O�ɱ���
	for (bid = bptr->firstchild[type]; bid > 0 &&
		brdnum < childcount+5; bid = bptr->next[type]) {
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
            bptr = getbcache(bid);
	    state = HasBoardPerm(bptr);
	    if ( !(state || GROUPOP()) || TITLE_MATCH(bptr, key) )
		continue;

	    if (bptr->brdattr & BRD_SYMBOLIC) {
		/* Only SYSOP/SYSSUPERSUBOP knows a board is a link or not. */
		if (HasUserPerm(PERM_SYSOP) || HasUserPerm(PERM_SYSSUPERSUBOP))
		    state |= NBRD_SYMBOLIC;
		else {
		    bid = BRD_LINK_TARGET(bptr);
		    if (bcache[bid - 1].brdname[0] == 0) {
			vmsg("�s���w�l���A�Ц� SYSOP �^�������D�C");
			continue;
		    }
		}
	    }
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    addnewbrdstat(bid-1, state);
	}
        if(childcount < brdnum) {
	    //Ptt: dirty fix fix soon
	    getbcache(class_bid)->childcount = 0;
	}


    }
}

static int
search_local_board()
{
    int             num;
    char            genbuf[IDLEN + 2];
    struct Vector namelist;

    move(0, 0);
    clrtoeol();
    Vector_init(&namelist, IDLEN + 1);
    assert(brdnum<=nbrdsize);
    Vector_resize(&namelist, brdnum);
    for (num = 0; num < brdnum; num++)
        if (!IS_LISTING_FAV() ||
            (nbrd[num].myattr & NBRD_BOARD && HasBoardPerm(B_BH(&nbrd[num]))) )
            Vector_add(&namelist, B_BH(&nbrd[num])->brdname);
    namecomplete2(&namelist, ANSI_REVERSE "�i �j�M�Ҧb��m�ݪO �j"
	    ANSI_RESET "\n�п�J�ݪO�W��(���ť���۰ʷj�M): ", genbuf);
    Vector_delete(&namelist);


    for (num = 0; num < brdnum; num++)
        if (!strcasecmp(B_BH(&nbrd[num])->brdname, genbuf))
            return num;
    return -1;
}

static int
search_board(const char *bname)
{
    int num = 0;
    assert(brdnum<=nbrdsize);

    for (num = 0; num < brdnum; num++) {
	boardstat_t *bptr = &nbrd[num];
	if (IS_LISTING_FAV() && !(bptr->myattr & NBRD_BOARD))
	    continue;
	if (!strcasecmp(B_BH(bptr)->brdname, bname))
	    return num;
    }
    return -1;
}

static int
unread_position(char *dirfile, boardstat_t * ptr)
{
    fileheader_t    fh;
    char            fname[FNLEN];
    register int    num, fd, step, total;

    total = B_TOTAL(ptr);
    num = total + 1;
    if ((ptr->myattr & NBRD_UNREAD) && (fd = open(dirfile, O_RDWR)) > 0) {
	if (!brc_initial_board(B_BH(ptr)->brdname)) {
	    num = 1;
	} else {
	    num = total - 1;
	    step = 4;
	    while (num > 0) {
		lseek(fd, (off_t) (num * sizeof(fh)), SEEK_SET);
		if (read(fd, fname, FNLEN) <= 0 ||
		    !brc_unread(ptr->bid, fname, 0))
		    break;
		num -= step;
		if (step < 32)
		    step += step >> 1;
	    }
	    if (num < 0)
		num = 0;
	    while (num < total) {
		lseek(fd, (off_t) (num * sizeof(fh)), SEEK_SET);
		if (read(fd, fname, FNLEN) <= 0 ||
		    brc_unread(ptr->bid, fname, 0))
		    break;
		num++;
	    }
	}
	close(fd);
    }
    if (num < 0)
	num = 0;
    return num;
}

static char
get_fav_type(boardstat_t *ptr)
{
    if (ptr->myattr & NBRD_FOLDER)
	return FAVT_FOLDER;
    else if (ptr->myattr & NBRD_BOARD)
	return FAVT_BOARD;
    else if (ptr->myattr & NBRD_LINE)
	return FAVT_LINE;
    return 0;
}

static void
brdlist_foot(void)
{
    vs_footer("  ��ܬݪO  ",
	    IS_LISTING_FAV() ?
	    "  (a)�W�[�ݪO (s)�i�J�w���O�W (y)�C�X���� (v/V)�wŪ/��Ū" :
            IN_CLASS() ?
	    "  (m)�[�J/���X�̷R (s)�i�J�w���O�W (v/V)�wŪ/��Ū " :
	    "  (m)�[�J/���X�̷R (y)�u�C�̷R (v/V)�wŪ/��Ū ");
}


static inline const char *
make_class_color(char *name)
{
    /* 0;34 is too dark */
    uint32_t index = (((uint32_t)name[0] + name[1] + name[2] + name[3]) & 0x07);
    const char *colorset[8] = {"", ANSI_COLOR(32),
	ANSI_COLOR(33), ANSI_COLOR(36), ANSI_COLOR(1;34),
	ANSI_COLOR(1), ANSI_COLOR(1;32), ANSI_COLOR(1;33)};
    const char *colorset2[8] = {"", ANSI_COLOR(32),
	ANSI_COLOR(33), ANSI_COLOR(36), ANSI_COLOR(35),
	"", ANSI_COLOR(32), ANSI_COLOR(33)};

    return HasUserFlag(UF_MENU_LIGHTBAR) ? colorset2[index] : colorset[index];
}

#define HILIGHT_COLOR	ANSI_COLOR(1;36)
#define HILIGHT_COLOR2	ANSI_COLOR(36)

static void
show_brdlist(int head, int clsflag, int newflag)
{
    int             myrow = 2;
    if (unlikely(IN_CLASSROOT())) {
	currstat = CLASS;
	myrow = 6;
	showtitle("�����ݪO", BBSName);
	move(1, 0);
	// TODO move ascii art to adbanner?
	outs(
	    "                                                              "
	    "��  �~�X" ANSI_COLOR(33) "��\n"
	    "                                                    ��X  " ANSI_RESET " "
	    "���i" ANSI_COLOR(47) "��" ANSI_COLOR(40) "�i�i����\n"
	    "  " ANSI_COLOR(44) "   �s�s�s�s�s�s�s�s                               "
	    ANSI_COLOR(33) "��" ANSI_RESET ANSI_COLOR(44) " �����i�i�i�������� " ANSI_RESET "\n"
	    "  " ANSI_COLOR(44) "                                                  "
	    ANSI_COLOR(33) "  " ANSI_RESET ANSI_COLOR(44) " �����i�i�i������ ��" ANSI_RESET "\n"
	    "                                  �s�s�s�s�s�s�s�s    " ANSI_COLOR(33)
	    "�x" ANSI_RESET "   ���i�i�i�i�� ��\n"
	    "                                                      " ANSI_COLOR(33) "��"
	    "�X�X" ANSI_RESET "  ��      �X��" ANSI_RESET);
    } else if (clsflag) {
	showtitle("�ݪO�C��", BBSName);
	outs("[��][q]�^�W�h [��][r]�\\Ū [����]��� [PgUp][PgDn]½�� [c]�s�峹 [/]�j�M [h]�D�U\n");

	// boards in Ptt series are very, very large.
	// let's create more space for board numbers,
	// and less space for BM.
	//
	// newflag is not so different now because we use all 5 digits.

	vbarf(ANSI_REVERSE "   %s   ��  �O       ���O   ��   ��   ��   �z"    
              "               �H�� �O   �D", newflag ? "�`��" : "�s��");
	move(b_lines, 0);
	brdlist_foot();
    }
    if (brdnum > 0) {
	boardstat_t    *ptr;
 	char *unread[2] = {ANSI_COLOR(37) "  " ANSI_RESET, ANSI_COLOR(1;31) "��" ANSI_RESET};
        if (HasUserFlag(UF_MENU_LIGHTBAR))
            unread[1] = ANSI_COLOR(31) "��" ANSI_RESET;

	if (IS_LISTING_FAV() && brdnum == 1 && get_fav_type(&nbrd[0]) == 0) {

	    // (a) or (i) needs HasUserPerm(PERM_LOGINOK)).
	    // 3 = first line of empty area
	    if (!HasFavEditPerm())
	    {
		// TODO actually we cannot use 's' (for PTT)...
		mvouts(3, 10,
		"--- ���U���ϥΪ̤~��s�W�ݪO�� (�i�� s ��ʿ��) ---");
	    } else {
		// normal user. tell him what to do.
		mvouts(3, 10,
		"--- �ťؿ��A�Ы� a �s�W�Υ� y �C�X�����ݪO��� z �W�R ---");
	    }
	    return;
	}

	while (++myrow < b_lines) {

	    move(myrow, 0);
	    clrtoeol();

	    if (head < brdnum) {
		assert(0<=head && head<nbrdsize);
		ptr = &nbrd[head++];
		if (ptr->myattr & NBRD_LINE){
		    if( !newflag )
			prints("%7d %c ", head, ptr->myattr & NBRD_TAG ? 'D' : ' ');
		    else
			prints("%7s   ", "");

		    if (!(ptr->myattr & NBRD_FAV))
			outs(ANSI_COLOR(1;30));

		    outs("------------"
			    "      "
			    // "------"
			    "------------------------------------------"
			    ANSI_RESET "\n");
		    continue;
		}
		else if (ptr->myattr & NBRD_FOLDER){
		    char *title = get_folder_title(ptr->bid);
		    prints("%7d %c ",
			    newflag ?
			    get_data_number(get_fav_folder(getfolder(ptr->bid))) :
			    head, ptr->myattr & NBRD_TAG ? 'D' : ' ');

		    // well, what to print with myfav folders?
		    // this style is too long and we don't want to
		    // fight with users...
		    // think about new way some otherday.
		    prints("%sMyFavFolder" ANSI_RESET "  �ؿ� ��%-34s",
			    !(HasUserFlag(UF_FAV_NOHILIGHT))?
                             (HasUserFlag(UF_MENU_LIGHTBAR) ?
                              HILIGHT_COLOR2 : HILIGHT_COLOR) : "",
			    title);
		    /*
		    if (!(HasUserFlag(UF_FAV_NOHILIGHT)))
			outs(HILIGHT_COLOR);
		    prints("%-12s", "[Folder]");
		    outs(ANSI_RESET);
		    prints(" �ؿ� �U%-34s", title);
		    */
		    /*
		    outs(ANSI_COLOR(0;36));
		    prints("�U%-70.70s", title);
		    outs(ANSI_RESET);
		    */
		    continue;
		}

		if (IN_CLASSROOT())
		    outs("          ");
		else {
		    if (!GROUPOP() && !HasBoardPerm(B_BH(ptr))) {
                        const char *reason = "[�T�J]";

			if (newflag)
                            prints("%7s", "");
			else
                            prints("%7d", head);

                        if (B_BH(ptr)->brdattr & BRD_HIDE)
                            reason = "[���O]";

                        // we don't print BM and popularity, so subject can be
                        // longer
			prints("X%c %-13.13s%-7.7s %-48.48s",
				ptr->myattr & NBRD_TAG ? 'D' : ' ',
                                B_BH(ptr)->brdname,
                                reason,
#ifdef USE_REAL_DESC_FOR_HIDDEN_BOARD_IN_MYFAV
                                B_BH(ptr)->title + 7
#else
                                "<�ثe�L�k�i�J���ݪO>"
#endif
                                );
			continue;
		    }
		}

#ifdef USE_REAL_DESC_FOR_HIDDEN_BOARD_IN_MYFAV
		const int should_show_sensitive_info = true;
#else
		// Show sensitive info if permission is *not* given by solely
		// PERM_SYSOP, GROUPOP, or both.
		const int should_show_sensitive_info =
		    !BoardPermNeedsSysopOverride(B_BH(ptr)) &&
		    !(GROUPOP() && !HasBoardPerm(B_BH(ptr)));
#endif


		if (newflag && B_BH(ptr)->brdattr & BRD_GROUPBOARD)
		    outs("          ");
		else if (should_show_sensitive_info)
		    prints("%7d%c%s",
			    newflag ? (int)(B_TOTAL(ptr)) : head,
			    !(B_BH(ptr)->brdattr & BRD_HIDE) ? ' ' :
			    (B_BH(ptr)->brdattr & BRD_POSTMASK) ? ')' : '-',
			    (ptr->myattr & NBRD_TAG) ? "D " :
			    (B_BH(ptr)->brdattr & BRD_GROUPBOARD) ? "  " :
			    unread[ptr->myattr & NBRD_UNREAD ? 1 : 0]);
		else {
		    if (newflag)
			prints("%7s", "");
		    else
			prints("%7d", head);
		    prints("X%s", (ptr->myattr & NBRD_TAG) ? "D " : unread[0]);
		}

		if (!IN_CLASSROOT()) {
		    prints("%s%-13s" ANSI_RESET "%s%5.5s" ANSI_COLOR(0;37)
			    "%2.2s" ANSI_RESET "%-34.34s",
			    ((!(HasUserFlag(UF_FAV_NOHILIGHT)) &&
			      getboard(ptr->bid) != NULL))?
                             (HasUserFlag(UF_MENU_LIGHTBAR) ? HILIGHT_COLOR2 :
                              HILIGHT_COLOR) : "",
			    B_BH(ptr)->brdname,
			    make_class_color(B_BH(ptr)->title),
			    B_BH(ptr)->title,
			    should_show_sensitive_info ?
				B_BH(ptr)->title + 5 : "",
			    should_show_sensitive_info ?
				B_BH(ptr)->title + 7 : "");

		    if (!should_show_sensitive_info)
			outs("   ");
#ifdef USE_COOLDOWN
		    else if (B_BH(ptr)->brdattr & BRD_COOLDOWN)
#else
		    else if (0)
#endif
                        outs("�R ");
                    // Note the nuser is not updated realtime, or have some bug.
		    else if (B_BH(ptr)->nuser < 1)
			prints(" %c ", B_BH(ptr)->bvote ? 'V' : ' ');
		    else if (B_BH(ptr)->nuser <= 10)
			prints("%2d ", B_BH(ptr)->nuser);
		    else if (B_BH(ptr)->nuser <= 50)
			prints(ANSI_COLOR(1;33) "%2d" ANSI_RESET " ", B_BH(ptr)->nuser);
#ifdef EXTRA_HOTBOARD_COLORS
		    // piaip 2008/02/04: new colors
		    else if (B_BH(ptr)->nuser >= 100000)
			outs(ANSI_COLOR(1;35) "�z!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 60000)
			outs(ANSI_COLOR(1;33) "�z!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 30000)
			outs(ANSI_COLOR(1;32) "�z!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 10000)
			outs(ANSI_COLOR(1;36) "�z!" ANSI_RESET);
#endif
		    else if (B_BH(ptr)->nuser >= 5000)
			outs(ANSI_COLOR(1;34) "�z!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 2000)
			outs(ANSI_COLOR(1;31) "�z!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 1000)
			outs(ANSI_COLOR(1) "�z!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 100)
			outs(ANSI_COLOR(1) "HOT" ANSI_RESET);
		    else //if (B_BH(ptr)->nuser > 50)
			prints(ANSI_COLOR(1;31) "%2d" ANSI_RESET " ", B_BH(ptr)->nuser);
		    prints("%.*s" ANSI_CLRTOEND, t_columns - 68, B_BH(ptr)->BM);
		} else {
		    prints("%-40.40s %.*s", B_BH(ptr)->title + 7,
			   t_columns - 68, B_BH(ptr)->BM);
		}
	    }
	    clrtoeol();
	}
    }
}

static void
set_menu_group_op(char *BM)
{
    int is_bm = 0;
    if (HasUserPerm(PERM_NOCITIZEN))
        return;
    if (HasUserPerm(PERM_BOARD)) {
        is_bm = 1;
    } else if (is_uBM(BM, cuser.userid)) {
        is_bm = 1;
    }

    if (!is_bm)
        return;

    currmode |= MODE_GROUPOP;

    // XXX ���O�ܽT�w�O�_�Ӧb�o�� save level?
    if (!HasUserPerm(PERM_SYSSUBOP) || !HasUserPerm(PERM_BM))
        pwcuBitEnableLevel(PERM_SYSSUBOP | PERM_BM);

}

static void replace_link_by_target(boardstat_t *board)
{
    assert(0<=board->bid-1 && board->bid-1<MAX_BOARD);
    board->bid = BRD_LINK_TARGET(getbcache(board->bid));
    board->myattr &= ~NBRD_SYMBOLIC;
}

static int
paste_taged_brds(int gid)
{
    fav_t *fav;
    int  bid, tmp;

    if (gid == 0  || ! (HasUserPerm(PERM_SYSOP) || GROUPOP()) ||
        vans("�K�W�аO���ݪO?(y/N)")!='y') return 0;
    fav = get_fav_root();
    for (tmp = 0; tmp < fav->DataTail; tmp++) {
	    boardheader_t  *bh;
	    bid = fav_getid(&fav->favh[tmp]);
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    bh = getbcache(bid);
	    if( !is_set_attr(&fav->favh[tmp], FAVH_ADM_TAG))
		continue;
	    set_attr(&fav->favh[tmp], FAVH_ADM_TAG, FALSE);
	    if (bh->gid != gid) {
		bh->gid = gid;
		assert(0<=bid-1 && bid-1<MAX_BOARD);
		substitute_record(FN_BOARD, bh,
				  sizeof(boardheader_t), bid);
		reset_board(bid);
		log_usies("SetBoardGID", bh->brdname);
	    }
	}
    sort_bcache();
    return 1;
}

static void
choose_board(int newflag)
{
    static int      num = 0;
    boardstat_t    *ptr;
    int             head = -1, ch = 0, currmodetmp, tmp, tmp1, bidtmp;
    char            keyword[13] = "", buf[PATHLEN];

    setutmpmode(newflag ? READNEW : READBRD);
    if( get_fav_root() == NULL ) {
	fav_load();
        if (!get_current_fav()) {
            vmsgf("�ڪ��̷R���J���ѡA�Ш�" BN_BUGREPORT "���i�z���e�i��F���ǰʧ@�A����");
            refresh();
            assert(get_current_fav());
            exit(-1);
        }
    }

    ++choose_board_depth;
    brdnum = 0;
    if (!cuser.userlevel)	/* guest yank all boards */
	LIST_BRD();

    do {
	if (brdnum <= 0) {
	    load_boards(keyword);
	    if (brdnum <= 0) {
		if (keyword[0] != 0) {
		    vmsg("�S������ݪO���D��������r");
		    keyword[0] = 0;
		    brdnum = -1;
		    continue;
		}
		if (IS_LISTING_BRD()) {
		    if (HasUserPerm(PERM_SYSOP) || GROUPOP()) {
			if (paste_taged_brds(class_bid) ||
    			    m_newbrd(class_bid, 0) == -1)
			    break;
			brdnum = -1;
			continue;
		    } else
			break;
		}
	    }
	    head = -1;
	}

	/* reset the cursor when out of range */
	if (num < 0)
	    num = 0;
	else if (num >= brdnum)
	    num = brdnum - 1;

	if (head < 0) {
	    if (newflag) {
		tmp = num;
		assert(brdnum<=nbrdsize);
		while (num < brdnum) {
		    ptr = &nbrd[num];
		    if (ptr->myattr & NBRD_UNREAD)
			break;
		    num++;
		}
		if (num >= brdnum)
		    num = tmp;
	    }
	    head = (num / p_lines) * p_lines;
	    show_brdlist(head, 1, newflag);
	} else if (num < head || num >= head + p_lines) {
	    head = (num / p_lines) * p_lines;
	    show_brdlist(head, 0, newflag);
	}
	if (IN_CLASSROOT())
	    ch = cursor_key(7 + num - head, 10);
	else
	    ch = cursor_key(3 + num - head, 0);

	switch (ch) {
		///////////////////////////////////////////////////////
		// General Hotkeys
		///////////////////////////////////////////////////////

	case 'h':
	    show_helpfile(fn_boardlisthelp);
	    show_brdlist(head, 1, newflag);
	    break;
	case Ctrl('W'):
	    whereami();
	    head = -1;
	    break;

	case 'c':
	    show_brdlist(head, 1, newflag ^= 1);
	    break;

	// ZA
	case Ctrl('Z'):
	    head = -1;
	    if (ZA_Select())
		ch = 'q';
	    else
		break;
	    // if selected, follow q.

	case 'e':
	case KEY_LEFT:
	case EOF:
	    ch = 'q';
	case 'q':
	    if (keyword[0]) {
		keyword[0] = 0;
		brdnum = -1;
		ch = ' ';
	    }
	    break;
	case KEY_PGUP:
	case 'P':
	case 'b':
	case Ctrl('B'):
	    if (num) {
		num -= p_lines;
		break;
	    }
	case KEY_END:
	case '$':
	    num = brdnum - 1;
	    break;
	case ' ':
	case KEY_PGDN:
	case 'N':
	case Ctrl('F'):
	    if (num == brdnum - 1)
		num = 0;
	    else
		num += p_lines;
	    break;
	case KEY_UP:
	case 'p':
	case 'k':
	    if (num-- <= 0)
		num = brdnum - 1;
	    break;
	case '*':
	    if (IS_LISTING_FAV()) {
		int i = 0;
		assert(brdnum<=nbrdsize);
		for (i = 0; i < brdnum; i++)
		{
		    ptr = &nbrd[i];
		    if (IS_LISTING_FAV()){
			assert(nbrdsize>0);
			if(get_fav_type(&nbrd[0]) != 0)
			    fav_tag(ptr->bid, get_fav_type(ptr), 2);
		    }
		    ptr->myattr ^= NBRD_TAG;
		}
		head = 9999;
	    }
	    break;
	case 't':
	    assert(0<=num && num<nbrdsize);
	    ptr = &nbrd[num];
	    if (IS_LISTING_FAV()){
		assert(nbrdsize>0);
		if(get_fav_type(&nbrd[0]) != 0)
		    fav_tag(ptr->bid, get_fav_type(ptr), EXCH);
	    }
	    else if (HasUserPerm(PERM_SYSOP) ||
		     HasUserPerm(PERM_SYSSUPERSUBOP) ||
		     HasUserPerm(PERM_SYSSUBOP) ||
		     HasUserPerm(PERM_BOARD)) {
		/* �����޲z�Ϊ� tag */
		if (ptr->myattr & NBRD_TAG)
		    set_attr(getadmtag(ptr->bid), FAVH_ADM_TAG, FALSE);
		else
		    fav_add_admtag(ptr->bid);
	    }
	    ptr->myattr ^= NBRD_TAG;
	    head = 9999;
	case KEY_DOWN:
	case 'n':
	case 'j':
	    if (++num < brdnum)
		break;
	case '0':
	case KEY_HOME:
	    num = 0;
	    break;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    if ((tmp = search_num(ch, brdnum)) >= 0)
		num = tmp;
	    brdlist_foot();
	    break;

	case '/':
            if (IN_HOTBOARD()) {
                vmsg("�����ݪO�Ҧ��U���䴩��������r�j�M");
            } else {
                getdata_buf(b_lines - 1, 0, "�п�J�ݪO��������r:",
                        keyword, sizeof(keyword), DOECHO);
                trim(keyword);
            }
            brdnum = -1;
	    break;

	case 'S':
	    if(IS_LISTING_FAV()){
		move(b_lines - 2, 0); clrtobot();
		outs("���s�ƧǬݪO "
			ANSI_COLOR(1;33) "(�`�N, �o�Ӱʧ@�|�мg��ӳ]�w)" ANSI_RESET " \n");
		tmp = vans("�ƧǤ覡 (1)���ӪO�W�Ƨ� (2)�������O�Ƨ� ==> [0]���� ");
		if( tmp == '1' )
		    fav_sort_by_name();
		else if( tmp == '2' )
		    fav_sort_by_class();
	    }
	    else
		pwcuToggleSortBoard();
	    brdnum = -1;
	    break;


	case 'v':
	case 'V':
	    assert(0<=num && num<nbrdsize);
	    ptr = &nbrd[num];
	    if(nbrd[num].bid < 0 || !HasBoardPerm(B_BH(ptr)))
		break;
	    if (ch == 'v') {
		ptr->myattr &= ~NBRD_UNREAD;
		brc_toggle_all_read(ptr->bid, 1);
	    } else {
		brc_toggle_all_read(ptr->bid, 0);
		ptr->myattr |= NBRD_UNREAD;
	    }
	    show_brdlist(head, 0, newflag);
	    break;
	case Ctrl('S'):
	    head = -1;
	    if ((tmp = search_local_board()) != -1) {
		num = tmp;
	    }
	    break;
	case 's':
	    {
		char bname[IDLEN+1];
                int tmpbid = currutmp->brc_id;
		move(0, 0);
		clrtoeol();
		// since now user can use Ctrl-S to get access
		// to folders, let's fallback to boards only here.
		CompleteBoard(ANSI_REVERSE
			"�i �j�M�����ݪO �j" ANSI_RESET
			"  (�Y�n���w�j�M�d�򬰥ثe�C��Ч�� Ctrl-S)\n"
			"�п�J�ݪO�W��(���ť���۰ʷj�M): ",
			bname);
		// force refresh
		head = -1;
		if (!*bname)
		    break;
		// try to search board
		if ((tmp = search_board(bname)) != -1)
		{
		    num = tmp;
		    break;
		}
		// try to enter board directly.
		if(enter_board(bname) >= 0)
		    Read();
		// restore my mode
                setutmpbid(tmpbid);
		setutmpmode(newflag ? READNEW : READBRD);
	    }
	    break;

	case KEY_RIGHT:
	case KEY_ENTER:
	case 'r':
	case 'l':
	    {
		if (IS_LISTING_FAV()) {
		    assert(nbrdsize>0);
		    if (get_fav_type(&nbrd[0]) == 0)
			break;
		    assert(0<=num && num<nbrdsize);
		    ptr = &nbrd[num];
		    if (ptr->myattr & NBRD_LINE)
			break;
		    if (ptr->myattr & NBRD_FOLDER){
			int t = num;
			num = 0;
			fav_folder_in(ptr->bid);
			choose_board(0);
			fav_folder_out();
			num = t;
			LIST_FAV(); // XXX press 'y' in fav makes yank_flag = LIST_BRD
			brdnum = -1;
			head = 9999;
			break;
		    }
		} else {
		    assert(0<=num && num<nbrdsize);
		    ptr = &nbrd[num];
		    if (ptr->myattr & NBRD_SYMBOLIC) {
			replace_link_by_target(ptr);
		    }
		}

		assert(0<=ptr->bid-1 && ptr->bid-1<MAX_BOARD);
		if (!(B_BH(ptr)->brdattr & BRD_GROUPBOARD)) {	/* �Dsub class */
		    if (HasBoardPerm(B_BH(ptr))) {
			brc_initial_board(B_BH(ptr)->brdname);

			if (newflag) {
			    setbdir(buf, currboard);
			    tmp = unread_position(buf, ptr);
			    head = tmp - t_lines / 2;
			    getkeep(buf, head > 1 ? head : 1, tmp + 1);
			}
			Read();
			check_newpost(ptr);
			head = -1;
			setutmpmode(newflag ? READNEW : READBRD);
		    }
		} else {	/* sub class */
		    move(12, 1);
		    bidtmp = class_bid;
		    currmodetmp = currmode;
		    tmp1 = num;
		    num = 0;
		    if (!(B_BH(ptr)->brdattr & BRD_TOP))
			class_bid = ptr->bid;
		    else
			class_bid = -1;	/* �����s�ե� */

		    if (!GROUPOP())	/* �p�G�٨S���p�ժ��v�� */
			set_menu_group_op(B_BH(ptr)->BM);

		    if (now < B_BH(ptr)->bupdate) {
			int mr = 0;

			setbfile(buf, B_BH(ptr)->brdname, fn_notes);
			mr = more(buf, NA);
			if (mr != -1 && mr != READ_NEXT)
			    pressanykey();
		    }
		    tmp = currutmp->brc_id;
		    setutmpbid(ptr->bid);
		    free(nbrd);
		    nbrd = NULL;
		    nbrdsize = 0;
	    	    if (IS_LISTING_FAV()) {
			LIST_BRD();
			choose_board(0);
			LIST_FAV();
    		    }
		    else
			choose_board(0);
		    currmode = currmodetmp;	/* ���}�O�O��N���v�������� */
		    num = tmp1;
		    class_bid = bidtmp;
		    setutmpbid(tmp);
		    brdnum = -1;
		}
	    }
	    break;
		///////////////////////////////////////////////////////
		// MyFav Functionality (Require PERM_BASIC)
		///////////////////////////////////////////////////////
	case 'y':
	    if (HasFavEditPerm() && !(IN_CLASS())) {
		if (get_current_fav() != NULL || !IS_LISTING_FAV()){
		    yank_flag ^= 1; /* FAV <=> BRD */
		}
		brdnum = -1;
	    }
	    break;
	case Ctrl('D'):
	    if (HasFavEditPerm()) {
		if (vans("�R���Ҧ��аO[N]?") == 'y'){
		    fav_remove_all_tagged_item();
		    brdnum = -1;
		}
	    }
	    break;
	case Ctrl('A'):
	    if (HasFavEditPerm()) {
		fav_add_all_tagged_item();
		brdnum = -1;
	    }
	    break;
	case Ctrl('T'):
	    if (HasFavEditPerm()) {
		fav_remove_all_tag();
		brdnum = -1;
	    }
	    break;
	case Ctrl('P'):
            if (paste_taged_brds(class_bid))
                brdnum = -1;
            break;

	case 'L':
	    if (IN_CLASS() &&
                (HasUserPerm(PERM_BOARD) ||
                 (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()))) {
		brdnum = -1;
		head = 9999;
		if (make_board_link_interactively(class_bid) < 0)
		    break;
	    }
	    else if (HasFavEditPerm() && IS_LISTING_FAV()) {
		if (fav_add_line() == NULL) {
		    vmsg("�s�W���ѡA���j�u/�`�̷R �ƶq�F�̤j�ȡC");
		    break;
		}
		/* done move if it's the first item. */
		assert(nbrdsize>0);
		if (get_fav_type(&nbrd[0]) != 0)
		    move_in_current_folder(brdnum, num);
		brdnum = -1;
		head = 9999;
	    }
	    break;

	case 'd': // why don't we enable 'd'?
	case 'z':
	case 'm':
	    if (HasFavEditPerm()) {
		assert(0<=num && num<nbrdsize);
		ptr = &nbrd[num];
		brdnum = -1;
		head = 9999;
		if (IS_LISTING_FAV()) {
		    if (ptr->myattr & NBRD_FAV) {
			if (vans("�A�T�w�R����? [N/y]") != 'y')
			    break;
			fav_remove_item(ptr->bid, get_fav_type(ptr));
			ptr->myattr &= ~NBRD_FAV;
		    }
		}
		else
		{
		    if (getboard(ptr->bid) != NULL) {
			fav_remove_item(ptr->bid, FAVT_BOARD);
			ptr->myattr &= ~NBRD_FAV;
		    }
		    else if (ch != 'd') // 'd' only deletes something.
		    {
			if (fav_add_board(ptr->bid) == NULL)
			    vmsg("�A���̷R�Ӧh�F�� �u���");
			else
			    ptr->myattr |= NBRD_FAV;
		    }
		}
	    }
	    break;
	case 'M':
	    if (HasFavEditPerm()){
		if (IN_FAVORITE() && IS_LISTING_FAV()){
		    imovefav(num);
		    brdnum = -1;
		    head = 9999;
		}
	    }
	    break;
	case 'g':
	    if (HasFavEditPerm() && IS_LISTING_FAV()) {
		fav_type_t  *ft;
		if (fav_stack_full()){
		    vmsg("�ؿ��w�F�̤j�h��!!");
		    break;
		}
		if ((ft = fav_add_folder()) == NULL) {
		    vmsg("�s�W���ѡA�ؿ�/�`�̷R �ƶq�F�̤j�ȡC");
		    break;
		}
		fav_set_folder_title(ft, "�s���ؿ�");
		/* don't move if it's the first item */
		assert(nbrdsize>0);
		if (get_fav_type(&nbrd[0]) != 0)
		    move_in_current_folder(brdnum, num);
		brdnum = -1;
    		head = 9999;
	    }
	    break;
	case 'T':
	    assert(0<=num && num<nbrdsize);
	    if (HasFavEditPerm() && nbrd[num].myattr & NBRD_FOLDER) {
		fav_type_t *ft = getfolder(nbrd[num].bid);
		strlcpy(buf, get_item_title(ft), sizeof(buf));
		getdata_buf(b_lines-1, 0, "�Эק�W��: ", buf, BTLEN+1, DOECHO);
		fav_set_folder_title(ft, buf);
		brdnum = -1;
	    }
	    break;
	case 'K':
	    if (HasFavEditPerm()) {
		char c, fname[80];
		brdnum = -1;
		if (get_current_fav() != get_fav_root()) {
		    vmsg("�Ш�ڪ��̷R�̤W�h���楻�\\��");
		    break;
		}

		c = vans("�п�� 2)�ƥ��ڪ��̷R 3)���^�̷R�ƥ� [Q]");
		if(!c)
		    break;
		if(vans("�T�w�� [y/N] ") != 'y')
		    break;
		switch(c){
		    case '2':
			fav_save();
			setuserfile(fname, FAV);
			sprintf(buf, "%s.bak", fname);
                        Copy(fname, buf);
			break;
		    case '3':
			setuserfile(fname, FAV);
			sprintf(buf, "%s.bak", fname);
			if (!dashf(buf)){
			    vmsg("�A�S���ƥ��A���̷R��");
			    break;
			}
                        Copy(buf, fname);
			fav_free();
			fav_load();
			break;
		}
	    }
	    break;

	case 'a':
	case 'i':
	    if(IN_FAVORITE() && HasFavEditPerm()){
		char         bname[IDLEN + 1];
		int          bid;
		move(0, 0);
		clrtoeol();
		/* use CompleteBoard or CompleteBoardAndGroup ? */
		CompleteBoard(ANSI_REVERSE "�i �W�[�ڪ��̷R �j" ANSI_RESET "\n"
			"�п�J���[�J���ݪO�W��(���ť���۰ʷj�M)�G",
			bname);

		if (bname[0] && (bid = getbnum(bname)) &&
			HasBoardPerm(getbcache(bid))) {
		    fav_type_t * ptr = getboard(bid);
		    if (ptr != NULL) { // already in fav list
			// move curser to item
			int i;
			for (i = 0; i < nbrdsize; ++i) {
			    if (bid == nbrd[i].bid) {
				num = i;
				break;
			    }
			}
			if (i == nbrdsize) {
			    assert(keyword[0]);
			    vmsg("�w�g�b�ڪ��̷R�̤F, ��������r�N��ݨ��o");
			    keyword[0] = '\0';
			    brdnum = -1;
			}
		    } else {
			ptr = fav_add_board(bid);

			if (ptr == NULL)
			    vmsg("�A���̷R�Ӧh�F�� �u���");
			else {
			    ptr->attr |= NBRD_FAV;

			    if (ch == 'i' && get_data_number(get_current_fav()) > 1)
				move_in_current_folder(brdnum, num);
			    else
				num = brdnum;
			}
		    }
		}
	    }
	    brdnum = -1;
	    head = 9999;
	    break;

	case 'w':
	    /* allowing save BRC/fav once per 10 minutes */
	    if (now - last_save_fav_and_brc > 10 * 60) {
		fav_save();
		brc_finalize();

		last_save_fav_and_brc = now;
		vmsg("�w�x�s�ݪO�\\Ū�O��");
	    } else
		vmsgf("���j�ɶ��Ӫ�A���x�s�ݪO�\\Ū�O�� [�е� %d ���A��]",
			(int)(600 - (now-last_save_fav_and_brc)));
	    break;

		///////////////////////////////////////////////////////
		// Administrator Only
		///////////////////////////////////////////////////////

	case 'F':
	case 'f':
	    if (HasUserPerm(PERM_SYSOP)) {
		getbcache(class_bid)->firstchild[HasUserFlag(UF_BRDSORT)
		    ? BRD_GROUP_LL_TYPE_CLASS : BRD_GROUP_LL_TYPE_NAME] = 0;
		brdnum = -1;
	    }
	    break;
	case 'D':
	    if (HasUserPerm(PERM_BOARD) ||
		    (HasUserPerm(PERM_SYSSUPERSUBOP) &&	GROUPOP())) {
		assert(0<=num && num<nbrdsize);
		ptr = &nbrd[num];
		if (ptr->myattr & NBRD_SYMBOLIC) {
		    if (vans("�T�w�R���s���H[N/y]") == 'y')
			delete_board_link(getbcache(ptr->bid), ptr->bid);
		}
		brdnum = -1;
	    }
	    break;
	case 'E':
	    if (HasUserPerm(PERM_BOARD) || GROUPOP()) {
		assert(0<=num && num<nbrdsize);
		ptr = &nbrd[num];
		move(1, 1);
		clrtobot();
		m_mod_board(B_BH(ptr)->brdname);
		brdnum = -1;
	    }
	    break;
	case 'R':
	    if (HasUserPerm(PERM_BOARD) || GROUPOP()) {
		m_newbrd(class_bid, 1);
		brdnum = -1;
	    }
	    break;
	case 'B':
	    if (HasUserPerm(PERM_BOARD) || GROUPOP()) {
		m_newbrd(class_bid, 0);
		brdnum = -1;
	    }
	    break;
	case 'W':
	    if (IN_SUBCLASS() &&
		(HasUserPerm(PERM_BOARD) || GROUPOP())) {
		setbpath(buf, getbcache(class_bid)->brdname);
		Mkdir(buf);	/* Ptt:�}�s�եؿ� */
		b_note_edit_bname(class_bid);
		brdnum = -1;
	    }
	    break;

	}
    } while (ch != 'q' && !ZA_Waiting());
    free(nbrd);
    nbrd = NULL;
    nbrdsize = 0;
    --choose_board_depth;
}

int
Class(void)
{
    init_brdbuf();
    class_bid = 1;
    LIST_BRD();
    choose_board(0);
    return 0;
}

int
TopBoards(void)
{
    init_brdbuf();
    class_bid = -1;
    LIST_BRD();
    choose_board(0);
    return 0;
}

int
Favorite(void)
{
    init_brdbuf();
    class_bid = 0;
    LIST_FAV();
    choose_board(0);
    return 0;
}

int
New(void)
{
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;

    class_bid = 0;
    init_brdbuf();
    choose_board(1);
    currutmp->mode = mode0;
    currstat = stat0;
    return 0;
}
