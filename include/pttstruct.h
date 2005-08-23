/* $Id$ */
#ifndef INCLUDE_STRUCT_H
#define INCLUDE_STRUCT_H


#define IDLEN      12             /* Length of bid/uid */

/* 競標資訊 */
#define SALE_COMMENTED 0x1
typedef struct bid_t {
    int     high;	/* 目前最高價 */
    int     buyitnow;	/* 直接購買價 */
    int     usermax;	/* 自動競標最高價 */
    int     increment;	/* 出價增額 */
    char    userid[IDLEN + 1];	/* 最高出價者 */
    time4_t enddate;	/* 結標日期 */
    char    payby;	/* 付款方式 */
	/* 1 cash 2 check or mail 4 wire 8 credit 16 postoffice */
    char    flag;	/* 屬性 (是否已評價) */
    char    pad[2];
    int     shipping;	/* 運費 */
} bid_t;

/* 小雞的資料 */
typedef struct chicken_t {
    char    name[20];
    char    type;             /* 物種 */
    unsigned char   tech[16]; /* 技能 */
    time4_t birthday;         /* 生日 */
    time4_t lastvisit;        /* 上次照顧時間 */
    int     oo;               /* 補品 */
    int     food;             /* 食物 */
    int     medicine;         /* 藥品 */
    int     weight;           /* 體重 */
    int     clean;            /* 乾淨 */
    int     run;              /* 敏捷度 */
    int     attack;           /* 攻擊力 */
    int     book;             /* 知識 */
    int     happy;            /* 快樂 */
    int     satis;            /* 滿意度 */
    int     temperament;      /* 氣質 */
    int     tiredstrong;      /* 疲勞度 */
    int     sick;             /* 病氣指數 */
    int     hp;               /* 血量 */
    int     hp_max;           /* 滿血量 */
    int     mm;               /* 法力 */
    int     mm_max;           /* 滿法力 */
    time4_t cbirth;           /* 實際計算用的生日 */
    int     pad[2];           /* 留著以後用 */
} chicken_t;

#define PASSLEN    14             /* Length of encrypted passwd field */
#define REGLEN     38             /* Length of registration data */

#define PASSWD_VERSION	2275

typedef struct userec_t {
    unsigned int    version;	/* version number of this sturcture, we
    				 * use revision number of project to denote.*/

    char    userid[IDLEN + 1];	/* ID */
    char    realname[20];	/* 真實姓名 */
    char    nickname[24];	/* 暱稱 */
    char    passwd[PASSLEN];	/* 密碼 */
    unsigned int    uflag;	/* 習慣1 */
    unsigned int    uflag2;	/* 習慣2 */
    unsigned int    userlevel;	/* 權限 */
    unsigned int    numlogins;	/* 上站次數 */
    unsigned int    numposts;	/* 文章篇數 */
    time4_t firstlogin;		/* 註冊時間 */
    time4_t lastlogin;		/* 最近上站時間 */
    char    lasthost[16];	/* 上次上站來源 */
    int     money;		/* Ptt幣 */
    char    remoteuser[3];	/* 保留 目前沒用到的 */
    char    proverb;		/* 座右銘 (unused) */
    char    email[50];		/* Email */
    char    address[50];	/* 住址 */
    char    justify[REGLEN + 1];    /* 審核資料 */
    unsigned char   month;	/* 生日 月 */
    unsigned char   day;	/* 生日 日 */
    unsigned char   year;	/* 生日 年 */
    unsigned char   sex;	/* 性別 */
    unsigned char   state;	/* TODO unknown (unused ?) */
    unsigned char   pager;	/* 呼叫器狀態 */
    unsigned char   invisible;	/* 隱形狀態 */
    unsigned int    exmailbox;	/* 購買信箱數 TODO short 就夠了 */
    chicken_t       mychicken;	/* 寵物 */
    time4_t lastsong;		/* 上次點歌時間 */
    unsigned int    loginview;	/* 進站畫面 */
    unsigned char   channel;	/* TODO unused */
    unsigned short  vl_count;	/* 違法記錄 ViolateLaw counter */
    unsigned short  five_win;	/* 五子棋戰績 勝 */
    unsigned short  five_lose;	/* 五子棋戰績 敗 */
    unsigned short  five_tie;	/* 五子棋戰績 和 */
    unsigned short  chc_win;	/* 象棋戰績 勝 */
    unsigned short  chc_lose;	/* 象棋戰績 敗 */
    unsigned short  chc_tie;	/* 象棋戰績 和 */
    int     mobile;		/* 手機號碼 */
    char    mind[4];		/* 心情 not a null-terminate string */
    char    pad0[11];
    unsigned char   signature;	/* 慣用簽名檔 */

    unsigned char   goodpost;	/* 評價為好文章數 */
    unsigned char   badpost;	/* 評價為壞文章數 */
    unsigned char   goodsale;	/* 競標 好的評價  */
    unsigned char   badsale;	/* 競標 壞的評價  */
    char    myangel[IDLEN+1];	/* 我的小天使 */
    unsigned short  chess_elo_rating;	/* 象棋等級分 */
    unsigned int    withme;	/* 我想找人下棋，聊天.... */
    char    pad[34];
} userec_t;
/* these are flags in userec_t.uflag */
#define PAGER_FLAG      0x4     /* true if pager was OFF last session */
#define CLOAK_FLAG      0x8     /* true if cloak was ON last session */
#define FRIEND_FLAG     0x10    /* true if show friends only */
#define BRDSORT_FLAG    0x20    /* true if the boards sorted alphabetical */
#define MOVIE_FLAG      0x40    /* true if show movie */

