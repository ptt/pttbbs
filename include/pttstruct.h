/* $Id$ */
#ifndef INCLUDE_STRUCT_H
#define INCLUDE_STRUCT_H


#define IDLEN      12             /* Length of bid/uid */

/* Äv¼Ð¸ê°T */
#define SALE_COMMENTED 0x1
typedef struct bid_t {
    int    high;
    int    buyitnow;
    int    usermax;
    int    increment;
    char   userid[IDLEN + 1];
    time_t enddate;
    char   payby; /* 1 cash 2 check or mail 4 wire 8 credit 16 postoffice */
    char   flag;
    char   pad[2];
    int    shipping;
}bid_t;

/* ¤pÂûªº¸ê®Æ */
typedef struct chicken_t {
    char    name[20];
    char    type;             /* ª«ºØ */
    unsigned char   tech[16]; /* §Þ¯à */
    time_t  birthday;         /* ¥Í¤é */
    time_t  lastvisit;        /* ¤W¦¸·ÓÅU®É¶¡ */
    int     oo;               /* ¸É«~ */
    int     food;             /* ­¹ª« */
    int     medicine;         /* ÃÄ«~ */
    int     weight;           /* Åé­« */
    int     clean;            /* °®²b */
    int     run;              /* ±Ó±¶«× */
    int     attack;           /* §ðÀ»¤O */
    int     book;             /* ª¾ÃÑ */
    int     happy;            /* §Ö¼Ö */
    int     satis;            /* º¡·N«× */
    int     temperament;      /* ®ð½è */
    int     tiredstrong;      /* ¯h³Ò«× */
    int     sick;             /* ¯f®ð«ü¼Æ */
    int     hp;               /* ¦å¶q */
    int     hp_max;           /* º¡¦å¶q */
    int     mm;               /* ªk¤O */
    int     mm_max;           /* º¡ªk¤O */
    time_t  cbirth;           /* ¹ê»Ú­pºâ¥Îªº¥Í¤é */
    int     pad[2];           /* ¯dµÛ¥H«á¥Î */
} chicken_t;

#define PASSLEN    14             /* Length of encrypted passwd field */
#define REGLEN     38             /* Length of registration data */

typedef struct userec_t {
    char    userid[IDLEN + 1];
    char    realname[20];
    char    username[24];
    char    passwd[PASSLEN];
    unsigned char   uflag;
    unsigned int    userlevel;
    unsigned short  numlogins;
    unsigned short  numposts;
    time_t  firstlogin;
    time_t  lastlogin;
    char    lasthost[16];
    int     money;
    char    remoteuser[3];           /* «O¯d ¥Ø«e¨S¥Î¨ìªº */
    char    proverb;
    char    email[50];
    char    address[50];
    char    justify[REGLEN + 1];
    unsigned char   month;
    unsigned char   day;
    unsigned char   year;
    unsigned char   sex;
    unsigned char   state;
    unsigned char   pager;
    unsigned char   invisible;
    unsigned int    exmailbox;
    chicken_t       mychicken;
    time_t  lastsong;
    unsigned int    loginview;
    unsigned char   channel;      /* °ÊºA¬ÝªO */
    unsigned short  vl_count;     /* ViolateLaw counter */
    unsigned short  five_win;
    unsigned short  five_lose;
    unsigned short  five_tie;
    unsigned short  chc_win;
    unsigned short  chc_lose;
    unsigned short  chc_tie;
    int     mobile;
    char    mind[4];
    char    ident[11];
    unsigned int    uflag2;
    unsigned char   signature;

    unsigned char   goodpost;		/* µû»ù¬°¦n¤å³¹¼Æ */
    unsigned char   badpost;		/* µû»ù¬°Ãa¤å³¹¼Æ */
    unsigned char   goodsale;		/* Äv¼Ð ¦nªºµû»ù  */
    unsigned char   badsale;		/* Äv¼Ð Ãaªºµû»ù  */
    char    myangel[IDLEN+1];           /* §Úªº¤p¤Ñ¨Ï */
    char    pad[54];
} userec_t;
/* these are flags in userec_t.uflag */
#define PAGER_FLAG      0x4     /* true if pager was OFF last session */
#define CLOAK_FLAG      0x8     /* true if cloak was ON last session */
#define FRIEND_FLAG     0x10    /* true if show friends only */
#define BRDSORT_FLAG    0x20    /* true if the boards sorted alphabetical */
#define MOVIE_FLAG      0x40    /* true if show movie */
#define COLOR_FLAG      0x80    /* true if the color mode open */
#define MIND_FLAG       0x100   /* true if mind search mode open <-Heat*/
/* these are flags in userec_t.uflag2 */
#define WATER_MASK      000003  /* water mask */
#define WATER_ORIG      0x0
#define WATER_NEW       0x1
#define WATER_OFO       0x2
#define WATERMODE(mode) ((cuser->uflag2 & WATER_MASK) == mode)
#define FAVNOHILIGHT    0x10   /* false if hilight favorite */
#define FAVNEW_FLAG     0x20   /* true if add new board into one's fav */
#define FOREIGN         0x100  /* true if a foreign */
#define LIVERIGHT       0x200  /* true if get "liveright" already */

