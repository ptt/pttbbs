/* $Id$ */
#define INCLUDE_VAR_H
#include "bbs.h"

const char * const str_permid[] = {
    "基本權力",			/* PERM_BASIC */
    "進入聊天室",		/* PERM_CHAT */
    "找人聊天",			/* PERM_PAGE */
    "發表文章",			/* PERM_POST */
    "註冊程序認證",		/* PERM_LOGINOK */
    "信件無上限",		/* PERM_MAILLIMIT */
    "隱身術",			/* PERM_CLOAK */
    "看見忍者",			/* PERM_SEECLOAK */
    "永久保留帳號",		/* PERM_XEMPT */
    "站長隱身術",		/* PERM_DENYPOST */
    "板主",			/* PERM_BM */
    "帳號總管",			/* PERM_ACCOUNTS */
    "聊天室總管",		/* PERM_CHATCLOAK */
    "看板總管",			/* PERM_BOARD */
    "站長",			/* PERM_SYSOP */
    "BBSADM",			/* PERM_POSTMARK */
    "不列入排行榜",		/* PERM_NOTOP */
    "違法通緝中",		/* PERM_VIOLATELAW */
#ifdef PLAY_ANGEL
    "可擔任小天使",		/* PERM_ANGEL */
#else
    "未使用",
#endif
    "不允許\認證碼註冊",	/* PERM_NOREGCODE */
    "視覺站長",			/* PERM_VIEWSYSOP */
    "觀察使用者行蹤",		/* PERM_LOGUSER */
    "禠奪公權",		        /* PERM_NOCITIZEN */
    "群組長",			/* PERM_SYSSUPERSUBOP */
    "帳號審核組",		/* PERM_ACCTREG */
    "程式組",			/* PERM_PRG */
    "活動組",			/* PERM_ACTION */
    "美工組",			/* PERM_PAINT */
    "警察總管",			/* PERM_POLICE_MAN */
    "小組長",			/* PERM_SYSSUBOP */
    "退休站長",			/* PERM_OLDSYSOP */
    "警察"			/* PERM_POLICE */
};

const char * const str_permboard[] = {
    "不可 Zap",			/* BRD_NOZAP */
    "不列入統計",		/* BRD_NOCOUNT */
    "不轉信",			/* BRD_NOTRAN */
    "群組板",			/* BRD_GROUPBOARD */
    "隱藏板",			/* BRD_HIDE */
    "限制(不需設定)",		/* BRD_POSTMASK */
    "匿名板",			/* BRD_ANONYMOUS */
    "預設匿名板",		/* BRD_DEFAULTANONYMOUS */
    "違法改進中看板",		/* BRD_BAD */
    "連署專用看板",		/* BRD_VOTEBOARD */
    "已警告要廢除",		/* BRD_WARNEL */
    "熱門看板群組",		/* BRD_TOP */
    "不可推薦",                 /* BRD_NORECOMMEND */
    "布落格",			/* BRD_BLOG */
    "板主設定列入記錄",		/* BRD_BMCOUNT */
    "連結看板",                 /* BRD_SYMBOLIC */
    "不可噓",                   /* BRD_NOBOO */
    "預設 Local Save",          /* BRD_LOCALSAVE */
    "限板友發文",               /* BRD_RESTRICTEDPOST */
    "Guest可以發表",            /* BRD_GUESTPOST */
#ifdef USE_COOLDOWN
    "冷靜",			/* BRD_COOLDOWN */
#else
    "冷靜(本站無效)",		/* BRD_COOLDOWN */
#endif
#ifdef USE_AUTOCPLOG
    "自動留轉錄記錄",		/* BRD_CPLOG */
#else
    "轉錄記錄(本站無效)",	/* BRD_CPLOG */
#endif
    "禁止快速推文",		/* BRD_NOFASTRECMD */
    "推文記錄 IP",		/* BRD_IPLOGRECMD */
    "十八禁",			/* BRD_OVER18 */
    "對齊式推文",		/* BRD_ALIGNEDCMT */
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
};

