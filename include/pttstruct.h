/* $Id$ */
#ifndef INCLUDE_STRUCT_H
#define INCLUDE_STRUCT_H


#define IDLEN      12             /* Length of bid/uid */

/* Äv¼Ð¸ê°T */
#define SALE_COMMENTED 0x1
typedef struct bid_t {
    int     high;
    int     buyitnow;
    int     usermax;
    int     increment;
    char    userid[IDLEN + 1];
    time4_t enddate;
    char    payby; /* 1 cash 2 check or mail 4 wire 8 credit 16 postoffice */
    char    flag;
    char    pad[2];
    int     shipping;
}bid_t;

/* ¤pÂûªº¸ê®Æ */
typedef struct chicken_t {
    char    name[20];
    char    type;             /* ª«ºØ */
    unsigned char   tech[16]; /* §Þ¯à */
    time4_t birthday;         /* ¥Í¤é */
    time4_t lastvisit;        /* ¤W¦¸·ÓÅU®É¶¡ */
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
    time4_t cbirth;           /* ¹ê»Ú­pºâ¥Îªº¥Í¤é */
    int     pad[2];           /* ¯dµÛ¥H«á¥Î */
} chicken_t;

#define PASSLEN    14             /* Length of encrypted passwd field */
#define REGLEN     38             /* Length of registration data */

#define PASSWD_VERSION	2275

typedef struct userec_t {
    unsigned int    version;	/* version number of this sturcture, we
    				 * use revision number of project to denote.*/

    char    userid[IDLEN + 1];
    char    realname[20];
    char    username[24];
    char    passwd[PASSLEN];
    unsigned int    uflag;
    unsigned int    uflag2;
    unsigned int    userlevel;
    unsigned int    numlogins;
    unsigned int    numposts;
    time4_t firstlogin;
    time4_t lastlogin;
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
    time4_t lastsong;
    unsigned int    loginview;
    unsigned char   channel;      /* °ÊºA¬ÝªO (unused?) */
    unsigned short  vl_count;     /* ViolateLaw counter */
    unsigned short  five_win;
    unsigned short  five_lose;
    unsigned short  five_tie;
    unsigned short  chc_win;
    unsigned short  chc_lose;
    unsigned short  chc_tie;
    int     mobile;
    char    mind[4];		/* not a null-terminate string */
    char    ident[11];
    unsigned char   signature;

    unsigned char   goodpost;		/* µû»ù¬°¦n¤å³¹¼Æ */
    unsigned char   badpost;		/* µû»ù¬°Ãa¤å³¹¼Æ */
    unsigned char   goodsale;		/* Äv¼Ð ¦nªºµû»ù  */
    unsigned char   badsale;		/* Äv¼Ð Ãaªºµû»ù  */
    char    myangel[IDLEN+1];           /* §Úªº¤p¤Ñ¨Ï */
    unsigned short  chess_elo_rating;	/* ¶H´Ñµ¥¯Å¤À */
    unsigned int    withme;
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

typedef struct boardheader_t {
    char    brdname[IDLEN + 1];          /* bid */
    char    title[BTLEN + 1];
    char    BM[IDLEN * 3 + 3];           /* BMs' userid, token '/' */
    unsigned int    brdattr;             /* boardªºÄÝ©Ê */
    char    chesscountry;
    unsigned char   vote_limit_posts;    /* ³s¸p : ¤å³¹½g¼Æ¤U­­ */
    unsigned char   vote_limit_logins;   /* ³s¸p : µn¤J¦¸¼Æ¤U­­ */
    char    pad[1];                      /* ¨S¥Î¨ìªº */
    time4_t bupdate;                     /* note update time */
    unsigned char   post_limit_posts;    /* µoªí¤å³¹ : ¤å³¹½g¼Æ¤U­­ */
    unsigned char   post_limit_logins;   /* µoªí¤å³¹ : µn¤J¦¸¼Æ¤U­­ */
    char    pad2[1];                     /* ¨S¥Î¨ìªº */
    unsigned char   bvote;               /* ¥¿Á|¿ì Vote ¼Æ */
    time4_t vtime;                       /* Vote close time */
    unsigned int    level;               /* ¥i¥H¬Ý¦¹ªOªºÅv­­ */
    int     unused;                      /* ÁÙ¨S¥Î¨ì */
    int     gid;                         /* ¬ÝªO©ÒÄÝªºÃþ§O ID */
    int     next[2];	                 /* ¦b¦P¤@­Ógid¤U¤@­Ó¬ÝªO °ÊºA²£¥Í*/
    int     firstchild[2];	         /* ÄÝ©ó³o­Ó¬ÝªOªº²Ä¤@­Ó¤l¬ÝªO */
    int     parent;
    int     childcount;                  /* ¦³¦h¤Ö­Óchild */
    int     nuser;                       /* ¦h¤Ö¤H¦b³oªO */
    int     postexpire;                  /* postexpire */
    time4_t endgamble;
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
#define BRD_NOBOO       000200000         /* ¤£¥i¼N */
#define BRD_LOCALSAVE   000400000         /* ¹w³] Local Save */
#define BRD_RESTRICTEDPOST 001000000      /* ªO¤Í¤~¯àµo¤å */
#define BRD_GUESTPOST   002000000      /* guest¯ààpo */

#define BRD_LINK_TARGET(x)	((x)->postexpire)
#define GROUPOP()               (currmode & MODE_GROUPOP)

#ifdef CHESSCOUNTRY
#define CHESSCODE_NONE   0
#define CHESSCODE_FIVE   1
#define CHESSCODE_CCHESS 2
#define CHESSCODE_MAX    2
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
	int money;
	int anon_uid;
	/* different order to match alignment */
#ifdef _BIG_ENDIAN
	struct {
	    unsigned char pad[2];   /* money & 0xffff0000 */
	    unsigned char logins;   /* money & 0xff00 */
	    unsigned char posts;    /* money & 0xff */
	} vote_limits;
	struct {
	    unsigned int flag:1;
	    unsigned int ref:31;
	} refer;
#else
	struct {
	    unsigned char posts;    /* money & 0xff */
	    unsigned char logins;   /* money & 0xff00 */
	    unsigned char pad[2];   /* money & 0xffff0000 */
	} vote_limits;
	struct {
	    unsigned int ref:31;
	    unsigned int flag:1;
	} refer;
#endif
    }	    multi;		    /* rocker: if bit32 on ==> reference */
    /* XXX dirty, split into flag and money if money of each file is less than 16bit? */
    unsigned char   filemode;        /* must be last field @ boards.c */
} fileheader_t;

