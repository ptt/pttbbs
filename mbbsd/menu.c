/* $Id$ */
#include "bbs.h"

/* help & menu processring */
static int      refscreen = NA;
extern char    *boardprefix;
extern struct utmpfile_t *utmpshm;
extern char    *fn_board;
extern char     board_hidden_status;

void
showtitle(char *title, char *mid)
{
    char            buf[40], numreg[50];
#ifndef DEBUG
    int             nreg;
#endif
    int             spc = 0, pad, bid;
    static char     lastboard[16] = {0};

    spc = strlen(mid);
    if (title[0] == 0)
	title++;
#ifdef DEBUG
    else {
	snprintf(numreg, sizeof(numreg),
		 "\033[41;5m  current pid: %6d  " TITLE_COLOR,
		 getpid());
	mid = numreg;
	spc = 22;
    }
#else
    else if (currutmp->mailalert) {
	mid = "\033[41;5m   郵差來按鈴囉   " TITLE_COLOR;
	spc = 22;
    } else if (HAS_PERM(PERM_SYSOP) && (nreg = dashs(fn_register) / 163) > 10) {
	snprintf(numreg, sizeof(numreg),
		 "\033[41;5m  有%03d/%03d未審核  " TITLE_COLOR,
		 nreg, (int)dashs("register.new.tmp") / 163);
	mid = numreg;
	spc = 22;
    }
#endif
    spc = 66 - strlen(title) - spc - strlen(currboard);
    if (spc < 0)
	spc = 0;
    pad = 1 - (spc & 1);
    memset(buf, ' ', spc >>= 1);
    buf[spc] = '\0';

    clear();
    prints(TITLE_COLOR "【%s】%s\033[33m%s%s%s\033[3%s《",
	   title, buf, mid, buf, " " + pad,
	currmode & MODE_SELECT ? "6m系列" : currmode & MODE_ETC ? "5m其他" :
	   currmode & MODE_DIGEST ? "2m文摘" : "7m看板");

    if (strcmp(currboard, lastboard)) {	/* change board */
	if (currboard[0] != 0 &&
	    (bid = getbnum(currboard)) > 0) {
	    // XXX: bid starts from 1
	    board_hidden_status = ((bcache[bid - 1].brdattr & BRD_HIDE) &&
				   (bcache[bid - 1].brdattr & BRD_POSTMASK));
	    strncpy(lastboard, currboard, sizeof(lastboard));
	}
    }
    if (board_hidden_status)
	prints("\033[32m%s", currboard);
    else
	prints("%s", currboard);
    prints("\033[3%dm》\033[0m\n", currmode & MODE_SELECT ? 6 :
	   currmode & MODE_ETC ? 5 : currmode & MODE_DIGEST ? 2 : 7);
}

/* 動畫處理 */
#define FILMROW 11
static unsigned char menu_row = 12;
static unsigned char menu_column = 20;

static void
show_status(void)
{
    int i;
    struct tm      *ptime = localtime(&now);
    char            mystatus[160];
    char           *myweek = "天一二三四五六";
    const char     *msgs[] = {"關閉", "打開", "拔掉", "防水", "好友"};

    i = ptime->tm_wday << 1;
    snprintf(mystatus, sizeof(mystatus),
	     "\033[34;46m[%d/%d 星期%c%c %d:%02d]\033[1;33;45m%-14s"
	     "\033[30;47m 目前坊裡有\033[31m%d\033[30m人, 我是\033[31m%-12s"
	     "\033[30m[扣機]\033[31m%s\033[0m",
	     ptime->tm_mon + 1, ptime->tm_mday, myweek[i], myweek[i + 1],
	     ptime->tm_hour, ptime->tm_min, currutmp->birth ?
	     "生日要請客唷" : SHM->today_is,
	     SHM->UTMPnumber, cuser.userid, msgs[currutmp->pager]);
    outmsg(mystatus);
}

static int
u_movie()
{
    cuser.uflag ^= MOVIE_FLAG;
    return 0;
}