#define BTLEN      48             /* Length of board title */

typedef struct boardheader_t {
    char    brdname[IDLEN + 1];          /* bid */
    char    title[BTLEN + 1];
    char    BM[IDLEN * 3 + 3];           /* BMs' userid, token '/' */
    unsigned int    brdattr;             /* boardªºÄÝ©Ê */
    char    pad[3];                      /* ¨S¥Î¨ìªº */
    time_t  bupdate;                     /* note update time */
    char    pad2[3];                     /* ¨S¥Î¨ìªº */
    unsigned char   bvote;               /* Vote flags */
    time_t  vtime;                       /* Vote close time */
    unsigned int    level;               /* ¥i¥H¬Ý¦¹ªOªºÅv­­ */
    int     unused;                      /* ÁÙ¨S¥Î¨ì */
    int     gid;                         /* ¬ÝªO©ÒÄÝªºÃþ§O ID */
    void    *next[2];	                 /* ¦b¦P¤@­Ógid¤U¤@­Ó¬ÝªO °ÊºA²£¥Í*/
    void    *firstchild[2];	         /* ÄÝ©ó³o­Ó¬ÝªOªº²Ä¤@­Ó¤l¬ÝªO */
    void    *parent;
    int     childcount;                  /* ¦³¦h¤Ö­Óchild */
    int     nuser;                       /* ¦h¤Ö¤H¦b³oªO */
    int     postexpire;                  /* postexpire */
    time_t  endgamble;
    char    posttype[33];
    char    posttype_f;
    char    pad3[50];
} boardheader_t;

#define BRD_NOZAP       000000001         /* ¤£¥izap  */
#define BRD_NOCOUNT     000000002         /* ¤£¦C¤J²Î­p */
#define BRD_NOTRAN      000000004         /* ¤£Âà«H */
#define BRD_GROUPBOARD  000000010         /* ¸s²ÕªO */
#define BRD_HIDE        000000020         /* ÁôÂÃªO (¬ÝªO¦n¤Í¤~¥i¬Ý) */
#define BRD_POSTMASK    000000040         /* ­­¨îµoªí©Î¾\Åª */
#define BRD_ANONYMOUS   000000100         /* °Î¦WªO */
#define BRD_DEFAULTANONYMOUS 000000200    /* ¹w³]°Î¦WªO */
#define BRD_BAD		000000400         /* ¹Hªk§ï¶i¤¤¬ÝªO */
#define BRD_VOTEBOARD   000001000         /* ³s¸p¾÷¬ÝªO */
#define BRD_WARNEL      000002000         /* ³s¸p¾÷¬ÝªO */
#define BRD_TOP         000004000         /* ¼öªù¬ÝªO¸s²Õ */
#define BRD_NORECOMMEND 000010000         /* ¤£¥i±ÀÂË */
#define BRD_BLOG        000020000         /* BLOG */
#define BRD_BMCOUNT	000040000	  /* ªO¥D³]©w¦C¤J°O¿ý */
#define BRD_SYMBOLIC	000100000	  /* symbolic link to board */

