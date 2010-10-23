/* $Id$ */
#ifndef INCLUDE_STRUCT_H
#define INCLUDE_STRUCT_H

#include <netinet/in.h>
#include <stdio.h>
#include "cmsys.h"	// for time4_t
#include "config.h"	// various sizes in SHM
#include "statistic.h"	// for MAX_STATS

// warning: because some other places is using #IDLEN to convert as string,
// so don't quote it - otherwise those code will fail.
#define IDLEN	     12     /* Length of bid/uid */
#define IPV4LEN	    (15)    /* a.b.c.d form */

// GCC pragma to prevent paddings
#define PACKSTRUCT	__attribute__ ((packed))

/* 小雞的資料 */
typedef struct chicken_t { /* 128 bytes */
    char    name[20];
    uint8_t type;             /* 物種 */
    uint8_t tech[16];         /* 技能 (unused) */
    uint8_t pad0[3];
    time4_t birthday;         /* 生日 */
    time4_t lastvisit;        /* 上次照顧時間 */
    int32_t oo;               /* 補品 */
    int32_t food;             /* 食物 */
    int32_t medicine;         /* 藥品 */
    int32_t weight;           /* 體重 */
    int32_t clean;            /* 乾淨 */
    int32_t run;              /* 敏捷度 */
    int32_t attack;           /* 攻擊力 */
    int32_t book;             /* 知識 */
    int32_t happy;            /* 快樂 */
    int32_t satis;            /* 滿意度 */
    int32_t temperament;      /* 氣質 */
    int32_t tiredstrong;      /* 疲勞度 */
    int32_t sick;             /* 病氣指數 */
    int32_t hp;               /* 血量 */
    int32_t hp_max;           /* 滿血量 */
    int32_t mm;               /* 法力 */
    int32_t mm_max;           /* 滿法力 */
    time4_t cbirth;           /* 實際計算用的生日 */
    int32_t pad[2];           /* 留著以後用 */
} PACKSTRUCT chicken_t;

#define PASSLEN    14             /* Length of encrypted passwd field */
#define REGLEN     38             /* Length of registration data */

#define PASSWD_VERSION	4194