int             usernum;
int             currmode = 0;
int             currsrmode = 0;
int             curredit = 0;
int             currbid;
char            quote_file[80] = "\0";
char            quote_user[80] = "\0";
char            currtitle[TTLEN + 1] = "\0";
const char     *currboard = "\0";
char            currBM[IDLEN * 3 + 10];
char            margs[64] = "\0";	/* main argv list */
pid_t           currpid;	/* current process ID */
time4_t         login_start_time, last_login_time;
time4_t         start_time;
userec_t        pwcuser;	/* current user structure */
crosspost_t     postrecord;	/* anti cross post */
unsigned int    currbrdattr;
unsigned int    currstat;

/* global string variables */
/* filename */

char * const fn_passwd = FN_PASSWD;
char * const fn_board = FN_BOARD;
char * const fn_note_ans = FN_NOTE_ANS;
const char * const fn_plans = "plans";
const char * const fn_writelog = "writelog";
const char * const fn_talklog = "talklog";
const char * const fn_overrides = FN_OVERRIDES;
const char * const fn_reject = FN_REJECT;
const char * const fn_notes = "notes";
const char * const fn_water = FN_WATER;
const char * const fn_visable = FN_VISABLE;
const char * const fn_mandex = "/.Names";
const char * const fn_boardlisthelp = FN_BRDLISTHELP;
const char * const fn_boardhelp = FN_BOARDHELP;

/* are descript in userec.loginview */

char           * const loginview_file[NUMVIEWFILE][2] = {
    {FN_NOTE_ANS, "酸甜苦辣流言板"},
    {FN_TOPSONG, "點歌排行榜"},
    {"etc/topusr", "十大排行榜"},
    {"etc/topusr100", "百大排行榜"},
    {"etc/weather.tmp", "天氣快報"},
    {"etc/stock.tmp", "股市快報"},
    {"etc/day", "今日十大話題"},
    {"etc/week", "一週五十大話題"},
    {"etc/today", "今天上站人次"},
    {"etc/yesterday", "昨日上站人次"},
    {"etc/history", "歷史上的今天"},
    {"etc/topboardman", "精華區排行榜"},
    {"etc/topboard.tmp", "看板人氣排行榜"},
    {"@calendar", "個人行事曆"},
    {NULL, NULL}
};

/* message */
char           * const msg_separator = MSG_SEPARATOR;

char           * const msg_cancel = MSG_CANCEL;
char           * const msg_usr_left = MSG_USR_LEFT;

char           * const msg_sure_ny = MSG_SURE_NY;
char           * const msg_sure_yn = MSG_SURE_YN;

char           * const msg_bid = MSG_BID;
char           * const msg_uid = MSG_UID;

char           * const msg_del_ok = MSG_DEL_OK;
char           * const msg_del_ny = MSG_DEL_NY;

char           * const msg_fwd_ok = MSG_FWD_OK;
char           * const msg_fwd_err1 = MSG_FWD_ERR1;
char           * const msg_fwd_err2 = MSG_FWD_ERR2;

char           * const err_board_update = ERR_BOARD_UPDATE;
char           * const err_bid = ERR_BID;
char           * const err_uid = ERR_UID;
char           * const err_filename = ERR_FILENAME;

char           * const str_mail_address = "." BBSUSER "@" MYHOSTNAME;
char           * const str_reply = "Re: ";
char           * const str_space = " \t\n\r";
char           * const str_sysop = "SYSOP";
char           * const str_author1 = STR_AUTHOR1;
char           * const str_author2 = STR_AUTHOR2;
char           * const str_post1 = STR_POST1;
char           * const str_post2 = STR_POST2;
char           * const BBSName = BBSNAME;

/* MAX_MODES is defined in common.h */