/* useless flag */
//#define COLOR_FLAG      0x80    /* true if the color mode open */
//#define MIND_FLAG       0x100   /* true if mind search mode open <-Heat*/

#define DBCSAWARE_FLAG	0x200	/* true if DBCS-aware enabled. */
/* please keep this even if you don't have DBCSAWARE features turned on */

/* these are flags in userec_t.uflag2 */
#define WATER_MASK      000003  /* water mask */
#define WATER_ORIG      0x0
#define WATER_NEW       0x1
#define WATER_OFO       0x2
#define WATERMODE(mode) ((cuser.uflag2 & WATER_MASK) == mode)
#define FAVNOHILIGHT    0x10   /* false if hilight favorite */
#define FAVNEW_FLAG     0x20   /* true if add new board into one's fav */
#define FOREIGN         0x100  /* true if a foreign */
#define LIVERIGHT       0x200  /* true if get "liveright" already */
#define REJ_OUTTAMAIL   0x400 /* true if don't accept outside mails */
#define REJECT_OUTTAMAIL (cuser.uflag2 & REJ_OUTTAMAIL)

/* flags in userec_t.withme */
#define WITHME_ALLFLAG	0x55555555
#define WITHME_TALK	0x00000001
#define WITHME_NOTALK	0x00000002
#define WITHME_FIVE	0x00000004
#define WITHME_NOFIVE	0x00000008
#define WITHME_PAT	0x00000010
#define WITHME_NOPAT	0x00000020
#define WITHME_CHESS	0x00000040
#define WITHME_NOCHESS	0x00000080
#define WITHME_DARK	0x00000100
#define WITHME_NODARK	0x00000200
#define WITHME_GO	0x00000400
#define WITHME_NOGO	0x00000800

#ifdef PLAY_ANGEL
#define REJ_QUESTION    0x800 /* true if don't want to be angel for a while */
#define REJECT_QUESTION (cuser.uflag2 & REJ_QUESTION)
#define ANGEL_MASK      0x3000
#define ANGEL_R_MAEL    0x1000 /* true if reject male */
#define ANGEL_R_FEMAEL  0x2000 /* true if reject female */
#define ANGEL_STATUS()  ((cuser.uflag2 & ANGEL_MASK) >> 12)
#define ANGEL_SET(X)    (cuser.uflag2 = (cuser.uflag2 & ~ANGEL_MASK) | \
                          (((X) & 3) << 12))
