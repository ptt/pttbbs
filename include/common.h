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

#define MSG_DEL_CANCEL  "取消刪除"
#define MSG_SELECT_BOARD          "\033[7m【 選擇看板 】\033[m\n" \
                                  "請輸入看板名稱(按空白鍵自動搜尋)："
#define MSG_CLOAKED     "哈哈！我隱形了！看不到勒... :P"
#define MSG_UNCLOAK     "我要重現江湖了...."
#define MSG_BIG_BOY     "我是大帥哥! ^o^Y"
#define MSG_BIG_GIRL    "世紀大美女 *^-^*"
#define MSG_LITTLE_BOY  "我是底迪啦... =)"
#define MSG_LITTLE_GIRL "最可愛的美眉! :>"
#define MSG_MAN         "麥當勞叔叔 (^O^)"
#define MSG_WOMAN       "叫我小阿姨!! /:>"
#define MSG_PLANT       "植物也有性別喔.."
#define MSG_MIME        "礦物總沒性別了吧"
#define MSG_PASSWD      "請輸入您的密碼: "
#define MSG_POSTER      "\033[34;46m 文章選讀 "\
                        "\033[31;47m (y)\033[30m回信 "\
			"\033[31m(=[]<>)\033[30m相關主題 "\
			"\033[31m(/?)\033[30m搜尋標題 "\
			"\033[31m(aA)\033[30m搜尋作者 "\
			"\033[31m(x)\033[30m轉錄 "\
			"\033[31m(V)\033[30m投票 \033[m"
#define MSG_SEPERATOR   "\
───────────────────────────────────────"

#define MSG_CLOAKED     "哈哈！我隱形了！看不到勒... :P"
#define MSG_UNCLOAK     "我要重現江湖了...."

#define MSG_WORKING     "處理中，請稍候..."

#define MSG_CANCEL      "取消。"
#define MSG_USR_LEFT    "User 已經離開了"
#define MSG_NOBODY      "目前無人上線"

#define MSG_DEL_OK      "刪除完畢"
#define MSG_DEL_CANCEL  "取消刪除"
#define MSG_DEL_ERROR   "刪除錯誤"
#define MSG_DEL_NY      "請確定刪除(Y/N)?[N] "

#define MSG_FWD_OK      "文章轉寄完成!"
#define MSG_FWD_ERR1    "轉寄失誤: system error"
#define MSG_FWD_ERR2    "轉寄失誤: address error"

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
#define MSG_MAILER      \
"\033[34;46m 鴻雁往返 \033[31;47m(R)\033[30m回信\033[31m(x)\033[30m轉達\
\033[31m(y)\033[30m群組回信\033[31m(D)\033[30m刪除\
\033[31m(c)\033[30m收入信件夾\033[31m(z)\033[30m信件夾 \033[31m[G]\033[30m繼續?\033[0m"
#define MSG_SHORTULIST  "\033[7m\
使用者代號    目前狀態   │使用者代號    目前狀態   │使用者代號    目前狀態  \033[0m"


#define STR_AUTHOR1     "作者:"
#define STR_AUTHOR2     "發信人:"
#define STR_POST1       "看板:"
#define STR_POST2       "站內:"

/* Flags to getdata input function */
#define NOECHO       0
#define DOECHO       1
#define LCECHO       2

#define YEA  1		       /* Booleans  (Yep, for true and false) */
#define NA   0

/* 好友關係 */
#define IRH 1   /* I reject him.             */
#define HRM 2   /* He reject me.             */
#define IBH 4   /* I am board friend of him. */
#define IFH 8   /* I am friend of him.       */
#define HFM 16  /* He is friend of me.       */
#define ST_FRIEND  (IBH | IFH | HFM)
#define ST_REJECT  (IRH | HRM)       

/* 鍵盤設定 */
#define KEY_TAB         9
#define KEY_ESC         27
#define KEY_UP          0x0101
#define KEY_DOWN        0x0102
#define KEY_RIGHT       0x0103
#define KEY_LEFT        0x0104
#define KEY_HOME        0x0201
#define KEY_INS         0x0202
#define KEY_DEL         0x0203
#define KEY_END         0x0204
#define KEY_PGUP        0x0205
#define KEY_PGDN        0x0206

#define QCAST           int (*)(const void *, const void *)
#define Ctrl(c)         (c & 037)
#define chartoupper(c)  ((c >= 'a' && c <= 'z') ? c+'A'-'a' : c)

#define LEN_AUTHOR1     5
#define LEN_AUTHOR2     7

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

#define CHE_O(c)          ((c) >> 3)
#define CHE_P(c)          ((c) & 7)
#define RTL(x)            (((x) - 3) >> 1)
#define dim(x)          (sizeof(x) / sizeof(x[0]))
#define LTR(x)            ((x) * 2 + 3)
#define CHE(a, b)         ((a) | ((b) << 3))

#define MAX_MODES 127

#ifndef MIN
#define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define	MAX(a,b)	(((a)>(b))?(a):(b))
#endif

#define char_lower(c)  ((c >= 'A' && c <= 'Z') ? c|32 : c)

#define STR_CURSOR      "●"
#define STR_UNCUR       "  "
#endif