char           * const ModeTypeTable[] = {
    "發呆",			/* IDLE */
    "主選單",			/* MMENU */
    "系統維護",			/* ADMIN */
    "郵件選單",			/* MAIL */
    "交談選單",			/* TMENU */
    "使用者選單",		/* UMENU */
    "XYZ 選單",			/* XMENU */
    "分類看板",			/* CLASS */
    "Play選單",			/* PMENU */
    "編特別名單",		/* NMENU */
    BBSMNAME2 "量販店",		/* PSALE */
    "發表文章",			/* POSTING */
    "看板列表",			/* READBRD */
    "閱\讀文章",		/* READING */
    "新文章列表",		/* READNEW */
    "選擇看板",			/* SELECT */
    "讀信",			/* RMAIL */
    "寫信",			/* SMAIL */
    "聊天室",			/* CHATING */
    "其他",			/* XMODE */
    "尋找好友",			/* FRIEND */
    "上線使用者",		/* LAUSERS */
    "使用者名單",		/* LUSERS */
    "追蹤站友",			/* MONITOR */
    "呼叫",			/* PAGE */
    "查詢",			/* TQUERY */
    "交談",			/* TALK  */
    "編名片檔",			/* EDITPLAN */
    "編簽名檔",			/* EDITSIG */
    "投票中",			/* VOTING */
    "設定資料",			/* XINFO */
    "寄給站長",			/* MSYSOP */
    "汪汪汪",			/* WWW */
    "打大老二",			/* BIG2 */
    "回應",			/* REPLY */
    "被水球打中",		/* HIT */
    "水球準備中",		/* DBACK */
    "筆記本",			/* NOTE */
    "編輯文章",			/* EDITING */
    "發系統通告",		/* MAILALL */
    "摸兩圈",			/* MJ */
    "電腦擇友",			/* P_FRIEND */
    "上站途中",			/* LOGIN */
    "查字典",			/* DICT */
    "打橋牌",			/* BRIDGE */
    "找檔案",			/* ARCHIE */
    "打地鼠",			/* GOPHER */
    "看News",			/* NEWS */
    "情書產生器",		/* LOVE */
    "編輯輔助器",		/* EDITEXP */
    "申請IP位址",		/* IPREG */
    "網管辦公中",		/* NetAdm */
    "虛擬實業坊",		/* DRINK */
    "計算機",			/* CAL */
    "編輯座右銘",		/* PROVERB */
    "公佈欄",			/* ANNOUNCE */
    "刻流言板",			/* EDNOTE */
    "英漢翻譯機",		/* CDICT */
    "檢視自己物品",		/* LOBJ */
    "點歌",			/* OSONG */
    "與寵物同樂",		/* CHICKEN */
    "玩彩券",			/* TICKET */
    "猜數字",			/* GUESSNUM */
    "遊樂場",			/* AMUSE */
    "單人黑白棋",		/* OTHELLO */
    "玩骰子",			/* DICE */
    "發票對獎",			/* VICE */
    "逼逼摳ing",		/* BBCALL */
    "繳罰單",			/* CROSSPOST */
    "五子棋",			/* M_FIVE */
    "21點ing",			/* JACK_CARD */
    "10點半ing",		/* TENHALF */
    "超級九十九",		/* CARD_99 */
    "火車查詢",			/* RAIL_WAY */
    "搜尋選單",			/* SREG */
    "下象棋",			/* CHC */
    "下暗棋",			/* DARK */
    "NBA大猜測",		/* TMPJACK */
    BBSMNAME2 "查榜系統",		/* JCEE */
    "重編文章",			/* REEDIT */
    "部落格",                   /* BLOGGING */
    "看棋",			/* CHESSWATCHING */
    "下圍棋",			/* UMODE_GO */
    "[系統錯誤]",		/* DEBUGSLEEPING */
    "連六棋",			/* UMODE_CONN6 */
    "黑白棋",			/* REVERSI */
    "BBS-Lua",			/* UMODE_BBSLUA */
    "播放動畫",			/* UMODE_ASCIIMOVIE */
    "",
    "",
    "", // 90
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // 100
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // 110
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // 120
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    ""
};

