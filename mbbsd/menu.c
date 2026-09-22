#include "bbs.h"
#include "psb.h"

// UNREGONLY 改為由 BASIC 來判斷是否為 guest.

#define CheckMenuPerm(x) \
    ( (x == MENU_UNREGONLY)? \
      ((!HasUserPerm(PERM_BASIC) || HasUserPerm(PERM_LOGINOK))?0:1) :\
	((!x) ? 1 :  \
         ((x & PERM_LOGINOK) ? HasBasicUserPerm(x) : HasUserPerm(x))))

/* help & menu processring */
extern char    *boardprefix;
extern struct utmpfile_t *utmpshm;

static const char *title_tail_msgs[] = {
    "看板",
    "系列",
    "文摘",
};
static const char *title_tail_attrs[] = {
    ANSI_COLOR(37),
    ANSI_COLOR(32),
    ANSI_COLOR(36),
};
enum {
    TITLE_TAIL_BOARD = 0,
    TITLE_TAIL_SELECT,
    TITLE_TAIL_DIGEST,
};

// 由於歷史因素，這裡會出現三種編號:
// MODE (定義於 modes.h)    是 BBS 對各種功能在 utmp 的編號 (var.c 要加字串)
// Menu Index (M_*)	    是 menu.c 內部分辨選單要對應哪個 mode 的 index
// AdBanner Index	    是動態看版要顯示什麼的值
// 從前這是用兩個 mode map 來轉換的 (令人看得滿頭霧水)
// 重整後 Menu Index 跟 AdBanner Index 合一，請見下面的說明
///////////////////////////////////////////////////////////////////////
// AdBanner (SHM->notes) 前幾筆是 Note 板精華區「<系統> 動態看板」(SYS)
// 目錄下的文章，所以編排 Menu (M_*) 時要照其順序：
// 精華區編號     => Menu Index => MODE
// (AdBannerIndex)
// ====================================
// 00離站畫面     =>  M_GOODBYE
// 01主選單       =>  M_MMENU   => MMENU
// 02系統維護區   =>  M_ADMIN   => ADMIN
// 03私人信件區   =>  M_MAIL    => MAIL
// 04休閒聊天區   =>  M_TMENU   => TMENU
// 05個人設定區   =>  M_UMENU   => UMENU
// 06系統工具區   =>  M_XMENU   => XMENU
// 07娛樂與休閒   =>  M_PMENU   => PMENU
// 08Ｐtt搜尋器   =>  M_SREG    => SREG
// 09Ｐtt量販店   =>  M_PSALE   => PSALE
// 10Ｐtt遊樂場   =>  M_AMUSE   => AMUSE
// 11Ｐtt棋院     =>  M_CHC     => CHC
// 12特別名單     =>  M_NMENU   => NMENU
///////////////////////////////////////////////////////////////////////
// 由於 MODE 與 menu 的順序現在已不一致 (最早可能是一致的)，所以中間的
// 轉換是靠 menu_mode_map 來處理。
// 要定義新 Menu 時，請在 M_MENU_MAX 之前加入新值，並在 menu_mode_map
// 加入對應的 MODE 值。 另外，在 Notes 下也要增加對應的 AdBanner 圖片
// 若不想加圖片則要修改 N_SYSADBANNER
///////////////////////////////////////////////////////////////////////

enum {
    M_GOODBYE=0,
    M_MMENU,	 M_ADMIN, M_MAIL, M_TMENU,
    M_UMENU,     M_XMENU, M_PMENU,M_SREG,
    M_PSALE,	 M_AMUSE, M_CHC,  M_NMENU,

    M_MENU_MAX,			// 這是 menu (M_*) 的最大值
    N_SYSADBANNER = M_MENU_MAX, // 定義 M_* 到多少有對應的 ADBANNER
    M_MENU_REFRESH= -1,		// 系統用不到的 index 值 (可顯示其它活動與點歌)
};

static const int menu_mode_map[M_MENU_MAX] = {
    0,
    MMENU,	ADMIN,	MAIL,	TMENU,
    UMENU,	XMENU,	PMENU,	SREG,
    PSALE,	AMUSE,	CHC,	NMENU
};

typedef struct menuitem_t {
    int     (*cmdfunc)();
    int     level;
    const char *desc;                /* hotkey/description */
    const struct menuitem_t *submenu;
    int     mode;                    /* menu index (M_*) */
    const char *status;              /* short caption for bottom status bar */
    const char *title;               /* long title for top header */
    int     default_enter;           /* default hotkey on entering menu */
    int     default_exit;            /* default hotkey on KEY_LEFT/exit */
} menuitem_t;

///////////////////////////////////////////////////////////////////////

void
showtitle(const char *title, const char *mid)
{
    int tail_type;
    int is_currboard_special = 0;
    char mid_buf[64];

    const char *high_attr = HasUserFlag(UF_CURSOR_STANDOUT) ? ANSI_COLOR(41) :
	ANSI_COLOR(41;5);

    /* prepare mid */
#ifdef DEBUG
    snprintf(mid_buf, sizeof(mid_buf), "%s  current pid: %6d  ",
	     high_attr, getpid());
    mid = mid_buf;
#else
    if (ISNEWMAIL(currutmp)) {
	snprintf(mid_buf, sizeof(mid_buf), "%s    你有新信件    ", high_attr);
	mid = mid_buf;
    } else if ( HasUserPerm(PERM_ACCTREG) ) {
	// TODO cache this value?
	int nreg = regform_estimate_queuesize();
	if(nreg > 100)
	{
	    nreg -= (nreg % 10);
	    snprintf(mid_buf, sizeof(mid_buf), "%s  超過 %03d 篇未審核  ",
		     high_attr, nreg);
	    mid = mid_buf;
	}
    }
#endif

    /* prepare tail */
    if (currmode & MODE_SELECT)
	tail_type = TITLE_TAIL_SELECT;
    else if (currmode & MODE_DIGEST)
	tail_type = TITLE_TAIL_DIGEST;
    else
	tail_type = TITLE_TAIL_BOARD;

    if(currbid > 0)
    {
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	is_currboard_special = (
		(getbcache(currbid)->brdattr & BRD_HIDE) &&
		(getbcache(currbid)->brdattr & BRD_POSTMASK));
    }

    vs_header(title, mid, currboard[0] ?
	      TEMPFORMAT(64, "%s%s《%s%s%s》",
		 title_tail_attrs[tail_type],
		 title_tail_msgs[tail_type],
		 is_currboard_special ? ANSI_COLOR(32) : "",
		 currboard,
		 title_tail_attrs[tail_type]) : "");
}