#endif

#define BTLEN      48             /* Length of board title */

/* TODO 動態更新的欄位不應該跟要寫入檔案的混在一起,
 * 至少用個 struct 包起來之類 */
typedef struct boardheader_t {
    char    brdname[IDLEN + 1];          /* bid */
    char    title[BTLEN + 1];
    char    BM[IDLEN * 3 + 3];           /* BMs' userid, token '/' */
    unsigned int    brdattr;             /* board的屬性 */
    char    chesscountry;
    unsigned char   vote_limit_posts;    /* 連署 : 文章篇數下限 */
    unsigned char   vote_limit_logins;   /* 連署 : 登入次數下限 */
    unsigned char   vote_limit_regtime;  /* 連署 : 註冊時間限制 */
    time4_t bupdate;                     /* note update time */
    unsigned char   post_limit_posts;    /* 發表文章 : 文章篇數下限 */
    unsigned char   post_limit_logins;   /* 發表文章 : 登入次數下限 */
    unsigned char   post_limit_regtime;  /* 發表文章 : 註冊時間限制 */
    unsigned char   bvote;               /* 正舉辦 Vote 數 */
    time4_t vtime;                       /* Vote close time */
    unsigned int    level;               /* 可以看此板的權限 */
    time4_t perm_reload;                 /* 最後設定看板的時間 */
    int     gid;                         /* 看板所屬的類別 ID */
    int     next[2];	                 /* 在同一個gid下一個看板 動態產生*/
    int     firstchild[2];	         /* 屬於這個看板的第一個子看板 */
    int     parent;
    int     childcount;                  /* 有多少個child */
    int     nuser;                       /* 多少人在這板 */
    int     postexpire;                  /* postexpire */
    time4_t endgamble;
    char    posttype[33];
    char    posttype_f;
    unsigned char fastrecommend_pause;	/* 快速連推間隔 */
    char    pad3[49];
} boardheader_t;

#define BRD_NOZAP       000000001         /* 不可zap  */
#define BRD_NOCOUNT     000000002         /* 不列入統計 */
#define BRD_NOTRAN      000000004         /* 不轉信 */
#define BRD_GROUPBOARD  000000010         /* 群組板 */
#define BRD_HIDE        000000020         /* 隱藏板 (看板好友才可看) */
#define BRD_POSTMASK    000000040         /* 限制發表或閱讀 */
#define BRD_ANONYMOUS   000000100         /* 匿名板 */
#define BRD_DEFAULTANONYMOUS 000000200    /* 預設匿名板 */
#define BRD_BAD		000000400         /* 違法改進中看板 */
#define BRD_VOTEBOARD   000001000         /* 連署機看板 */
#define BRD_WARNEL      000002000         /* 連署機看板 */
#define BRD_TOP         000004000         /* 熱門看板群組 */
#define BRD_NORECOMMEND 000010000         /* 不可推薦 */
#define BRD_BLOG        000020000         /* BLOG */
#define BRD_BMCOUNT	000040000	  /* 板主設定列入記錄 */
#define BRD_SYMBOLIC	000100000	  /* symbolic link to board */
#define BRD_NOBOO       000200000         /* 不可噓 */
#define BRD_LOCALSAVE   000400000         /* 預設 Local Save */
#define BRD_RESTRICTEDPOST 001000000      /* 板友才能發文 */
#define BRD_GUESTPOST   002000000         /* guest能 post */
#define BRD_COOLDOWN    004000000         /* 冷靜 */
#define BRD_CPLOG       010000000         /* 自動留轉錄記錄 */
#define BRD_NOFASTRECMD 020000000         /* 禁止快速推文 */

#define BRD_LINK_TARGET(x)	((x)->postexpire)
#define GROUPOP()               (currmode & MODE_GROUPOP)