#define BRD_LINK_TARGET(x)	((x)->postexpire)
#define GROUPOP()               (currmode & MODE_GROUPOP)



#define TTLEN      64             /* Length of title */
#define FNLEN      33             /* Length of filename  */

#define FHR_REFERENCE	(1<<31)

typedef struct fileheader_t {
    char    filename[FNLEN];         /* M.9876543210.A */
    char    recommend;               /* important level */
    char    owner[IDLEN + 2];        /* uid[.] */
    char    date[6];                 /* [02/02] or space(5) */
    char    title[TTLEN + 1];
    int     money;	             /* rocker: if bit32 on ==> reference */
    unsigned char   filemode;        /* must be last field @ boards.c */
} fileheader_t;

#define FILE_LOCAL      0x1     /* local saved */
#define FILE_READ       0x1     /* already read : mail only */
#define FILE_MARKED     0x2     /* opus: 0x8 */
#define FILE_DIGEST     0x4     /* digest */
#define FILE_HOLD       0x8     /* unused */
#define FILE_BOTTOM     0x8     /* push_bottom */
#define FILE_SOLVED	0x10	/* problem solved, sysop only */
#define FILE_HIDE       0x20    /* hild */
#define FILE_BM         0x40    /* BM only */
#define FILE_MULTI      0x100   /* multi send for mail */
#define FILE_BID        0x20    /* for bid */
#define FILE_VOTE       0x40    /* for vote */
#define FILE_ANONYMOUS  0x80   /* anonymous file */

#define STRLEN     80             /* Length of most string data */


/* uhash is a userid->uid hash table -- jochang */

#define HASH_BITS 16

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

typedef struct msgque_t {
    pid_t   pid;
    char    userid[IDLEN + 1];
    char    last_call_in[76];
    int     msgmode;
} msgque_t;

typedef struct userinfo_t {
    int     uid;                  /* Used to find user name in passwd file */
    pid_t   pid;                  /* kill() to notify user of talk request */
    int     sockaddr;             /* ... */
    int     destuid;              /* talk uses this to identify who called */
    int     destuip;              /* dest index in utmpshm->uinfo[] */
    unsigned char   active;         /* When allocated this field is true */
    unsigned char   invisible;      /* Used by cloaking function in Xyz menu */
    unsigned char   sockactive;     /* Used to coordinate talk requests */
    unsigned int    userlevel;
    unsigned char   mode;           /* UL/DL, Talk Mode, Chat Mode, ... */
    unsigned char   pager;          /* pager toggle, YEA, or NA */
    unsigned char   in_chat;        /* for in_chat commands   */
    unsigned char   sig;            /* signal type */
    char    userid[IDLEN + 1];
    char    chatid[11];             /* chat id, if in chat mode */
    char    username[24];
    char    from[27];               /* machine name the user called in from */
    int     from_alias;
    char    birth;                   /* ¬O§_¬O¥Í¤é Ptt*/
    char    tty[11];                 /* tty port */
    short   nFriends;                /* ¤U­± friend[] ¥u¥Î¨ì«e´X­Ó,
                                        ¥Î¨Ó bsearch */
    int     friend[MAX_FRIEND];
    int     friend_online[MAX_FRIEND];/* point¨ì½u¤W¦n¤Í utmpshmªº¦ì¸m */
			          /* ¦n¤Í¤ñ¸ûªºcache «e¨â­Óbit¬Oª¬ºA */
    int     reject[MAX_REJECT];

    unsigned char   goodpost;		/* µû»ù¬°¦n¤å³¹¼Æ */
    unsigned char   badpost;		/* µû»ù¬°Ãa¤å³¹¼Æ */
    unsigned char   goodsale;		/* Äv¼Ð ¦nªºµû»ù  */
    unsigned char   badsale;		/* Äv¼Ð Ãaªºµû»ù  */

    int     lock;
    int     friendtotal;              /* ¦n¤Í¤ñ¸ûªºcache ¤j¤p */ 
    char    msgcount;
    msgque_t        msgs[MAX_MSGS];
    // uptime ¦n¹³¨S¥Î¨ì
    time_t  uptime;
    time_t  lastact;               /* ¤W¦¸¨Ï¥ÎªÌ°Êªº®É¶¡ */
    unsigned int    brc_id;
    unsigned char   lockmode;       /* ¤£­ã multi_login ª±ªºªF¦è */
    char    turn;                    /* for gomo */
    char    mateid[IDLEN + 1];       /* for gomo */
    unsigned short  int     five_win;
    unsigned short  int     five_lose;
    unsigned short  int     five_tie;
    unsigned short  int     chc_win;
    unsigned short  int     chc_lose;
    unsigned short  int     chc_tie;
    char    mailalert;
    char    sex;
    char    color;
    char    mind[4];
#ifdef NOKILLWATERBALL
    time_t  wbtime;
#endif
} userinfo_t;