static void
clear_main(void)
{
    // Keep the title (and bottom line) and clear the main UI.
    move(1, 0);
    clrtoln(b_lines - 1);
}

int TopBoards(void);

/* Ctrl-Z Anywhere Fast Switch, not ZG. */
static char zacmd = 0;

// ZA is waiting, hurry to the meeting stone!
int
ZA_Waiting(void)
{
    return (zacmd != 0);
}

void
ZA_Drop(void)
{
    zacmd = 0;
}

// Promp user our ZA bar and return for selection.
int
ZA_Select(void)
{
    int k;

    if (!is_login_ready ||
        !HasUserPerm(PERM_BASIC) ||
        HasUserPerm(PERM_VIOLATELAW))
        return 0;

    // TODO refresh status bar?
    vs_footer(VCLR_ZA_CAPTION " 快速切換 ",
	    " (b)文章列表 (c)分類 (t)熱門 (f)我的最愛 (m)信箱 (u)使用者名單");
    k = vkey();

    if (k < ' ' || k >= 'z') return 0;
    k = tolower(k);

    if(strchr("bcfmut", k) == NULL)
	return 0;

    zacmd = k;
    return 1;
}

// The ZA processor, only invoked in menu.
void
ZA_Enter(void)
{
    char cmd = zacmd;
    while (zacmd)
    {
	cmd = zacmd;
	zacmd = 0;

	// All ZA applets must check ZA_Waiting() at every stack of event loop.
	switch(cmd) {
	    case 'b':
		Read();
		break;
	    case 'c':
		Class();
		break;
	    case 't':
		TopBoards();
		break;
	    case 'f':
		Favorite();
		break;
	    case 'm':
		m_read();
		break;
	    case 'u':
		t_users();
		break;
	}
	// if user exit with new ZA assignment,
	// direct enter in next loop.
    }
}

/* 動畫處理 */
#define FILMROW 11
static unsigned short menu_row = 12;
static unsigned short menu_column = 20;

#ifdef EXP_ALERT_ADBANNER_USONG
static int
decide_menu_row(const menuitem_t *p) {
    if ((p[0].level && !HasUserPerm(p[0].level)) &&
        HasUserFlag(UF_ADBANNER_USONG) &&
        HasUserFlag(UF_ADBANNER)) {
        return menu_row + 1;
    }

    return menu_row;
}
#else
# define decide_menu_row(x) (menu_row)
#endif

void
show_status(void)
{
    struct tm      ptime;
    static const char * const myweek[] = {
	"日", "一", "二", "三", "四", "五", "六"
    };

    localtime4_r(&now, &ptime);
    move(b_lines, 0);
    // Length: timer =15, today=14, online=8+N, me=5+12,
    // pager=10, help=7
    vbarlr(TEMPFORMAT(ANSILINELEN, ANSI_COLOR(34;46) "%d/%d周%s %d:%02d"
	  ANSI_COLOR(1;33;45) "%-14s"
	  ANSI_COLOR(30;47) " 線上" ANSI_COLOR(31)
	  "%d" ANSI_COLOR(30) "人,我是" ANSI_COLOR(31) "%s"
          ANSI_COLOR(30) ",呼叫器" ANSI_COLOR(0;34;47) "%s", ptime.tm_mon + 1, ptime.tm_mday, myweek[ptime.tm_wday],
	  ptime.tm_hour, ptime.tm_min, SHM->today_is,
	  SHM->UTMPnumber, cuser.userid,
	  str_pager_modes[currutmp->pager % PAGER_MODES]), ANSI_COLOR(31) "(h)" ANSI_COLOR(30) "說明");
}

/*
 * current caller of adbanner:
 *   xyz.c:   adbanner_goodbye();   // logout
 *   menu.c:  adbanner(cmdmode);    // ...
 *   board.c: adbanner(0);	    // 後來變在 board.c 裡自己處理(應該是那隻魚)
 */

void
adbanner_goodbye()
{
    adbanner(M_GOODBYE);
}