#ifdef CHESSCOUNTRY
#define CHESSCODE_NONE   0
#define CHESSCODE_FIVE   1
#define CHESSCODE_CCHESS 2
#define CHESSCODE_GO     3
#define CHESSCODE_MAX    3
#endif /* defined(CHESSCOUNTRY) */


#define TTLEN      64             /* Length of title */
#define FNLEN      33             /* Length of filename  */

typedef struct fileheader_t {
    char    filename[FNLEN];         /* M.9876543210.A */
    char    recommend;               /* important level */
    char    owner[IDLEN + 2];        /* uid[.] */
    char    date[6];                 /* [02/02] or space(5) */
    char    title[TTLEN + 1];
    union {
	/* TODO: MOVE money to outside multi!!!!!! */
	int money;
	int anon_uid;
	/* different order to match alignment */
	struct {
	    unsigned char posts;    /* money & 0xff */
	    unsigned char logins;   /* money & 0xff00 */
	    unsigned char regtime;   /* money & 0xff0000 */
	    unsigned char pad[1];   /* money & 0xffff0000 */
	} vote_limits;
	struct {
	    /* is this ordering correct? */
	    unsigned int ref:31;
	    unsigned int flag:1;
	} refer;
    }	    multi;		    /* rocker: if bit32 on ==> reference */
    /* XXX dirty, split into flag and money if money of each file is less than 16bit? */
    unsigned char   filemode;        /* must be last field @ boards.c */
} fileheader_t;

#define FILE_LOCAL      0x1     /* local saved */
#define FILE_READ       0x1     /* already read : mail only */
#define FILE_MARKED     0x2     /* opus: 0x8 */
#define FILE_DIGEST     0x4     /* digest */
#define FILE_BOTTOM     0x8     /* push_bottom */
#define FILE_SOLVED	0x10	/* problem solved, sysop/BM only */
#define FILE_HIDE       0x20    /* hide,	in announce */
#define FILE_BID        0x20    /* bid,		in non-announce */
#define FILE_BM         0x40    /* BM only,	in announce */
#define FILE_VOTE       0x40    /* for vote,	in non-announce */
#define FILE_ANONYMOUS  0x80   /* anonymous file */
/* TODO filemode is unsigned, IS THIS MULTI CORRECT? DANGEROUS!!! */
#define FILE_MULTI      0x100   /* multi send for mail */

#define STRLEN     80             /* Length of most string data */


union xitem_t {
    struct {                    /* bbs_item */
	char    fdate[9];       /* [mm/dd/yy] */
	char    editor[13];      /* user ID */
	char    fname[31];
    } B;
    struct {                    /* gopher_item */
	char    path[81];
	char    server[48];
	int     port;
    } G;
};

typedef struct {
    char    title[63];
    union   xitem_t X;
} item_t;

typedef struct {
    item_t  *item[MAX_ITEMS];
    char    mtitle[STRLEN];
    char    *path;
    int     num, page, now, level;
} gmenu_t;

#define FAVMAX   1024		  /* Max boards of Myfavorite */
#define FAVGMAX    32             /* Max groups of Myfavorite */
#define FAVGSLEN    8		  /* Max Length of Description String */

/* values of msgque_t::msgmode */
#define MSGMODE_TALK      0
#define MSGMODE_WRITE     1
#ifdef PLAY_ANGEL
#define MSGMODE_FROMANGEL 2
#define MSGMODE_TOANGEL   3
#endif

typedef struct msgque_t {
    pid_t   pid;
    char    userid[IDLEN + 1];
    char    last_call_in[76];
    int     msgmode;
} msgque_t;

