/* $Id$ */
#include "bbs.h"

// UNREGONLY 改為由 BASIC 來判斷是否為 guest.

#define CheckMenuPerm(x) \
    ( (x == MENU_UNREGONLY)? \
      ((!HasUserPerm(PERM_BASIC) || HasUserPerm(PERM_LOGINOK))?0:1) :\
	((x) ? HasUserPerm(x) : 1))

/* help & menu processring */
static int      refscreen = NA;
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

void
showtitle(const char *title, const char *mid)
{
    /* we have to...
     * - display title in left, cannot truncate.
     * - display mid message, cannot truncate
     * - display tail (board info), if possible.
     */
    int llen, rlen, mlen, mpos = 0;
    int pos = 0;
    int tail_type;
    const char *mid_attr = ANSI_COLOR(33);
    int is_currboard_special = 0;
    char buf[64];


    /* prepare mid */
#ifdef DEBUG
    {
	sprintf(buf, "  current pid: %6d  ", getpid());
	mid = buf; 
	mid_attr = ANSI_COLOR(41;5);
    }
#else
    if (ISNEWMAIL(currutmp)) {
	mid = "    你有新信件    ";
	mid_attr = ANSI_COLOR(41;5);
    } else if ( HasUserPerm(PERM_ACCTREG) ) {
	int nreg = regform_estimate_queuesize();
	if(nreg > 100)
	{
	    sprintf(buf, "  有 %03d 未審核  ", nreg);
	    mid_attr = ANSI_COLOR(41;5);
	    mid = buf;
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

    /* now, calculate real positioning info */
    llen = strlen(title);
    mlen = strlen(mid);
    mpos = (t_columns -1 - mlen)/2;

    /* first, print left. */
    clear();
    outs(TITLE_COLOR "【");
    outs(title);
    outs("】");
    pos = llen + 4;

    /* print mid */
    while(pos++ < mpos)
	outc(' ');
    outs(mid_attr);
    outs(mid);
    pos += mlen;
    outs(TITLE_COLOR);

    /* try to locate right */
    rlen = strlen(currboard) + 4 + 4;
    if(currboard[0] && pos+rlen < t_columns)
    {
	// print right stuff
	while(pos++ < t_columns-rlen)
	    outc(' ');
	outs(title_tail_attrs[tail_type]);
	outs(title_tail_msgs[tail_type]);
	outs("《");

	if (is_currboard_special)
	    outs(ANSI_COLOR(32));
	outs(currboard);
	outs(title_tail_attrs[tail_type]);
	outs("》" ANSI_RESET "\n");
    } else {
	// just pad it.
	while(pos++ < t_columns)
	    outc(' ');
	outs(ANSI_RESET "\n");
    }

}

/* Ctrl-Z Anywhere Fast Switch, not ZG. */
static char zacmd = 0;

// ZA is waiting, hurry to the meeting stone!
int 
ZA_Waiting(void)
{
    return (zacmd != 0);
}

// Promp user our ZA bar and return for selection.
int
ZA_Select(void)
{
    int k;

    // TODO refresh status bar?
    vs_footer(VCLR_ZA_CAPTION " ★快速切換: ",
	    " (b)文章列表 (c)分類看板 (f)我的最愛 (m)信箱 (u)使用者名單");
    k = vkey();

    if (k < ' ' || k >= 'z') return 0;
    k = tolower(k);

    if(strchr("bcfmu", k) == NULL)
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

static void
show_status(void)
{
    int i;
    struct tm      *ptime = localtime4(&now);
    char           *myweek = "天一二三四五六";
    const char     *msgs[] = {"關閉", "打開", "拔掉", "防水", "好友"};

    i = ptime->tm_wday << 1;
    move(b_lines, 0);
    vbarf(ANSI_COLOR(34;46) "[%d/%d 星期%c%c %d:%02d]" 
	  ANSI_COLOR(1;33;45) "%-14s"
	  ANSI_COLOR(30;47) " 線上" ANSI_COLOR(31) 
	  "%d" ANSI_COLOR(30) "人, 我是" ANSI_COLOR(31) "%s"
	  ANSI_COLOR(30) "\t[扣機]" ANSI_COLOR(31) "%s ",
	  ptime->tm_mon + 1, ptime->tm_mday, myweek[i], myweek[i + 1],
	  ptime->tm_hour, ptime->tm_min, currutmp->birth ?
	  "生日要請客唷" : SHM->today_is,
	  SHM->UTMPnumber, cuser.userid,
	  msgs[currutmp->pager]);
}

/*
 * current caller of movie:
 *   board.c: movie(0);    // called when IN_CLASSROOT()
 *                         // with currstat = CLASS -> don't show movies
 *   xyz.c:   movie(999999);  // logout
 *   menu.c:  movie(cmdmode); // ...
 */
#define N_SYSMOVIE (sizeof(movie_map) / sizeof(movie_map[0]))
void
movie(int cmdmode)
{
    int i;

    // movie 前幾筆是 Note 板精華區「<系統> 動態看板」(SYS) 目錄下的文章
    // movie_map 是用來依 cmdmode 挑出特定的動態看板，index 跟 mode_map 一樣。
    const int movie_map[] = {
	2, 10, 11, -1, 3, -1, 12,
	7, 9, 8, 4, 5, 6,
    };

    // don't show if stat in class or user wants to skip movies
    if (currstat == CLASS || !(cuser.uflag & MOVIE_FLAG))
	return;
    // also prevent SHM busy status
    if (SHM->Pbusystate || SHM->last_film <= 0)
	return;

    if (cmdmode < N_SYSMOVIE &&
	    0 < movie_map[cmdmode] && movie_map[cmdmode] <= SHM->last_film) {
	i = movie_map[cmdmode];
    } else if (cmdmode == 999999) {	/* Goodbye my friend */
	i = 0;
    } else {
	i = N_SYSMOVIE + (int)(((float)SHM->last_film - N_SYSMOVIE + 1) * (random() / (RAND_MAX + 1.0)));
    }

    move(1, 0);
    clrtoln(1 + FILMROW);	/* 清掉上次的 */
#ifdef LARGETERM_CENTER_MENU
    out_lines(SHM->notes[i], 11, (t_columns - 80)/2);	/* 只印11行就好 */
#else
    out_lines(SHM->notes[i], 11, 0);	/* 只印11行就好 */
#endif
    outs(reset_color);
}

typedef struct {
    int     (*cmdfunc)();
    int     level;
    char    *desc;                   /* next/key/description */
} commands_t;

static int
show_menu(int moviemode, const commands_t * p)
{
    register int    n = 0;
    register char  *s;

    movie(moviemode);

    // seems not everyone likes the menu in center.
#ifdef LARGETERM_CENTER_MENU
    // update menu column [fixed const because most items are designed in this way)
    menu_column = (t_columns-40)/2;
    menu_row = 12 + (t_lines-24)/2;
#endif 

    move(menu_row, 0);
    while ((s = p[n].desc)) {
	if (CheckMenuPerm(p[n].level)) {
	    prints("%*s  (" ANSI_COLOR(1;36) "%c" ANSI_COLOR(0) ")%s\n", menu_column, "", s[1],
		   s+2);
	}
	n++;
    }
    return n - 1;
}


enum {
    M_ADMIN = 0, M_AMUSE, M_CHC, M_JCEE, M_MAIL, M_MMENU, M_NMENU,
    M_PMENU, M_PSALE, M_SREG, M_TMENU, M_UMENU, M_XMENU, M_XMAX
};

static const int mode_map[] = {
    ADMIN, AMUSE, CHC, JCEE, MAIL, MMENU, NMENU,
    PMENU, PSALE, SREG, TMENU, UMENU, XMENU,
};

static void
domenu(int cmdmode, const char *cmdtitle, int cmd, const commands_t cmdtable[])
{
    int             lastcmdptr, moviemode;
    int             n, pos, total, i;
    int             err;

    moviemode = cmdmode;
    assert(cmdmode < M_XMAX);
    cmdmode = mode_map[cmdmode];

    setutmpmode(cmdmode);

    showtitle(cmdtitle, BBSName);

    total = show_menu(moviemode, cmdtable);

    show_status();
    lastcmdptr = pos = 0;

    do {
	i = -1;
	switch (cmd) {
	case Ctrl('Z'):
	    ZA_Select(); // we'll have za loop later.
	    refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case Ctrl('I'):
	    t_idle();
	    refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case Ctrl('N'):
	    New();
	    refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case Ctrl('A'):
	    if (mail_man() == FULLUPDATE)
		refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case KEY_DOWN:
	    i = lastcmdptr;
	case KEY_HOME:
	case KEY_PGUP:
	    do {
		if (++i > total)
		    i = 0;
	    } while (!CheckMenuPerm(cmdtable[i].level));
	    break;
	case KEY_END:
	case KEY_PGDN:
	    i = total;
	    break;
	case KEY_UP:
	    i = lastcmdptr;
	    do {
		if (--i < 0)
		    i = total;
	    } while (!CheckMenuPerm(cmdtable[i].level));
	    break;
	case KEY_LEFT:
	case 'e':
	case 'E':
	    if (cmdmode == MMENU)
		cmd = 'G';
	    else if ((cmdmode == MAIL) && chkmailbox())
		cmd = 'R';
	    else
		return;
	default:
	    if ((cmd == 's' || cmd == 'r') &&
		(cmdmode == MMENU || cmdmode == TMENU || cmdmode == XMENU)) {
		if (cmd == 's')
		    ReadSelect();
		else
		    Read();
		refscreen = YEA;
		i = lastcmdptr;
		currstat = cmdmode;
		break;
	    }
	    if (cmd == '\n' || cmd == '\r' || cmd == KEY_RIGHT) {
		move(b_lines, 0);
		clrtoeol();

		currstat = XMODE;

		if ((err = (*cmdtable[lastcmdptr].cmdfunc) ()) == QUIT)
		    return;
		currutmp->mode = currstat = cmdmode;

		if (err == XEASY) {
		    refresh();
		    safe_sleep(1);
		} else if (err != XEASY + 1 || err == FULLUPDATE)
		    refscreen = YEA;

		if (err != -1)
		    cmd = cmdtable[lastcmdptr].desc[0];
		else
		    cmd = cmdtable[lastcmdptr].desc[1];
	    }
	    if (cmd >= 'a' && cmd <= 'z')
		cmd &= ~0x20;
	    while (++i <= total)
		if (cmdtable[i].desc[1] == cmd)
		    break;

	    if (!CheckMenuPerm(cmdtable[i].level)) {
		for (i = 0; cmdtable[i].desc; i++)
		    if (CheckMenuPerm(cmdtable[i].level))
			break;
		if (!cmdtable[i].desc)
		    return;
	    }

	    if (cmd == 'H' && i > total){
		/* TODO: Add menu help */
	    }
	}

	// end of all commands
	if (ZA_Waiting())
	{
	    ZA_Enter();
	    refscreen = 1;
	    currstat = cmdmode;
	}

	if (i > total || !CheckMenuPerm(cmdtable[i].level))
	    continue;

	if (refscreen) {
	    showtitle(cmdtitle, BBSName);
	    show_menu(moviemode, cmdtable);
	    show_status();
	    refscreen = NA;
	}
	cursor_clear(menu_row + pos, menu_column);
	n = pos = -1;
	while (++n <= (lastcmdptr = i))
	    if (CheckMenuPerm(cmdtable[n].level))
		pos++;

	cursor_show(menu_row + pos, menu_column);
    } while (((cmd = igetch()) != EOF) || refscreen);

    abort_bbs(0);
}
/* INDENT OFF */

/* administrator's maintain menu */
static const commands_t adminlist[] = {
    {m_user, PERM_SYSOP,              "UUser          使用者資料"},
    {search_user_bypwd, PERM_ACCOUNTS|PERM_POLICE_MAN,
                                      "SSearch User   特殊搜尋使用者"},
    {search_user_bybakpwd,PERM_ACCOUNTS,"OOld User data 查閱\備份使用者資料"},
    {m_board, PERM_SYSOP|PERM_BOARD,  "BBoard         設定看板"},
    {m_register, PERM_ACCOUNTS|PERM_ACCTREG,
                                      "RRegister      審核註冊表單"},
    {x_file, PERM_SYSOP|PERM_VIEWSYSOP,
                                      "XXfile         編輯系統檔案"},
    {give_money, PERM_SYSOP|PERM_VIEWSYSOP,
                                      "GGivemoney     紅包雞"},
    {m_loginmsg, PERM_SYSOP,          "MMessage Login 進站水球"},
    {NULL, 0, NULL}
};

/* mail menu */
static const commands_t maillist[] = {
    {m_new, PERM_READMAIL,      "RNew           閱\讀新進郵件"},
    {m_read, PERM_READMAIL,     "RRead          多功\能讀信選單"},
    {m_send, PERM_LOGINOK,      "RSend          站內寄信"},
    {mail_list, PERM_LOGINOK,   "RMail List     群組寄信"},
    {x_love, PERM_LOGINOK,      "PPaper         情書產生器"},
// #ifdef USE_MAIL_AUTO_FORWARD
    {setforward, PERM_LOGINOK,  "FForward       " ANSI_COLOR(1;32) 
				"設定信箱自動轉寄" ANSI_RESET},
// #endif // USE_MAIL_AUTO_FORWARD
    {m_sysop, 0,                "YYes, sir!     寫信給站長"},
    {m_internet, PERM_INTERNET, "RInternet      寄信到站外"},
    {mail_mbox, PERM_INTERNET,  "RZip UserHome  把所有私人資料打包回去"},
    {built_mail_index, PERM_LOGINOK, "SSavemail      重建信箱索引"},
    {mail_all, PERM_SYSOP,      "RAll           寄信給所有使用者"},
    {NULL, 0, NULL}
};

/* Talk menu */
static const commands_t talklist[] = {
    {t_users, 0,            "UUsers         完全聊天手冊"},
    {t_pager, PERM_BASIC,   "PPager         切換呼叫器"},
    {t_idle, 0,             "IIdle          發呆"},
    {t_query, 0,            "QQuery         查詢網友"},
    {t_qchicken, 0,         "WWatch Pet     查詢寵物"},
    // PERM_PAGE - 水球都要 PERM_LOGIN 了
    // 沒道理可以 talk 不能水球。
    {t_talk, PERM_LOGINOK,  "TTalk          找人聊聊"},
    // PERM_CHAT 非 login 也有，會有人用此吵別人。
    {t_chat, PERM_LOGINOK,  "CChat          找家茶坊喫茶去"},
#ifdef PLAY_ANGEL
    {t_changeangel, PERM_LOGINOK, "UAChange Angel 更換小天使"},
    {t_angelmsg, PERM_ANGEL, "LLeave message 留言給小主人"},
#endif
    {t_display, 0,          "DDisplay       顯示上幾次熱訊"},
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

static const commands_t namelist[] = {
    {t_override, PERM_LOGINOK,"OOverRide      好友名單"},
    {t_reject, PERM_LOGINOK,  "BBlack         壞人名單"},
    {t_aloha,PERM_LOGINOK,    "AALOHA         上站通知名單"},
    {t_fix_aloha,PERM_LOGINOK,"XXFixALOHA     修正上站通知"},
    {t_special,PERM_LOGINOK,  "SSpecial       其他特別名單"},
    {NULL, 0, NULL}
};

void Customize(); // user.c
int u_customize()
{
    Customize();
    return 0;
}

int u_fixgoodpost(void); // assess.c
/* User menu */
static const commands_t userlist[] = {
    {u_customize, PERM_BASIC,       "UUCustomize    個人化設定"},
    {u_info, PERM_LOGINOK,    	    "IInfo          設定個人資料與密碼"},
    {calendar, PERM_LOGINOK,        "CCalendar      個人行事曆"},
    {u_loginview, PERM_BASIC,       "LLogin View    選擇進站畫面"},
    {u_editplan, PERM_LOGINOK,      "QQueryEdit     編輯名片檔"},
    {u_editsig, PERM_LOGINOK,       "SSignature     編輯簽名檔"},
#if HAVE_FREECLOAK
    {u_cloak, PERM_LOGINOK,         "KKCloak        隱身術"},
#else
    {u_cloak, PERM_CLOAK,           "KKCloak        隱身術"},
#endif
    {u_register, MENU_UNREGONLY,    "RRegister      填寫《註冊申請單》"},
#ifdef ASSESS
    {u_cancelbadpost, PERM_LOGINOK, "BBye BadPost   申請刪除劣文"},
    {u_fixgoodpost, PERM_LOGINOK,   "FFix GoodPost  修復優文"},
#endif // ASSESS
    {u_list, PERM_SYSOP,            "XUsers         列出註冊名單"},
#ifdef MERGEBBS
//    {m_sob, PERM_LOGUSER|PERM_SYSOP,             "SSOB Import    沙灘變身術"},
    {m_sob, PERM_BASIC,             "SSOB Import    沙灘變身術"},
#endif
    {NULL, 0, NULL}
};

#ifdef DEBUG
int _debug_check_keyinput();
int _debug_reportstruct()
{
    clear();
    prints("boardheader_t:\t%d\n", sizeof(boardheader_t));
    prints("fileheader_t:\t%d\n", sizeof(fileheader_t));
    prints("userinfo_t:\t%d\n", sizeof(userinfo_t));
    prints("screenline_t:\t%d\n", sizeof(screenline_t));
    prints("SHM_t:\t%d\n", sizeof(SHM_t));
    prints("bid_t:\t%d\n", sizeof(bid_t));
    prints("userec_t:\t%d\n", sizeof(userec_t));
    pressanykey();
    return 0;
}

#endif

/* XYZ tool menu */
static const commands_t xyzlist[] = {
#ifndef DEBUG
    /* All these are useless in debug mode. */
#ifdef  HAVE_LICENSE
    {x_gpl, 0,       "LLicense       GNU 使用執照"},
#endif
#ifdef HAVE_INFO
    {x_program, 0,   "PProgram       本程式之版本與版權宣告"},
#endif
    {x_boardman,0,   "MMan Boards    《看板精華區排行榜》"},
//  {x_boards,0,     "HHot Boards    《看板人氣排行榜》"},
    {x_history, 0,   "HHistory       《我們的成長》"},
    {x_note, 0,      "NNote          《酸甜苦辣流言板》"},
    {x_login,0,      "SSystem        《系統重要公告》"},
    {x_week, 0,      "WWeek          《本週五十大熱門話題》"},
    {x_issue, 0,     "IIssue         《今日十大熱門話題》"},
    {x_today, 0,     "TToday         《今日上線人次統計》"},
    {x_yesterday, 0, "YYesterday     《昨日上線人次統計》"},
    {x_user100 ,0,   "UUsers         《使用者百大排行榜》"},
#else
    {_debug_check_keyinput, 0, 
	    	     "MMKeycode      檢查按鍵控制碼工具"},
    {_debug_reportstruct, 0, 
	    	     "RReportStruct  報告各種結構的大小"},
#endif

    {p_sysinfo, 0,   "XXinfo         《查看系統資訊》"},
    {NULL, 0, NULL}
};

/* Ptt money menu */
static const commands_t moneylist[] = {
    {p_give, 0,         "00Give        給其他人錢"},
    {save_violatelaw, 0,"11ViolateLaw  繳罰單"},
#if !HAVE_FREECLOAK
    {p_cloak, 0,        "22Cloak       切換 隱身/現身   $19  /次"},
#endif
    {p_from, 0,         "33From        暫時修改故鄉     $49  /次"},
    {ordersong,0,       "44OSong       歐桑動態點歌機   $200 /次"},
    {p_exmail, 0,       "55Exmail      購買信箱         $1000/封"},
    {NULL, 0, NULL}
};

static const commands_t      cmdlist[] = {
    {admin, PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD|PERM_VIEWSYSOP|PERM_ACCTREG|PERM_POLICE_MAN, 
				"00Admin       【 系統維護區 】"},
    {Announce,	0,		"AAnnounce     【 精華公佈欄 】"},
#ifdef DEBUG
    {Favorite,	0,		"FFavorite     【 我的最不愛 】"},
#else
    {Favorite,	0,		"FFavorite     【 我 的 最愛 】"},
#endif
    {Class,	0,		"CClass        【 分組討論區 】"},
    {Mail, 	PERM_BASIC,	"MMail         【 私人信件區 】"},
    {Talk, 	0,		"TTalk         【 休閒聊天區 】"},
    {User, 	PERM_BASIC,	"UUser         【 個人設定區 】"},
    {Xyz, 	0,		"XXyz          【 系統資訊區 】"},
    {Play_Play, PERM_LOGINOK, 	"PPlay         【 娛樂與休閒 】"},
    {Name_Menu, PERM_LOGINOK,	"NNamelist     【 編特別名單 】"},
#ifdef DEBUG
    {Goodbye, 	0, 		"GGoodbye      再見再見再見再見"},
#else
    {Goodbye, 	0, 		"GGoodbye         離開，再見… "},
#endif
    {NULL, 	0, 		NULL}
};

int main_menu(void) {
    domenu(M_MMENU, "主功\能表", (ISNEWMAIL(currutmp) ? 'M' : 'C'), cmdlist);
    return 0;
}

static int p_money() {
    domenu(M_PSALE, BBSMNAME2 "量販店", '0', moneylist);
    return 0;
};

// static int forsearch();
static int playground();
static int chessroom();

/* Ptt Play menu */
static const commands_t playlist[] = {
    {note, PERM_LOGINOK,     "NNote        【 刻刻流言板 】"},
    /* // useless.
    {forsearch,PERM_LOGINOK, "SSearchEngine【" ANSI_COLOR(1;35) " " 
	BBSMNAME2 "搜尋器 " ANSI_RESET "】"},
	*/
    {topsong,PERM_LOGINOK,   "TTop Songs   【" ANSI_COLOR(1;32) " 點歌排行榜 " ANSI_RESET "】"},
    {p_money,PERM_LOGINOK,   "PPay         【" ANSI_COLOR(1;31) " "
	BBSMNAME2 "量販店 " ANSI_RESET "】"},
    {chicken_main,PERM_LOGINOK, "CChicken     "
     "【" ANSI_COLOR(1;34) " " BBSMNAME2 "養雞場 " ANSI_RESET "】"},
    {playground,PERM_LOGINOK, "AAmusement   【" ANSI_COLOR(1;33) " "
	BBSMNAME2 "遊樂場 " ANSI_RESET "】"},
    {chessroom, PERM_LOGINOK, "BBChess      【" ANSI_COLOR(1;34) " "
	BBSMNAME2 "棋院   " ANSI_RESET "】"},
    {NULL, 0, NULL}
};

static const commands_t chesslist[] = {
    {chc_main,         PERM_LOGINOK, "11CChessFight   【" ANSI_COLOR(1;33) " 象棋邀局 " ANSI_RESET "】"},
    {chc_personal,     PERM_LOGINOK, "22CChessSelf    【" ANSI_COLOR(1;34) " 象棋打譜 " ANSI_RESET "】"},
    {chc_watch,        PERM_LOGINOK, "33CChessWatch   【" ANSI_COLOR(1;35) " 象棋觀棋 " ANSI_RESET "】"},
    {gomoku_main,      PERM_LOGINOK, "44GomokuFight   【" ANSI_COLOR(1;33) "五子棋邀局" ANSI_RESET "】"},
    {gomoku_personal,  PERM_LOGINOK, "55GomokuSelf    【" ANSI_COLOR(1;34) "五子棋打譜" ANSI_RESET "】"},
    {gomoku_watch,     PERM_LOGINOK, "66GomokuWatch   【" ANSI_COLOR(1;35) "五子棋觀棋" ANSI_RESET "】"},
    {gochess_main,     PERM_LOGINOK, "77GoChessFight  【" ANSI_COLOR(1;33) " 圍棋邀局 " ANSI_RESET "】"},
    {gochess_personal, PERM_LOGINOK, "88GoChessSelf   【" ANSI_COLOR(1;34) " 圍棋打譜 " ANSI_RESET "】"},
    {gochess_watch,    PERM_LOGINOK, "99GoChessWatch  【" ANSI_COLOR(1;35) " 圍棋觀棋 " ANSI_RESET "】"},
    {NULL, 0, NULL}
};

static int chessroom() {
    domenu(M_CHC, BBSMNAME2 "棋院", '1', chesslist);
    return 0;
}

static const commands_t plist[] = {

/*    {p_ticket_main, PERM_LOGINOK,"00Pre         【 總統機 】"},
      {alive, PERM_LOGINOK,        "00Alive       【  訂票雞  】"},
*/
    {ticket_main, PERM_LOGINOK,  "11Gamble      【 " BBSMNAME2 "賭場 】"},
    {guess_main, PERM_LOGINOK,   "22Guess number【  猜數字  】"},
    {othello_main, PERM_LOGINOK, "33Othello     【  黑白棋  】"},
//    {dice_main, PERM_LOGINOK,    "44Dice        【 玩骰子   】"},
    {vice_main, PERM_LOGINOK,    "44Vice        【 發票對獎 】"},
    {g_card_jack, PERM_LOGINOK,  "55Jack        【  黑傑克  】"},
    {g_ten_helf, PERM_LOGINOK,   "66Tenhalf     【  十點半  】"},
    {card_99, PERM_LOGINOK,      "77Nine        【  九十九  】"},
    {NULL, 0, NULL}
};

static int playground() {
    domenu(M_AMUSE, BBSMNAME2 "遊樂場",'1',plist);
    return 0;
}

static const commands_t slist[] = {
    /*
    // x_dict: useless
    {x_dict,0,                   "11Dictionary  "
     "【" ANSI_COLOR(1;33) " 趣味大字典 " ANSI_RESET "】"},
     */
    {x_mrtmap, 0,                "22MRTmap      "
	 "【" ANSI_COLOR(1;34) "  捷運地圖  " ANSI_RESET "】"},
    {NULL, 0, NULL}
};

/* // nothing to search...
static int forsearch() {
    domenu(M_SREG, BBSMNAME2 "搜尋器", '1', slist);
    return 0;
}
*/

/* main menu */

int
admin(void)
{
    domenu(M_ADMIN, "系統維護", 'X', adminlist);
    return 0;
}

int
Mail(void)
{
    domenu(M_MAIL, "電子郵件", 'R', maillist);
    return 0;
}

int
Talk(void)
{
    domenu(M_TMENU, "聊天說話", 'U', talklist);
    return 0;
}

int
User(void)
{
    domenu(M_UMENU, "個人設定", 'U', userlist);
    return 0;
}

int
Xyz(void)
{
    if (strcmp(cuser.userid, "piaip") == 0)
	x_file();
    else
    domenu(M_XMENU, "工具程式", 'M', xyzlist);
    return 0;
}

int
Play_Play(void)
{
    domenu(M_PMENU, "網路遊樂場", 'A', playlist);
    return 0;
}

int
Name_Menu(void)
{
    domenu(M_NMENU, "白色恐怖", 'O', namelist);
    return 0;
}
 