void
adbanner(int menu_index)
{
    int i = menu_index;

    // don't show if stat in class or user wants to skip adbanners
    if (currstat == CLASS || !(HasUserFlag(UF_ADBANNER)))
	return;

    // also prevent SHM busy status
    if (SHM->Pbusystate || SHM->last_film <= 0)
	return;

    if (    i != M_MENU_REFRESH &&
	    i >= 0		&&
	    i <  N_SYSADBANNER  &&
	    i <= SHM->last_film)
    {
	// use system menu - i
    } else {
	// To display ADBANNERs in slide show mode.
	// Since menu is updated per hour, the total presentation time
	// should be less than one hour. 60*60/MAX_ADBANNER[500]=7 (seconds).
	// @ Note: 60 * 60 / MAX_ADBANNER =3600/MAX_ADBANNER = "how many seconds
	// can one ADBANNER to display" to slide through every banners in one hour.
	// @ now / (3600 / MAx_ADBANNER) means "get the index of which to show".
	// syncnow();

	const int slideshow_duration = 3600 / MAX_ADBANNER,
		  slideshow_index    = now  / slideshow_duration;

	// index range: 0 =>[system] => N_SYSADBANNER    => [user esong] =>
	//              last_usong   => [advertisements] => last_film
	int valid_usong_range = (SHM->last_usong > N_SYSADBANNER &&
				 SHM->last_usong < SHM->last_film);

	if (SHM->last_film > N_SYSADBANNER) {
	    if (HasUserFlag(UF_ADBANNER_USONG) || !valid_usong_range)
		i = N_SYSADBANNER +       slideshow_index % (SHM->last_film+1-N_SYSADBANNER);
	    else
		i = SHM->last_usong + 1 + slideshow_index % (SHM->last_film - SHM->last_usong);
	}
	else
	    i = 0; // SHM->last_film;
    }

    // make it safe!
    i %= MAX_ADBANNER;

    move(1, 0);
    clrtoln(1 + FILMROW);	/* 清掉上次的 */
    const char *note_ptr = TEMP_STORAGE_TO_MB_SZ(sizeof(SHM->notes[0]), SHM->notes[i]);
#ifdef LARGETERM_CENTER_MENU
    out_lines(note_ptr, 11, (t_columns - 80)/2);	/* 只印11行就好 */
#else
    out_lines(note_ptr, 11, 0);	/* 只印11行就好 */
#endif
    outs(ANSI_RESET);
#ifdef DEBUG
    // XXX piaip test
    move(FILMROW, 0); prints(" [ %d ] ", i);
#endif
}

static void domenu(const menuitem_t *menu);

typedef struct {
    int menu_index;
    int cmdmode;
    const char *title;
    const char *status;
    const menuitem_t *menu;
    const menuitem_t *cmdtable;
    int table_max;
    int target_table_idx;
    bool is_refresh;
} menu_ctx_t;

static int
menu_first_permitted_item(const menuitem_t cmdtable[], int table_max)
{
    for (int i = 0; i <= table_max && cmdtable[i].desc; i++) {
        if (CheckMenuPerm(cmdtable[i].level))
            return i;
    }
    return -1;
}

static int
menu_find_item_by_key(const menuitem_t cmdtable[], int table_max, int key)
{
    int target = toupper((unsigned char)key);
    for (int i = 0; i <= table_max && cmdtable[i].desc; i++) {
        if (toupper((unsigned char)cmdtable[i].desc[0]) == target) {
            if (CheckMenuPerm(cmdtable[i].level))
                return i;
            return menu_first_permitted_item(cmdtable, table_max);
        }
    }
    return -1;
}

static int
menu_pos_to_table_idx(const menuitem_t cmdtable[], int table_max, int pos)
{
    int p = -1;
    for (int i = 0; i <= table_max && cmdtable[i].desc; i++) {
        if (CheckMenuPerm(cmdtable[i].level)) {
            if (++p == pos)
                return i;
        }
    }
    return -1;
}

static int
menu_table_idx_to_pos(const menuitem_t cmdtable[], int table_idx)
{
    int p = -1;
    for (int i = 0; i <= table_idx && cmdtable[i].desc; i++) {
        if (CheckMenuPerm(cmdtable[i].level))
            p++;
    }
    return p >= 0 ? p : 0;
}

static int
menu_header(PSB_CTX *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->cmd.priv;
    showtitle(cx->title, BBSNAME);
    adbanner(cx->is_refresh ? M_MENU_REFRESH : cx->menu_index);
    cx->is_refresh = true;

#ifdef EXP_ALERT_ADBANNER_USONG
    if ((cx->cmdtable[0].level && !HasUserPerm(cx->cmdtable[0].level)) &&
        HasUserFlag(UF_ADBANNER_USONG) &&
        HasUserFlag(UF_ADBANNER)) {
        int alert_column = menu_column;
        int row = menu_row;
        move(row, 0);
        vpad(t_columns - 2, "─");
        if (alert_column > 2)
            alert_column -= 2;
        alert_column -= alert_column % 2;
        move(row, alert_column);
        outs(" 上方為使用者心情點播留言區，不代表本站立場 ");
    }
#endif
    return 0;
}

static int
menu_footer(PSB_CTX *ctx GCC_UNUSED)
{
    show_status();
    return 0;
}

static int
menu_renderer(int idx, PSB_CTX *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->cmd.priv;
    int table_idx = menu_pos_to_table_idx(cx->cmdtable, cx->table_max, idx);
    if (table_idx < 0)
        return 0;
    const char *s = cx->cmdtable[table_idx].desc;
    if (!s)
        return 0;
    prints("%*s  (%s%c" ANSI_RESET ")%s",
           menu_column, "",
           ANSI_COLOR(1;36), s[0], s + 1);
    return 0;
}

static int
menu_cursor(int y, PSB_CTX *ctx GCC_UNUSED)
{
    cursor_show(y, menu_column);
    return 0;
}

static int
menu_loader(PSB_CTX *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->cmd.priv;
#ifdef LARGETERM_CENTER_MENU
    menu_column = (t_columns - 40) / 2;
    menu_row = 12 + (t_lines - 24) / 2;
#endif
    ctx->header_lines = decide_menu_row(cx->cmdtable);

    int permitted = 0;
    for (int i = 0; i <= cx->table_max && cx->cmdtable[i].desc; i++) {
        if (CheckMenuPerm(cx->cmdtable[i].level))
            permitted++;
    }
    ctx->cmd.total = permitted;

    if (cx->target_table_idx >= 0) {
        if (cx->target_table_idx > cx->table_max ||
            !CheckMenuPerm(cx->cmdtable[cx->target_table_idx].level)) {
            int first = menu_first_permitted_item(cx->cmdtable, cx->table_max);
            if (first < 0) {
                ctx->cmd.quit = true;
                return 0;
            }
            cx->target_table_idx = first;
        }
        ctx->cmd.curr = menu_table_idx_to_pos(cx->cmdtable, cx->target_table_idx);
        cx->target_table_idx = -1;
    }
    return 0;
}