typedef struct water_t {
    pid_t   pid;
    char    userid[IDLEN + 1];
    int     top, count;
    msgque_t        msg[MAX_REVIEW];
    userinfo_t      *uin;     // Ptt:³o¥i¥H¨ú¥Nalive
} water_t;

typedef struct {
    fileheader_t    *header;
    char    mtitle[STRLEN];
    char    *path;
    int     num, page, now, level;
} menu_t;

/* Used to pass commands to the readmenu.
 * direct mapping, indexed by ascii code. */
#define onekey_size ((int) 'z')
typedef int (* onekey_t)();

#define ANSILINELEN (511)                /* Maximum Screen width in chars */

/* anti_crosspost */
typedef struct crosspost_t {
    int     checksum[4]; /* 0 -> 'X' cross post  1-3 -> Â²¬d¤å³¹¦æ */
    int     times;       /* ²Ä´X¦¸ */
} crosspost_t;

#define SORT_BY_ID    0
#define SORT_BY_CLASS 1
#define SORT_BY_STAT  1
#define SORT_BY_IDLE  2
#define SORT_BY_FROM  3
#define SORT_BY_FIVE  4
#define SORT_BY_SEX   5

typedef struct keeploc_t {
    char    *key;
    int     top_ln;
    int     crs_ln;
    struct  keeploc_t       *next;
} keeploc_t;

#define USHM_SIZE       (MAX_ACTIVE + 4)  /* why+4? */

/* MAX_BMs is dirty hardcode 4 in mbbsd/cache.c:is_BM_cache() */
#define MAX_BMs         4                 /* for BMcache, ¤@­Ó¬ÝªO³Ì¦h´XªO¥D */