void
movie(int i)
{
    static short    history[MAX_HISTORY];
    int             j;

    if ((currstat != CLASS) && (cuser.uflag & MOVIE_FLAG) &&
	!SHM->Pbusystate && SHM->max_film > 0) {
	if (currstat == PSALE) {
	    i = PSALE;
	    reload_money();
	} else {
	    do {
		if (!i)
		    i = 1 + (int)(((float)SHM->max_film *
				   rand()) / (RAND_MAX + 1.0));

		for (j = SHM->max_history; j >= 0; j--)
		    if (i == history[j]) {
			i = 0;
			break;
		    }
	    } while (i == 0);
	}

	memmove(history, &history[1], SHM->max_history * sizeof(short));
	history[SHM->max_history] = j = i;

	if (i == 999)		/* Goodbye my friend */
	    i = 0;

	move(1, 0);
	clrtoline(1 + FILMROW);	/* 清掉上次的 */
	out_lines(SHM->notes[i], 11);	/* 只印11行就好 */
	outs(reset_color);
    }
    show_status();
    refresh();
}

static int
show_menu(commands_t * p)
{
    register int    n = 0;
    register char  *s;
    const char     *state[4] = {"用功\型", "安逸型", "自定型", "SHUTUP"};
    char            buf[80];

    movie(currstat);

    move(menu_row, 0);
    while ((s = p[n].desc)) {
	if (HAS_PERM(p[n].level)) {
	    snprintf(buf, sizeof(buf), s + 2, state[cuser.proverb % 4]);
	    prints("%*s  (\033[1;36m%c\033[0m)%s\n", menu_column, "", s[1],
		   buf);
	}
	n++;
    }
    return n - 1;
}

void
domenu(int cmdmode, char *cmdtitle, int cmd, commands_t cmdtable[])
{
    int             lastcmdptr;
    int             n, pos, total, i;
    int             err;
    static char     cmd0[LOGIN];

    /* XXX: 傳進來的 cmd 若權限不足, 則不知 cursor 在哪, 導致 crash */
    if (cmd0[cmdmode])
	cmd = cmd0[cmdmode];

    setutmpmode(cmdmode);

    showtitle(cmdtitle, BBSName);

    total = show_menu(cmdtable);

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
	    } while (!HAS_PERM(cmdtable[i].level));
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
	    } while (!HAS_PERM(cmdtable[i].level));
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
		cmd0[cmdmode] = cmdtable[lastcmdptr].desc[0];
	    }
	    if (cmd >= 'a' && cmd <= 'z')
		cmd &= ~0x20;
	    while (++i <= total)
		if (cmdtable[i].desc[1] == cmd)
		    break;
	}

	if (i > total || !HAS_PERM(cmdtable[i].level))
	    continue;

	if (refscreen) {
	    showtitle(cmdtitle, BBSName);

	    show_menu(cmdtable);

	    show_status();
	    refscreen = NA;
	}
	cursor_clear(menu_row + pos, menu_column);
	n = pos = -1;
	while (++n <= (lastcmdptr = i))
	    if (HAS_PERM(cmdtable[n].level))
		pos++;

	cursor_show(menu_row + pos, menu_column);
    } while (((cmd = igetch()) != EOF) || refscreen);

    abort_bbs(0);
}
/* INDENT OFF */