typedef struct userec_t {
    uint32_t	version;	/* version number of this sturcture, we
    				 * use revision number of project to denote.*/

    char	userid[IDLEN+1];/* 使用者ID */
    char	realname[20];	/* 真實姓名 */
    char	nickname[24];	/* 暱稱 */
    char	passwd[PASSLEN];/* 密碼 */

    char	pad_1;

    uint32_t    uflag;		/* 習慣1 , see uflags.h */
    uint32_t    deprecated_uflag2;		/* deprecated: 習慣2 , see uflags.h */
    uint32_t    userlevel;	/* 權限 */
    uint32_t    numlogindays;	/* 上線資歷 (每日最多+1的登入次數) */
    uint32_t    numposts;	/* 文章篇數 */
    time4_t	firstlogin;	/* 註冊時間 */
    time4_t	lastlogin;	/* 最近上站時間(包含隱身) */
    char	lasthost[IPV4LEN+1];/* 上次上站來源 */
    int32_t     money;		/* Ptt幣 */

    char	unused_1[4];

    char	email[50];	/* Email */
    char	address[50];	/* 住址 */
    char	justify[REGLEN + 1];/* 審核資料 */
    uint8_t	month;		/* 生日 月 */
    uint8_t	day;		/* 生日 日 */
    uint8_t	year;		/* 生日 年 */
#ifdef USE_USER_SEX
    uint8_t	sex;		/* 性別 */
#else
    uint8_t     nonuse_sex;     /* 性別 (已停用) */
#endif

    uint8_t	pager_ui_type;	/* 呼叫器界面類別 (was: WATER_*) */
    uint8_t	pager;		/* 呼叫器狀態 */
    uint8_t	invisible;	/* 隱形狀態 */

    char	unused_3[2];

    uint32_t    exmailbox;	/* 購買信箱數 TODO short 就夠了 */

    // r3968 移出 sizeof(chicken_t)=128 bytes
    char	chkpad0[4];

    char	career[40];	/* 學歷職業 */
    char	phone[20];	/* 電話 */

    uint32_t	old_numlogins;	/* 轉換前的 numlogins, 備份檢視用 */
    char	chkpad1[48];
    time4_t	lastseen;	/* 最近上站時間(隱身不計) */
    time4_t	chkpad2[2];	/* in case 有人忘了把 time4_t 調好... */
    // 以上應為 sizeof(chicken_t) 同等大小
    
    time4_t	lastsong;	/* 上次點歌時間 */
    uint32_t	loginview;	/* 進站畫面 */

    uint8_t	unused_4;	// was: channel
    uint8_t	pad_2;

    uint16_t	vl_count;	/* 違法記錄 ViolateLaw counter */
    uint16_t	five_win;	/* 五子棋戰績 勝 */
    uint16_t	five_lose;	/* 五子棋戰績 敗 */
    uint16_t	five_tie;	/* 五子棋戰績 和 */
    uint16_t	chc_win;	/* 象棋戰績 勝 */
    uint16_t	chc_lose;	/* 象棋戰績 敗 */
    uint16_t	chc_tie;	/* 象棋戰績 和 */
    int32_t     mobile;		/* 手機號碼 */
    char	mind[4];	/* 心情 XXX not a null-terminate string */
    uint16_t	go_win;		/* 圍棋戰績 勝 */
    uint16_t	go_lose;	/* 圍棋戰績 敗 */
    uint16_t	go_tie;		/* 圍棋戰績 和 */

    char	unused_5[5];	/* 從前放 ident 身份證字號，使用前請先清0 */

    uint8_t	signature;	/* 慣用簽名檔 */
    uint8_t	goodpost;	/* 評價為好文章數 */
    uint8_t	badpost;	/* 評價為壞文章數 */
    uint8_t	unused_6;	/* 從前放競標好評(goodsale), 使用前請先清0 */
    uint8_t	unused_7;	/* 從前放競標壞評(badsale),  使用前請先清0 */
    char	myangel[IDLEN+1];/* 我的小天使 */

    char	pad_3;

    uint16_t	chess_elo_rating;/* 象棋等級分 */
    uint32_t    withme;		/* 我想找人下棋，聊天.... */
    time4_t	timeremovebadpost;/* 上次刪除劣文時間 */
    time4_t	timeviolatelaw; /* 被開罰單時間 */

    char	pad_tail[28];
} PACKSTRUCT userec_t;

#ifndef NO_CONST_CUSER
// const userec_t  cuser;
# define cuser_ref   ((const userec_t*)&pwcuser)
# define cuser	    (*cuser_ref)
#else
# define cuser_ref   (&cuser)
# define cuser	     pwcuser
#endif

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

#define BTLEN      48             /* Length of board title */

/* TODO 動態更新的欄位不應該跟要寫入檔案的混在一起,
 * 至少用個 struct 包起來之類 */
