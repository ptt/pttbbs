#include "bbs.h"
#include "psb.h"

/* personal board state
 * 相對於看板的 attr (BRD_* in ../include/pttstruct.h),
 * 這些是用在 user interface 的 flag */
#define NBRD_FAV    	 1
#define NBRD_BOARD	 2
#define NBRD_LINE   	 4
#define NBRD_FOLDER	 8
#define NBRD_TAG	16
#define NBRD_UNREAD     32
#define NBRD_SYMBOLIC   64

static int
board_title_has_key(const boardheader_t *bptr, const char *key)
{
    return mbs_strcasestr(TEMP_BRD_TITLE(bptr), key) != NULL;
}
#define TITLE_MATCH(bptr, key)	((key)[0] && !board_title_has_key((bptr), (key)))

#define B_TOTAL(bptr)        (SHM->total[(bptr)->bid - 1])
#define B_LASTPOSTTIME(bptr) (SHM->lastposttime[(bptr)->bid - 1])
#define B_BH(bptr)           (&bcache[(bptr)->bid - 1])

#define HasFavEditPerm() HasUserPerm(PERM_BASIC)

typedef struct {
    int             bid;
    unsigned char   myattr;
} __attribute__ ((packed)) boardstat_t;

/**
 * class_bid 的意義
 *   class_bid < 0   熱門看板
 *   class_bid = 0   我的最愛
 *   class_bid = 1   分類看板
 *   class_bid > 1   其他目錄
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

// 程式中有特別用途所以不得自行 post / 修改的看板
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

    STRLCPY(bname, bh->brdname);
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

    getdata(b_lines - 1, 0, "請輸入新次序:", buf, sizeof(buf), DOECHO);
    new = atoi(buf) - 1;
    if (new < 0 || brdnum <= new){
	vmsg("輸入範圍有誤!");
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

static int
HasBoardPermNormally(boardheader_t *bptr)
{
    register int    level, brdattr;

    level = bptr->level;
    brdattr = bptr->brdattr;

    // allow POLICE to enter BM boards
    if ((level & PERM_BM) &&
	(HasUserPerm(PERM_POLICE) || HasUserPerm(PERM_POLICE_MAN)))
	return 1;

    /* 板主 */
    if( is_BM_cache(bptr - bcache + 1) ) /* XXXbid */
	return 1;

    /* 祕密看板：核對首席板主的好友名單 */
    if (brdattr & BRD_HIDE) {	/* 隱藏 */
	if (!is_hidden_board_friend((int)(bptr - bcache) + 1, currutmp->uid)) {
	    if (brdattr & BRD_POSTMASK)
		return 0;
	    else
		return 2;  // What's this?
	} else
	    return 1;
    }

    // TODO Change this to a query on demand.
    /* 十八禁看板 */
    if( (brdattr & BRD_OVER18) && !cuser.over_18 )
	return 0;

    /* 限制閱\讀權限 */
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
    vs_hdr("自訂注意事項");

    setbfile(buf, currboard, FN_POST_NOTE);
    move(b_lines-2, 0); clrtobot();

    if (more(buf, NA) == -1)
	more("etc/" FN_POST_NOTE, NA);

    getdata(b_lines - 2, 0, "自訂發文注意事項: (y)編輯/(d)刪除/(n)不變? [y/d/N]:",
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
   int i, modified = 0;
   char filepath[PATHLEN], genbuf[60];
   char posttype_f, posttype[sizeof(bp->posttype)]="";

   assert(0<=currbid-1 && currbid-1<MAX_BOARD);
   bp = getbcache(currbid);
   posttype_f = bp->posttype_f;
   memcpy(posttype, bp->posttype, sizeof(bp->posttype));

   vs_hdr("設定文章類別");

   do {
       move(2, 0);
       clrtobot();
       for (i = 0; i < 8; i++) {
           brd_get_posttype_slot(posttype, i, genbuf, sizeof(genbuf));
           if (!genbuf[0])
               break;
           prints(" %d. %s %s\n", i + 1, genbuf,
                  posttype_f & (1 << i) ? "(有範本)": "");
       }
       if (!getdata(15, 0,
                    "請輸入要設定的項目編號，或 c 設定總數,或 ENTER 離開:",
                    genbuf, 3, LCECHO))
           break;

       if (genbuf[0] == 'c') {
           getdata(15, 0, "要保留幾項類別呢？ [0-8或 ENTER 離開]: ", genbuf, 3,
                   NUMECHO);
           if (!isdigit(genbuf[0]))
               continue;
           i = atoi(genbuf);
           if (i < 0 || i > 8)
               continue;
           brd_set_posttype_count(posttype, sizeof(posttype), i);
           continue;
       }

       i = atoi(genbuf) - 1;
       if (i < 0 || i >= 8)
           continue;
       char mb_type[SZ_COLS(5)];
       brd_get_posttype_slot(posttype, i, mb_type, sizeof(mb_type));
       if (getdata_str(16, 0, "類別名稱: ", mb_type, 5, DOECHO, mb_type))
           brd_set_posttype_slot(posttype, sizeof(posttype), i, mb_type);
       getdata(17, 0, "要使用範本嗎? [y/n/K(不改變)]: ", genbuf, 2, LCECHO);
       if (genbuf[0] == 'y')
           posttype_f |= 1 << i;
       else if (genbuf[0] == 'n') {
           posttype_f &= ~(1 << i);
           continue;
       }
       getdata(18, 0, "要編輯範本檔案嗎? [y/N]: ", genbuf, 2, LCECHO);
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
       /* 這邊應該要防race condition */
       STRLCPY(bp->posttype, posttype);
       modified = 1;
   }
   if (modified) {
       substitute_record(FN_BOARD, bp, sizeof(boardheader_t), currbid);
       vmsg("資料已更新。");
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
typedef struct {
    boardheader_t *bp;
    PSB_CTX *psb;
    int touched;
    bool readonly;
    bool can_edit;
} bconfig_ctx_t;

typedef struct BConfigItem {
    const char *desc;
    const char *on_str;
    const char *off_str;
    int flag;
    int perm;
    const char *(*getter)(const bconfig_ctx_t *cx, const struct BConfigItem *item,
                          char *buf, size_t sz);
    cmd_cb_t setter;
} BConfigItem;

static const char *
bcfg_get_flag(const bconfig_ctx_t *cx, const BConfigItem *item, char *buf, size_t sz)
{
    strlcpy(buf, (cx->bp->brdattr & item->flag) ? item->on_str : item->off_str, sz);
    return buf;
}

static const char *
bcfg_get_title(const bconfig_ctx_t *cx, const BConfigItem *item GCC_UNUSED,
               char *buf, size_t sz)
{
    strlcpy(buf, TEMP_BRD_TITLE_DESC(cx->bp), sz);
    return buf;
}

#ifndef OLDRECOMMEND
static const char *
bcfg_get_noboo(const bconfig_ctx_t *cx, const BConfigItem *item GCC_UNUSED,
               char *buf, size_t sz)
{
    strlcpy(buf, ((cx->bp->brdattr & BRD_NORECOMMEND) || (cx->bp->brdattr & BRD_NOBOO))
                 ? ANSI_COLOR(1) "不開放" ANSI_RESET : "開放", sz);
    return buf;
}
#endif

static const char *
bcfg_get_fastrecmd(const bconfig_ctx_t *cx, const BConfigItem *item GCC_UNUSED,
                   char *buf, size_t sz)
{
    const boardheader_t *bp = cx->bp;
    int d = 0;
    if (bp->brdattr & BRD_NORECOMMEND)
        d = -1;
    else if ((bp->brdattr & BRD_NOFASTRECMD) && bp->fastrecommend_pause > 0)
        d = bp->fastrecommend_pause;
    if (d > 0)
        snprintf(buf, sz, ANSI_COLOR(1) "限制 (間隔 %d 秒)" ANSI_RESET, d);
    else if (d < 0)
        strlcpy(buf, ANSI_COLOR(1) "限制 (已禁推)" ANSI_RESET, sz);
    else
        strlcpy(buf, "開放", sz);
    return buf;
}

static const char *
bcfg_get_edit_entry(const bconfig_ctx_t *cx, const BConfigItem *item GCC_UNUSED,
                    char *buf, size_t sz)
{
    strlcpy(buf, cx->readonly ? "[唯讀檢視]" : "[進入編輯]", sz);
    return buf;
}

static const char *
bcfg_get_enter_entry(const bconfig_ctx_t *cx, const BConfigItem *item GCC_UNUSED,
                     char *buf, size_t sz)
{
    strlcpy(buf, cx->readonly ? "[唯讀檢視]" : "[進入管理]", sz);
    return buf;
}

#define PERM_SYSGROUPOP (PERM_SYSOP | PERM_SYSSUPERSUBOP)
#define PERM_POLICEOP   (PERM_SYSOP | PERM_POLICE | PERM_SYSSUPERSUBOP)
#ifdef ALLOW_BM_SET_NOSELFDELPOST
# define PERM_SELFDEL   (PERM_BM | PERM_SYSGROUPOP)
#else
# define PERM_SELFDEL   PERM_SYSGROUPOP
#endif

static const char *
bcfg_getter(const bconfig_ctx_t *cx, const BConfigItem *item, char *buf, size_t sz)
{
    (item->getter ? item->getter : bcfg_get_flag)(cx, item, buf, sz);
    if (!cx->readonly && item->perm != PERM_BM) {
        const char *note = NULL;
        if (item->perm == PERM_SYSOP)
            note = " (限站長)";
        else if (item->perm == PERM_SYSGROUPOP)
            note = " (限群組長/站長)";
        else if (item->perm == (int)PERM_POLICEOP)
            note = " (限警察/群組長/站長)";
        if (note)
            strlcat(buf, note, sz);
    }
    return buf;
}

static int
bcfg_set_title(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    boardheader_t *bp = cx->bp;
    char genbuf[SZ_COLS(BTLEN + 1)];
    ctx->reload = true;
    move(b_lines, 0); clrtoeol();
    outs("請輸入看板新中文敘述: ");
    vgetstr(genbuf, BTLEN - 16, 0, TEMP_BRD_TITLE_DESC(bp));
    if (!genbuf[0] || strcmp(genbuf, TEMP_BRD_TITLE_DESC(bp)) == 0)
        return 0;
    cx->touched = 1;
    strip_control_sequence(genbuf, genbuf);
    brd_set_title_desc(bp, genbuf);
    assert(0 <= currbid - 1 && currbid - 1 < MAX_BOARD);
    substitute_record(FN_BOARD, bp, sizeof(boardheader_t), currbid);
    log_usies("SetBoard", currboard);
    return 0;
}

static int
bcfg_set_hide(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    boardheader_t *bp = cx->bp;
    char ans[2];
    ctx->reload = true;
    move(b_lines - 2, 0); clrtobot();
    if (getdata(b_lines - 1, 0, (bp->brdattr & BRD_HIDE) ?
            ANSI_COLOR(1;32) " +++ 確定要解除看板隱形嗎?" ANSI_RESET " [y/N]: " :
            ANSI_COLOR(1;31) " --- 確定要隱形看板嗎?" ANSI_RESET " [y/N]: ",
            ans, sizeof(ans), LCECHO) < 1 || ans[0] != 'y')
        return 0;
    if (bp->brdattr & BRD_HIDE) {
        bp->brdattr &= ~BRD_HIDE;
        bp->brdattr &= ~BRD_POSTMASK;
        hbflreload(currbid);
    } else {
        bp->brdattr |= BRD_HIDE;
        bp->brdattr |= BRD_POSTMASK;
    }
    bp->perm_reload = now;
    cx->touched = 1;
    vmsg((bp->brdattr & BRD_HIDE) ? " 注意: 看板已隱形" : " 注意: 看板已解除隱形");
    return 0;
}

static int
bcfg_set_bmcount(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_BMCOUNT;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

static int
bcfg_set_restrictedpost(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_RESTRICTEDPOST;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

static int
bcfg_set_noreply(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_NOREPLY;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

static int
bcfg_set_noselfdelpost(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_NOSELFDELPOST;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

static int
bcfg_set_norecommend(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_NORECOMMEND;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

#ifndef OLDRECOMMEND
static int
bcfg_set_noboo(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    boardheader_t *bp = cx->bp;
    if (bp->brdattr & BRD_NORECOMMEND)
        bp->brdattr |= BRD_NOBOO;
    bp->brdattr ^= BRD_NOBOO;
    cx->touched = 1;
    ctx->reload = true;
    if (!(bp->brdattr & BRD_NOBOO))
        bp->brdattr &= ~BRD_NORECOMMEND;
    return 0;
}
#endif

static int
bcfg_set_fastrecmd(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    boardheader_t *bp = cx->bp;
    bp->brdattr &= ~BRD_NORECOMMEND;
    bp->brdattr ^= BRD_NOFASTRECMD;
    cx->touched = 1;
    ctx->reload = true;
    if (bp->brdattr & BRD_NOFASTRECMD) {
        char buf[8] = "";
        if (bp->fastrecommend_pause > 0)
            sprintf(buf, "%d", bp->fastrecommend_pause);
        getdata_str(b_lines - 1, 0,
                    "請輸入連推時間限制(單位: 秒) [5~240]: ",
                    buf, 4, NUMECHO, buf);
        if (buf[0] >= '0' && buf[0] <= '9')
            bp->fastrecommend_pause = atoi(buf);
        if (bp->fastrecommend_pause < 5 || bp->fastrecommend_pause > 240) {
            if (buf[0])
                vmsg("輸入時間無效，請使用 5~240 之間的數字。");
            bp->fastrecommend_pause = 0;
            bp->brdattr &= ~BRD_NOFASTRECMD;
        }
    }
    return 0;
}

static int
bcfg_set_iplogrecmd(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    boardheader_t *bp = cx->bp;
    char ans[2];
    ctx->reload = true;
    move(b_lines - 2, 0); clrtobot();
    if (getdata(b_lines - 1, 0, (bp->brdattr & BRD_IPLOGRECMD) ?
            ANSI_COLOR(1;32) " --- 確定要停止記錄推文 IP 嗎?" ANSI_RESET " [y/N]: " :
            ANSI_COLOR(1;31) " +++ 確定要記錄推文 IP 嗎?" ANSI_RESET " [y/N]: ",
            ans, sizeof(ans), LCECHO) < 1 || ans[0] != 'y')
        return 0;
    bp->brdattr ^= BRD_IPLOGRECMD;
    cx->touched = 1;
    vmsg((bp->brdattr & BRD_IPLOGRECMD) ? " 注意: 開始記錄推文IP" : " 注意: 已停止記錄推文IP");
    return 0;
}

static int
bcfg_set_alignedcmt(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_ALIGNEDCMT;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

static int
bcfg_set_mask_content(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_BM_MASK_CONTENT;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}

#ifdef USE_AUTOCPLOG
static int
bcfg_set_cplog(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    cx->bp->brdattr ^= BRD_CPLOG;
    cx->touched = 1;
    ctx->reload = true;
    return 0;
}
#endif

#ifdef USE_COOLDOWN
static int
bcfg_set_cooldown(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    char ans[50];
    ctx->reload = true;
    getdata(b_lines - 1, 0, "請輸入理由(空白放棄設定):", ans, sizeof(ans), DOECHO);
    if (!*ans) {
        vmsg("未輸入理由，放棄設定。");
        return 0;
    }
    cx->bp->brdattr ^= BRD_COOLDOWN;
    post_policelog(cx->bp->brdname, NULL, "冷靜", ans, (cx->bp->brdattr & BRD_COOLDOWN));
    cx->touched = 1;
    return 0;
}
#endif

static int
bcfg_set_over18(cmd_ctx_t *ctx)
{
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    ctx->reload = true;
    if (!cuser.over_18)
        vmsg("板主本身未滿 18 歲。");
    else {
        cx->bp->brdattr ^= BRD_OVER18;
        cx->touched = 1;
    }
    return 0;
}

static int
bcfg_set_banned(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    edit_banned_list_for_board(currboard);
    clear();
    return 0;
}

static int
bcfg_set_visible(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    friend_edit(BOARD_VISABLE);
    assert(0 <= currbid - 1 && currbid - 1 < MAX_BOARD);
    hbflreload(currbid);
    clear();
    return 0;
}

static int
bcfg_set_canvote(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    friend_edit(FRIEND_CANVOTE);
    clear();
    return 0;
}

static int
bcfg_set_vote_maintain(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    b_vote_maintain();
    clear();
    return 0;
}

static int
bcfg_set_posttype(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    b_posttype();
    clear();
    return 0;
}

static int
bcfg_set_postnote(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    b_post_note();
    clear();
    return 0;
}

static int
bcfg_set_notes_edit(cmd_ctx_t *ctx)
{
    ctx->reload = true;
    clear();
    b_notes_edit();
    clear();
    return 0;
}

static const BConfigItem bconfig_items[] = {
    { "中文敘述", NULL, NULL, 0, PERM_BM, bcfg_get_title, bcfg_set_title },
    { "公開狀態 (是否隱形)", ANSI_COLOR(1;31) "隱形" ANSI_RESET, "公開", BRD_HIDE, PERM_BM, NULL, bcfg_set_hide },
    { "隱板時進入十大排行榜", ANSI_COLOR(1) "可以" ANSI_RESET, "不可", BRD_BMCOUNT, PERM_BM, NULL, bcfg_set_bmcount },
    { "非看板會員發文", ANSI_COLOR(1) "不開放" ANSI_RESET, "開放", BRD_RESTRICTEDPOST, PERM_SYSOP, NULL, bcfg_set_restrictedpost },
    { "回應文章", ANSI_COLOR(1) "不開放" ANSI_RESET, "開放", BRD_NOREPLY, PERM_SYSGROUPOP, NULL, bcfg_set_noreply },
    { "自刪文章", ANSI_COLOR(1) "不開放" ANSI_RESET, "開放", BRD_NOSELFDELPOST, PERM_SELFDEL, NULL, bcfg_set_noselfdelpost },
    { "推薦文章 (推文)", ANSI_COLOR(1) "不開放" ANSI_RESET, "開放", BRD_NORECOMMEND, PERM_BM, NULL, bcfg_set_norecommend },
#ifndef OLDRECOMMEND
    { "噓文", NULL, NULL, 0, PERM_BM, bcfg_get_noboo, bcfg_set_noboo },
#endif
    { "快速連推文章", NULL, NULL, 0, PERM_BM, bcfg_get_fastrecmd, bcfg_set_fastrecmd },
    { "推文時記錄來源 IP", ANSI_COLOR(1) "自動記錄" ANSI_RESET, "不會記錄", BRD_IPLOGRECMD, PERM_BM, NULL, bcfg_set_iplogrecmd },
    { "推文開頭自動對齊", ANSI_COLOR(1) "對齊" ANSI_RESET, "不用對齊", BRD_ALIGNEDCMT, PERM_BM, NULL, bcfg_set_alignedcmt },
    { "板主刪除部份違規文字", ANSI_COLOR(1) "可" ANSI_RESET, "無法", BRD_BM_MASK_CONTENT, PERM_SYSGROUPOP, NULL, bcfg_set_mask_content },
#ifdef USE_AUTOCPLOG
    { "轉錄文章自動記錄", ANSI_COLOR(1) "會 (需發文權限)" ANSI_RESET, "不會", BRD_CPLOG, PERM_BM, NULL, bcfg_set_cplog },
#endif
#ifdef USE_COOLDOWN
    { "冷靜模式", ANSI_COLOR(1;31) "已設為冷靜模式" ANSI_RESET, "未設定", BRD_COOLDOWN, PERM_POLICEOP, NULL, bcfg_set_cooldown },
#endif
    { "未滿十八歲進入", ANSI_COLOR(1) "禁止" ANSI_RESET, "允許\ ", BRD_OVER18, PERM_BM, NULL, bcfg_set_over18 },
    { "[名單] 設定水桶名單", NULL, NULL, 0, PERM_BM, bcfg_get_edit_entry, bcfg_set_banned },
    { "[名單] 可見會員名單", NULL, NULL, 0, PERM_BM, bcfg_get_edit_entry, bcfg_set_visible },
    { "[名單] 投票限制名單", NULL, NULL, 0, PERM_BM, bcfg_get_edit_entry, bcfg_set_canvote },
    { "[管理] 舉辦與管理投票", NULL, NULL, 0, PERM_BM, bcfg_get_enter_entry, bcfg_set_vote_maintain },
    { "[管理] 文章類別設定", NULL, NULL, 0, PERM_BM, bcfg_get_edit_entry, bcfg_set_posttype },
    { "[管理] 發文注意事項", NULL, NULL, 0, PERM_BM, bcfg_get_edit_entry, bcfg_set_postnote },
    { "[管理] 編輯進板畫面", NULL, NULL, 0, PERM_BM, bcfg_get_edit_entry, bcfg_set_notes_edit },
};

static int
bconfig_col_measurer(int i, int col, PSB_CTX *ctx) {
    if (i < 0)
        return 0;
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->cmd.priv;
    const BConfigItem *item = &bconfig_items[i];
    if (col == 0)
        return stream_width(item->desc);
    if (col == 1) {
        char val[64];
        bcfg_getter(cx, item, val, sizeof(val));
        return stream_width(val);
    }
    return 0;
}

static int
bconfig_header(PSB_CTX *ctx) {
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->cmd.priv;
    boardheader_t *bp = cx->bp;
    const char *mode;

    if (cx->readonly)
        mode = cx->can_edit ? "[唯讀模式 (按 Ctrl-P 編輯)]" : "[唯讀檢視]";
    else
        mode = "[編輯模式 (按 Ctrl-P 唯讀)]";

    vs_draw_hdr2("看板設定與管理", mode);

    const char *bm = does_board_have_public_bm(bp) ? TEMP_BRD_BM(bp) : "(無)";
    prints("看板名稱: " ANSI_COLOR(1;33) "%s" ANSI_RESET
           " 板主名單: " ANSI_COLOR(1;36) "%s" ANSI_RESET "\n",
           bp->brdname, bm);
    return 0;
}

static int
bconfig_renderer(int i, PSB_CTX *ctx) {
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->cmd.priv;
    int w0 = ctx->col_widths[0];
    int w1 = ctx->col_widths[1];
    const BConfigItem *item = &bconfig_items[i];
    char val[SZ_COLS(64)];
    bcfg_getter(cx, item, val, sizeof(val));
    if (w1 > 0) {
        int off = stream_col_offset(w1, val);
        if (off >= 0) {
            val[off] = '\0';
            mbs_safe_trim(val);
        }
    }
    int pad0 = w0 - stream_width(item->desc);
    prints("  " ANSI_COLOR(1;36) "%2d" ANSI_RESET ".   %s%*s  %s\n",
           i + 1, item->desc, pad0 > 0 ? pad0 : 0, "", val);
    return 0;
}

static int
bconfig_cmd_select(cmd_ctx_t *ctx) {
    assert(ctx->curr >= 0 && ctx->curr < (int)ARRAY_SIZE(bconfig_items));
    const BConfigItem *item = &bconfig_items[ctx->curr];
    if (!psb_check_perm(item->perm)) {
        vmsg("您沒有修改此項設定的權限");
        return 0;
    }
    return item->setter(ctx);
}

static const cmd_t bconfig_edit_cmds[];
static const cmd_t bconfig_readonly_cmds[];

static int
bconfig_cmd_toggle_edit(cmd_ctx_t *ctx) {
    bconfig_ctx_t *cx = (bconfig_ctx_t *)ctx->priv;
    PSB_CTX *psb = cx->psb;
    cx->readonly = !cx->readonly;
    if (cx->readonly) {
        psb->cmds = bconfig_readonly_cmds;
        ctx->caption = " 看板設定(唯讀) ";
    } else {
        psb->cmds = bconfig_edit_cmds;
        ctx->caption = " 看板設定(編輯) ";
    }
    psb->cached_cols = 0;
    ctx->redraw = true;
    return 0;
}

static const cmd_t bconfig_readonly_cmds[] = {
    { Ctrl('P'), "開啟編輯", "開啟看板設定修改與管理模式", bconfig_cmd_toggle_edit, PERM_BM | PERM_POLICEOP, CMD_PRIO_MAX },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t bconfig_edit_cmds[] = {
    { KEY_ENTER, "切換/執行", "切換選取的設定值或進入管理功\能", bconfig_cmd_select, 0, CMD_PRIO_MAX, true },
    { KEY_RIGHT, NULL, NULL, bconfig_cmd_select, 0, CMD_PRIO_NONE, true },
    { ' ', NULL, NULL, bconfig_cmd_select, 0, CMD_PRIO_NONE, true },
    { Ctrl('P'), NULL, "切換回唯讀檢視模式", bconfig_cmd_toggle_edit, 0, CMD_PRIO_NONE },
    { 'w', "水桶名單", "設定看板水桶名單", bcfg_set_banned, PERM_BM, CMD_PRIO_HIGH, true },
    { 'v', "可見名單", "編輯看板可見會員名單", bcfg_set_visible, PERM_BM, CMD_PRIO_NORM, true },
    { 'm', "舉辦投票", "舉辦或管理看板投票", bcfg_set_vote_maintain, PERM_BM, CMD_PRIO_NORM, true },
    { 'c', "文章類別", "設定看板文章類別", bcfg_set_posttype, PERM_BM, CMD_PRIO_NORM, true },
    { 'o', NULL, "編輯投票限制名單", bcfg_set_canvote, PERM_BM, CMD_PRIO_NONE, true },
    { 'b', NULL, "修改看板中文敘述", bcfg_set_title, PERM_BM, CMD_PRIO_NONE, true },
    { 'g', NULL, "切換隱板是否進入十大排行榜", bcfg_set_bmcount, PERM_BM, CMD_PRIO_NONE, true },
    { 'e', NULL, "切換非看板會員發文限制", bcfg_set_restrictedpost, PERM_SYSOP, CMD_PRIO_NONE, true },
    { 'y', NULL, "切換是否開放回應文章", bcfg_set_noreply, PERM_SYSGROUPOP, CMD_PRIO_NONE, true },
    { 'd', NULL, "切換是否開放自刪文章", bcfg_set_noselfdelpost, PERM_SELFDEL, CMD_PRIO_NONE, true },
    { 'r', NULL, "切換是否開放推文", bcfg_set_norecommend, PERM_BM, CMD_PRIO_NONE, true },
#ifndef OLDRECOMMEND
    { 's', NULL, "切換是否開放噓文", bcfg_set_noboo, PERM_BM, CMD_PRIO_NONE, true },
#endif
    { 'f', NULL, "設定快速連推間隔限制", bcfg_set_fastrecmd, PERM_BM, CMD_PRIO_NONE, true },
    { 'i', NULL, "切換推文是否記錄來源 IP", bcfg_set_iplogrecmd, PERM_BM, CMD_PRIO_NONE, true },
    { 'a', NULL, "切換推文開頭是否自動對齊", bcfg_set_alignedcmt, PERM_BM, CMD_PRIO_NONE, true },
#ifdef USE_AUTOCPLOG
    { 'x', NULL, "切換轉錄文章是否自動記錄", bcfg_set_cplog, PERM_BM, CMD_PRIO_NONE, true },
#endif
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

int
b_config(void)
{
    boardheader_t *bp = getbcache(currbid);
    bool can_edit = psb_check_perm(PERM_BM | PERM_POLICEOP);

    bconfig_ctx_t cx = {
        .bp = bp,
        .touched = 0,
        .readonly = true,
        .can_edit = can_edit,
    };

    PSB_CTX ctx = {
        .cmd = {
            .curr = 0,
            .total = ARRAY_SIZE(bconfig_items),
            .priv = &cx,
            .caption = can_edit ? " 看板設定 " : " 看板資訊 ",
        },
        .header_lines = 2,
        .footer_lines = 1,
        .allow_pbs_version_message = 0,
        .cols = 2,
        .col_paddings = 10,
        .col_measurer = bconfig_col_measurer,
        .header = bconfig_header,
        .renderer = bconfig_renderer,
        .cmds = bconfig_readonly_cmds,
    };
    cx.psb = &ctx;

    psb_main(&ctx);

    if (cx.touched) {
        assert(0 <= currbid - 1 && currbid - 1 < MAX_BOARD);
        substitute_record(FN_BOARD, bp, sizeof(boardheader_t), currbid);
        log_usies("SetBoard", bp->brdname);
        vmsg("已儲存新設定");
    } else if (!cx.readonly) {
        vmsg("未改變任何設定");
    }

    return FULLUPDATE;
}

int
b_quick_acl(int ent GCC_UNUSED, fileheader_t *fhdr GCC_UNUSED,
            const char *direct GCC_UNUSED)
{
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
        vmsg("該使用者帳號不存在");
        return FULLUPDATE;
    }

    edit_user_acl_for_board(uid, bp->brdname);

    return FULLUPDATE;
}

static int
check_newpost(boardstat_t * ptr)
{				/* Ptt 改 */
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

    // Note: 以前這裡有 if(ftime > now [+10]) 就重設 ftime 的 code;
    // 但實際的狀況是那樣產生的 lastposttime 會與看板內的文章不同，
    // 造成未讀符號永遠不會消失。 又，在這邊的 now 其實是非同步的，
    // 所以若有人在進入這裡面卡很久(ex, system overload)那就會非常容易
    // 變成 ftime > now.
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

	    // XXX TODO 很多人死在這裡，但我不確定他們是 fav 臨時壞掉還是該永久修正
	    // workaround 應該是 nfav = fav ? get_data_number(fav) : 0;
            if (!get_current_fav()) {
                vmsgf("我的最愛系統錯誤，請到" BN_BUGREPORT "報告您之前進行了哪些動作，謝謝");
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
				if (board_title_has_key(bptr, key))
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
		// 有些人 某些 bid < 0 Orzz // ptt2 local modification
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
        // 預留兩個以免大量開板時掛調
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
			vmsg("連結已損毀，請至 SYSOP 回報此問題。");
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
    namecomplete2(&namelist, ANSI_REVERSE "【 搜尋所在位置看板 】"
	    ANSI_RESET "\n請輸入看板名稱(按空白鍵自動搜尋): ", genbuf);
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

static const cmd_t myfav_cmds[];
static const cmd_t board_fav_cmds[];
static const cmd_t board_admin_cmds[];
static const cmd_t boardlist_cmds[];

static const char *
brdlist_caption(void)
{
    if (IN_CLASSROOT())
        return " 分類看板 ";
    if (IN_FAVORITE())
        return " 我的最愛 ";
    return " 看板列表 ";
}

static void
brdlist_foot(void)
{
    const cmd_layer_t layers[] = {
        { IS_LISTING_FAV() ? myfav_cmds : board_fav_cmds, NULL },
        { board_admin_cmds, NULL },
        { boardlist_cmds,   NULL },
        { bbs_global_cmds,  NULL },
        { NULL, NULL }
    };
    vs_cmd_bar(IN_CLASSROOT() ? VS_FOOTER : (VS_SUB_HEADER | VS_FOOTER),
               brdlist_caption(), layers);
}


static const char *
make_class_color(char *name)
{
    /* 0;34 is too dark */
    uint32_t index = (((uint32_t)name[0] + name[1] + name[2] + name[3]) & 0x07);
    const char *colorset[8] = {"", ANSI_COLOR(32),
	ANSI_COLOR(33), ANSI_COLOR(36), ANSI_COLOR(1;34),
	ANSI_COLOR(1), ANSI_COLOR(1;32), ANSI_COLOR(1;33)};

    return colorset[index];
}

#define HILIGHT_COLOR	ANSI_COLOR(1;36)
#define HILIGHT_COLOR2	ANSI_COLOR(36)

typedef struct {
    int  *num;
    int  *newflag;
    char *keyword;
    size_t keyword_sz;
    cmd_layer_t *layers;
} boardlist_ctx_t;

static int
brdlist_header(PSB_CTX *ctx)
{
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->cmd.priv;
    int newflag = *cx->newflag;
    if (unlikely(IN_CLASSROOT())) {
	currstat = CLASS;
	showtitle("分類看板", BBSNAME);
	move(1, 0);
	// TODO move ascii art to adbanner?
	outs(
	    "                                                              "
	    "◣  ╭—" ANSI_COLOR(33) "●\n"
	    "                                                    ╬—  " ANSI_RESET " "
	    "◢█" ANSI_COLOR(47) "⊙" ANSI_COLOR(40) "██◣╤\n"
	    "  " ANSI_COLOR(44) "   ︿︿︿︿︿︿︿︿                               "
	    ANSI_COLOR(33) "║" ANSI_RESET ANSI_COLOR(44) " ◣◢███▼▼▼║ " ANSI_RESET "\n"
	    "  " ANSI_COLOR(44) "                                                  "
	    ANSI_COLOR(33) "  " ANSI_RESET ANSI_COLOR(44) " ◤◥███▲▲▲ ║" ANSI_RESET "\n"
	    "                                  ︿︿︿︿︿︿︿︿    " ANSI_COLOR(33)
	    "│" ANSI_RESET "   ◥████◤ ║\n"
	    "                                                      " ANSI_COLOR(33) "╫"
	    "——" ANSI_RESET "  ◤      —＋" ANSI_RESET);
    } else {
	showtitle("看板列表", BBSNAME);
	brdlist_foot();
	move(vs_row_line(VS_COL_HEADER), 0);
	vbar(TEMPFORMAT(STRLEN, ANSI_REVERSE "   %s   看  板       類別   中   文   敘   述"
              "               人氣 板   主", newflag ? "總數" : "編號"));
    }
    return 0;
}

static int
brdlist_footer(PSB_CTX *ctx GCC_UNUSED)
{
    brdlist_foot();
    return 0;
}

static int
brdlist_empty_renderer(PSB_CTX *ctx GCC_UNUSED)
{
    if (IS_LISTING_FAV()) {
	if (!HasFavEditPerm()) {
	    mvouts(3, 10,
		"--- 註冊的使用者才能新增看板喔 (可按 s 手動選取) ---");
	} else {
	    mvouts(3, 10,
		"--- 空目錄，請按 a 新增或用 y 列出全部看板後按 z 增刪 ---");
	}
    }
    return 0;
}

static int
brdlist_renderer(int idx, PSB_CTX *ctx)
{
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->cmd.priv;
    int newflag = *cx->newflag;
    int head = idx + 1;
    boardstat_t *ptr;
    char *unread[2] = {ANSI_COLOR(37) "  " ANSI_RESET, ANSI_COLOR(1;31) "ˇ" ANSI_RESET};

    assert(0 <= idx && idx < nbrdsize);
    ptr = &nbrd[idx];
    if (ptr->myattr & NBRD_LINE) {
	if (!newflag)
	    prints("%7d %c ", head, ptr->myattr & NBRD_TAG ? 'D' : ' ');
	else
	    prints("%7s   ", "");

	if (!(ptr->myattr & NBRD_FAV))
	    outs(ANSI_COLOR(1;30));

	outs("------------"
	     "      "
	     "------------------------------------------"
	     ANSI_RESET);
	clrtoeol();
	return 0;
    } else if (ptr->myattr & NBRD_FOLDER) {
	char *title = get_folder_title(ptr->bid);
	prints("%7d %c ",
	       newflag ?
	       get_data_number(get_fav_folder(getfolder(ptr->bid))) :
	       head, ptr->myattr & NBRD_TAG ? 'D' : ' ');
	prints("%sMyFavFolder" ANSI_RESET "  目錄 □%-34s",
	       !(HasUserFlag(UF_FAV_NOHILIGHT)) ?
	       HILIGHT_COLOR : "",
	       title);
	clrtoeol();
	return 0;
    }

    if (IN_CLASSROOT()) {
	outs("          ");
    } else if (!GROUPOP() && !HasBoardPerm(B_BH(ptr))) {
	const char *reason = "[禁入]";

	if (newflag)
	    prints("%7s", "");
	else
	    prints("%7d", head);

	if (B_BH(ptr)->brdattr & BRD_HIDE)
	    reason = "[隱板]";

#ifdef USE_REAL_DESC_FOR_HIDDEN_BOARD_IN_MYFAV
	prints("X%c %-13.13s%s  %s",
	       ptr->myattr & NBRD_TAG ? 'D' : ' ',
	       B_BH(ptr)->brdname,
	       reason,
	       TEMP_BRD_TITLE_DESC(B_BH(ptr)));
#else
	prints("X%c %-13.13s%s  <目前無法進入此看板>",
	       ptr->myattr & NBRD_TAG ? 'D' : ' ',
	       B_BH(ptr)->brdname,
	       reason);
#endif
	clrtoeol();
	return 0;
    }

#ifdef USE_REAL_DESC_FOR_HIDDEN_BOARD_IN_MYFAV
    const int should_show_sensitive_info = true;
#else
    const int should_show_sensitive_info =
	!BoardPermNeedsSysopOverride(B_BH(ptr)) &&
	!(GROUPOP() && !HasBoardPerm(B_BH(ptr)));
#endif

    if (newflag && (B_BH(ptr)->brdattr & BRD_GROUPBOARD))
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
	char t_cls[SZ_COLS(6)], t_sym[SZ_COLS(3)], t_desc[SZ_COLS(BTLEN + 1)];
	brd_get_title_class(B_BH(ptr), t_cls, sizeof(t_cls));
	if (should_show_sensitive_info) {
	    brd_get_title_symbol(B_BH(ptr), t_sym, sizeof(t_sym));
	    strlcpy(t_desc, TEMP_BRD_TITLE_DESC(B_BH(ptr)), sizeof(t_desc));
	} else {
	    t_sym[0] = 0;
	    t_desc[0] = 0;
	}
	while (stream_width(t_desc) > 34) { t_desc[strlen(t_desc) - 1] = 0; mbs_safe_trim(t_desc); }
	prints("%s%-13s" ANSI_RESET "%s%s%*s" ANSI_COLOR(0;37)
	       "%s%*s" ANSI_RESET "%s%*s",
	       ((!(HasUserFlag(UF_FAV_NOHILIGHT)) &&
	         getboard(ptr->bid) != NULL)) ? HILIGHT_COLOR : "",
	       B_BH(ptr)->brdname,
	       make_class_color(B_BH(ptr)->title),
	       t_cls, 5 - (int)stream_width(t_cls) > 0 ? 5 - (int)stream_width(t_cls) : 0, "",
	       t_sym, 2 - (int)stream_width(t_sym) > 0 ? 2 - (int)stream_width(t_sym) : 0, "",
	       t_desc, 34 - (int)stream_width(t_desc) > 0 ? 34 - (int)stream_width(t_desc) : 0, "");

	if (!should_show_sensitive_info)
	    outs("   ");
	else if (B_BH(ptr)->brdattr & BRD_COOLDOWN)
	    outs("靜 ");
	else if (B_BH(ptr)->nuser < 1)
	    prints(" %c ", B_BH(ptr)->bvote ? 'V' : ' ');
	else if (B_BH(ptr)->nuser <= 10)
	    prints("%2d ", B_BH(ptr)->nuser);
	else if (B_BH(ptr)->nuser <= 50)
	    prints(ANSI_COLOR(1;33) "%2d" ANSI_RESET " ", B_BH(ptr)->nuser);
#ifdef EXTRA_HOTBOARD_COLORS
	else if (B_BH(ptr)->nuser >= 100000)
	    outs(ANSI_COLOR(1;35) "爆!" ANSI_RESET);
	else if (B_BH(ptr)->nuser >= 60000)
	    outs(ANSI_COLOR(1;33) "爆!" ANSI_RESET);
	else if (B_BH(ptr)->nuser >= 30000)
	    outs(ANSI_COLOR(1;32) "爆!" ANSI_RESET);
	else if (B_BH(ptr)->nuser >= 10000)
	    outs(ANSI_COLOR(1;36) "爆!" ANSI_RESET);
#endif
	else if (B_BH(ptr)->nuser >= 5000)
	    outs(ANSI_COLOR(1;34) "爆!" ANSI_RESET);
	else if (B_BH(ptr)->nuser >= 2000)
	    outs(ANSI_COLOR(1;31) "爆!" ANSI_RESET);
	else if (B_BH(ptr)->nuser >= 1000)
	    outs(ANSI_COLOR(1) "爆!" ANSI_RESET);
	else if (B_BH(ptr)->nuser >= 100)
	    outs(ANSI_COLOR(1) "HOT" ANSI_RESET);
	else
	    prints(ANSI_COLOR(1;31) "%2d" ANSI_RESET " ", B_BH(ptr)->nuser);
	char t_bm[SZ_COLS(IDLEN * 3 + 3)];
	strlcpy(t_bm, TEMP_BRD_BM(B_BH(ptr)), sizeof(t_bm));
	while (t_columns > 68 && (int)stream_width(t_bm) > t_columns - 68) {
	    t_bm[strlen(t_bm) - 1] = 0;
	    mbs_safe_trim(t_bm);
	}
	prints("%s" ANSI_CLRTOEND, t_bm);
    } else {
	char t_desc[SZ_COLS(BTLEN + 1)], t_bm[SZ_COLS(IDLEN * 3 + 3)];
	strlcpy(t_desc, TEMP_BRD_TITLE_DESC(B_BH(ptr)), sizeof(t_desc));
	while (stream_width(t_desc) > 40) { t_desc[strlen(t_desc) - 1] = 0; mbs_safe_trim(t_desc); }
	strlcpy(t_bm, TEMP_BRD_BM(B_BH(ptr)), sizeof(t_bm));
	while (t_columns > 68 && (int)stream_width(t_bm) > t_columns - 68) {
	    t_bm[strlen(t_bm) - 1] = 0;
	    mbs_safe_trim(t_bm);
	}
	prints("%s%*s %s", t_desc,
	       40 - (int)stream_width(t_desc) > 0 ? 40 - (int)stream_width(t_desc) : 0, "",
	       t_bm);
	clrtoeol();
    }
    return 0;
}

static int
brdlist_cursor(int y, PSB_CTX *ctx GCC_UNUSED)
{
    cursor_show(y, IN_CLASSROOT() ? 10 : 0);
    return 0;
}

static void
set_menu_group_op(const char *BM)
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

    // XXX 不是很確定是否該在這邊 save level?
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

    if (gid <= 0  || ! (HasUserPerm(PERM_SYSOP) || GROUPOP()) ||
        vans("貼上標記的看板?(y/N)")!='y') return 0;
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
choose_board_add_favorite(int ch, int brdnum, int *num_ptr, char *keyword)
{
    if (!IN_FAVORITE() || !HasFavEditPerm())
        return;

    char bname[IDLEN + 1];
    int bid;
    move(0, 0);
    clrtoeol();
    CompleteBoard(ANSI_REVERSE "【 增加我的最愛 】" ANSI_RESET "\n"
                  "請輸入欲加入的看板名稱(按空白鍵自動搜尋)：",
                  bname);

    if (!bname[0] || !(bid = getbnum(bname)) || !HasBoardPerm(getbcache(bid)))
        return;

    fav_type_t *ptr = getboard(bid);
    if (ptr != NULL) {
        int i;
        for (i = 0; i < nbrdsize; ++i) {
            if (bid == nbrd[i].bid) {
                *num_ptr = i;
                break;
            }
        }
        if (i == nbrdsize) {
            assert(keyword[0]);
            vmsg("已經在我的最愛裡了, 取消關鍵字就能看到囉");
            keyword[0] = '\0';
        }
    } else {
        ptr = fav_add_board(bid);
        if (ptr == NULL) {
            vmsg("你的最愛太多了啦 真花心");
        } else {
            ptr->attr |= NBRD_FAV;
            if (ch == 'i' && get_data_number(get_current_fav()) > 1)
                move_in_current_folder(brdnum, *num_ptr);
            else
                *num_ptr = brdnum;
        }
    }
}


static void choose_board(int newflag);

///////////////////////////////////////////////////////////////////////////
// Layer 1: Boardlist Base Commands

static int
board_cmd_whereami(cmd_ctx_t *ctx) {
    whereami();
    ctx->redraw = true;
    return 0;
}

static int
board_cmd_toggle_newflag(cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    *cx->newflag ^= 1;
    ctx->redraw = true;
    return 0;
}

static int
board_cmd_quit(cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    if (cx->keyword[0]) {
        cx->keyword[0] = 0;
        brdnum = -1;
        ctx->reload = true;
        return 0;
    }
    ctx->quit = true;
    return 0;
}

static int
board_cmd_pgup(cmd_ctx_t *ctx) {
    if (ctx->curr)
        ctx->curr -= ctx->rows;
    else
        ctx->curr = ctx->total - 1;
    return 0;
}

static int
board_cmd_end(cmd_ctx_t *ctx) {
    ctx->curr = ctx->total - 1;
    return 0;
}

static int
board_cmd_pgdn(cmd_ctx_t *ctx) {
    if (ctx->curr == ctx->total - 1)
        ctx->curr = 0;
    else
        ctx->curr += ctx->rows;
    return 0;
}

static int
board_cmd_up(cmd_ctx_t *ctx) {
    if (--ctx->curr < 0)
        ctx->curr = ctx->total - 1;
    return 0;
}

static int
board_cmd_down(cmd_ctx_t *ctx) {
    if (++ctx->curr >= ctx->total)
        ctx->curr = 0;
    return 0;
}

static int
board_cmd_home(cmd_ctx_t *ctx) {
    ctx->curr = 0;
    return 0;
}

static int
board_cmd_tag(cmd_ctx_t *ctx) {
    int num = ctx->curr;
    boardstat_t *ptr;
    assert(0 <= num && num < nbrdsize);
    ptr = &nbrd[num];
    if (IS_LISTING_FAV()) {
        assert(nbrdsize > 0);
        if (get_fav_type(&nbrd[0]) != 0)
            fav_tag(ptr->bid, get_fav_type(ptr), EXCH);
    } else if (HasUserPerm(PERM_SYSOP) ||
               HasUserPerm(PERM_SYSSUPERSUBOP) ||
               HasUserPerm(PERM_SYSSUBOP) ||
               HasUserPerm(PERM_BOARD)) {
        if (ptr->myattr & NBRD_TAG)
            set_attr(getadmtag(ptr->bid), FAVH_ADM_TAG, FALSE);
        else
            fav_add_admtag(ptr->bid);
    }
    ptr->myattr ^= NBRD_TAG;
    ctx->redraw = true;
    if (++ctx->curr >= ctx->total)
        ctx->curr = 0;
    return 0;
}

static int
board_cmd_num(cmd_ctx_t *ctx) {
    int tmp;
    if ((tmp = search_num(ctx->key, ctx->total)) >= 0)
        ctx->curr = tmp;
    ctx->redraw_footer_lines = 1;
    return 0;
}

static int
board_cmd_search_keyword(cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    if (IN_HOTBOARD()) {
        vmsg("熱門看板模式下不支援中文關鍵字搜尋");
        ctx->redraw_footer_lines = 1;
    } else {
        getdata_buf(b_lines - 1, 0, "請輸入看板中文關鍵字:",
                    cx->keyword, cx->keyword_sz, DOECHO);
        trim(cx->keyword);
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
board_cmd_sort(cmd_ctx_t *ctx) {
    if (IS_LISTING_FAV()) {
        int tmp;
        move(b_lines - 2, 0); clrtobot();
        outs("重新排序看板 "
             ANSI_COLOR(1;33) "(注意, 這個動作會覆寫原來設定)" ANSI_RESET " \n");
        tmp = vans("排序方式 (1)按照板名排序 (2)按照類別排序 ==> [0]取消 ");
        if (tmp == '1')
            fav_sort_by_name();
        else if (tmp == '2')
            fav_sort_by_class();
    } else {
        pwcuToggleSortBoard();
    }
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
board_cmd_mark_read(cmd_ctx_t *ctx) {
    int num = ctx->curr;
    int ch = ctx->key;
    boardstat_t *ptr;
    assert(0 <= num && num < nbrdsize);
    ptr = &nbrd[num];
    if (nbrd[num].bid < 0 || !HasBoardPerm(B_BH(ptr)))
        return 0;
    if (ch == 'v') {
        ptr->myattr &= ~NBRD_UNREAD;
        brc_toggle_all_read(ptr->bid, 1);
    } else {
        brc_toggle_all_read(ptr->bid, 0);
        ptr->myattr |= NBRD_UNREAD;
    }
    ctx->redraw = true;
    return 0;
}

static int
board_cmd_search_local(cmd_ctx_t *ctx) {
    int tmp;
    ctx->redraw = true;
    if ((tmp = search_local_board()) != -1)
        ctx->curr = tmp;
    return 0;
}

static int
board_cmd_search_global(cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    char bname[IDLEN + 1];
    int tmp;
    int tmpbid = currutmp->brc_id;
    move(0, 0);
    clrtoeol();
    CompleteBoard(ANSI_REVERSE
                  "【 搜尋全站看板 】" ANSI_RESET
                  "  (若要限定搜尋範圍為目前列表請改用 Ctrl-S)\n"
                  "請輸入看板名稱(按空白鍵自動搜尋): ",
                  bname);
    ctx->redraw = true;
    if (!*bname)
        return 0;
    if ((tmp = search_board(bname)) != -1) {
        ctx->curr = tmp;
        return 0;
    }
    if (enter_board(bname) >= 0)
        Read();
    setutmpbid(tmpbid);
    setutmpmode(*cx->newflag ? READNEW : READBRD);
    return 0;
}

static void
board_enter_fav_folder(boardstat_t *ptr, cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    int t = ctx->curr;
    *cx->num = 0;
    fav_folder_in(ptr->bid);
    choose_board(0);
    fav_folder_out();
    *cx->num = t;
    ctx->curr = t;
    LIST_FAV();
    brdnum = -1;
    ctx->reload = true;
}

static void
board_enter_normal(boardstat_t *ptr, cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    char buf[PATHLEN];
    if (!HasBoardPerm(B_BH(ptr)))
        return;
    brc_initial_board(B_BH(ptr)->brdname);
    if (*cx->newflag) {
        setbdir(buf, currboard);
        int tmp = unread_position(buf, ptr);
        int head = tmp - t_lines / 2;
        getkeep(buf, head > 1 ? head : 1, tmp + 1);
    }
    Read();
    check_newpost(ptr);
    ctx->redraw = true;
    setutmpmode(*cx->newflag ? READNEW : READBRD);
}

static void
board_enter_group(boardstat_t *ptr, cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    char buf[PATHLEN];
    move(12, 1);
    int bidtmp = class_bid;
    int currmodetmp = currmode;
    int tmp1 = ctx->curr;
    *cx->num = 0;
    class_bid = (B_BH(ptr)->brdattr & BRD_TOP) ? -1 : ptr->bid;

    if (!GROUPOP())
        set_menu_group_op(TEMP_BRD_BM(B_BH(ptr)));

    if (time4_lt(now, B_BH(ptr)->bupdate)) {
        setbfile(buf, B_BH(ptr)->brdname, fn_notes);
        int mr = more(buf, NA);
        if (mr != -1 && mr != READ_NEXT)
            pressanykey();
    }
    int tmp = currutmp->brc_id;
    setutmpbid(ptr->bid);
    free(nbrd);
    nbrd = NULL;
    nbrdsize = 0;
    if (IS_LISTING_FAV()) {
        LIST_BRD();
        choose_board(0);
        LIST_FAV();
    } else {
        choose_board(0);
    }
    currmode = currmodetmp;
    *cx->num = tmp1;
    ctx->curr = tmp1;
    class_bid = bidtmp;
    setutmpbid(tmp);
    brdnum = -1;
    ctx->reload = true;
}

static int
board_cmd_select(cmd_ctx_t *ctx) {
    int num = ctx->curr;
    assert(0 <= num && num < nbrdsize);
    boardstat_t *ptr = &nbrd[num];

    if (IS_LISTING_FAV()) {
        if (get_fav_type(&nbrd[0]) == 0 || (ptr->myattr & NBRD_LINE))
            return 0;
        if (ptr->myattr & NBRD_FOLDER) {
            board_enter_fav_folder(ptr, ctx);
            return 0;
        }
    } else if (ptr->myattr & NBRD_SYMBOLIC) {
        replace_link_by_target(ptr);
    }

    assert(0 <= ptr->bid - 1 && ptr->bid - 1 < MAX_BOARD);
    if (B_BH(ptr)->brdattr & BRD_GROUPBOARD)
        board_enter_group(ptr, ctx);
    else
        board_enter_normal(ptr, ctx);
    return 0;
}

static int
board_cmd_save_brc(cmd_ctx_t *ctx) {
    if (time4_diff(now, last_save_fav_and_brc) > 10 * 60) {
        fav_save();
        brc_finalize();
        last_save_fav_and_brc = now;
        vmsg("已儲存看板閱\讀記錄");
    } else {
        vmsgf("間隔時間太短, 暫不儲存看板閱\讀記錄 [請等 %d 秒]",
              (int)(600 - time4_diff(now, last_save_fav_and_brc)));
    }
    ctx->redraw_footer_lines = 1;
    return 0;
}

static const cmd_t boardlist_cmds[] = {
    { KEY_LEFT, "回上層", "離開看板列表", board_cmd_quit, 0, CMD_PRIO_MAX },
    { 'e', NULL, NULL, board_cmd_quit, 0, CMD_PRIO_NONE },
    { EOF, NULL, NULL, board_cmd_quit, 0, CMD_PRIO_NONE },
    { 'q', NULL, NULL, board_cmd_quit, 0, CMD_PRIO_NONE },
    { KEY_RIGHT, "進入", "進入選取的看板或目錄", board_cmd_select, 0, CMD_PRIO_NORM, true },
    { 's', "找看板", "搜尋全站看板名稱", board_cmd_search_global, 0, CMD_PRIO_NORM },
    { '/', "搜尋", "搜尋看板中文關鍵字", board_cmd_search_keyword, 0, CMD_PRIO_NORM },
    { 'c', "新文章", "切換顯示看板編號或文章數", board_cmd_toggle_newflag, 0, CMD_PRIO_NORM },
    { 'S', "排序", "切換看板排序方式", board_cmd_sort, 0, CMD_PRIO_NORM },
    { 'v', "已讀/未讀", "標記看板為已讀/未讀", board_cmd_mark_read, 0, CMD_PRIO_HIGH, true },
    { 'V', NULL, NULL, board_cmd_mark_read, 0, CMD_PRIO_NONE, true },
    { KEY_PGUP, "上頁", "向上翻頁", board_cmd_pgup, 0, CMD_PRIO_NAV },
    { 'P', NULL, NULL, board_cmd_pgup, 0, CMD_PRIO_NONE },
    { 'b', NULL, NULL, board_cmd_pgup, 0, CMD_PRIO_NONE },
    { Ctrl('B'), NULL, NULL, board_cmd_pgup, 0, CMD_PRIO_NONE },
    { KEY_PGDN, "下頁", "向下翻頁", board_cmd_pgdn, 0, CMD_PRIO_NAV },
    { ' ', NULL, NULL, board_cmd_pgdn, 0, CMD_PRIO_NONE },
    { 'N', NULL, NULL, board_cmd_pgdn, 0, CMD_PRIO_NONE },
    { Ctrl('F'), NULL, NULL, board_cmd_pgdn, 0, CMD_PRIO_NONE },
    { KEY_UP, NULL, "向上移動", board_cmd_up, 0, CMD_PRIO_NONE },
    { 'p', NULL, NULL, board_cmd_up, 0, CMD_PRIO_NONE },
    { 'k', NULL, NULL, board_cmd_up, 0, CMD_PRIO_NONE },
    { KEY_DOWN, NULL, "向下移動", board_cmd_down, 0, CMD_PRIO_NONE },
    { 'n', NULL, NULL, board_cmd_down, 0, CMD_PRIO_NONE },
    { 'j', NULL, NULL, board_cmd_down, 0, CMD_PRIO_NONE },
    { '0', NULL, "移至第一筆", board_cmd_home, 0, CMD_PRIO_NONE },
    { KEY_HOME, NULL, NULL, board_cmd_home, 0, CMD_PRIO_NONE },
    { KEY_END, NULL, "移至最後一筆", board_cmd_end, 0, CMD_PRIO_NONE },
    { '$', NULL, NULL, board_cmd_end, 0, CMD_PRIO_NONE },
    { '1', NULL, "輸入編號跳轉", board_cmd_num, 0, CMD_PRIO_NONE },
    { '2', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '3', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '4', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '5', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '6', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '7', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '8', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { '9', NULL, NULL, board_cmd_num, 0, CMD_PRIO_NONE },
    { Ctrl('Y'), NULL, "查詢目前位置", board_cmd_whereami, 0, CMD_PRIO_NONE },
    { Ctrl('W'), NULL, NULL, board_cmd_whereami, 0, CMD_PRIO_NONE },
    { 't', "標記", "標記看板項目", board_cmd_tag, PERM_BASIC, CMD_PRIO_HIGH, true },
    { Ctrl('S'), NULL, "搜尋目前列表看板", board_cmd_search_local, 0, CMD_PRIO_NONE, true },
    { KEY_ENTER, NULL, NULL, board_cmd_select, 0, CMD_PRIO_NONE, true },
    { 'r', NULL, NULL, board_cmd_select, 0, CMD_PRIO_NONE, true },
    { 'l', NULL, NULL, board_cmd_select, 0, CMD_PRIO_NONE, true },
    { 'w', NULL, "手動儲存閱\讀記錄", board_cmd_save_brc, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

///////////////////////////////////////////////////////////////////////////
// Layer 2: Board Admin Commands

static int
board_cmd_reset_sort(cmd_ctx_t *ctx) {
    if (!IN_CLASS())
        return 0;
    getbcache(class_bid)->firstchild[HasUserFlag(UF_BRDSORT)
        ? BRD_GROUP_LL_TYPE_CLASS : BRD_GROUP_LL_TYPE_NAME] = 0;
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
board_cmd_del_link(cmd_ctx_t *ctx) {
    if (ctx->total <= 0 || (!HasUserPerm(PERM_BOARD) && !GROUPOP()))
        return 0;
    int num = ctx->curr;
    assert(0 <= num && num < nbrdsize);
    boardstat_t *ptr = &nbrd[num];
    if (ptr->myattr & NBRD_SYMBOLIC) {
        if (vans("確定刪除連結？[N/y]") == 'y')
            delete_board_link(getbcache(ptr->bid), ptr->bid);
    }
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
board_cmd_edit_board(cmd_ctx_t *ctx) {
    if (ctx->total <= 0 || (!HasUserPerm(PERM_BOARD) && !GROUPOP()))
        return 0;
    int num = ctx->curr;
    assert(0 <= num && num < nbrdsize);
    boardstat_t *ptr = &nbrd[num];
    move(1, 1);
    clrtobot();
    m_mod_board(B_BH(ptr)->brdname);
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
board_cmd_new_group(cmd_ctx_t *ctx) {
    if (!IN_CLASS() || (!HasUserPerm(PERM_BOARD) && !GROUPOP()))
        return 0;
    m_newbrd(class_bid, 1);
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
board_cmd_new_board(cmd_ctx_t *ctx) {
    if (!IN_CLASS() || (!HasUserPerm(PERM_BOARD) && !GROUPOP()))
        return 0;
    m_newbrd(class_bid, 0);
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
board_cmd_edit_note(cmd_ctx_t *ctx) {
    if (!IN_SUBCLASS() || (!HasUserPerm(PERM_BOARD) && !GROUPOP()))
        return 0;
    char buf[PATHLEN];
    setbpath(buf, getbcache(class_bid)->brdname);
    Mkdir(buf);
    b_note_edit_bname(class_bid);
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

#define PERM_BRD_OR_GROUPOP (PERM_BOARD | PERM_SYSSUBOP | PERM_SYSSUPERSUBOP)

static const cmd_t board_admin_cmds[] = {
    { 'F', NULL, "重設群組排序快取", board_cmd_reset_sort, PERM_SYSOP, CMD_PRIO_NONE },
    { 'f', NULL, NULL, board_cmd_reset_sort, PERM_SYSOP, CMD_PRIO_NONE },
    { 'D', NULL, "刪除看板連結", board_cmd_del_link, PERM_BOARD | PERM_SYSSUPERSUBOP, CMD_PRIO_NONE, true },
    { 'E', NULL, "修改看板設定", board_cmd_edit_board, PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE, true },
    { 'R', NULL, "新增群組目錄", board_cmd_new_group, PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE },
    { 'B', NULL, "開闢新看板", board_cmd_new_board, PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE },
    { 'W', NULL, "編輯群組進板畫面", board_cmd_edit_note, PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

///////////////////////////////////////////////////////////////////////////
// Layer 3: MyFav Overlay Commands

static int
fav_cmd_tag_all(cmd_ctx_t *ctx) {
    if (ctx->total <= 0 || !IS_LISTING_FAV())
        return 0;
    assert(brdnum <= nbrdsize);
    for (int i = 0; i < brdnum; i++) {
        boardstat_t *ptr = &nbrd[i];
        assert(nbrdsize > 0);
        fav_tag(ptr->bid, get_fav_type(ptr), 2);
        ptr->myattr ^= NBRD_TAG;
    }
    ctx->redraw = true;
    return 0;
}

static int
fav_cmd_yank(cmd_ctx_t *ctx) {
    if (!(IN_CLASS())) {
        if (get_current_fav() != NULL || !IS_LISTING_FAV())
            yank_flag ^= 1;
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
fav_cmd_del_tagged(cmd_ctx_t *ctx) {
    if (vans("刪除所有標記[N]?") == 'y') {
        fav_remove_all_tagged_item();
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
fav_cmd_add_tagged(cmd_ctx_t *ctx) {
    fav_add_all_tagged_item();
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
fav_cmd_untag_all(cmd_ctx_t *ctx) {
    fav_remove_all_tag();
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
fav_cmd_paste_tagged(cmd_ctx_t *ctx) {
    if (IN_CLASS() && paste_taged_brds(class_bid)) {
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
fav_cmd_add_line_or_link(cmd_ctx_t *ctx) {
    int num = ctx->curr;
    if (IN_CLASS() &&
        (HasUserPerm(PERM_BOARD) ||
         (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP()))) {
        brdnum = -1;
        ctx->reload = true;
        if (make_board_link_interactively(class_bid) < 0)
            return 0;
    } else if (IS_LISTING_FAV()) {
        if (fav_add_line() == NULL) {
            vmsg("新增失敗，分隔線/總最愛 數量達最大值。");
            return 0;
        }
        assert(nbrdsize > 0);
        if (get_fav_type(&nbrd[0]) != 0)
            move_in_current_folder(brdnum, num);
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
fav_cmd_toggle_or_del(cmd_ctx_t *ctx) {
    int num = ctx->curr;
    int ch = ctx->key;
    assert(0 <= num && num < nbrdsize);
    boardstat_t *ptr = &nbrd[num];

    brdnum = -1;
    ctx->reload = true;
    if (IS_LISTING_FAV()) {
        if ((ptr->myattr & NBRD_FAV) && vans("你確定刪除嗎? [N/y]") == 'y') {
            fav_remove_item(ptr->bid, get_fav_type(ptr));
            ptr->myattr &= ~NBRD_FAV;
        }
        return 0;
    }

    if (getboard(ptr->bid) != NULL) {
        fav_remove_item(ptr->bid, FAVT_BOARD);
        ptr->myattr &= ~NBRD_FAV;
    } else if (ch != 'd') {
        if (fav_add_board(ptr->bid) == NULL)
            vmsg("你的最愛太多了啦 真花心");
        else
            ptr->myattr |= NBRD_FAV;
    }
    return 0;
}

static int
fav_cmd_move(cmd_ctx_t *ctx) {
    if (IN_FAVORITE() && IS_LISTING_FAV()) {
        imovefav(ctx->curr);
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
fav_cmd_add_folder(cmd_ctx_t *ctx) {
    if (!IS_LISTING_FAV())
        return 0;
    if (fav_stack_full()) {
        vmsg("目錄已達最大層數!!");
        return 0;
    }
    fav_type_t *ft = fav_add_folder();
    if (ft == NULL) {
        vmsg("新增失敗，目錄/總最愛 數量達最大值。");
        return 0;
    }
    fav_set_folder_title(ft, "新的目錄");
    assert(nbrdsize > 0);
    if (get_fav_type(&nbrd[0]) != 0)
        move_in_current_folder(brdnum, ctx->curr);
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static int
fav_cmd_edit_title(cmd_ctx_t *ctx) {
    int num = ctx->curr;
    char buf[PATHLEN];
    assert(0 <= num && num < nbrdsize);
    if ((nbrd[num].myattr & NBRD_FOLDER)) {
        fav_type_t *ft = getfolder(nbrd[num].bid);
        STRLCPY(buf, get_item_title(ft));
        getdata_buf(b_lines - 1, 0, "請修改名稱: ", buf, BTLEN + 1, DOECHO);
        fav_set_folder_title(ft, buf);
        brdnum = -1;
        ctx->reload = true;
    }
    return 0;
}

static int
fav_cmd_backup(cmd_ctx_t *ctx) {
    char c, fname[80], buf[PATHLEN];
    brdnum = -1;
    ctx->reload = true;
    if (get_current_fav() != get_fav_root()) {
        vmsg("請到我的最愛最上層執行本功\能");
        return 0;
    }
    c = vans("請選擇 2)備份我的最愛 3)取回最愛備份 [Q]");
    if (!c || vans("確定嗎 [y/N] ") != 'y')
        return 0;
    setuserfile(fname, FAV);
    sprintf(buf, "%s.bak", fname);
    if (c == '2') {
        fav_save();
        Copy(fname, buf);
    } else if (c == '3') {
        if (!dashf(buf)) {
            vmsg("你沒有備份你的最愛喔");
            return 0;
        }
        Copy(buf, fname);
        fav_free();
        fav_load();
    }
    return 0;
}

static int
fav_cmd_add_board(cmd_ctx_t *ctx) {
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->priv;
    choose_board_add_favorite(ctx->key, brdnum, &ctx->curr, cx->keyword);
    brdnum = -1;
    ctx->reload = true;
    return 0;
}

static const cmd_t myfav_cmds[] = {
    { 'a', "增加看板", "輸入看板名稱加入最愛", fav_cmd_add_board, PERM_BASIC, CMD_PRIO_HIGH },
    { 'i', NULL, NULL, fav_cmd_add_board, PERM_BASIC, CMD_PRIO_NONE },
    { 'y', "列出全部", "切換顯示全部看板/我的最愛", fav_cmd_yank, PERM_BASIC, CMD_PRIO_NORM },
    { 'd', "刪除", "從我的最愛移除項目", fav_cmd_toggle_or_del, PERM_BASIC, CMD_PRIO_HIGH, true },
    { 'm', NULL, "將看板加入或移出最愛", fav_cmd_toggle_or_del, PERM_BASIC, CMD_PRIO_NONE, true },
    { 'z', NULL, NULL, fav_cmd_toggle_or_del, PERM_BASIC, CMD_PRIO_NONE, true },
    { 'g', "新增目錄", "在我的最愛新增子目錄", fav_cmd_add_folder, PERM_BASIC, CMD_PRIO_HIGH },
    { 'M', "移動位置", "移動最愛項目順序", fav_cmd_move, PERM_BASIC, CMD_PRIO_HIGH, true },
    { 'L', "加分隔線", "新增分隔線或看板連結", fav_cmd_add_line_or_link, PERM_BASIC, CMD_PRIO_LOW },
    { 'T', "改目錄名", "修改最愛子目錄標題", fav_cmd_edit_title, PERM_BASIC, CMD_PRIO_LOW, true },
    { 'K', "備份還原", "備份或還原我的最愛", fav_cmd_backup, PERM_BASIC, CMD_PRIO_LOW },
    { '*', NULL, "反選全部標記", fav_cmd_tag_all, PERM_BASIC, CMD_PRIO_NONE, true },
    { Ctrl('D'), NULL, "刪除所有已標記項目", fav_cmd_del_tagged, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('A'), NULL, "將標記項目加入最愛", fav_cmd_add_tagged, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('E'), NULL, "清除所有標記", fav_cmd_untag_all, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('T'), NULL, NULL, fav_cmd_untag_all, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('P'), NULL, "貼上標記看板", fav_cmd_paste_tagged, PERM_SYSOP | PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t board_fav_cmds[] = {
    { 'm', "加入最愛", "將看板加入或移出最愛", fav_cmd_toggle_or_del, PERM_BASIC, CMD_PRIO_HIGH, true },
    { 'z', NULL, NULL, fav_cmd_toggle_or_del, PERM_BASIC, CMD_PRIO_NONE, true },
    { 'y', "只列最愛", "切換顯示全部看板/我的最愛", fav_cmd_yank, PERM_BASIC, CMD_PRIO_NORM },
    { 'L', NULL, "新增看板連結", fav_cmd_add_line_or_link, PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE },
    { '*', NULL, "反選全部標記", fav_cmd_tag_all, PERM_BASIC, CMD_PRIO_NONE, true },
    { Ctrl('A'), NULL, "將標記項目加入最愛", fav_cmd_add_tagged, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('E'), NULL, "清除所有標記", fav_cmd_untag_all, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('T'), NULL, NULL, fav_cmd_untag_all, PERM_BASIC, CMD_PRIO_NONE },
    { Ctrl('P'), NULL, "貼上標記看板", fav_cmd_paste_tagged, PERM_SYSOP | PERM_BRD_OR_GROUPOP, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

// Returns: 1 = loaded successfully, 0 = retry loop (continue), -1 = exit loop (break)
static int
board_reload_and_check_empty(char *keyword) {
    load_boards(keyword);
    if (brdnum > 0)
        return 1;

    if (keyword[0] != 0) {
        vmsg("沒有任何看板標題有此關鍵字");
        keyword[0] = 0;
        brdnum = -1;
        return 0;
    }
    if (!IS_LISTING_BRD())
        return 1;
    if (!HasUserPerm(PERM_SYSOP) && !GROUPOP())
        return -1;
    if (paste_taged_brds(class_bid) || m_newbrd(class_bid, 0) == -1)
        return -1;
    brdnum = -1;
    return 0;
}

static int
board_find_first_unread(int num, int total) {
    assert(total <= nbrdsize);
    for (int i = num; i < total; i++) {
        if (nbrd[i].myattr & NBRD_UNREAD)
            return i;
    }
    return num;
}

static int
brdlist_loader(PSB_CTX *ctx)
{
    boardlist_ctx_t *cx = (boardlist_ctx_t *)ctx->cmd.priv;
    cx->layers[0].cmds = IS_LISTING_FAV() ? myfav_cmds : board_fav_cmds;
    ctx->header_lines = IN_CLASSROOT() ? 7 : 3;
    ctx->cmd.caption = brdlist_caption();

    if (brdnum <= 0) {
        while (brdnum <= 0) {
            int status = board_reload_and_check_empty(cx->keyword);
            if (status < 0) {
                ctx->cmd.quit = true;
                return 0;
            }
            if (status > 0)
                break;
        }
        if (*cx->newflag && brdnum > 0) {
            ctx->cmd.curr = board_find_first_unread(ctx->cmd.curr, brdnum);
        }
    }

    if (IS_LISTING_FAV() && brdnum == 1 && get_fav_type(&nbrd[0]) == 0) {
        ctx->cmd.total = 0;
    } else {
        ctx->cmd.total = brdnum;
    }
    return 0;
}

static void
choose_board(int newflag)
{
    static int      num = 0;
    char            keyword[SZ_COLS(13)] = "";
    boardlist_ctx_t cx = {
        .num = &num,
        .newflag = &newflag,
        .keyword = keyword,
        .keyword_sz = sizeof(keyword),
    };
    cmd_layer_t layers[] = {
        { myfav_cmds,       &cx },
        { board_admin_cmds, &cx },
        { boardlist_cmds,   &cx },
        { bbs_global_cmds,  NULL },
        { NULL, NULL }
    };
    cx.layers = layers;

    setutmpmode(newflag ? READNEW : READBRD);
    if (get_fav_root() == NULL) {
        fav_load();
        if (!get_current_fav()) {
            vmsgf("我的最愛載入失敗，請到" BN_BUGREPORT "報告您之前進行了哪些動作，謝謝");
            refresh();
            assert(get_current_fav());
            exit(-1);
        }
    }

    ++choose_board_depth;
    brdnum = 0;
    if (!cuser.userlevel)
        LIST_BRD();

    PSB_CTX psbctx = {
        .cmd = {
            .curr = num,
            .priv = &cx,
            .caption = brdlist_caption(),
        },
        .header_lines = IN_CLASSROOT() ? 7 : 3,
        .footer_lines = 1,
        .layers = layers,
        .loader = brdlist_loader,
        .header = brdlist_header,
        .footer = brdlist_footer,
        .renderer = brdlist_renderer,
        .empty_renderer = brdlist_empty_renderer,
        .cursor = brdlist_cursor,
    };

    psb_main(&psbctx);
    num = psbctx.cmd.curr;

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