typedef struct {
    /* uhash */
    char    userid[MAX_USERS][IDLEN + 1];
    int     next_in_hash[MAX_USERS];
    int     money[MAX_USERS];
    int     hash_head[1 << HASH_BITS];
    int     number;				/* # of users total */
    int     loaded;				/* .PASSWD has been loaded? */

    /* utmpshm */
    userinfo_t      uinfo[USHM_SIZE];
    userinfo_t      *sorted[2][8][USHM_SIZE];
                    /* ²Ä¤@ºûdouble buffer ¥Ñcurrsorted«ü¦V¥Ø«e¨Ï¥Îªº
		       ²Ä¤Gºûsort type */
    int     currsorted;
    time_t  UTMPuptime;
    int     UTMPnumber;
    char    UTMPneedsort;
    char    UTMPbusystate;

    /* brdshm */
    int     BMcache[MAX_BOARD][MAX_BMs];
    boardheader_t   bcache[MAX_BOARD];
    boardheader_t   *bsorted[2][MAX_BOARD]; /* 0: by name 1: by class */
#if HOTBOARDCACHE
    unsigned char    nHOTs;
    boardheader_t   *HBcache[HOTBOARDCACHE];
#endif
#if DIRCACHESIZE
    // Ptt: dricache À³³§ïï¦¨¨¥ucache ¼öö­Ìªùù¬ÝÝª©©Á××§Kmemory®öö¶O
    fileheader_t    dircache[MAX_BOARD][DIRCACHESIZE]; 
#endif
    time_t  busystate_b[MAX_BOARD];
    int     total[MAX_BOARD];
    unsigned char  n_bottom[MAX_BOARD]; /* number of bottom */
    int     hbfl[MAX_BOARD][MAX_FRIEND + 1];
    time_t  lastposttime[MAX_BOARD];
    time_t  Buptime;
    time_t  Btouchtime;
    int     Bnumber;
    int     Bbusystate;
    time_t  close_vote_time;

    /* pttcache */
    char    notes[MAX_MOVIE][200*11];
    char    today_is[20];
    int     n_notes[MAX_MOVIE_SECTION];      /* ¤@¸`¤¤¦³´X­Ó ¬ÝªO */
    int     next_refresh[MAX_MOVIE_SECTION]; /* ¤U¤@¦¸­nrefreshªº ¬ÝªO */
    msgque_t loginmsg;  /* ¶i¯¸¤ô²y */
    int     max_film;
    int     max_history;
    time_t  Puptime;
    time_t  Ptouchtime;
    int     Pbusystate;

    int     GLOBALVAR[10];                   /*  mbbsd¶¡ªº global variable
						 ¥Î¥H°µ²Î­pµ¥¸ê®Æ («D±`ºA)  */

    union {
	int     v[256];
	struct {
	    int     dymaxactive;  /* °ÊºA³]©w³Ì¤j¤H¼Æ¤W­­     */
	    int     toomanyusers; /* ¶W¹L¤H¼Æ¤W­­¤£µ¹¶iªº­Ó¼Æ */
	    int     noonlineuser; /* ¯¸¤W¨Ï¥ÎªÌ¤£°ª«G«×Åã¥Ü   */
#ifdef OUTTA_TIMER
	    time_t  now;
#endif
	    int     nWelcomes;
	} e;
    } GV2;

    /* fromcache */
    char    domain[MAX_FROM][50];
    char    replace[MAX_FROM][50];
    int     top;
    int     max_user;
    time_t  max_time;
    time_t  Fuptime;
    time_t  Ftouchtime;
    int     Fbusystate;
} SHM_t;

typedef struct {
    unsigned char oldlen;                /* previous line length */
    unsigned char len;                   /* current length of line */
    unsigned char mode;                  /* status of line, as far as update */
    unsigned char smod;                  /* start of modified data */
    unsigned char emod;                  /* end of modified data */
    unsigned char sso;                   /* start stand out */
    unsigned char eso;                   /* end stand out */
    unsigned char data[ANSILINELEN + 1];
} screenline_t;

typedef struct {
    int r, c;
} rc_t;

#define BRD_ROW           10
#define BRD_COL           9

typedef int board_t[BRD_ROW][BRD_COL];

/* name.c ¤¤¹B¥Îªº¸ê®Æµ²ºc */
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
    time_t  mailtime;
    char    sender[IDLEN + 1];
    char    username[24];
    char    rcpt[50];
    int     method;
    char    *niamod;
} MailQueue;

enum  {MQ_TEXT, MQ_UUENCODE, MQ_JUSTIFY};

typedef struct
{ 
    time_t  chrono;
    int     recno;
} TagItem;

#ifdef OUTTACACHE
typedef struct {
    int     index; // ¦b SHM->uinfo[index]
    int     uid;   // Á×§K¦b cache server ¤W¤£¦P¨B, ¦A½T»{¥Î.
    int     friendstat;
    int     rfriendstat;
} ocfs_t;
#endif

#endif