static int
menu_cmd_up(cmd_ctx_t *ctx)
{
    if (--ctx->curr < 0)
        ctx->curr = ctx->total - 1;
    return 0;
}

static int
menu_cmd_down(cmd_ctx_t *ctx)
{
    if (++ctx->curr >= ctx->total)
        ctx->curr = 0;
    return 0;
}

static int
menu_cmd_home(cmd_ctx_t *ctx)
{
    ctx->curr = 0;
    return 0;
}

static int
menu_cmd_end(cmd_ctx_t *ctx)
{
    ctx->curr = ctx->total - 1;
    return 0;
}

static int
menu_cmd_left(cmd_ctx_t *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->priv;
    if (cx->cmdmode == MAIL && chkmailbox()) {
        int idx = menu_find_item_by_key(cx->cmdtable, cx->table_max, 'R');
        if (idx >= 0)
            ctx->curr = menu_table_idx_to_pos(cx->cmdtable, idx);
    } else if (cx->menu->default_exit) {
        int idx = menu_find_item_by_key(cx->cmdtable, cx->table_max, cx->menu->default_exit);
        if (idx >= 0)
            ctx->curr = menu_table_idx_to_pos(cx->cmdtable, idx);
    } else {
        ctx->quit = true;
    }
    return 0;
}

static int
menu_cmd_enter(cmd_ctx_t *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->priv;
    int idx = menu_pos_to_table_idx(cx->cmdtable, cx->table_max, ctx->curr);
    if (idx < 0)
        return 0;
    int err;

    currstat = XMODE;
    if (cx->cmdtable[idx].cmdfunc != Goodbye)
        clear_main();

    if (cx->cmdtable[idx].submenu) {
        domenu(&cx->cmdtable[idx]);
        err = 0;
    } else {
        err = (*cx->cmdtable[idx].cmdfunc)();
    }
    if (err == QUIT) {
        ctx->quit = true;
        return 0;
    }
    currutmp->mode = currstat = cx->cmdmode;
    cx->target_table_idx = idx;

    if (err == XEASY) {
        refresh();
        sleep(1);
    } else if (err != XEASY + 1 || err == FULLUPDATE) {
        cx->is_refresh = true;
        ctx->reload = true;
    }
    return 0;
}

static int
menu_cmd_unread(cmd_ctx_t *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->priv;
    int idx = menu_pos_to_table_idx(cx->cmdtable, cx->table_max, ctx->curr);
    clear_main();
    New();
    currutmp->mode = currstat = cx->cmdmode;
    cx->target_table_idx = idx;
    cx->is_refresh = true;
    ctx->reload = true;
    return 0;
}

static int
menu_cmd_select_board(cmd_ctx_t *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->priv;
    int idx = menu_pos_to_table_idx(cx->cmdtable, cx->table_max, ctx->curr);
    ReadSelect();
    currutmp->mode = currstat = cx->cmdmode;
    cx->target_table_idx = idx;
    cx->is_refresh = true;
    ctx->reload = true;
    return 0;
}

static int
menu_cmd_read_board(cmd_ctx_t *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->priv;
    int idx = menu_pos_to_table_idx(cx->cmdtable, cx->table_max, ctx->curr);
    Read();
    currutmp->mode = currstat = cx->cmdmode;
    cx->target_table_idx = idx;
    cx->is_refresh = true;
    ctx->reload = true;
    return 0;
}

static void
menu_help(void)
{
    static const char * const col1[] = {
        "【 選單基本操作 】", NULL,
        "  上個選項",     "↑",
        "  下個選項",     "↓",
        "  執行選項",     "→Enter",
        "  回到前一層",   "← e",
        "  最上方選項",   "Home PgUp",
        "  最下方選項",   "End  PgDn",
        "  跳到選項",     "(選項字母)",
        "  按鍵說明",     "h",
        "", "",
        "【 快捷鍵 】",   NULL,
        "  選擇看板",     "s",
        "  進入看板",     "r",
        "  未讀文章",     "Ctrl-Y",
        "  回覆訊息",     "Ctrl-R",
        "  使用者名單",   "Ctrl-U",
        "  隨處切換(ZA)", "Ctrl-Z",
        NULL,
    };
    const char * const *p[] = { col1 };

    show_help_table(p, ARRAY_SIZE(p), "選單按鍵說明");
}

static int
menu_cmd_help(cmd_ctx_t *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->priv;
    int idx = menu_pos_to_table_idx(cx->cmdtable, cx->table_max, ctx->curr);
    menu_help();
    cx->target_table_idx = idx;
    cx->is_refresh = true;
    ctx->reload = true;
    return 0;
}

