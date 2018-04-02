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

/* �p������� */
typedef struct chicken_t { /* 128 bytes */
    char    name[20];
    uint8_t type;             /* ���� */
    uint8_t _pad0[3];
    int32_t _pad1[4];
    time4_t birthday;         /* �ͤ� */
    time4_t lastvisit;        /* �W�����U�ɶ� */
    int32_t oo;               /* �ɫ~ */
    int32_t food;             /* ���� */
    int32_t medicine;         /* �ī~ */
    int32_t weight;           /* �魫 */
    int32_t dirty;            /* ż�� */
    int32_t _run;             /* (�w����) �ӱ��� */
    int32_t _attack;          /* (�w����) �����O */
    int32_t book;             /* �ǰ� */
    int32_t happy;            /* �ּ� */
    int32_t satis;            /* ���N�� */
    int32_t _temperament;     /* (�w����) ��� */
    int32_t tired;            /* �h�ҫ� */
    int32_t sick;             /* �f����� */
    int32_t hp;               /* ��q */
    int32_t hp_max;           /* ����q */
    int32_t _mm;              /* (�w����) �k�O */
    int32_t _mm_max;          /* (�w����) ���k�O */
    time4_t cbirth;           /* ��ڭp��Ϊ��ͤ� */
    int32_t commonsense;      /* �`���I�� */
    int32_t pad2[1];           /* �d�ۥH��� */
} PACKSTRUCT chicken_t;

#define PASS_INPUT_LEN  8         /* Length of valid input password length.
                                     For DES, set to 8. */
#define PASSLEN    14             /* Length of encrypted passwd field */
#define REGLEN     38             /* Length of registration data */

#define PASSWD_VERSION	4194