#define FILE_LOCAL      0x1     /* local saved */
#define FILE_READ       0x1     /* already read : mail only */
#define FILE_MARKED     0x2     /* opus: 0x8 */
#define FILE_DIGEST     0x4     /* digest */
#define FILE_HOLD       0x8     /* unused */
#define FILE_BOTTOM     0x8     /* push_bottom */
#define FILE_SOLVED	0x10	/* problem solved, sysop/BM only */
#define FILE_HIDE       0x20    /* hild */
#define FILE_BID        0x20    /* for bid */
#define FILE_BM         0x40    /* BM only */
#define FILE_MULTI      0x100   /* multi send for mail */
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
typedef struct userinfo_t {
    int     uid;                  /* Used to find user name in passwd file */
    pid_t   pid;                  /* kill() to notify user of talk request */
    int     sockaddr;             /* ... */
    int     destuid;              /* talk uses this to identify who called */
    int     destuip;              /* dest index in utmpshm->uinfo[] */
    unsigned char   active;         /* When allocated this field is true */
    unsigned char   invisible;      /* Used by cloaking function in Xyz menu */
    unsigned char   sockactive;     /* Used to coordinate talk requests */
    unsigned char   angel;
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
    short   nFriends;                /* ¤U­± friend[] ¥u¥Î¨ì«e´X­Ó,
                                        ¥Î¨Ó bsearch */
    int     friend[MAX_FRIEND];
    int     friend_online[MAX_FRIEND];/* point¨ì½u¤W¦n¤Í utmpshmªº¦ì¸m */
			          /* ¦n¤Í¤ñ¸ûªºcache «e¨â­Óbit¬Oª¬ºA */
    int     reject[MAX_REJECT];
    unsigned short  int     chess_elo_rating;
    int     lock;
    int     friendtotal;              /* ¦n¤Í¤ñ¸ûªºcache ¤j¤p */ 
    char    msgcount;
    msgque_t        msgs[MAX_MSGS];
    unsigned int    withme;
    time4_t lastact;               /* ¤W¦¸¨Ï¥ÎªÌ°Êªº®É¶¡ */
    unsigned int    brc_id;
    unsigned char   lockmode;       /* ¤£­ã multi_login ª±ªºªF¦è */
    char    turn;                    /* for gomo */
    char    mateid[IDLEN + 1];       /* for gomo */

    /* ¬°¤F sync ¦^ .PASSWDS ®É¨Ï¥Î */
    unsigned short  int     five_win;
    unsigned short  int     five_lose;
    unsigned short  int     five_tie;
    unsigned short  int     chc_win;
    unsigned short  int     chc_lose;
    unsigned short  int     chc_tie;

    unsigned char goodpost;
    char pad_1;
    unsigned char badpost;
    char pad_2;
    unsigned char goodsale;
    char pad_3;
    unsigned char badsale;
    char pad_4;

    char    mailalert;
    char    sex;
    char    color;
    char    mind[4];
#ifdef NOKILLWATERBALL
    time4_t wbtime;
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
    int     checksum[4]; /* 0 -> 'X' cross post  1-3 -> ÀË¬d¤å³¹¦æ */
    short   times;       /* ²Ä´X¦¸ */
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
    char    *key;
    int     top_ln;
    int     crs_ln;
    struct  keeploc_t       *next;
} keeploc_t;

