/* $Id$ */
#define INCLUDE_VAR_H
#include "bbs.h"

char           *str_permid[] = {
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
    "不接受站外的信",		/* PERM_ */
    "沒想到",			/* PERM_ */
    "視覺站長",			/* PERM_VIEWSYSOP */
    "觀察使用者行蹤",		/* PERM_LOGUSER */
    "精華區總整理權",		/* PERM_Announce */
    "公關組",			/* PERM_RELATION */
    "特務組",			/* PERM_SMG */
    "程式組",			/* PERM_PRG */
    "活動組",			/* PERM_ACTION */
    "美工組",			/* PERM_PAINT */
    "立法組",			/* PERM_LAW */
    "小組長",			/* PERM_SYSSUBOP */
    "一級主管",			/* PERM_LSYSOP */
    "Ｐｔｔ"			/* PERM_PTT */
};

char           *str_permboard[] = {
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
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
    "沒想到",
};

int             usernum;
int             currmode = 0;
int             curredit = 0;
int             showansi = 1;
int             paste_level;
int             currbid;
char            quote_file[80] = "\0";
char            quote_user[80] = "\0";
char            paste_title[STRLEN];
char            paste_path[256];
char            currtitle[TTLEN + 1] = "\0";
char            vetitle[TTLEN + 1] = "\0";
char            currowner[IDLEN + 2] = "\0";
char            currauthor[IDLEN + 2] = "\0";
char            currfile[FNLEN];/* current file name @ bbs.c mail.c */
char            currboard[IDLEN + 2];
char            currBM[IDLEN * 3 + 10];
char            reset_color[4] = "\033[m";
char            margs[64] = "\0";	/* main argv list */
pid_t           currpid;	/* current process ID */
time_t          login_start_time;
time_t          start_time;
time_t          paste_time;
userec_t        cuser;		/* current user structure */
userec_t        xuser;		/* lookup user structure */
crosspost_t     postrecord;	/* anti cross post */
unsigned int    currbrdattr;
unsigned int    currstat;
unsigned char   currfmode;	/* current file mode */

/* global string variables */
/* filename */

char           *fn_passwd = FN_PASSWD;
char           *fn_board = FN_BOARD;
char           *fn_note_ans = FN_NOTE_ANS;
char           *fn_register = "register.new";
char           *fn_plans = "plans";
char           *fn_writelog = "writelog";
char           *fn_talklog = "talklog";
char           *fn_overrides = FN_OVERRIDES;
char           *fn_reject = FN_REJECT;
char           *fn_canvote = FN_CANVOTE;
char           *fn_notes = "notes";
char           *fn_water = FN_WATER;
char           *fn_visable = FN_VISABLE;
char           *fn_mandex = "/.Names";
char           *fn_proverb = "proverb";

/* are descript in userec.loginview */

char           *loginview_file[NUMVIEWFILE][2] = {
    {FN_NOTE_ANS, "酸甜苦辣流言板"},
    {FN_TOPSONG, "點歌排行榜"},
    {"etc/topusr", "十大排行榜"},
    {"etc/topusr100", "百大排行榜"},
    {"etc/birth.today", "今日壽星"},
    {"etc/weather.tmp", "天氣快報"},
    {"etc/stock.tmp", "股市快報"},
    {"etc/day", "今日十大話題"},
    {"etc/week", "一週五十大話題"},
    {"etc/today", "今天上站人次"},
    {"etc/yesterday", "昨日上站人次"},
    {"etc/history", "歷史上的今天"},
    {"etc/topboardman", "精華區排行榜"},
    {"etc/topboard.tmp", "看板人氣排行榜"}
};

/* message */
char           *msg_seperator = MSG_SEPERATOR;
char           *msg_mailer = MSG_MAILER;
char           *msg_shortulist = MSG_SHORTULIST;

char           *msg_cancel = MSG_CANCEL;
char           *msg_usr_left = MSG_USR_LEFT;
char           *msg_nobody = MSG_NOBODY;

char           *msg_sure_ny = MSG_SURE_NY;
char           *msg_sure_yn = MSG_SURE_YN;

char           *msg_bid = MSG_BID;
char           *msg_uid = MSG_UID;

char           *msg_del_ok = MSG_DEL_OK;
char           *msg_del_ny = MSG_DEL_NY;

char           *msg_fwd_ok = MSG_FWD_OK;
char           *msg_fwd_err1 = MSG_FWD_ERR1;
char           *msg_fwd_err2 = MSG_FWD_ERR2;

char           *err_board_update = ERR_BOARD_UPDATE;
char           *err_bid = ERR_BID;
char           *err_uid = ERR_UID;
char           *err_filename = ERR_FILENAME;

char           *str_mail_address = "." BBSUSER "@" MYHOSTNAME;
char           *str_new = "new";
char           *str_reply = "Re: ";
char           *str_space = " \t\n\r";
char           *str_sysop = "SYSOP";
char           *str_author1 = STR_AUTHOR1;
char           *str_author2 = STR_AUTHOR2;
char           *str_post1 = STR_POST1;
char           *str_post2 = STR_POST2;
char           *BBSName = BBSNAME;