/* administrator's maintain menu */
static commands_t adminlist[] = {
    {m_user, PERM_ACCOUNTS,           "UUser          使用者資料"},
    {search_user_bypwd, PERM_SYSOP,   "SSearch User   特殊搜尋使用者"},
    {search_user_bybakpwd,PERM_SYSOP, "OOld User data 查閱\備份使用者資料"},
    {m_board, PERM_SYSOP,             "BBoard         設定看板"},
    {m_register, PERM_SYSOP,          "RRegister      審核註冊表單"},
    {cat_register, PERM_SYSOP,        "CCatregister   無法審核時用的"},
    {x_file, PERM_SYSOP|PERM_VIEWSYSOP,     "XXfile         編輯系統檔案"},
    {give_money, PERM_SYSOP|PERM_VIEWSYSOP, "GGivemoney     紅包雞"},
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
static commands_t maillist[] = {
    {m_new, PERM_READMAIL,      "RNew           閱\讀新進郵件"},
    {m_read, PERM_READMAIL,     "RRead          多功\能讀信選單"},
    {m_send, PERM_BASIC,        "RSend          站內寄信"},
    {x_love, PERM_LOGINOK,      "PPaper         \033[1;32m情書產生器\033[m "},
    {mail_list, PERM_BASIC,     "RMail List     群組寄信"},
    {setforward, PERM_LOGINOK,"FForward       \033[32m設定信箱自動轉寄\033[m"},
    {m_sysop, 0,                "YYes, sir!     諂媚站長"},
    {m_internet, PERM_INTERNET, "RInternet      寄信到 Internet"},
    {mail_mbox, PERM_INTERNET,  "RZip UserHome  把所有私人資料打包回去"},
    {built_mail_index, PERM_LOGINOK, "SSavemail      重建信箱索引"},
    {mail_all, PERM_SYSOP,      "RAll           寄信給所有使用者"},
    {NULL, 0, NULL}
};

/* Talk menu */
static commands_t talklist[] = {
    {t_users, 0,            "UUsers         完全聊天手冊"},
    {t_pager, PERM_BASIC,   "PPager         切換呼叫器"},
    {t_idle, 0,             "IIdle          發呆"},
    {t_query, 0,            "QQuery         查詢網友"},
    {t_qchicken, 0,         "WWatch Pet     查詢寵物"},
    {t_talk, PERM_PAGE,     "TTalk          找人聊聊"},
    {t_chat, PERM_CHAT,     "CChat          找家茶坊喫茶去"},
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

static commands_t namelist[] = {
    {t_override, PERM_LOGINOK,"OOverRide      好友名單"},
    {t_reject, PERM_LOGINOK,  "BBlack         壞人名單"},
    {t_aloha,PERM_LOGINOK,    "AALOHA         上站通知名單"},
#ifdef POSTNOTIFY
    {t_post,PERM_LOGINOK,     "NNewPost       新文章通知名單"},
#endif
    {t_special,PERM_LOGINOK,  "SSpecial       其他特別名單"},
    {NULL, 0, NULL}
};

/* User menu */
static commands_t userlist[] = {
    {u_info, PERM_LOGINOK,          "IInfo          設定個人資料與密碼"},
    {calendar, PERM_LOGINOK,          "CCalendar      個人行事曆"},
    {u_editcalendar, PERM_LOGINOK,    "EEditCalendar  編輯個人行事曆"},
    {u_loginview, PERM_LOGINOK,     "LLogin View    選擇進站畫面"},
    {u_ansi, 0, "AANSI          切換 ANSI \033[36m彩\033[35m色\033[37m/"
     "\033[30;47m黑\033[1;37m白\033[m模示"},
    {u_movie, 0,                    "MMovie         切換動畫模示"},
#ifdef  HAVE_SUICIDE
    {u_kill, PERM_BASIC,            "IKill          自殺！！"},
#endif
    {u_editplan, PERM_LOGINOK,      "QQueryEdit     編輯名片檔"},
    {u_editsig, PERM_LOGINOK,       "SSignature     編輯簽名檔"},
#if HAVE_FREECLOAK
    {u_cloak, PERM_LOGINOK,           "CCloak         隱身術"},
#else
    {u_cloak, PERM_CLOAK,           "CCloak         隱身術"},
#endif
    {u_register, PERM_BASIC,        "RRegister      填寫《註冊申請單》"},
    {u_list, PERM_SYSOP,            "UUsers         列出註冊名單"},
    {NULL, 0, NULL}
};

/* XYZ tool menu */
static commands_t xyzlist[] = {
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
    {x_birth, 0,     "BBirthday      《今日壽星大觀》"},
    {p_sysinfo, 0,   "XXinfo         《查看系統資訊》"},
    {NULL, 0, NULL}
};

/* Ptt money menu */
static commands_t moneylist[] = {
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

static int p_money() {
    domenu(PSALE, "Ｐtt量販店", '0', moneylist);
    return 0;
};

#if 0
static commands_t jceelist[] = {
    {x_90,PERM_LOGINOK,	     "0090 JCEE     【90學年度大學聯招查榜系統】"},
    {x_89,PERM_LOGINOK,	     "1189 JCEE     【89學年度大學聯招查榜系統】"},
    {x_88,PERM_LOGINOK,      "2288 JCEE     【88學年度大學聯招查榜系統】"},
    {x_87,PERM_LOGINOK,      "3387 JCEE     【87學年度大學聯招查榜系統】"},
    {x_86,PERM_LOGINOK,      "4486 JCEE     【86學年度大學聯招查榜系統】"},
    {NULL, 0, NULL}
};

static int m_jcee() {
    domenu(JCEE, "Ｐtt查榜系統", '0', jceelist);
    return 0;
}
#endif

static int forsearch();
static int playground();
static int chessroom();

/* Ptt Play menu */
static commands_t playlist[] = {
#if 0
#if HAVE_JCEE
    {m_jcee, PERM_LOGINOK,   "JJCEE        【 大學聯考查榜系統 】"},
#endif
#endif
    {note, PERM_LOGINOK,     "NNote        【 刻刻流言板 】"},
    {x_weather,0 ,           "WWeather     【 氣象預報 】"},
/* XXX 壞掉了 */
/*    {x_stock,0 ,             "SStock       【 股市行情 】"},*/
    {forsearch,PERM_LOGINOK, "SSearchEngine【\033[1;35m Ｐtt搜尋器 \033[m】"},
    {topsong,PERM_LOGINOK,   "TTop Songs   【\033[1;32m歐桑點歌排行榜\033[m】"},
    {p_money,PERM_LOGINOK,   "PPay         【\033[1;31m Ｐtt量販店 \033[m】"},
    {chicken_main,PERM_LOGINOK, "CChicken     "
     "【\033[1;34m Ｐtt養雞場 \033[m】"},
    {playground,PERM_LOGINOK, "AAmusement   【\033[1;33m Ｐtt遊樂場 \033[m】"},
    {chessroom, PERM_LOGINOK, "BChineseChess 【\033[1;34m Ｐtt棋院\033[m】"},
    {NULL, 0, NULL}
};

static commands_t chesslist[] = {
    {chc_main, PERM_LOGINOK, "1ChessFight    【\033[1;33m  邀 局  \033[m】"},
    {chc_personal, PERM_LOGINOK, "2SelfPlay      【\033[1;34m  打 譜  \033[m】"},
    {chc_watch, PERM_LOGINOK, "3ChessWatch    【\033[1;35m  觀 棋  \033[m】"},
    {NULL, 0, NULL}
};

static int chessroom() {
    domenu(CHC, "Ｐtt棋院", '1', chesslist);
    return 0;
}

static commands_t plist[] = {

/*    {p_ticket_main, PERM_LOGINOK,"00Pre         【 總統機 】"},
      {alive, PERM_LOGINOK,        "00Alive       【  訂票雞  】"},
*/
    {ticket_main, PERM_LOGINOK,  "11Gamble      【 Ｐtt賭場 】"},
    {guess_main, PERM_LOGINOK,   "22Guess number【 猜數字   】"},
    {othello_main, PERM_LOGINOK, "33Othello     【 黑白棋   】"},
//    {dice_main, PERM_LOGINOK,    "44Dice        【 玩骰子   】"},
    {vice_main, PERM_LOGINOK,    "44Vice        【 發票對獎 】"},
    {g_card_jack, PERM_LOGINOK,  "55Jack        【 黑傑克 】"},
    {g_ten_helf, PERM_LOGINOK,   "66Tenhalf     【 十點半 】"},
    {card_99, PERM_LOGINOK,      "77Nine        【 九十九 】"},
    {NULL, 0, NULL}
};

static int playground() {
    domenu(AMUSE, "Ｐtt遊樂場",'1',plist);
    return 0;
}

static commands_t slist[] = {
    {x_dict,0,                   "11Dictionary  "
     "【\033[1;33m 趣味大字典 \033[m】"},
    {x_mrtmap, 0,                "22MRTmap      "
	 "【\033[1;34m  捷運地圖  \033[m】"},
    {main_railway, PERM_LOGINOK,  "33Railway     "
     "【\033[1;32m 火車表查詢 \033[m】"},
    {NULL, 0, NULL}
};

static int forsearch() {
    domenu(SREG, "Ｐtt搜尋器", '1', slist);
    return 0;
}

/* main menu */

int admin()
{
    domenu(ADMIN, "系統維護", 'X', adminlist);
    return 0;
}

int Mail()
{
    domenu(MAIL, "電子郵件", 'R', maillist);
    return 0;
}

int Talk()
{
    domenu(TMENU, "聊天說話", 'U', talklist);
    return 0;
}

int User()
{
    domenu(UMENU, "個人設定", 'A', userlist);
    return 0;
}

int Xyz()
{
    domenu(XMENU, "工具程式", 'M', xyzlist);
    return 0;
}

int Play_Play()
{
    domenu(PMENU, "網路遊樂場", 'A', playlist);
    return 0;
}

int Name_Menu()
{
    domenu(NMENU, "白色恐怖", 'O', namelist);
    return 0;
}
 