/* term.c */
int             b_lines = 23; // bottom line of screen (= t_lines - 1)
int             t_lines = 24; // term lines
int             p_lines = 20; // 扣掉 header(3), footer(1), 畫面上可以顯示資料的行數
int             t_columns = 80;

/* refer to ansi.h for *len */
char           * const strtstandout = ANSI_REVERSE;
const int       strtstandoutlen = 4;
char           * const endstandout = ANSI_RESET;
const int        endstandoutlen = 3;
char           * const clearbuf = ESC_STR "[H" ESC_STR "[J";
const int        clearbuflen = 6;
char           * const cleolbuf = ESC_STR "[K";
const int        cleolbuflen = 3;
char           * const scrollrev = ESC_STR "M";
const int       scrollrevlen = 2;
int             automargins = 1;

/* io.c */
time4_t         now;
int             KEY_ESC_arg;
int             watermode = -1;
int             wmofo = NOTREPLYING;
/*
 * PAGER_UI_IS(PAGER_UI_ORIG) | PAGER_UI_IS(PAGER_UI_NEW):
 * ????????????????????
 * Ptt 水球回顧   (FIXME: guessed by scw)
 * watermode = -1 沒在回水球
 *           = 0   在回上一顆水球  (Ctrl-R)
 *           > 0   在回前 n 顆水球 (Ctrl-R Ctrl-R)
 * 
 * PAGER_UI_IS(PAGER_UI_OFO)  by in2
 * wmofo     = NOTREPLYING     沒在回水球
 *           = REPLYING        正在回水球
 *           = RECVINREPLYING  回水球間又接到水球
 *
 * wmofo     >=0  時收到水球將只顯示, 不會到water[]裡,
 *                待回完水球的時候一次寫入.
 */


/* cache.c */
int             numboards = -1;
SHM_t          *SHM;
boardheader_t  *bcache;
userinfo_t     *currutmp;

/* read.c */
int             TagNum = 0;		/* tag's number */
int		TagBoard = -1;		/* TagBoard = 0 : user's mailbox */
                                        /* TagBoard > 0 : bid where last taged */
char            currdirect[64];		/* XXX TODO change this to PATHLEN? */

/* bbs.c */
char            real_name[IDLEN + 1];
char            local_article;

/* mbbsd.c */
char            fromhost[STRLEN] = "\0";
char		fromhost_masked[32] = "\0"; // masked 'fromhost'
char            water_usies = 0;
char            over18 = 0;
char            is_first_login_of_today = 0;
FILE           *fp_writelog = NULL;
water_t         *water, *swater[6], *water_which;

/* chc_play.c */

/* user.c */
#ifdef CHESSCOUNTRY
int user_query_mode;
/*
 * user_query_mode = 0  simple data
 *                 = 1  gomoku chess country data
 *                 = 2  chc chess country data
 *                 = 3  go chess country data
 */
#endif /* defined(CHESSCOUNTRY) */

/* screen.c */
#define scr_lns         t_lines
#define scr_cols        ANSILINELEN
screenline_t   *big_picture = NULL;
char            roll = 0;
char		msg_occupied = 0;

/* gomo.c */
const char     * const bw_chess[] = {"○", "●", "。", "‧"};

/* friend.c */
/* Ptt 各種特別名單的檔名 */
char           *friend_file[8] = {
    FN_OVERRIDES,
    FN_REJECT,
    "alohaed",
    "postlist",
    "", /* may point to other filename */
    FN_CANVOTE,
    FN_WATER,
    FN_VISABLE
};

#ifdef NOKILLWATERBALL
char    reentrant_write_request = 0;
#endif

#ifdef PTTBBS_UTIL
    #ifdef OUTTA_TIMER
	#define COMMON_TIME (SHM->GV2.e.now)
    #else
	#define COMMON_TIME (time(0))
    #endif
#else
    #define COMMON_TIME (now)
#endif