/* user data in shm */
/* use GAP to detect and avoid data overflow and overriding */
typedef struct userinfo_t {
    int     uid;                  /* Used to find user name in passwd file */
    pid_t   pid;                  /* kill() to notify user of talk request */
    int     sockaddr;             /* ... */

    /* user data */
    unsigned int    userlevel;
    char    userid[IDLEN + 1];
    char    nickname[24];
    char    from[27];               /* machine name the user called in from */
    int     from_alias;
    char    sex;
    unsigned char goodpost;
    unsigned char badpost;
    unsigned char goodsale;
    unsigned char badsale;
    unsigned char angel;

    /* friends */
    int     friendtotal;              /* 好友比較的cache 大小 */ 
    short   nFriends;                /* 下面 friend[] 只用到前幾個,
                                        用來 bsearch */
    int     friend[MAX_FRIEND];
    char    gap_1[4];
    int     friend_online[MAX_FRIEND];/* point到線上好友 utmpshm的位置 */
			          /* 好友比較的cache 前兩個bit是狀態 */
    char    gap_2[4];
    int     reject[MAX_REJECT];
    char    gap_3[4];

    /* messages */
    char    msgcount;
    msgque_t        msgs[MAX_MSGS];
    char    gap_4[sizeof(msgque_t)];   /* avoid msgs racing and overflow */

    /* user status */
    char    birth;                   /* 是否是生日 Ptt*/
    unsigned char   active;         /* When allocated this field is true */
    unsigned char   invisible;      /* Used by cloaking function in Xyz menu */
    unsigned char   mode;           /* UL/DL, Talk Mode, Chat Mode, ... */
    unsigned char   pager;          /* pager toggle, YEA, or NA */
    time4_t lastact;               /* 上次使用者動的時間 */
    char    mailalert;
    char    mind[4];

    /* chatroom/talk/games calling */
    unsigned char   sig;            /* signal type */
    int     destuid;              /* talk uses this to identify who called */
    int     destuip;              /* dest index in utmpshm->uinfo[] */
    unsigned char   sockactive;     /* Used to coordinate talk requests */

    /* chat */
    unsigned char   in_chat;        /* for in_chat commands   */
    char    chatid[11];             /* chat id, if in chat mode */

    /* games */
    unsigned char   lockmode;       /* 不准 multi_login 玩的東西 */
    char    turn;                    /* 遊戲的先後 */
    char    mateid[IDLEN + 1];       /* 遊戲對手的 id */
    char    color;                   /* 暗棋 顏色 */

    /* game record */
    unsigned short  int     five_win;
    unsigned short  int     five_lose;
    unsigned short  int     five_tie;
    unsigned short  int     chc_win;
    unsigned short  int     chc_lose;
    unsigned short  int     chc_tie;
    unsigned short  int     chess_elo_rating;

    /* misc */
    unsigned int    withme;
    unsigned int    brc_id;


#ifdef NOKILLWATERBALL
    time4_t wbtime;
#endif
} userinfo_t;

typedef struct water_t {
    pid_t   pid;
    char    userid[IDLEN + 1];
    int     top, count;
    msgque_t        msg[MAX_REVIEW];
    userinfo_t      *uin;     // Ptt:這可以取代alive
} water_t;

typedef struct {
    int row, col;
    int y, x;
    void *raw_memory;
} screen_backup_t;

typedef struct {
    fileheader_t    *header;
    char    mtitle[STRLEN];
    const char    *path;
    int     num, page, now, level;
} menu_t;

/* Used to pass commands to the readmenu.
 * direct mapping, indexed by ascii code. */
#define onekey_size ((int) 'z')
typedef int (* onekey_t)();

#define ANSILINELEN (511)                /* Maximum Screen width in chars */

/* anti_crosspost */
typedef struct crosspost_t {
    int     checksum[4]; /* 0 -> 'X' cross post  1-3 -> 檢查文章行 */
    short   times;       /* 第幾次 */
    short   last_bid;    /* crossport to which board */
} crosspost_t;

#define SORT_BY_ID    0
#define SORT_BY_CLASS 1
#define SORT_BY_STAT  1
#define SORT_BY_IDLE  2
#define SORT_BY_FROM  3
#define SORT_BY_FIVE  4
#define SORT_BY_SEX   5

