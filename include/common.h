/* $Id$ */
#ifndef INCLUDE_COMMON_H
#define INCLUDE_COMMON_H

#define STR_GUEST                 "guest"
#define DEFAULT_BOARD   str_sysop

#define FN_PASSWD       BBSHOME "/.PASSWDS"      /* User records */
#define FN_USSONG       "ussong"        /* 點歌統計 */
#define FN_POST_NOTE    "post.note"     /* po文章備忘錄 */
#define FN_POST_BID     "post.bid"
#define FN_MONEY        "etc/money"
#define FN_OVERRIDES    "overrides"
#define FN_REJECT       "reject"
#define FN_WATER        "water"
#define FN_CANVOTE      "can_vote"
#define FN_VISABLE      "visable"
#define FN_USIES        "usies"         /* BBS log */
#define FN_DIR		".DIR"
#define FN_BOARD        ".BRD"       /* board list */
#define FN_USEBOARD     "usboard"       /* 看板統計 */
#define FN_NOTE_ANS     "note.ans"
#define FN_TOPSONG      "etc/topsong"
#define FN_OVERRIDES    "overrides"
#define FN_TICKET       "ticket"
#define FN_TICKET_END   "ticket.end"
#define FN_TICKET_LOCK  "ticket.end.lock"
#define FN_TICKET_ITEMS "ticket.items"
#define FN_TICKET_RECORD "ticket.data"
#define FN_TICKET_USER    "ticket.user"  
#define FN_TICKET_OUTCOME "ticket.outcome"
#define FN_TICKET_BRDLIST "boardlist"
#define FN_BRDLISTHELP	"etc/boardlist.help"
#define FN_BOARDHELP	"etc/board.help"

#define MSG_DEL_CANCEL  "取消刪除"
#define MSG_BIG_BOY     "我是大帥哥! ^o^Y"
#define MSG_BIG_GIRL    "世紀大美女 *^-^*"
#define MSG_LITTLE_BOY  "我是底迪啦... =)"
#define MSG_LITTLE_GIRL "最可愛的美眉! :>"
#define MSG_MAN         "麥當勞叔叔 (^O^)"
#define MSG_WOMAN       "叫我小阿姨!! /:>"
#define MSG_PLANT       "植物也有性別喔.."
#define MSG_MIME        "礦物總沒性別了吧"

#define MSG_CLOAKED     "哈哈！我隱形了！看不到勒... :P"
#define MSG_UNCLOAK     "我要重現江湖了...."

#define MSG_WORKING     "處理中，請稍候..."

#define MSG_CANCEL      "取消。"
#define MSG_USR_LEFT    "使用者已經離開了"
#define MSG_NOBODY      "目前無人上線"

#define MSG_DEL_OK      "刪除完畢"
#define MSG_DEL_CANCEL  "取消刪除"
#define MSG_DEL_ERROR   "刪除錯誤"
#define MSG_DEL_NY      "請確定刪除(Y/N)?[N] "

#define MSG_FWD_OK      "文章轉寄完成!"
#define MSG_FWD_ERR1    "轉寄失誤: 系統錯誤 system error"
#define MSG_FWD_ERR2    "轉寄失誤: 位址錯誤 address error"

#define MSG_SURE_NY     "請您確定(Y/N)？[N] "
#define MSG_SURE_YN     "請您確定(Y/N)？[Y] "

#define MSG_BID         "請輸入看板名稱："
#define MSG_UID         "請輸入使用者代號："
#define MSG_PASSWD      "請輸入您的密碼: "

#define MSG_BIG_BOY     "我是大帥哥! ^o^Y"
#define MSG_BIG_GIRL    "世紀大美女 *^-^*"
#define MSG_LITTLE_BOY  "我是底迪啦... =)"
#define MSG_LITTLE_GIRL "最可愛的美眉! :>"
#define MSG_MAN         "麥當勞叔叔 (^O^)"
#define MSG_WOMAN       "叫我小阿姨!! /:>"
#define MSG_PLANT       "植物也有性別喔.."
#define MSG_MIME        "礦物總沒性別了吧"