#define VALID_USHM_ENTRY(X) ((X) >= 0 && (X) < USHM_SIZE)
#define USHM_SIZE       (MAX_ACTIVE + 4)
/* USHM_SIZE ¤ñ MAX_ACTIVE ¤j¬O¬°¤F¨¾¤îÀË¬d¤H¼Æ¤W­­®É, ¤S¦P®É½Ä¶i¨Ó
 * ·|³y¦¨§ä shm ªÅ¦ìªºµL½a°j°é. ¤S, ¦] USHM ¤¤¥Î hash, ªÅ¶¡µy¤j®É®Ä²v¸û¦n. */

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
    int             sorted[2][8][USHM_SIZE];
                    /* ²Ä¤@ºûdouble buffer ¥Ñcurrsorted«ü¦V¥Ø«e¨Ï¥Îªº
		       ²Ä¤Gºûsort type */
    int     currsorted;
    time4_t UTMPuptime;
    int     UTMPnumber;
    char    UTMPneedsort;
    char    UTMPbusystate;

    /* brdshm */
    int     BMcache[MAX_BOARD][MAX_BMs];
    boardheader_t   bcache[MAX_BOARD];
    int     bsorted[2][MAX_BOARD]; /* 0: by name 1: by class */
#if HOTBOARDCACHE
    unsigned char    nHOTs;
    int              HBcache[HOTBOARDCACHE];
#endif
    time4_t busystate_b[MAX_BOARD];
    int     total[MAX_BOARD];
    unsigned char  n_bottom[MAX_BOARD]; /* number of bottom */
    int     hbfl[MAX_BOARD][MAX_FRIEND + 1];
    time4_t lastposttime[MAX_BOARD];
    time4_t Buptime;
    time4_t Btouchtime;
    int     Bnumber;
    int     Bbusystate;
    time4_t close_vote_time;

    /* pttcache */
    char    notes[MAX_MOVIE][200*11];
    char    today_is[20];
    int     n_notes[MAX_MOVIE_SECTION];      /* ¤@¸`¤¤¦³´X­Ó ¬ÝªO */
    int     next_refresh[MAX_MOVIE_SECTION]; /* ¤U¤@¦¸­nrefreshªº ¬ÝªO */
    msgque_t loginmsg;  /* ¶i¯¸¤ô²y */
    int     max_film;
    int     max_history;
    time4_t Puptime;
    time4_t Ptouchtime;
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
	    time4_t now;
#endif
	    int     nWelcomes;
	} e;
    } GV2;

    /* ¬G¶m fromcache */
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

typedef struct {
    unsigned char oldlen;                /* previous line length */
    unsigned char len;                   /* current length of line */
    unsigned char mode;                  /* status of line, as far as update */
    unsigned char smod;                  /* start of modified data */
    unsigned char emod;                  /* end of modified data */
    unsigned char sso;                   /* start stand out */
    unsigned char eso;                   /* end stand out */
    /* data ¥²»Ý¬O³Ì«á¤@­ÓÄæ¦ì, see screen_backup() */
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

/* type in gomo.c, structure passing through socket */
typedef struct {
    char            x;
    char            y;
} Horder_t;

#ifdef OUTTACACHE
typedef struct {
    int     index; // ¦b SHM->uinfo[index]
    int     uid;   // Á×§K¦b cache server ¤W¤£¦P¨B, ¦A½T»{¥Î.
    int     friendstat;
    int     rfriendstat;
} ocfs_t;
#endif

#endif
