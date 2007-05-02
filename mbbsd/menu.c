/* $Id$ */
#include "bbs.h"

#define CheckMenuPerm(x) ( (x) ? HasUserPerm(x) : 1)

/* help & menu processring */
static int      refscreen = NA;
extern char    *boardprefix;
extern struct utmpfile_t *utmpshm;
extern char     board_hidden_status;

static const char *title_tail_msgs[] = {
    "看板",
    "文摘",
    "系列",
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
    int llen = -1, rlen = -1, mlen = -1, mpos = 0;
    int pos = 0;
    int tail_type = TITLE_TAIL_BOARD;
    const char *mid_attr = ANSI_COLOR(33);

    static char     lastboard[16] = {0};
    char buf[64];

    if (currmode & MODE_SELECT)
       tail_type = TITLE_TAIL_SELECT;
    else if (currmode & MODE_DIGEST)
	tail_type = TITLE_TAIL_DIGEST;

    /* check if board was changed. */
    if (strcmp(currboard, lastboard) != 0 && currboard[0]) {
	int bid = getbnum(currboard);
	if(bid > 0)
	{
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    board_hidden_status = ((getbcache(bid)->brdattr & BRD_HIDE) &&
				   (getbcache(bid)->brdattr & BRD_POSTMASK));
	    strlcpy(lastboard, currboard, sizeof(lastboard));
	}
    }

    /* next, determine if title was overrided. */
#ifdef DEBUG
    {
	sprintf(buf, "  current pid: %6d  ", getpid());
	mid = buf; 
	mid_attr = ANSI_COLOR(41;5);
	mlen = strlen(mid);
    }
#else
    if (ISNEWMAIL(currutmp)) {
	mid = "   郵差來按鈴囉   ";
	mid_attr = ANSI_COLOR(41;5);
	mlen = strlen(mid);
    } else if ( HasUserPerm(PERM_ACCTREG) ) {
	int nreg = dashs((char *)fn_register) / 163;
	if(nreg > 100)
	{
	    sprintf(buf, "  有 %03d 未審核  ", nreg);
	    mid_attr = ANSI_COLOR(41;5);
	    mid = buf;
	    mlen = strlen(mid);
	}
    }
#endif
    /* now, calculate real positioning info */
    if(llen < 0) llen = strlen(title);
    if(mlen < 0) mlen = strlen(mid);
    mpos = (t_columns - mlen)/2;

    /* first, print left. */
    clear();
    outs(TITLE_COLOR "【");
    outs(title);
    outs("】");
    pos = llen + 4;
    /* prepare for mid */
    while(pos < mpos)
	outc(' '), pos++;
    outs(mid_attr);
    outs(mid), pos+=mlen;
    outs(TITLE_COLOR);
    /* try to locate right */
    rlen = strlen(currboard) + 4 + 4;
    if(currboard[0] && pos+rlen <= t_columns)
    {
	// print right stuff
	while(++pos < t_columns-rlen)
	    outc(' ');
	outs(title_tail_attrs[tail_type]);
	outs(title_tail_msgs[tail_type]);
	outs("《");
	if (board_hidden_status)
	    outs(ANSI_COLOR(32));
	outs(currboard);
	outs(title_tail_attrs[tail_type]);
	outs("》" ANSI_RESET "\n");
    } else {
	// just pad it.
	while(++pos < t_columns)
	    outc(' ');
	outs(ANSI_RESET "\n");
    }

}

/* 動畫處理 */
#define FILMROW 11
static const unsigned char menu_row = 12;
static const unsigned char menu_column = 20;