/* #define MAX_MODES 78 */
/* MAX_MODES is defined in common.h */

char           *ModeTypeTable[MAX_MODES] = {
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
    "Ｐtt量販店",		/* PSALE */
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
    "正在玩小雞",		/* CHICKEN */
    "玩彩券",			/* TICKET */
    "猜數字",			/* GUESSNUM */
    "遊樂場",			/* AMUSE */
    "黑白棋",			/* OTHELLO */
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
    "下暗琪",			/* DARK */
    "NBA大猜測"			/* TMPJACK */
    "Ｐtt查榜系統",		/* JCEE */
    "重編文章"			/* REEDIT */
    "部落格",                   /* BLOGGING */
    "", /* for future usage */
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    ""
};

/* indict.c */
char            dict[41], database[41];

/* term.c */
int             b_lines = 23;
int             t_lines = 24;
int             p_lines = 20;
int             t_columns = 80;
char           *strtstandout = "\33[7m";
int             strtstandoutlen = 4;
char           *endstandout = "\33[m";
int             endstandoutlen = 3;
char           *clearbuf = "\33[H\33[J";
int             clearbuflen = 6;
char           *cleolbuf = "\33[K";
int             cleolbuflen = 3;
char           *scrollrev = "\33M";
int             scrollrevlen = 2;
int             automargins = 1;

/* io.c */
time_t          now;
int             KEY_ESC_arg;
int             watermode = -1;
int             wmofo = -1;
/*
 * WATERMODE(WATER_ORIG) | WATERMODE(WATER_NEW): Ptt 水球回顧 e = -1
 * 沒在回水球 = 0   在回上一顆水球  (Ctrl-R) > 0   在回前 n 顆水球 (Ctrl-R
 * Ctrl-R)
 * 
 * WATERMODE(WATER_OFO)  by in2 wmofo     = -1  沒在回水球 = 0   正在回水球 = 1
 * 回水球間又接到水球 wmofo     >=0 時收到水球將只顯示, 不會到water[]裡,
 * 待回完水球的時候一次寫入.
 */


/* cache.c */
int             numboards = -1;
int            *GLOBALVAR;
SHM_t          *SHM;
boardheader_t  *bcache;
userinfo_t     *currutmp;

/* board.c */
int             class_bid = 0;
int             brc_num;
int             brc_list[BRC_MAXNUM];

/* read.c */
int             TagNum;			/* tag's number */
TagItem         TagList[MAXTAGS];	/* ascending list */
int		TagBoard = -1;		/* TagBoard = 0 : user's mailbox
					   TagBoard > 0 : bid where last taged*/
char            currdirect[64];

/* edit.c */
char            save_title[STRLEN];

/* bbs.c */
time_t          board_visit_time;
char            real_name[IDLEN + 1];
int             local_article;

/* mbbsd.c */
int             talkrequest = NA;
char            fromhost[STRLEN] = "\0";
char            water_usies = 0;
FILE           *fp_writelog = NULL;
water_t         water[6], *swater[6], *water_which = &water[0];

/* announce.c */
char            trans_buffer[256];

/* screen.c */
screenline_t   *big_picture = NULL;
unsigned char   scr_lns;
unsigned short  scr_cols;
char            roll;

/* name.c */
word_t         *toplev;

#ifndef _BBS_UTIL_C_
/* menu.c */
commands_t      cmdlist[] = {
    {admin, PERM_SYSOP | PERM_VIEWSYSOP, "00Admin       【 系統維護區 】"},
    {Announce, 0, "AAnnounce     【 精華公佈欄 】"},
    {Boards, 0, "FFavorite     【 我 的 最愛 】"},
    {root_board, 0, "CClass        【 分組討論區 】"},
    {Mail, PERM_BASIC, "MMail         【 私人信件區 】"},
    {Talk, 0, "TTalk         【 休閒聊天區 】"},
    {User, 0, "UUser         【 個人設定區 】"},
    {Xyz, 0, "XXyz          【 系統工具區 】"},
    {Play_Play, PERM_BASIC, "PPlay         【 娛樂/休閒生活】"},
    {Name_Menu, PERM_LOGINOK, "NNamelist     【 編特別名單 】"},
    {Goodbye, 0, "GGoodbye       離開，再見……"},
    {NULL, 0, NULL}
};
#endif

/* friend.c */
/* Ptt 各種特別名單的檔名 */
char           *friend_file[8] = {
    FN_OVERRIDES,
    FN_REJECT,
    "alohaed",
    "postlist",
    "",
    FN_CANVOTE,
    FN_WATER,
    FN_VISABLE
};

#ifdef PTTBBS_UTIL
    #ifdef OUTTA_TIMER
	#define COMMON_TIME (SHM->GV2.e.now)
    #else
	#define COMMON_TIME (time(0))
    #endif
#else
    #define COMMON_TIME (now)
#endif