typedef struct keeploc_t {
    unsigned int hashkey;
    int     top_ln;
    int     crs_ln;
} keeploc_t;

#define VALID_USHM_ENTRY(X) ((X) >= 0 && (X) < USHM_SIZE)
#define USHM_SIZE       ((MAX_ACTIVE)*10/9)
/* USHM_SIZE 比 MAX_ACTIVE 大是為了防止檢查人數上限時, 又同時衝進來
 * 會造成找 shm 空位的無窮迴圈. 
 * 又, 因 USHM 中用 hash, 空間稍大時效率較好. */

/* MAX_BMs is dirty hardcode 4 in mbbsd/cache.c:is_BM_cache() */
#define MAX_BMs         4                 /* for BMcache, 一個看板最多幾板主 */

#define SHM_VERSION 2582
typedef struct {
    int     version;
    /* uhash */
    /* uhash is a userid->uid hash table -- jochang */
    char    userid[MAX_USERS][IDLEN + 1];
    char    gap_1[IDLEN+1];
    int     next_in_hash[MAX_USERS];
    char    gap_2[sizeof(int)];
    int     money[MAX_USERS];
    char    gap_3[sizeof(int)];
#ifdef USE_COOLDOWN
    time4_t cooldowntime[MAX_USERS];
#endif
    char    gap_4[sizeof(int)];
    int     hash_head[1 << HASH_BITS];
    char    gap_5[sizeof(int)];
    int     number;				/* # of users total */
    int     loaded;				/* .PASSWD has been loaded? */

    /* utmpshm */
    userinfo_t      uinfo[USHM_SIZE];
    char    gap_6[sizeof(userinfo_t)];
    int             sorted[2][8][USHM_SIZE];
                    /* 第一維double buffer 由currsorted指向目前使用的
		       第二維sort type */
    char    gap_7[sizeof(int)];
    int     currsorted;
    time4_t UTMPuptime;
    int     UTMPnumber;
    char    UTMPneedsort;
    char    UTMPbusystate;

    /* brdshm */
    char    gap_8[sizeof(int)];
    int     BMcache[MAX_BOARD][MAX_BMs];
    char    gap_9[sizeof(int)];
    boardheader_t   bcache[MAX_BOARD];
    char    gap_10[sizeof(int)];
    int     bsorted[2][MAX_BOARD]; /* 0: by name 1: by class */ /* 裡頭存的是 bid-1 */
    char    gap_11[sizeof(int)];
#if HOTBOARDCACHE
    unsigned char    nHOTs;
    int              HBcache[HOTBOARDCACHE];
#endif
    char    gap_12[sizeof(int)];
    time4_t busystate_b[MAX_BOARD];
    char    gap_13[sizeof(int)];
    int     total[MAX_BOARD];
    char    gap_14[sizeof(int)];
    unsigned char  n_bottom[MAX_BOARD]; /* number of bottom */
    char    gap_15[sizeof(int)];
    int     hbfl[MAX_BOARD][MAX_FRIEND + 1]; /* hidden board friend list, 0: load time, 1-MAX_FRIEND: uid */
    char    gap_16[sizeof(int)];
    time4_t lastposttime[MAX_BOARD];
    char    gap_17[sizeof(int)];
    time4_t Buptime;
    time4_t Btouchtime;
    int     Bnumber;
    int     Bbusystate;
    time4_t close_vote_time;

    /* pttcache */
    char    notes[MAX_MOVIE][200*11];
    char    gap_18[sizeof(int)];
    char    today_is[20];
    int     n_notes[MAX_MOVIE_SECTION];      /* 一節中有幾個 看板 */
    char    gap_19[sizeof(int)];
    int     next_refresh[MAX_MOVIE_SECTION]; /* 下一次要refresh的 看板 */
    char    gap_20[sizeof(int)];
    msgque_t loginmsg;  /* 進站水球 */
    int     max_film;
    int     max_history;
    time4_t Puptime;
    time4_t Ptouchtime;
    int     Pbusystate;

    /* SHM 中的全域變數, 可用 shmctl 設定或顯示. 供動態調整或測試使用 */
    union {
	int     v[512];
	struct {
	    int     dymaxactive;  /* 動態設定最大人數上限     */
	    int     toomanyusers; /* 超過人數上限不給進的個數 */
	    int     noonlineuser; /* 站上使用者不高亮度顯示   */
#ifdef OUTTA_TIMER
	    time4_t now;
#endif
	    int     nWelcomes;

	    /* 注意, 應保持 align sizeof(int) */
	} e;
    } GV2;
    /* statistic */
    int     statistic[STAT_MAX];

    /* 故鄉 fromcache */
    unsigned int    home_ip[MAX_FROM];
    unsigned int    home_mask[MAX_FROM];
    char            home_desc[MAX_FROM][32];
    int        	    home_num;

    int     max_user;
    time4_t max_time;
    time4_t Fuptime;
    time4_t Ftouchtime;
    int     Fbusystate;

#ifdef I18N    
    /* i18n(internationlization) */
    char	*i18nstrptr[MAX_LANG][MAX_STRING];
    char	i18nstrbody[22 * MAX_LANG * MAX_STRING]; 
    	/* Based on the statistis, we found the lengh of one string
    	   is 22 bytes approximately.
    	*/
#endif    
} SHM_t;