static void
show_status(void)
{
    int i;
    struct tm      *ptime = localtime4(&now);
    char            mystatus[160];
    char           *myweek = "天一二三四五六";
    const char     *msgs[] = {"關閉", "打開", "拔掉", "防水", "好友"};

    i = ptime->tm_wday << 1;
    snprintf(mystatus, sizeof(mystatus),
	     ANSI_COLOR(34;46) "[%d/%d 星期%c%c %d:%02d]" 
	     ANSI_COLOR(1;33;45) "%-14s"
	     ANSI_COLOR(30;47) " 目前坊裡有" ANSI_COLOR(31) 
	     "%d" ANSI_COLOR(30) "人, 我是" ANSI_COLOR(31) "%s"
	     ANSI_COLOR(30) ,
	     ptime->tm_mon + 1, ptime->tm_mday, myweek[i], myweek[i + 1],
	     ptime->tm_hour, ptime->tm_min, currutmp->birth ?
	     "生日要請客唷" : SHM->today_is,
	     SHM->UTMPnumber, cuser.userid);
    outmsg(mystatus);
    i = strlen(mystatus) - (3*7+25);
    sprintf(mystatus, "[扣機]" ANSI_COLOR(31) "%s ",
	msgs[currutmp->pager]);
    outslr("", i, mystatus, strlen(msgs[currutmp->pager]) + 7);
    outs(ANSI_RESET);
}

/*
 * current callee of movie:
 *   board.c: movie(0);    // called when IN_CLASSROOT()
 *                         // with currstat = CLASS -> don't show movies
 *   xyz.c:   movie(999999);  // logout
 *   menu.c:  movie(cmdmode); // ...
 */
void
movie(int cmdmode)
{
    // movie 前幾筆是 Note 板精華區「<系統> 動態看板」(SYS) 目錄下的文章
    // movie_map 是用來依 cmdmode 挑出特定的動態看板，index 跟 mode_map 一樣。
    const int movie_map[] = {
	2, 10, 11, -1, 3, -1, 12,
	7, 9, 8, 4, 5, 6,
    };

#define N_SYSMOVIE (sizeof(movie_map) / sizeof(movie_map[0]))
    int i;
    if ((currstat != CLASS) && (cuser.uflag & MOVIE_FLAG) &&
	!SHM->Pbusystate && SHM->last_film > 0) {
	if (cmdmode < sizeof(movie_map) / sizeof(movie_map[0]) &&
	    0 < movie_map[cmdmode] && movie_map[cmdmode] <= SHM->last_film) {
	    i = movie_map[cmdmode];
	} else if (cmdmode == 999999) {	/* Goodbye my friend */
	    i = 0;
	} else {
	    i = N_SYSMOVIE + (int)(((float)SHM->last_film - N_SYSMOVIE + 1) * (rand() / (RAND_MAX + 1.0)));
	}
#undef N_SYSMOVIE

	move(1, 0);
	clrtoline(1 + FILMROW);	/* 清掉上次的 */
	out_lines(SHM->notes[i], 11);	/* 只印11行就好 */
	outs(reset_color);
    }
    show_status();
    refresh();
}