#define ERR_BOARD_OPEN  ".BOARD 開啟錯誤"
#define ERR_BOARD_UPDATE        ".BOARD 更新有誤"
#define ERR_PASSWD_OPEN ".PASSWDS 開啟錯誤"

#define ERR_BID         "你搞錯了啦！沒有這個板喔！"
#define ERR_UID         "這裡沒有這個人啦！"
#define ERR_PASSWD      "密碼不對喔！你有沒有冒用人家的名字啊？"
#define ERR_FILENAME    "檔名不合法！"

#define STR_AUTHOR1     "作者:"
#define STR_AUTHOR2     "發信人:"
#define STR_POST1       "看板:"
#define STR_POST2       "站內:"

/* AIDS */
#define AID_DISPLAYNAME	"文章代碼(AID)"
/* end of AIDS */

/* LONG MESSAGES */
#define MSG_SELECT_BOARD ANSI_COLOR(7) "【 選擇看板 】" ANSI_RESET "\n" \
			"請輸入看板名稱(按空白鍵自動搜尋)："

#define MSG_POSTER_LEN (78)
#define MSG_POSTER      ANSI_COLOR(34;46) " 文章選讀 "\
	ANSI_COLOR(31;47) " (y)"	ANSI_COLOR(30) "回信"\
	ANSI_COLOR(31) "(X%)" 	ANSI_COLOR(30) "推文"\
	ANSI_COLOR(31) "(x)" 	ANSI_COLOR(30) "轉錄 "\
	ANSI_COLOR(31) "(=[]<>)" 	ANSI_COLOR(30) "相關主題 "\
	ANSI_COLOR(31) "(/?a)" 	ANSI_COLOR(30) "搜尋標題/作者  "\
	ANSI_COLOR(31) "(V)" 	ANSI_COLOR(30) "投票 "\
	""
#define MSG_MAILER_LEN (78)
#define MSG_MAILER      \
	ANSI_COLOR(34;46) " 鴻雁往返 " \
	ANSI_COLOR(31;47) " (R)"	ANSI_COLOR(30) "回信" \
	ANSI_COLOR(31) "(x)"	ANSI_COLOR(30) "轉寄" \
	ANSI_COLOR(31) "(y)" 	ANSI_COLOR(30) "回群組信 " \
	ANSI_COLOR(31) "(D)" 	ANSI_COLOR(30) "刪除 " \
	ANSI_COLOR(31) "(c)" 	ANSI_COLOR(30) "收入信件夾" \
	ANSI_COLOR(31) "(z)" 	ANSI_COLOR(30) "信件夾 " \
	ANSI_COLOR(31) "←[q]" 	ANSI_COLOR(30) "離開 " \
	""

#define MSG_SEPERATOR   "\
───────────────────────────────────────"

/* Flags to getdata input function */
#define NOECHO       0
#define DOECHO       1
#define LCECHO       2
#define NUMECHO	     4

#define YEA  1		       /* Booleans  (Yep, for true and false) */
#define NA   0

#define EQUSTR 0	/* for strcmp */

/* 好友關係 */
#define IRH 1   /* I reject him.		*/
#define HRM 2   /* He reject me.		*/
#define IBH 4   /* I am board friend of him.	*/
#define IFH 8   /* He is one of my friends.	*/
#define HFM 16  /* I am one of his friends.	*/
#define ST_FRIEND  (IBH | IFH | HFM)
#define ST_REJECT  (IRH | HRM)       