#ifdef SHMALIGNEDSIZE
#   define SHMSIZE (sizeof(SHM_t)/(SHMALIGNEDSIZE)+1)*SHMALIGNEDSIZE
#else
#   define SHMSIZE (sizeof(SHM_t))
#endif

typedef struct {
    unsigned short oldlen;                /* previous line length */
    unsigned short len;                   /* current length of line */
    unsigned short smod;                  /* start of modified data */
    unsigned short emod;                  /* end of modified data */
    unsigned short sso;                   /* start stand out */
    unsigned short eso;                   /* end stand out */
    unsigned char mode;                  /* status of line, as far as update */
    /* data 必需是最後一個欄位, see screen_backup() */
    unsigned char data[ANSILINELEN + 1];
} screenline_t;

typedef struct {
    int r, c;
} rc_t;

/* name.c 中運用的資料結構 */
typedef struct word_t {
    char    *word;
    struct  word_t  *next;
} word_t;

typedef struct commands_t {
    int     (*cmdfunc)();
    int     level;
    char    *desc;                   /* next/key/description */
} commands_t;

typedef struct MailQueue {
    char    filepath[FNLEN];
    char    subject[STRLEN];
    time4_t mailtime;
    char    sender[IDLEN + 1];
    char    username[24];
    char    rcpt[50];
    int     method;
    char    *niamod;
} MailQueue;

enum  {MQ_TEXT, MQ_UUENCODE, MQ_JUSTIFY};

typedef struct
{ 
    time4_t chrono;
    int     recno;
} TagItem;

/*
 * signature information
 */
typedef struct
{
    int total;		/* total sig files found */
    int max;		/* max number of available sig */
    int show_start;	/* by which page to start display */
    int show_max;	/* max covered range in last display */
} SigInfo;

/* type in gomo.c, structure passing through socket */
typedef struct {
    char            x;
    char            y;
} Horder_t;

#ifdef OUTTACACHE
typedef struct {
    int     index; // 在 SHM->uinfo[index]
    int     uid;   // 避免在 cache server 上不同步, 再確認用.
    int     friendstat;
    int     rfriendstat;
} ocfs_t;
#endif

// kcwu: for bug tracking
/* not used right now */
enum {
    F_VER,
    F_EDIT,
    F_MORE,
    F_WRITE_REQUEST,
    F_TALK_REQUEST,
    F_WATER,
    F_USERLIST,
    F_GEM,
};


#endif