typedef struct userec_t {
    uint32_t	version;	/* version number of this sturcture, we
    				 * use revision number of project to denote.*/

    char	userid[IDLEN+1];/* �ϥΪ�ID */
    char	realname[20];	/* �u��m�W */
    char	nickname[24];	/* �ʺ� */
    char	passwd[PASSLEN];/* �K�X */
    char	pad_1;

    uint32_t    uflag;		/* �ߺD, see uflags.h */
    uint32_t    _unused1;	/* �q�e��ߺD2, �ϥΫe�Х��M0 */
    uint32_t    userlevel;	/* �v�� */
    uint32_t    numlogindays;	/* �W�u��� (�C��̦h+1���n�J����) */
    uint32_t    numposts;	/* �峹�g�� */
    time4_t	firstlogin;	/* ���U�ɶ� */
    time4_t	lastlogin;	/* �̪�W���ɶ�(�]�t����) */
    char	lasthost[IPV4LEN+1];/* �W���W���ӷ� */
    int32_t     money;		/* Ptt�� */
    char	_unused[4];

    char	email[50];	/* Email */
    char	address[50];	/* ��} */
    char	justify[REGLEN+1];/* �f�ָ�� */
    uint8_t     _unused_birth[3]; /* �ͤ� ���~ */
    uint8_t     over_18;        /* �O�_�w��18�� */
    uint8_t	pager_ui_type;	/* �I�s���ɭ����O (was: WATER_*) */
    uint8_t	pager;		/* �I�s�����A */
    uint8_t	invisible;	/* ���Ϊ��A */
    char	_unused4[2];
    uint32_t    exmailbox;	/* �ʶR�H�c�� */

    // r3968 ���X sizeof(chicken_t)=128 bytes
    char	_unused5[4];
    char	career[40];	/* �Ǿ�¾�~ */
    char	_unused_phone[20];	/* �q�� */
    uint32_t	_unused6;	/* �q�e���ഫ�e�� numlogins, �ϥΫe�Х��M0 */
    char	chkpad1[44];
    uint32_t    role;           /* Role-specific permissions */
    time4_t	lastseen;	/* �̪�W���ɶ�(�������p) */
    time4_t     timesetangel;   /* �W���o��ѨϮɶ� */
    time4_t	timeplayangel;	/* �W���P�ѨϤ��ʮɶ� (by day) */
    // �H�W���� sizeof(chicken_t) �P���j�p

    time4_t	lastsong;	/* �W���I�q�ɶ� */
    uint32_t	loginview;	/* �i���e�� */
    uint8_t	_unused8;	// was: channel
    uint8_t	pad_2;

    uint16_t	vl_count;	/* �H�k�O�� ViolateLaw counter */
    uint16_t	five_win;	/* ���l�Ѿ��Z �� */
    uint16_t	five_lose;	/* ���l�Ѿ��Z �� */
    uint16_t	five_tie;	/* ���l�Ѿ��Z �M */
    uint16_t	chc_win;	/* �H�Ѿ��Z �� */
    uint16_t	chc_lose;	/* �H�Ѿ��Z �� */
    uint16_t	chc_tie;	/* �H�Ѿ��Z �M */
    uint16_t    conn6_win;      /* ���l�Ѿ��Z �� */
    uint16_t    conn6_lose;     /* ���l�Ѿ��Z �� */
    uint16_t    conn6_tie;      /* ���l�Ѿ��Z �M */
    char	_unused_mind[2];/* �¤߱� */
    uint16_t	go_win;		/* ��Ѿ��Z �� */
    uint16_t	go_lose;	/* ��Ѿ��Z �� */
    uint16_t	go_tie;		/* ��Ѿ��Z �M */
    uint16_t    dark_win;       /* �t�Ѿ��Z �� */
    uint16_t    dark_lose;      /* �t�Ѿ��Z �� */
    uint8_t     ua_version;     /* Applicable user agreement version */

    uint8_t	signature;	/* �D��ñ�W�� */
    uint8_t	_unused10;	/* �q�e��n�峹��, �ϥΫe�Х��M0 */
    uint8_t	badpost;	/* �������a�峹�� */
    uint16_t    dark_tie;       /* �t�Ѿ��Z �M */
    char	myangel[IDLEN+1];/* �ڪ��p�Ѩ� */
    char	pad_3;

    uint16_t	chess_elo_rating;/* �H�ѵ��Ť� */
    uint32_t    withme;	 	 /* �ڷQ��H�U�ѡA���.... */
    time4_t	timeremovebadpost;/* �W���R���H��ɶ� */
    time4_t	timeviolatelaw;  /* �Q�}�@��ɶ� */

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
#define WITHME_CONN6	0x00001000
#define WITHME_NOCONN6	0x00002000

#define BTLEN      48             /* Length of board title */

/* TODO �ʺA��s����줣���Ӹ�n�g�J�ɮת��V�b�@�_,
 * �ܤ֥έ� struct �]�_�Ӥ��� */
typedef struct boardheader_t { /* 256 bytes */
    char    brdname[IDLEN + 1];	    /* bid */
    char    title[BTLEN + 1];
    char    BM[IDLEN * 3 + 3];	    /* BMs' userid, token '/' */
    char    pad1[3];
    uint32_t brdattr;		    /* board���ݩ� */
    char    chesscountry;	    /* �Ѱ� */
    uint8_t _vote_limit_posts;	    /* (�w����) �s�p : �峹�g�ƤU�� */
    uint8_t vote_limit_logins;	    /* �s�p : �n�J���ƤU�� */
    uint8_t pad2_1[1];	            /* (�w����) �s�p : ���U�ɶ����� */
    time4_t bupdate;		    /* note update time */
    uint8_t _post_limit_posts;	    /* (�w����) �o��峹 : �峹�g�ƤU�� */
    uint8_t post_limit_logins;	    /* �o��峹 : �n�J���ƤU�� */
    uint8_t pad2_2[1];	            /* (�w����) �o��峹 : ���U�ɶ����� */
    uint8_t bvote;		    /* ���|�� Vote �� */
    time4_t vtime;		    /* Vote close time */
    uint32_t level;		    /* �i�H�ݦ��O���v�� */
    time4_t perm_reload;	    /* �̫�]�w�ݪO���ɶ� */
    int32_t gid;		    /* �ݪO���ݪ����O ID */
    int32_t next[2];		    /* �b�P�@��gid�U�@�ӬݪO �ʺA����*/
    int32_t firstchild[2];	    /* �ݩ�o�ӬݪO���Ĥ@�Ӥl�ݪO */
    int32_t parent;		    /* �o�ӬݪO�� parent �ݪO bid */
    int32_t childcount;		    /* ���h�֭�child */
    int32_t nuser;		    /* �h�֤H�b�o�O */
    int32_t postexpire;		    /* postexpire */
    time4_t endgamble;
    char    posttype[33];
    char    posttype_f;
    uint8_t fastrecommend_pause;    /* �ֳt�s�����j */
    uint8_t vote_limit_badpost;	    /* �s�p : �H��W�� */
    uint8_t post_limit_badpost;	    /* �o��峹 : �H��W�� */
    char    pad3[3];
    time4_t SRexpire;		    /* SR Records expire time */
    char    pad4[40];
} PACKSTRUCT boardheader_t;

// TODO BRD ���z�F�A����H �ǳƱq pad3 ���@�Өӷ� attr2 �a...
// #define BRD_NOZAP       	0x00000001	/* ���i ZAP */
#define BRD_NOCOUNT		0x00000002	/* ���C�J�έp */
//#define BRD_NOTRAN		0x00000004	/* ����H */
#define BRD_GROUPBOARD		0x00000008	/* �s�ժO */
#define BRD_HIDE		0x00000010	/* ���êO (�ݪO�n�ͤ~�i��) */
#define BRD_POSTMASK		0x00000020	/* ����o��ξ\Ū */
#define BRD_ANONYMOUS		0x00000040	/* �ΦW�O */
#define BRD_DEFAULTANONYMOUS	0x00000080	/* �w�]�ΦW�O */
#define BRD_NOCREDIT		0x00000100	/* �o��L���y�ݪO */
#define BRD_VOTEBOARD		0x00000200	/* �s�p���ݪO */
#define BRD_WARNEL		0x00000400	/* �s�p���ݪO */
#define BRD_TOP			0x00000800	/* �����ݪO�s�� */
#define BRD_NORECOMMEND		0x00001000	/* ���i���� */
#define BRD_ANGELANONYMOUS	0x00002000	/* �p�Ѩϥi�ΦW */
#define BRD_BMCOUNT		0x00004000	/* �O�D�]�w�C�J�O�� */
#define BRD_SYMBOLIC		0x00008000	/* symbolic link to board */
#define BRD_NOBOO		0x00010000	/* ���i�N */
//#define BRD_LOCALSAVE		0x00020000	/* �w�] Local Save */
#define BRD_RESTRICTEDPOST	0x00040000	/* �O�ͤ~��o�� */
#define BRD_GUESTPOST		0x00080000	/* guest�� post */
#define BRD_COOLDOWN		0x00100000	/* �N�R */
#define BRD_CPLOG		0x00200000	/* �۰ʯd����O�� */
#define BRD_NOFASTRECMD		0x00400000	/* �T��ֳt���� */
#define BRD_IPLOGRECMD		0x00800000	/* ����O�� IP */
#define BRD_OVER18		0x01000000	/* �Q�K�T */
#define BRD_NOREPLY		0x02000000	/* ���i�^�� */
#define BRD_ALIGNEDCMT		0x04000000	/* ����������� */
#define BRD_NOSELFDELPOST       0x08000000      /* ���i�ۧR */
#define BRD_BM_MASK_CONTENT	0x10000000	/* ���\�O�D�R���S�w��r */

// Board group linked-list type. Used for array index of firstchild and next.
#define BRD_GROUP_LL_TYPE_NAME  (0)
#define BRD_GROUP_LL_TYPE_CLASS (1)

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

// Prefix of str_reply and str_forward usually needs longer.
// In artitlcle list, the normal length is (80-34)=46.
#define DISP_TTLEN  46

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
    unsigned short int reply_counter;
    char    pad3;
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
#define MSGMODE_ALOHA     4

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
    uint16_t dark_win;
    uint16_t dark_lose;
    char    gap_0;
    unsigned char angelpause;       // TODO move to somewhere else in future.
    uint16_t dark_tie;

    /* friends */
    int     friendtotal;              /* �n�ͤ����cache �j�p */ 
    short   nFriends;                /* �U�� friend[] �u�Ψ�e�X��,
                                        �Ψ� bsearch */
    short   _unused3;
    int     myfriend[MAX_FRIEND];
    char    gap_1[4];
    unsigned int friend_online[MAX_FRIEND];/* point��u�W�n�� utmpshm����m */
			          /* �n�ͤ����cache �e���bit�O���A */
    char    gap_2[4];
    int     reject[MAX_REJECT];
    char    gap_3[4];

    /* messages */
    char    msgcount;
    char    _unused4[3];
    msgque_t        msgs[MAX_MSGS];
    char    gap_4[sizeof(msgque_t)];   /* avoid msgs racing and overflow */

    /* user status */
    char    birth;                   /* �O�_�O�ͤ� Ptt*/
    unsigned char   active;         /* When allocated this field is true */
    unsigned char   invisible;      /* Used by cloaking function in Xyz menu */
    unsigned char   mode;           /* UL/DL, Talk Mode, Chat Mode, ... */
    unsigned char   pager;          /* pager toggle, YEA, or NA */
    char    _unused5;
    uint16_t conn6_win;
    time4_t lastact;               /* �W���ϥΪ̰ʪ��ɶ� */
    char    alerts;             /* mail alert, passwd update... */
    char    _unused_mind;
    uint16_t conn6_lose;
    char    _unused_mind2;

    /* chatroom/talk/games calling */
    unsigned char   sig;            /* signal type */
    uint16_t conn6_tie;
    int     destuid;              /* talk uses this to identify who called */
    int     destuip;              /* dest index in utmpshm->uinfo[] */
    unsigned char   sockactive;     /* Used to coordinate talk requests */

    /* chat */
    unsigned char   in_chat;        /* for in_chat commands   */
    char    chatid[11];             /* chat id, if in chat mode */

    /* games */
    unsigned char   lockmode;       /* ���� multi_login �����F�� */
    char    turn;                    /* �C�������� */
    char    mateid[IDLEN + 1];       /* �C����⪺ id */
    char    color;                   /* �t�� �C�� */

    /* game record */
    uint16_t five_win;
    uint16_t five_lose;
    uint16_t five_tie;
    uint16_t chc_win;
    uint16_t chc_lose;
    uint16_t chc_tie;
    uint16_t chess_elo_rating;
    uint16_t go_win;
    uint16_t go_lose;
    uint16_t go_tie;

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
    userinfo_t      *uin;     // Ptt:�o�i�H���Nalive
} water_t;