typedef struct boardheader_t { /* 256 bytes */
    char    brdname[IDLEN + 1];	    /* bid */
    char    title[BTLEN + 1];
    char    BM[IDLEN * 3 + 3];	    /* BMs' userid, token '/' */
    char    pad1[3];
    uint32_t brdattr;		    /* board的屬性 */
    char    chesscountry;	    /* 棋國 */
    uint8_t vote_limit_posts;	    /* 連署 : 文章篇數下限 */
    uint8_t vote_limit_logins;	    /* 連署 : 登入次數下限 */
    uint8_t vote_limit_regtime;	    /* 連署 : 註冊時間限制 */
    time4_t bupdate;		    /* note update time */
    uint8_t post_limit_posts;	    /* 發表文章 : 文章篇數下限 */
    uint8_t post_limit_logins;	    /* 發表文章 : 登入次數下限 */
    uint8_t post_limit_regtime;	    /* 發表文章 : 註冊時間限制 */
    uint8_t bvote;		    /* 正舉辦 Vote 數 */
    time4_t vtime;		    /* Vote close time */
    uint32_t level;		    /* 可以看此板的權限 */
    time4_t perm_reload;	    /* 最後設定看板的時間 */
    int32_t gid;		    /* 看板所屬的類別 ID */
    int32_t next[2];		    /* 在同一個gid下一個看板 動態產生*/
    int32_t firstchild[2];	    /* 屬於這個看板的第一個子看板 */
    int32_t parent;		    /* 這個看板的 parent 看板 bid */
    int32_t childcount;		    /* 有多少個child */
    int32_t nuser;		    /* 多少人在這板 */
    int32_t postexpire;		    /* postexpire */
    time4_t endgamble;
    char    posttype[33];
    char    posttype_f;
    uint8_t fastrecommend_pause;    /* 快速連推間隔 */
    uint8_t vote_limit_badpost;	    /* 連署 : 劣文上限 */
    uint8_t post_limit_badpost;	    /* 發表文章 : 劣文上限 */
    char    pad3[3];
    time4_t SRexpire;		    /* SR Records expire time */
    char    pad4[40];
} PACKSTRUCT boardheader_t;

// TODO BRD 快爆了，怎麼辦？ 準備從 pad3 偷一個來當 attr2 吧...
// #define BRD_NOZAP		0x00000001	/* (沒用到) 不可zap */
#define BRD_NOCOUNT		0x00000002	/* 不列入統計 */
#define BRD_NOTRAN		0x00000004	/* 不轉信 */
#define BRD_GROUPBOARD		0x00000008	/* 群組板 */
#define BRD_HIDE		0x00000010	/* 隱藏板 (看板好友才可看) */
#define BRD_POSTMASK		0x00000020	/* 限制發表或閱讀 */
#define BRD_ANONYMOUS		0x00000040	/* 匿名板 */
#define BRD_DEFAULTANONYMOUS	0x00000080	/* 預設匿名板 */
#define BRD_BAD			0x00000100	/* 違法改進中看板 */
#define BRD_VOTEBOARD		0x00000200	/* 連署機看板 */
#define BRD_WARNEL		0x00000400	/* 連署機看板 */
#define BRD_TOP			0x00000800	/* 熱門看板群組 */
#define BRD_NORECOMMEND		0x00001000	/* 不可推薦 */
// #define BRD_BLOG		0x00002000	/* (已停用) BLOG */
#define BRD_BMCOUNT		0x00004000	/* 板主設定列入記錄 */
#define BRD_SYMBOLIC		0x00008000	/* symbolic link to board */
#define BRD_NOBOO		0x00010000	/* 不可噓 */
#define BRD_LOCALSAVE		0x00020000	/* 預設 Local Save */
#define BRD_RESTRICTEDPOST	0x00040000	/* 板友才能發文 */
#define BRD_GUESTPOST		0x00080000	/* guest能 post */
#define BRD_COOLDOWN		0x00100000	/* 冷靜 */
#define BRD_CPLOG		0x00200000	/* 自動留轉錄記錄 */
#define BRD_NOFASTRECMD		0x00400000	/* 禁止快速推文 */
#define BRD_IPLOGRECMD		0x00800000	/* 推文記錄 IP */
#define BRD_OVER18		0x01000000	/* 十八禁 */
#define BRD_NOREPLY		0x02000000	/* 不可回文 */
#define BRD_ALIGNEDCMT		0x04000000	/* 對齊式的推文 */
#define BRD_NOSELFDELPOST       0x08000000      /* 不可自刪 */

#define BRD_LINK_TARGET(x)	((x)->postexpire)
#define GROUPOP()               (currmode & MODE_GROUPOP)