static int
show_menu(int moviemode, const commands_t * p)
{
    register int    n = 0;
    register char  *s;

    movie(moviemode);

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
    M_PMENU, M_PSALE, M_SREG, M_TMENU, M_UMENU, M_XMENU,
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

    static char cursor_position[sizeof(mode_map) / sizeof(mode_map[0])] = { 0 };

    moviemode = cmdmode;
    cmdmode = mode_map[cmdmode];

    if (cursor_position[cmdmode])
	cmd = cursor_position[cmdmode];

    setutmpmode(cmdmode);

    showtitle(cmdtitle, BBSName);

    total = show_menu(moviemode, cmdtable);

    show_status();
    lastcmdptr = pos = 0;

    do {
	i = -1;
	switch (cmd) {
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
	    (currstat == MMENU || currstat == TMENU || currstat == XMENU)) {
		if (cmd == 's')
		    ReadSelect();
		else
		    Read();
		refscreen = YEA;
		i = lastcmdptr;
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
		cursor_position[cmdmode] = cmdtable[lastcmdptr].desc[0];
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
    {m_board, PERM_SYSOP,             "BBoard         設定看板"},
    {m_register, PERM_ACCOUNTS|PERM_ACCTREG,
                                      "RRegister      審核註冊表單"},
    {cat_register, PERM_SYSOP,        "CCatregister   無法審核時用的"},
    {x_file, PERM_SYSOP|PERM_VIEWSYSOP,
                                      "XXfile         編輯系統檔案"},
    {give_money, PERM_SYSOP|PERM_VIEWSYSOP,
                                      "GGivemoney     紅包雞"},
    {m_loginmsg, PERM_SYSOP,          "MMessage Login 進站水球"},
#ifdef  HAVE_MAILCLEAN
    {m_mclean, PERM_SYSOP,            "MMail Clean    清理使用者個人信箱"},
#endif
#ifdef  HAVE_REPORT
    {m_trace, PERM_SYSOP,             "TTrace         設定是否記錄除錯資訊"},
#endif
    {NULL, 0, NULL}
};

/* mail menu */
static const commands_t maillist[] = {
    {m_new, PERM_READMAIL,      "RNew           閱\讀新進郵件"},
    {m_read, PERM_READMAIL,     "RRead          多功\能讀信選單"},
    {m_send, PERM_LOGINOK,      "RSend          站內寄信"},
    {x_love, PERM_LOGINOK,      "PPaper         " ANSI_COLOR(1;32) "情書產生器" ANSI_RESET " "},
    {mail_list, PERM_LOGINOK,   "RMail List     群組寄信"},
    {setforward, PERM_LOGINOK, "FForward       " ANSI_COLOR(32) "設定信箱自動轉寄" ANSI_RESET},
    {m_sysop, 0,                "YYes, sir!     諂媚站長"},
    {m_internet, PERM_INTERNET, "RInternet      寄信到 Internet"},
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
    {t_talk, PERM_PAGE,     "TTalk          找人聊聊"},
    {t_chat, PERM_CHAT,     "CChat          找家茶坊喫茶去"},
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
#ifdef POSTNOTIFY
    {t_post,PERM_LOGINOK,     "NNewPost       新文章通知名單"},
#endif
    {t_special,PERM_LOGINOK,  "SSpecial       其他特別名單"},
    {NULL, 0, NULL}
};

void Customize(); // user.c
int u_customize()
{
    Customize();
    return 0;
}

/* User menu */
static const commands_t userlist[] = {
    {u_info, PERM_LOGINOK,    	    "IInfo          設定個人資料與密碼"},
    {u_customize, PERM_LOGINOK,     "IUCustomize    個人化設定"},
    {calendar, PERM_LOGINOK,        "CCalendar      個人行事曆"},
    {u_editcalendar, PERM_LOGINOK,  "CDEditCalendar 編輯個人行事曆"},
    {u_loginview, PERM_LOGINOK,     "LLogin View    選擇進站畫面"},
#ifdef  HAVE_SUICIDE
    {u_kill, PERM_BASIC,            "IKill          自殺！！"},
#endif
    {u_editplan, PERM_LOGINOK,      "QQueryEdit     編輯名片檔"},
    {u_editsig, PERM_LOGINOK,       "SSignature     編輯簽名檔"},
#if HAVE_FREECLOAK
    {u_cloak, PERM_LOGINOK,         "KKCloak        隱身術"},
#else
    {u_cloak, PERM_CLOAK,           "KKCloak        隱身術"},
#endif
    {u_register, PERM_BASIC,        "RRegister      填寫《註冊申請單》"},
    {u_cancelbadpost, PERM_LOGINOK, "BBye BadPost   申請刪除劣文"},
    {u_list, PERM_SYSOP,            "XUsers         列出註冊名單"},
#ifdef MERGEBBS
//    {m_sob, PERM_LOGUSER|PERM_SYSOP,             "SSOB Import    沙灘變身術"},
    {m_sob, PERM_BASIC,             "SSOB Import    沙灘變身術"},
#endif
    {NULL, 0, NULL}
};

#ifdef DEBUG
int _debug_check_keyinput();
int _debug_testregcode();
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
    {_debug_testregcode, 0, 
	    	     "RRegcode       檢查註冊碼公式"},
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

int main_menu(void) {
    domenu(M_MMENU, "主功\能表", (ISNEWMAIL(currutmp) ? 'M' : 'C'), cmdlist);
    return 0;
}

static int p_money() {
    domenu(M_PSALE, "Ｐtt量販店", '0', moneylist);
    return 0;
};

#if 0
const static commands_t jceelist[] = {
    {x_90,PERM_LOGINOK,	     "0090 JCEE     【90學年度大學聯招查榜系統】"},
    {x_89,PERM_LOGINOK,	     "1189 JCEE     【89學年度大學聯招查榜系統】"},
    {x_88,PERM_LOGINOK,      "2288 JCEE     【88學年度大學聯招查榜系統】"},
    {x_87,PERM_LOGINOK,      "3387 JCEE     【87學年度大學聯招查榜系統】"},
    {x_86,PERM_LOGINOK,      "4486 JCEE     【86學年度大學聯招查榜系統】"},
    {NULL, 0, NULL}
};

static int m_jcee() {
    domenu(M_JCEE, "Ｐtt查榜系統", '0', jceelist);
    return 0;
}
#endif

static int forsearch();
static int playground();
static int chessroom();

/* Ptt Play menu */
static const commands_t playlist[] = {
#if 0
#if HAVE_JCEE
    {m_jcee, PERM_LOGINOK,   "JJCEE        【 大學聯考查榜系統 】"},
#endif
#endif
    {note, PERM_LOGINOK,     "NNote        【 刻刻流言板 】"},
/* XXX 壞掉了, 或許可以換成 weather.today/weather.tomorrow 但反正沒意義 */
/* {x_weather,0 ,           "WWeather     【 氣象預報 】"}, */
/* XXX 壞掉了 */
/*    {x_stock,0 ,             "SStock       【 股市行情 】"},*/
    {forsearch,PERM_LOGINOK, "SSearchEngine【" ANSI_COLOR(1;35) " Ｐtt搜尋器 " ANSI_RESET "】"},
    {topsong,PERM_LOGINOK,   "TTop Songs   【" ANSI_COLOR(1;32) " 點歌排行榜 " ANSI_RESET "】"},
    {p_money,PERM_LOGINOK,   "PPay         【" ANSI_COLOR(1;31) " Ｐtt量販店 " ANSI_RESET "】"},
    {chicken_main,PERM_LOGINOK, "CChicken     "
     "【" ANSI_COLOR(1;34) " Ｐtt養雞場 " ANSI_RESET "】"},
    {playground,PERM_LOGINOK, "AAmusement   【" ANSI_COLOR(1;33) " Ｐtt遊樂場 " ANSI_RESET "】"},
    {chessroom, PERM_LOGINOK, "BBChess      【" ANSI_COLOR(1;34) " Ｐtt棋院   " ANSI_RESET "】"},
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
    domenu(M_CHC, "Ｐtt棋院", '1', chesslist);
    return 0;
}

static const commands_t plist[] = {

/*    {p_ticket_main, PERM_LOGINOK,"00Pre         【 總統機 】"},
      {alive, PERM_LOGINOK,        "00Alive       【  訂票雞  】"},
*/
    {ticket_main, PERM_LOGINOK,  "11Gamble      【 Ｐtt賭場 】"},
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
    domenu(M_AMUSE, "Ｐtt遊樂場",'1',plist);
    return 0;
}

static const commands_t slist[] = {
    {x_dict,0,                   "11Dictionary  "
     "【" ANSI_COLOR(1;33) " 趣味大字典 " ANSI_RESET "】"},
    {x_mrtmap, 0,                "22MRTmap      "
	 "【" ANSI_COLOR(1;34) "  捷運地圖  " ANSI_RESET "】"},
    {NULL, 0, NULL}
};

static int forsearch() {
    domenu(M_SREG, "Ｐtt搜尋器", '1', slist);
    return 0;
}

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
    domenu(M_UMENU, "個人設定", 'I', userlist);
    return 0;
}

int
Xyz(void)
{
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
 