typedef struct {
    int row, col;
    int y, x;
    int roll;	// for scrolling
    void *raw_memory;
} screen_backup_t;

// menu_t ���O gmenu_t (deprecated), ��ذϱM�� menu
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
/* keymap, �Y needitem = 0 ��ܤ��ݭn item, func �� type ���� int (*)(void).
 * �_�h���� int (*)(int ent, const fileheader_t *fhdr, const char *direct) */
typedef struct {
    char needitem;
    int (*func)();
} onekey_t;

#define ANSILINELEN (511)                /* Maximum Screen width in chars */

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
/* USHM_SIZE �� MAX_ACTIVE �j�O���F�����ˬd�H�ƤW����, �S�P�ɽĶi��
 * �|�y���� shm �Ŧ쪺�L�a�j��. 
 * �S, �] USHM ���� hash, �Ŷ��y�j�ɮĲv���n. */

/* MAX_BMs is dirty hardcode 4 in mbbsd/cache.c:is_BM_cache() */
#define MAX_BMs         4                 /* for BMcache, �@�ӬݪO�̦h�X�O�D */

// TODO
// ���ѽЦn�ߤH��z shm: 
// (2) userinfo_t �i�H�����@�Ǥw���Ϊ�

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
    // TODO(piaip) Always have this var - no more #ifdefs in structure.
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
                    /* �Ĥ@��double buffer ��currsorted���V�ثe�ϥΪ�
		       �ĤG��sort type */
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
    int     bsorted[2][MAX_BOARD]; /* 0: by name 1: by class */ /* ���Y�s���O bid-1 */
    char    gap_11[sizeof(int)];
    // TODO(piaip) Always have this var - no more #ifdefs in structure.
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
    int     __never_used__n_notes[MAX_ADBANNER_SECTION];      /* �@�`�����X�� �ݪO */
    char    gap_19[sizeof(int)];
    // FIXME remove it
    int     __never_used__next_refresh[MAX_ADBANNER_SECTION]; /* �U�@���nrefresh�� �ݪO */
    char    gap_20[sizeof(int)];
    msgque_t loginmsg;  /* �i�����y */
    int     last_film;
    int     last_usong;
    time4_t Puptime;
    time4_t Ptouchtime;
    int     Pbusystate;

    /* SHM ���������ܼ�, �i�� shmctl �]�w�����. �ѰʺA�վ�δ��ըϥ� */
    union {
	int     v[512];
	struct {
	    int     dymaxactive;  /* �ʺA�]�w�̤j�H�ƤW��     */
	    int     toomanyusers; /* �W�L�H�ƤW�������i���Ӽ� */
	    int     noonlineuser; /* ���W�ϥΪ̤����G�����   */
	    time4_t now;
	    int     nWelcomes;
	    int     shutdown;     /* shutdown flag */

	    /* �`�N, ���O�� align sizeof(int) */
	} e;
    } GV2;
    /* statistic */
    unsigned int    statistic[STAT_MAX];

    // �q�e�@���G�m�ϥ� (fromcache). �{�w�Q daemon/fromd ���N�C
    unsigned int    _deprecated_home_ip[MAX_FROM];
    unsigned int    _deprecated_home_mask[MAX_FROM];
    char            _deprecated_home_desc[MAX_FROM][32];
    int        	    _deprecated_home_num;

    int     max_user;
    time4_t max_time;
    time4_t Fuptime;
    time4_t Ftouchtime;
    int     Fbusystate;

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
    /* data ���ݬO�̫�@�����, see screen_backup() */
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