#ifdef CHESSCOUNTRY
#define CHESSCODE_NONE   0
#define CHESSCODE_FIVE   1
#define CHESSCODE_CCHESS 2
#define CHESSCODE_GO     3
#define CHESSCODE_REVERSI 4
#define CHESSCODE_MAX    4
#endif /* defined(CHESSCOUNTRY) */


#define TTLEN      64             /* Length of title */
#define FNLEN      28             /* Length of filename */

typedef struct fileheader_t { /* 128 bytes */
    char    filename[FNLEN];         /* M.1120582370.A.1EA [19+1], create time */
    time4_t modified;		     /* last modified time */
    char    pad;		     /* padding, not used */
    char    recommend;               /* important level */
    char    owner[IDLEN + 2];        /* uid[.] */
    char    date[6];                 /* [02/02] or space(5) */
    char    title[TTLEN + 1];
    /* TODO this multi is a mess now. */
    char    pad2;
    union {
	/* TODO: MOVE money to outside multi!!!!!! */
	int money;
	int anon_uid;
	/* different order to match alignment */
	struct {
	    unsigned char posts;
	    unsigned char logins;
	    unsigned char regtime;
	    unsigned char badpost;
	} vote_limits;
	struct {
	    /* is this ordering correct? */
	    unsigned int ref:31;
	    unsigned int flag:1;
	} refer;
    }	    multi;		    /* rocker: if bit32 on ==> reference */
    /* XXX dirty, split into flag and money if money of each file is less than 16bit? */
    unsigned char   filemode;        /* must be last field @ boards.c */
    char    pad3[3];
} PACKSTRUCT fileheader_t;

#define FILE_LOCAL      0x01    /* local saved,  non-mail */
#define FILE_READ       0x01    /* already read, mail only */
#define FILE_MARKED     0x02    /* non-mail + mail */
#define FILE_DIGEST     0x04    /* digest,       non-mail */
#define FILE_REPLIED    0x04    /* replied,      mail only */
#define FILE_BOTTOM     0x08    /* push_bottom,  non-mail */
#define FILE_MULTI      0x08    /* multi send,   mail only */
#define FILE_SOLVED     0x10    /* problem solved, sysop/BM non-mail only */
#define FILE_HIDE       0x20    /* hide,	in announce */
#define FILE_BID        0x20    /* bid,		in non-announce */
#define FILE_BM         0x40    /* BM only,	in announce */
#define FILE_VOTE       0x40    /* for vote,	in non-announce */
#define FILE_ANONYMOUS  0x80    /* anonymous file */

#define STRLEN     80             /* Length of most string data */

#define FAVMAX   1024		  /* Max boards of Myfavorite */
#define FAVGMAX    32             /* Max groups of Myfavorite */
#define FAVGSLEN    8		  /* Max Length of Description String */

/* values of msgque_t::msgmode */
#define MSGMODE_TALK      0
#define MSGMODE_WRITE     1
#define MSGMODE_FROMANGEL 2
#define MSGMODE_TOANGEL   3

typedef struct msgque_t {
    pid_t   pid;
    char    userid[IDLEN + 1];
    char    last_call_in[76];
    int     msgmode;
} msgque_t;

#define ALERT_NEW_MAIL        (0x01)
#define ISNEWMAIL(utmp)       (utmp->alerts & ALERT_NEW_MAIL)
#define CLEAR_ALERT_NEWMAIL(utmp)    { utmp->alerts &= ~ALERT_NEW_MAIL; }
#define ALERT_PWD_PERM	      (0x02)
#define ISNEWPERM(utmp)	      (utmp->alerts & ALERT_PWD_PERM)
#define CLEAR_ALERT_NEWPERM(utmp)    { utmp->alerts &= ~ALERT_PWD_PERM; }