static const cmd_t menu_nav_cmds[] = {
    { 'h', "按鍵說明", "顯示選單按鍵說明", menu_cmd_help, 0, CMD_PRIO_NONE },
    { 'H', NULL, NULL, menu_cmd_help, 0, CMD_PRIO_NONE },
    { KEY_UP, "上個選項", "移動至上一個選單項目", menu_cmd_up, 0, CMD_PRIO_NAV, true },
    { KEY_DOWN, "下個選項", "移動至下一個選單項目", menu_cmd_down, 0, CMD_PRIO_NAV, true },
    { KEY_RIGHT, "執行選項", "執行目前選取的選單項目", menu_cmd_enter, 0, CMD_PRIO_NORM, true },
    { KEY_ENTER, NULL, NULL, menu_cmd_enter, 0, CMD_PRIO_NONE, true },
    { KEY_LEFT, "回到前一層", "離開目前選單或回到上一層", menu_cmd_left, 0, CMD_PRIO_MAX },
    { 'e', NULL, NULL, menu_cmd_left, 0, CMD_PRIO_NONE },
    { 'E', NULL, NULL, menu_cmd_left, 0, CMD_PRIO_NONE },
    { KEY_HOME, "最上方選項", "移動至第一個選單項目", menu_cmd_home, 0, CMD_PRIO_NAV, true },
    { KEY_PGUP, NULL, NULL, menu_cmd_home, 0, CMD_PRIO_NONE, true },
    { KEY_END, "最下方選項", "移動至最後一個選單項目", menu_cmd_end, 0, CMD_PRIO_NAV, true },
    { KEY_PGDN, NULL, NULL, menu_cmd_end, 0, CMD_PRIO_NONE, true },
    { Ctrl('Y'), "未讀文章", "檢視所有未讀文章", menu_cmd_unread, 0, CMD_PRIO_NORM },
    { Ctrl('N'), NULL, NULL, menu_cmd_unread, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t menu_board_shortcut_cmds[] = {
    { 's', "選擇看板", "搜尋並切換至指定看板", menu_cmd_select_board, 0, CMD_PRIO_NORM },
    { 'r', "進入看板", "進入目前看板閱\讀文章", menu_cmd_read_board, 0, CMD_PRIO_NORM },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static const cmd_t menu_empty_cmds[] = {
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static int
menu_on_key(PSB_CTX *ctx)
{
    menu_ctx_t *cx = (menu_ctx_t *)ctx->cmd.priv;
    int key = ctx->cmd.key;
    if (key == 'e' || key == 'E')
        return PSB_NA;
    if ((key == 's' || key == 'r') &&
        (cx->cmdmode == MMENU || cx->cmdmode == TMENU || cx->cmdmode == XMENU))
        return PSB_NA;
    int idx = menu_find_item_by_key(cx->cmdtable, cx->table_max, key);
    if (idx >= 0) {
        ctx->cmd.curr = menu_table_idx_to_pos(cx->cmdtable, idx);
        return 0;
    }
    return PSB_NA;
}

static const char *
extract_menu_title(const char *desc, char *buf, size_t size)
{
    const char *start, *end;

    if (!desc)
        return "";

    start = mbs_strstr(desc, "【");
    if (start) {
        start += strlen("【");
        end = mbs_strstr(start, "】");
        if (!end)
            end = start + strlen(start);
    } else {
        start = desc;
        while (*start && isascii((unsigned char)*start))
            start++;
        end = start + strlen(start);
    }

    while (start < end && isspace((unsigned char)*start))
        start++;
    while (end > start && isspace((unsigned char)*(end - 1)))
        end--;

    if ((size_t)(end - start) >= size)
        end = start + size - 1;
    memcpy(buf, start, end - start);
    buf[end - start] = '\0';
    return buf;
}

static void
domenu(const menuitem_t *menu)
{
    const menuitem_t *cmdtable = menu->submenu;
    int menu_index = menu->mode;
    int cmd = menu->default_enter;
    char title_buf[STRLEN];
    const char *title = menu->title;
    int cmdmode;
    int total = 0;
    bool has_board_shortcuts;

    if (!title)
        title = extract_menu_title(menu->desc, title_buf, sizeof(title_buf));


    assert(0 <= menu_index && menu_index < M_MENU_MAX);
    cmdmode = menu_mode_map[menu_index];
    has_board_shortcuts = (cmdmode == MMENU || cmdmode == TMENU || cmdmode == XMENU);

    setutmpmode(cmdmode);

    while (cmdtable[total].desc)
        total++;
    total--;
    if (total < 0)
        return;

    int init_idx = menu_find_item_by_key(cmdtable, total, cmd);
    if (init_idx < 0)
        init_idx = menu_first_permitted_item(cmdtable, total);
    if (init_idx < 0)
        return;

    if (ZA_Waiting()) {
        ZA_Enter();
        currstat = cmdmode;
    }

    menu_ctx_t cx = {
        .menu_index = menu_index,
        .cmdmode = cmdmode,
        .title = title,
        .status = title,
        .menu = menu,
        .cmdtable = cmdtable,
        .table_max = total,
        .target_table_idx = init_idx,
        .is_refresh = false,
    };
    cmd_layer_t layers[] = {
        { menu_nav_cmds, &cx },
        { has_board_shortcuts ? menu_board_shortcut_cmds : menu_empty_cmds, &cx },
        { bbs_global_cmds, NULL },
        { NULL, NULL }
    };

    PSB_CTX psbctx = {
        .cmd = {
            .curr = menu_table_idx_to_pos(cmdtable, init_idx),
            .priv = &cx,
            .caption = title,
        },
        .header_lines = decide_menu_row(cmdtable),
        .footer_lines = 1,
        .layers = layers,
        .loader = menu_loader,
        .header = menu_header,
        .footer = menu_footer,
        .renderer = menu_renderer,
        .cursor = menu_cursor,
        .on_key = menu_on_key,
    };

    while (1) {
        psb_main(&psbctx);
        if (psbctx.cmd.key == EOF)
            abort_bbs(0);
        if (ZA_Waiting()) {
            ZA_Enter();
            currutmp->mode = currstat = cmdmode;
            cx.is_refresh = true;
            psbctx.cmd.quit = false;
            psbctx.cmd.reload = true;
            continue;
        }
        return;
    }
}
/* INDENT OFF */

static int
view_user_money_log() {
    char userid[IDLEN+1];
    char fpath[PATHLEN];

    vs_hdr("檢視使用者交易記錄");
    usercomplete("請輸入要檢視的ID: ", userid);
    if (!is_validuserid(userid))
        return 0;
    sethomefile(fpath, userid, FN_RECENTPAY);
    if (more(fpath, YEA) < 0)
        vmsgf("使用者 %s 無最近交易記錄", userid);
    return 0;
}

static int
view_user_login_log() {
    char userid[IDLEN+1];
    char fpath[PATHLEN];

    vs_hdr("檢視使用者最近上線記錄");
    usercomplete("請輸入要檢視的ID: ", userid);
    if (!is_validuserid(userid))
        return 0;
    sethomefile(fpath, userid, FN_RECENTLOGIN);
    if (more(fpath, YEA) < 0)
        vmsgf("使用者 %s 無最近上線記錄", userid);
    return 0;
}

static int
view_security_log() {
    char userid[IDLEN+1];
    char fpath[PATHLEN];

    vs_hdr("檢視使用者帳號安全記錄");
    usercomplete("請輸入要檢視的ID: ", userid);
    if (!is_validuserid(userid))
        return 0;
    sethomefile(fpath, userid, FN_USERSECURITY);
    if (more(fpath, YEA) < 0)
        vmsgf("使用者 %s 無記錄", userid);
    return 0;
}


// ----------------------------------------------------------- MENU DEFINITION
// 注意每個 menu 最多不能同時顯示超過 11 項 (80x24 標準大小的限制)

static const menuitem_t m_admin_money[] = {
    {view_user_money_log, PERM_SYSOP|PERM_ACCOUNTS,
                                                "View Log      檢視交易記錄"},
    {give_money, PERM_SYSOP|PERM_VIEWSYSOP,	"Givemoney     紅包雞"},
    {NULL, 0, NULL}
};

static const menuitem_t m_admin_user[] = {
    {view_user_money_log, PERM_SYSOP|PERM_ACCOUNTS,
                                        "Money Log      最近交易記錄"},
    {view_user_login_log, PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD,
                                        "OLogin Log     最近上線記錄"},
    {view_security_log, PERM_SYSOP|PERM_ACCOUNTS,
                                        "Security Log   帳號安全記錄"},
    {u_list, PERM_SYSOP,		"Users List     列出註冊名單"},
    {search_user_bybakpwd, PERM_SYSOP|PERM_ACCOUNTS,
                                        "DOld User data 查閱\備份使用者資料"},
    {u_admin_disable_2fa, PERM_SYSOP|PERM_ACCOUNTS,
                                        "2FA Disable    強制關閉 2FA"},
    {NULL, 0, NULL}
};

/* administrator's maintain menu */
static const menuitem_t adminlist[] = {
    {m_user, PERM_SYSOP,		"User          使用者資料"},
    {m_board, PERM_BOARD,		"Board         設定看板"},
    {m_register,
	PERM_ACCOUNTS|PERM_ACCTREG,	"Register      審核註冊表單"},
    {x_file, PERM_SYSOP|PERM_VIEWSYSOP,	"Xfile         編輯系統檔案"},
    {
	.submenu = m_admin_money,
	.level = PERM_SYSOP|PERM_ACCOUNTS|PERM_VIEWSYSOP,
	.desc = "Money         【" MONEYNAME "相關】",
	.mode = M_XMENU,
	.status = "金錢管理",
	.title = "金錢相關管理",
	.default_enter = 'V',
    },
    {
	.submenu = m_admin_user,
	.level = PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD|PERM_POLICE_MAN,
	.desc = "LUser Log     【使用者資料記錄】",
	.mode = M_XMENU,
	.status = "記錄管理",
	.title = "使用者記錄管理",
	.default_enter = 'O',
    },
    {search_user_bypwd,
	PERM_ACCOUNTS|PERM_POLICE_MAN,	"Search User    特殊搜尋使用者"},
#ifdef USE_VERIFYDB
    {verifydb_admin_search_display,
	PERM_ACCOUNTS,			"Verify Search  搜尋認證資料庫"},
#endif
    {NULL, 0, NULL}
};

/* mail menu */
static const menuitem_t maillist[] = {
    {m_read, PERM_READMAIL,     "Read          我的信箱"},
    {m_send, PERM_LOGINOK,      "Send          站內寄信"},
    {mail_list, PERM_LOGINOK,   "Mail List     群組寄信"},
    {setforward, PERM_LOGINOK,  "Forward       設定信箱自動轉寄" },
    {mail_mbox, PERM_INTERNET,  "Zip UserHome  把所有私人資料打包回去"},
    {built_mail_index,
	PERM_LOGINOK,		"Savemail      重建信箱索引"},
#ifdef USE_MAIL_ACCOUNT_SYSOP
    {mail_account_sysop, 0,     "Contact AM    寄信給帳號站長"},
#endif
    {NULL, 0, NULL}
};

static const menuitem_t angelmenu[] GCC_UNUSED = {
    {a_angelmsg, PERM_ANGEL,"Leave message 留言給小主人"},
    {a_angelmsg2,PERM_ANGEL,"Call screen   呼叫畫面個性留言"},
    {angel_check_master,PERM_ANGEL,
                            "Master check  查詢小主人狀態"},
    // Cannot use R because r is reserved for Read/Mail due to TMENU.
    {a_angelreport, 0,      "PReport       線上天使狀態報告"},
    {NULL, 0, NULL}
};



/* Talk menu */
static const menuitem_t talklist[] = {
    {t_users, 0,            "Users         線上使用者列表"},
    {t_query, 0,            "Query         查詢網友"},
    // PERM_PAGE - 水球都要 PERM_LOGIN 了
    // 沒道理可以 talk 不能水球。
    {t_talk, PERM_LOGINOK,  "Talk          找人聊聊"},
    // PERM_CHAT 非 login 也有，會有人用此吵別人。
    {t_chat, PERM_LOGINOK,  "Chat          多人聊天室"},
    {t_qchicken, 0,         "Watch Pet     查詢寵物"},
#ifdef PLAY_ANGEL
    {a_changeangel,
	PERM_LOGINOK,	    "AChange Angel 更換小天使"},
    {
	.submenu = angelmenu,
	.level = PERM_ANGEL|PERM_SYSOP,
	.desc = "BAngel Beats! 【天使公會】",
	.mode = M_TMENU,
	.status = "天使公會",
	.title = "Angel Beats! 天使公會",
	.default_enter = 'L',
    },
#endif
    {pager_show_log, 0,          "Display       顯示上幾次熱訊"},
    {NULL, 0, NULL}
};

/* name menu */
static int t_aloha() {
    friend_edit(FRIEND_ALOHA);
    return 0;
}

static int t_special() {
    friend_edit(FRIEND_SPECIAL);
    return 0;
}

static const menuitem_t namelist[] = {
    {t_override, PERM_LOGINOK,"OverRide      好友名單"},
    {t_reject, PERM_LOGINOK,  "Black         壞人名單"},
    {t_aloha,PERM_LOGINOK,    "ALOHA         上站通知名單"},
    {t_special,PERM_LOGINOK,  "Special       其他特別名單"},
    {NULL, 0, NULL}
};

static int u_view_recentlogin()
{
    char fn[PATHLEN];
    setuserfile(fn, FN_RECENTLOGIN);
    return more(fn, YEA);
}

#ifdef USE_RECENTPAY
static int u_view_recentpay()
{
    char fn[PATHLEN];
    clear();
    mvouts(10, 5, "注意: 此處內容僅供參考，實際" MONEYNAME
                        "異動以站方內部資料為準");
    pressanykey();
    setuserfile(fn, FN_RECENTPAY);
    return more(fn, YEA);
}
#endif

static int u_view_security()
{
    char fn[PATHLEN];
    setuserfile(fn, FN_USERSECURITY);
    return more(fn, YEA);
}

static const menuitem_t myfilelist[] = {
    {u_editplan,    PERM_LOGINOK,   "QueryEdit     編輯名片檔"},
    {u_editsig,	    PERM_LOGINOK,   "Signature     編輯簽名檔"},
    {NULL, 0, NULL}
};

static const menuitem_t myuserlog[] = {
    {u_view_recentlogin, 0,   "LRecent Login  最近上站記錄"},
#ifdef USE_RECENTPAY
    {u_view_recentpay,   0,   "PRecent Pay    最近交易記錄"},
#endif
    {u_view_security,    0,   "Security       帳號安全記錄"},
    {NULL, 0, NULL}
};



void Customize(); // user.c

static int
u_customize()
{
    Customize();
    return 0;
}


/* User menu */
static const menuitem_t userlist[] = {
    {u_customize,   PERM_BASIC,	    "UCustomize    個人化設定"},
    {u_info,	    PERM_BASIC,     "Info          設定個人資料與密碼"},
    {u_loginview,   PERM_BASIC,     "VLogin View   選擇進站畫面"},
    {
	.submenu = myfilelist,
	.level = PERM_LOGINOK,
	.desc = "My Files      【個人檔案】 (名片,簽名檔...)",
	.mode = M_UMENU,
	.default_enter = 'Q',
    },
    {
	.submenu = myuserlog,
	.level = PERM_LOGINOK,
	.desc = "LMy Logs      【個人記錄】 (最近上線...)",
	.mode = M_UMENU,
	.default_enter = 'L',
    },
    {u_register,    PERM_BASIC,     "Register      新增帳號認證"},
    {u_setup_2fa,   PERM_BASIC,     "2FA           設定兩階段認證"},
#ifdef ASSESS
    {u_cancelbadpost,PERM_LOGINOK,  "Bye BadPost   申請刪除退文"},
#endif // ASSESS
    {NULL, 0, NULL}
};

#ifdef HAVE_USERAGREEMENT
static int
x_agreement(void)
{
    more(HAVE_USERAGREEMENT, YEA);
    return 0;
}
#endif



#ifdef HAVE_INFO
static int
x_program(void)
{
    more("etc/version", YEA);
    return 0;
}
#endif

#ifdef HAVE_LICENSE
static int
x_gpl(void)
{
    more("etc/GPL", YEA);
    return 0;
}
#endif

#ifdef HAVE_SYSUPDATES
static int
x_sys_updates(void)
{
    more("etc/sysupdates", YEA);
    return 0;
}
#endif

#ifdef DEBUG
int _debug_reportstruct()
{
    clear();
    prints("boardheader_t:\t%d\n", sizeof(boardheader_t));
    prints("fileheader_t:\t%d\n", sizeof(fileheader_t));
    prints("userinfo_t:\t%d\n", sizeof(userinfo_t));
    prints("screenline_t:\t%d\n", sizeof(screenline_t));
    prints("SHM_t:\t%d\n", sizeof(SHM_t));
    prints("userec_t:\t%d\n", sizeof(userec_t));
    pressanykey();
    return 0;
}

#endif

/* XYZ tool sub menu */
static const menuitem_t m_xyz_hot[] = {
    {x_week, 0,      "Week          《本週五十大熱門話題》"},
    {x_issue, 0,     "Issue         《今日十大熱門話題》"},
    {x_boardman,0,   "Man Boards    《看板精華區排行榜》"},
    {NULL, 0, NULL}
};

/* XYZ tool sub menu */
static const menuitem_t m_xyz_user[] = {
    {x_user100 ,0,   "Users         《使用者百大排行榜》"},
    {topsong,PERM_LOGINOK,
	             "GTop Songs    《使用者心情點播排行》"},
    {x_today, 0,     "Today         《今日上線人次統計》"},
    {x_yesterday, 0, "Yesterday     《昨日上線人次統計》"},
    {NULL, 0, NULL}
};



/* XYZ tool menu */
static const menuitem_t xyzlist[] = {
    {
	.submenu = m_xyz_hot,
	.desc = "THot Topics   【熱門話題與看板】",
	.mode = M_XMENU,
	.status = "熱門話題",
	.default_enter = 'W',
    },
    {
	.submenu = m_xyz_user,
	.desc = "Users         【使用者相關統計】",
	.mode = M_XMENU,
	.status = "統計資訊",
	.title = "使用者統計資訊",
	.default_enter = 'U',
    },
#ifndef DEBUG
    /* All these are useless in debug mode. */
#ifdef HAVE_USERAGREEMENT
    {x_agreement,0,  "Agreement     《本站使用者條款》"},
#endif
#ifdef  HAVE_LICENSE
    {x_gpl, 0,       "ILicense       GNU 使用執照"},
#endif
#ifdef HAVE_INFO
    {x_program, 0,   "Program       本程式之版本與版權宣告"},
#endif
    {x_history, 0,   "History       《我們的成長》"},
    {x_login,0,      "System        《系統重要公告》"},
#ifdef HAVE_SYSUPDATES
    {x_sys_updates,0,"LUpdates      《本站系統程式更新紀錄》"},
#endif

#else // !DEBUG
    {_debug_reportstruct, 0,
	    	     "ReportStruct  報告各種結構的大小"},
#endif // !DEBUG

    {p_sysinfo, 0,   "Xinfo         《查看系統資訊》"},
    {NULL, 0, NULL}
};

/* Ptt money menu */
static const menuitem_t moneylist[] = {
    {p_give, 0,         "0Give        給其他人" MONEYNAME},
    {save_violatelaw, 0,"1ViolateLaw  繳罰單"},
    {p_from, 0,         "2From        暫時修改故鄉"},
    {ordersong,0,       "3OSong       心情點播機"},
    {NULL, 0, NULL}
};

static const menuitem_t chesslist[] = {
    {chc_main,         PERM_LOGINOK, "1CChessFight    象棋邀局 "},
    {gomoku_main,      PERM_LOGINOK, "2GomokuFight   五子棋邀局"},
    {NULL, 0, NULL}
};

/* Ptt Play menu */
static const menuitem_t playlist[] = {
    {
	.submenu = moneylist,
	.level = PERM_LOGINOK,
	.desc = "Pay         【 " BBSMNAME2 "量販店 】",
	.mode = M_PSALE,
	.status = "量販商店",
	.default_enter = '0',
    },
    {chicken_main, PERM_LOGINOK,
			     "Chicken        " BBSMNAME2 "養雞場"},
    {ticket_main, PERM_LOGINOK,
                             "Gamble         " BBSMNAME2 "彩券"},
    {
	.submenu = chesslist,
	.level = PERM_LOGINOK,
	.desc = "BChess      【 " BBSMNAME2 "棋院   】",
	.mode = M_CHC,
	.status = "休閒棋院",
	.default_enter = '1',
    },
    {NULL, 0, NULL}
};

static const menuitem_t cmdlist[] = {
    {
	.submenu = adminlist,
	.level = PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD|PERM_VIEWSYSOP|PERM_ACCTREG|PERM_POLICE_MAN,
	.desc = "0Admin       【 系統維護區 】",
	.mode = M_ADMIN,
	.status = "系統維護",
	.title = "系統維護",
	.default_enter = 'L',
    },
    {Announce,	0,		"Announce     【 精華公佈欄 】"},
    {Favorite,	0,		"Favorite     【 我 的 最愛 】"},
    {Class,	0,		"Class        【 分組討論區 】"},
    // TODO 目前很多人被停權時會變成 -R-1-3 (PERM_LOGINOK, PERM_VIOLATELAW,
    // PERM_NOREGCODE) 沒有 PERM_READMAIL，但這樣麻煩的是他們就搞不懂發生什麼事
    {
	.submenu = maillist,
	.level = PERM_BASIC,
	.desc = "Mail         【 私人信件區 】",
	.mode = M_MAIL,
	.status = "電子郵件",
	.title = "電子郵件",
	.default_enter = 'R',
    },
    // 有些 bot 喜歡整天 query online accounts, 所以聊天改為 LOGINOK
    {
	.submenu = talklist,
	.level = PERM_LOGINOK,
	.desc = "Talk         【 休閒聊天區 】",
	.mode = M_TMENU,
	.status = "聊天說話",
	.title = "聊天說話",
	.default_enter = 'U',
    },
    {
	.submenu = userlist,
	.level = PERM_BASIC,
	.desc = "User         【 個人設定區 】",
	.mode = M_UMENU,
	.status = "個人設定",
	.title = "個人設定",
	.default_enter = 'U',
    },
    {
	.submenu = xyzlist,
	.desc = "Xyz          【 系統資訊區 】",
	.mode = M_XMENU,
	.status = "工具程式",
	.title = "工具程式",
	.default_enter = 'T',
    },
    {
	.submenu = playlist,
	.level = PERM_LOGINOK,
	.desc = "Play         【 娛樂與休閒 】",
	.mode = M_PMENU,
	.status = "休閒遊樂",
	.title = "網路遊樂場",
	.default_enter = 'G',
    },
    {
	.submenu = namelist,
	.level = PERM_LOGINOK,
	.desc = "Namelist     【 編特別名單 】",
	.mode = M_NMENU,
	.status = "名單編輯",
	.title = "名單編輯",
	.default_enter = 'O',
    },
    {Goodbye, 	0, 		"Goodbye         離開，再見… "},
    {NULL, 	0, 		NULL}
};

int
main_menu(void)
{
    const menuitem_t menu = {
	.submenu = cmdlist,
	.mode = M_MMENU,
	.title = "主功\能表",
	.default_enter = ISNEWMAIL(currutmp) ? 'M' : 'C',
	.default_exit = 'G',
    };
    domenu(&menu);
    return 0;
}