/* 鍵盤設定 */
#define KEY_TAB         9
#define KEY_ESC         27
#define KEY_UP          0x0101
#define KEY_DOWN        0x0102
#define KEY_RIGHT       0x0103
#define KEY_LEFT        0x0104
#define KEY_STAB        0x0105	/* shift-tab */
#define KEY_HOME        0x0201
#define KEY_INS         0x0202
#define KEY_DEL         0x0203
#define KEY_END         0x0204
#define KEY_PGUP        0x0205
#define KEY_PGDN        0x0206
#define KEY_F1		0x0301
#define KEY_F2		0x0302
#define KEY_F3		0x0303
#define KEY_F4		0x0304
#define KEY_F5		0x0305
#define KEY_F6		0x0306
#define KEY_F7		0x0307
#define KEY_F8		0x0308
#define KEY_F9		0x0309
#define KEY_F10		0x030A
#define KEY_F11		0x030B
#define KEY_F12		0x030C
#define KEY_UNKNOWN	0x0FFF	/* unknown sequence */

#define QCAST           int (*)(const void *, const void *)
#define Ctrl(c)         (c & 037)
#define chartoupper(c)  ((c >= 'a' && c <= 'z') ? c+'A'-'a' : c)

#define LEN_AUTHOR1     5
#define LEN_AUTHOR2     7

/* the title of article will be truncate to PROPER_TITLE_LEN */
#define PROPER_TITLE_LEN	42


/* ----------------------------------------------------- */
/* 水球模式 邊界定義                                     */
/* ----------------------------------------------------- */
#define WB_OFO_USER_TOP		7
#define WB_OFO_USER_BOTTOM	11
#define WB_OFO_USER_HEIGHT	((WB_OFO_MSG_BOTTOM) - (WB_OFO_MSG_TOP) + 1)
#define WB_OFO_USER_LEFT	28
#define WB_OFO_MSG_TOP		15
#define WB_OFO_MSG_BOTTOM	22
#define WB_OFO_MSG_LEFT		4


/* ----------------------------------------------------- */
/* 群組名單模式   Ptt                                    */
/* ----------------------------------------------------- */
#define FRIEND_OVERRIDE 0
#define FRIEND_REJECT   1
#define FRIEND_ALOHA    2
#define FRIEND_POST     3         
#define FRIEND_SPECIAL  4
#define FRIEND_CANVOTE  5
#define BOARD_WATER     6
#define BOARD_VISABLE   7 

#define LOCK_THIS   1    // lock這線不能重複玩
#define LOCK_MULTI  2    // lock所有線不能重複玩   

#define I_TIMEOUT   (-2)       /* Used for the getchar routine select call */
#define I_OTHERDATA (-333)     /* interface, (-3) will conflict with chinese */

#define MAX_MODES	(127)
#define MAX_RECOMMENDS  (100)

#ifndef MIN
#define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define	MAX(a,b)	(((a)>(b))?(a):(b))
#endif

#define toSTR(x)	__toSTR(x)
#define __toSTR(x)	#x

#define char_lower(c)  ((c >= 'A' && c <= 'Z') ? c|32 : c)

#define STR_CURSOR      "●"
#define STR_UNCUR       "  "

#define NOTREPLYING     -1
#define REPLYING        0
#define RECVINREPLYING  1

/* ----------------------------------------------------- */
/* 編輯器選項                                            */
/* ----------------------------------------------------- */
#define EDITFLAG_TEXTONLY   (0x00000001)
#define EDITFLAG_UPLOAD	    (0x00000002)
#define EDITFLAG_ALLOWLARGE (0x00000004)

/* ----------------------------------------------------- */
/* Grayout Levels                                        */
/* ----------------------------------------------------- */
#define GRAYOUT_COLORBOLD (-2)
#define GRAYOUT_BOLD (-1)
#define GRAYOUT_DARK (0)
#define GRAYOUT_NORM (1)
#define GRAYOUT_COLORNORM (+2)

/* ----------------------------------------------------- */
/* Macros                                                */
/* ----------------------------------------------------- */

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
    #define __builtin_expect(exp,c) (exp)

#endif

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#ifndef SHM_HUGETLB
#define SHM_HUGETLB	04000	/* segment is mapped via hugetlb */
#endif

#endif