// userinfo_t.angelpause values 
#define ANGELPAUSE_NONE	    (0)	// reject none (accept all)
#define ANGELPAUSE_REJNEW   (1) // reject only new requests
#define ANGELPAUSE_REJALL   (2) // reject all requests
#define ANGELPAUSE_MODES    (3)	// max value, used as (angelpause % ANGELPAUSE_MODES)

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
    in_addr_t	from_ip;	    // was: int     from_alias;
#ifdef USE_USER_SEX
    char    sex;
#else
    char    nonuse_sex;             // deprecated: sex info
#endif
    char    nonuse[4];
    /*
    unsigned char goodpost;
    unsigned char badpost;
    unsigned char goodsale;
    unsigned char badsale;
    */
    unsigned char angelpause;

    /* friends */
    int     friendtotal;              /* 好友比較的cache 大小 */ 
    short   nFriends;                /* 下面 friend[] 只用到前幾個,
                                        用來 bsearch */
    int     myfriend[MAX_FRIEND];
    char    gap_1[4];
    unsigned int friend_online[MAX_FRIEND];/* point到線上好友 utmpshm的位置 */
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
    char    alerts;             /* mail alert, passwd update... */
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
    unsigned short  int     go_win;
    unsigned short  int     go_lose;
    unsigned short  int     go_tie;

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
    int roll;	// for scrolling
    void *raw_memory;
} screen_backup_t;

// menu_t 其實是 gmenu_t (deprecated), 精華區專用 menu
typedef struct menu_t {
    int     num, page, now, level, bid;
    int     header_size;
    fileheader_t   *header;
    const char     *path;
    struct  menu_t *next;
    char    mtitle[STRLEN];
} menu_t;

/* Used to pass commands to the readmenu.
 * direct mapping, indexed by ascii code. */
#define onekey_size ((int) '~')
/* keymap, 若 needitem = 0 表示不需要 item, func 的 type 應為 int (*)(void).
 * 否則應為 int (*)(int ent, const fileheader_t *fhdr, const char *direct) */
typedef struct {
    char needitem;
    int (*func)();
} onekey_t;

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
#define USHM_SIZE       ((MAX_ACTIVE)*41/40)
/* USHM_SIZE 比 MAX_ACTIVE 大是為了防止檢查人數上限時, 又同時衝進來
 * 會造成找 shm 空位的無窮迴圈. 
 * 又, 因 USHM 中用 hash, 空間稍大時效率較好. */

/* MAX_BMs is dirty hardcode 4 in mbbsd/cache.c:is_BM_cache() */
#define MAX_BMs         4                 /* for BMcache, 一個看板最多幾板主 */

// TODO
// 哪天請好心人整理 shm: 
// (2) userinfo_t 可以移掉一些已不用的

#define SHM_VERSION 4842
typedef struct {
    int   version;  // SHM_VERSION   for verification
    int   size;	    // sizeof(SHM_t) for verification
   
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
    int             sorted[2][9][USHM_SIZE];
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
    char    notes[MAX_ADBANNER][256*MAX_ADBANNER_HEIGHT];
    char    gap_18[sizeof(int)];
    char    today_is[20];
    // FIXME remove it
    int     __never_used__n_notes[MAX_ADBANNER_SECTION];      /* 一節中有幾個 看板 */
    char    gap_19[sizeof(int)];
    // FIXME remove it
    int     __never_used__next_refresh[MAX_ADBANNER_SECTION]; /* 下一次要refresh的 看板 */
    char    gap_20[sizeof(int)];
    msgque_t loginmsg;  /* 進站水球 */
    int     last_film;
    int     last_usong;
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
	    int     shutdown;     /* shutdown flag */

	    /* 注意, 應保持 align sizeof(int) */
	} e;
    } GV2;
    /* statistic */
    int     statistic[STAT_MAX];

    // TODO XXX 有 fromd 後可以拔掉了。
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

#endif
