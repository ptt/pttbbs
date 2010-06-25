/* $Id$ */
#ifndef INCLUDE_COMMON_H
#define INCLUDE_COMMON_H

#define STR_GUEST	"guest"	    // guest account
#define STR_REGNEW	"new"	    // 用來建新帳號的名稱

#define DEFAULT_BOARD   str_sysop

// BBS Configuration Files
#define FN_CONF_EDITABLE	"etc/editable"	    // 站務可編輯的系統檔案列表
#define FN_CONF_RESERVED_ID	"etc/reserved.id"   // 保留系統用無法註冊的 ID
#define FN_CONF_BINDPORTS	"etc/bindports.conf"   // 預設要提供連線服務的 port 列表

// BBS Data File Names
#define FN_PASSWD       BBSHOME "/.PASSWDS"      /* User records */
#define FN_CHICKEN	"chicken"
#define FN_USSONG       "ussong"        /* 點歌統計 */
#define FN_POST_NOTE    "post.note"     /* po文章備忘錄 */
#define FN_POST_BID     "post.bid"
#define FN_MONEY        "etc/money"
#define FN_OVERRIDES    "overrides"
#define FN_REJECT       "reject"
#define FN_WATER        "water"
#define FN_CANVOTE      "can_vote"
#define FN_VISABLE      "visable"	// 不知道是誰拼錯的，將錯就錯吧...
#define FN_USIES        "usies"         /* BBS log */
#define FN_DIR		".DIR"
#define FN_BOARD        ".BRD"       /* board list */
#define FN_USEBOARD     "usboard"       /* 看板統計 */
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
#define FN_USERMEMO	"memo.txt"	// 使用者個人記事本
#define FN_BADLOGIN	"logins.bad"	// in BBSHOME & user directory
#define FN_RECENTLOGIN	"logins.recent"	// in user directory
#ifndef SZ_RECENTLOGIN
#define SZ_RECENTLOGIN	(16000)		// size of max recent log before rotation
#endif


// 自訂刪除文章時出現的標題與檔案
#ifndef FN_SAFEDEL
#define FN_SAFEDEL	".deleted"
#endif
#ifndef STR_SAFEDEL_TITLE
#define STR_SAFEDEL_TITLE   "(本文已被刪除)"
#endif 

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
#define MSG_FWD_ERR1    "轉寄錯誤: 系統錯誤 system error"
#define MSG_FWD_ERR2    "轉寄錯誤: 位址錯誤 address error"

#define MSG_SURE_NY     "請您確定(Y/N)？[N] "
#define MSG_SURE_YN     "請您確定(Y/N)？[Y] "

#define MSG_BID         "請輸入看板名稱: "
#define MSG_UID         "請輸入使用者代號: "
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
#define ERR_PASSWD      "密碼不對喔！請檢查帳號及密碼大小寫有無輸入錯誤。"
#define ERR_FILENAME    "檔名不正確！"

#define TN_ANNOUNCE	"[公告]"

#define STR_AUTHOR1     "作者:"
#define STR_AUTHOR2     "發信人:"
#define STR_POST1       "看板:"
#define STR_POST2       "站內:"

#define STR_LOGINDAYS	"登入次數"
#define STR_LOGINDAYS_QTY "次"

/* AIDS */
#define AID_DISPLAYNAME	"文章代碼(AID)"
/* end of AIDS */

/* QUERY_ARTICLE_URL */
#define URL_DISPLAYNAME "文章網址"
/* end of QUERY_ARTICLE_URL */

/* LONG MESSAGES */
#define MSG_SELECT_BOARD ANSI_COLOR(7) "【 選擇看板 】" ANSI_RESET "\n" \
			"請輸入看板名稱(按空白鍵自動搜尋): "

#define MSG_SEPARATOR   "\
───────────────────────────────────────"

/* Flags to getdata input function */
#define NOECHO       0
#define DOECHO       1
#define LCECHO       2
#define NUMECHO	     4
#define GCARRY	     8	// (from M3) do not empty input buffer. 

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

#define QCAST           int (*)(const void *, const void *)
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
// #define FRIEND_POST     3	    // deprecated
#define FRIEND_SPECIAL  4
#define FRIEND_CANVOTE  5
#define BOARD_WATER     6
#define BOARD_VISABLE   7 

#define LOCK_THIS   1    // lock這線不能重複玩
#define LOCK_MULTI  2    // lock所有線不能重複玩   

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
#define EDITFLAG_ALLOWTITLE (0x00000008)
// #define EDITFLAG_ANONYMOUS  (0x00000010)
#define EDITFLAG_KIND_NEWPOST	(0x00000010)
#define EDITFLAG_KIND_REPLYPOST	(0x00000020)
#define EDITFLAG_KIND_SENDMAIL	(0x00000040)
#define EDIT_ABORTED	-1

/* ----------------------------------------------------- */
/* Grayout Levels                                        */
/* ----------------------------------------------------- */
#define GRAYOUT_COLORBOLD (-2)
#define GRAYOUT_BOLD (-1)
#define GRAYOUT_DARK (0)
#define GRAYOUT_NORM (1)
#define GRAYOUT_COLORNORM (+2)

/* Typeahead */
#define TYPEAHEAD_NONE	(-1)
#define TYPEAHEAD_STDIN	(0)

/* ----------------------------------------------------- */
/* Constants                                             */
/* ----------------------------------------------------- */
#define DAY_SECONDS	    (86400)
#define MONTH_SECONDS	    (DAY_SECONDS*30)
#define MILLISECONDS	    (1000)  // milliseconds of one second

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
