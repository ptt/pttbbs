/* $Id$ */
#ifndef INCLUDE_CONFIG_H
#define INCLUDE_CONFIG_H

#include <syslog.h>
#include "../pttbbs.conf"

#define BBSPROG         BBSHOME "/bin/mbbsd"         /* 祘Α */
#define BAN_FILE        "BAN"                        /* 闽硄郎 */
#define LOAD_FILE       "/proc/loadavg"              /* for Linux */

/* ╰参(秎ンノ)某禬筁 3 じ 冈ǎ sample/pttbbs.conf */
#ifndef BBSMNAME
#define BBSMNAME	"Ptt"
#endif

/* ╰参(匡虫ノ)某 4 じ 冈ǎ sample/pttbbs.conf */
#ifndef BBSMNAME2
#define BBSMNAME2	"⑥tt"
#endif

/* 窥刽某 3 じ 冈ǎ sample/pttbbs.conf */
#ifndef MONEYNAME
#define MONEYNAME	"Ptt"
#endif

#ifndef BBSUSER
#define BBSUSER "bbs"
#endif

#ifndef BBSUID
#define BBSUID (9999)
#endif

#ifndef BBSGID
#define BBSGID (99)
#endif

/* Default Board Names */
#ifndef BN_BUGREPORT
#define BN_BUGREPORT "SYSOP"
#endif

#ifndef BN_SYSOP
#define BN_SYSOP "SYSOP"
#endif

#ifndef BN_ID_PROBLEM
#define BN_ID_PROBLEM "SYSOP"
#endif

#ifndef BN_LAW
#define BN_LAW  BBSMNAME "Law"
#endif

#ifndef BN_NEWBIE
#define BN_NEWBIE BBSMNAME "Newhand"
#endif

#ifndef BN_TEST
#define BN_TEST "Test"
#endif

#ifndef BN_NOTE
#define BN_NOTE "Note"
#endif

#ifndef BN_SECURITY
#define BN_SECURITY "Security"
#endif

#ifndef BN_RECORD
#define BN_RECORD "Record"
#endif

#ifndef BN_FOREIGN
#define BN_FOREIGN BBSMNAME "Foreign"
#endif

/* Environment */
#ifndef RELAY_SERVER_IP                     /* 盚獺 mail server */
#define RELAY_SERVER_IP    "127.0.0.1"
#endif

#ifndef MAX_USERS                           /* 程蔼爹计 */
#define MAX_USERS          (150000)
#endif

#ifndef MAX_ACTIVE
#define MAX_ACTIVE        (1024)         /* 程计 */
#endif

#ifndef MAX_GUEST
#define MAX_GUEST         (100)          /* 程 guest 计 */
#endif

#ifndef MAX_CPULOAD
#define MAX_CPULOAD       (70)           /* CPU 程蔼load */
#endif

#ifndef MAX_LANG
#define MAX_LANG          (1)			 /* 程ㄏノ粂ē */
#endif

#ifndef MAX_STRING
#define MAX_STRING        (8000)         /* ╰参程ㄏノ﹃ */
#endif

#ifndef MAX_POST_MONEY                      /* 祇ゅ彻絑禣 */
#define MAX_POST_MONEY     (100)
#endif

#ifndef MAX_CHICKEN_MONEY                   /* 緄蔓初莉 */
#define MAX_CHICKEN_MONEY  (100)
#endif

#ifndef MAX_GUEST_LIFE                      /* 程ゼ粄靡ㄏノ玂痙丁() */
#define MAX_GUEST_LIFE     (3 * 24 * 60 * 60)
#endif

#ifndef MAX_EDIT_LINE
#define MAX_EDIT_LINE 2048                  /* ゅ彻程絪胯 */
#endif 

#ifndef MAX_EDIT_LINE_LARGE
#define MAX_EDIT_LINE_LARGE (32000)
#endif

#ifndef MAX_LIFE                            /* 程ㄏノ玂痙丁() */
#define MAX_LIFE           (120 * 24 * 60 * 60)
#endif

#ifndef MAX_FROM
#define MAX_FROM           (300)            /* 程珿秏计 */
#endif

#ifndef DEBUGSLEEP_SECONDS
#define DEBUGSLEEP_SECONDS (3600)	    /* debug 单丁 */
#endif

#ifndef THREAD_SEARCH_RANGE
#define THREAD_SEARCH_RANGE (500)
#endif

#ifndef HAVE_JCEE                           /* 厩羛σ琩篯╰参 */
#define HAVE_JCEE 0
#endif

#ifndef MEM_CHECK
#define MEM_CHECK 0x98761234
#endif

#ifndef FOREIGN_REG_DAY                     /* 膟ㄏノ刚ノら戳 */
#define FOREIGN_REG_DAY   30
#endif

#ifndef HAVE_FREECLOAK
#define HAVE_FREECLOAK     0
#endif

#ifndef FORCE_PROCESS_REGISTER_FORM
#define FORCE_PROCESS_REGISTER_FORM 0
#endif

#ifndef TITLE_COLOR
#define TITLE_COLOR       ANSI_COLOR(0;1;37;46)
#endif

#ifndef SYSLOG_FACILITY
#define SYSLOG_FACILITY   LOG_LOCAL0
#endif

#ifndef TAR_PATH
#define TAR_PATH "tar"
#endif

#ifndef MUTT_PATH
#define MUTT_PATH "mutt"
#endif

#ifndef HBFLexpire
#define HBFLexpire        (432000)       /* 5 days */
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN        (256)
#endif

#ifndef PATHLEN
#define PATHLEN           (256)
#endif

#ifndef MAX_BOARD
#define MAX_BOARD         (8192)         /* 程秨狾计 */
#endif

#ifndef MAX_EXKEEPMAIL
#define MAX_EXKEEPMAIL    (1000)         /* 程獺絚ぶ */
#endif

#ifndef OVERLOADBLOCKFDS
#define OVERLOADBLOCKFDS  (0)            /* 禬更穦玂痙硂或 fd */
#endif

#ifndef HOTBOARDCACHE
#define HOTBOARDCACHE     (0)            /* 荐狾е */
#endif

#ifndef INNTIMEZONE
#define INNTIMEZONE       "+0000 (UTC)"
#endif

#ifndef ADD_EXMAILBOX
#define ADD_EXMAILBOX     0              /* 秘癳獺絚 */
#endif

#ifndef HASH_BITS
#define HASH_BITS         16             /* userid->uid hashing bits */
#endif

#ifndef VICE_MIN
#define VICE_MIN	(1)	    /* 程祇布肂 */
#endif // VICE_MIN

/* (deprecated) more.c いゅ彻计(lines/22), +4 for safe */
#define MAX_PAGES         (MAX_EDIT_LINE / 22 + 4)

/* piaip modules */
#define USE_PMORE	(1)	// pmore is the only pager now.
// #define USE_PFTERM	(1)	// pfterm is still experimental

/* 临ゼ俱瞶 */
#define MAX_FRIEND        (256)          /* 更 cache ぇ程狟ね计ヘ */
#define MAX_REJECT        (32)           /* 更 cache ぇ程胊计ヘ */
#define MAX_MSGS          (10)           /* 瞴(荐癟)г瑻 */
#define MAX_MOVIE         (500)          /* 程笆篈狾计 */
#define MAX_MOVIE_SECTION (10)		 /* 程笆篈狾摸 */
#define MAX_ITEMS         (1000)         /* ヘ魁程Τ碭兜 */
#define MAX_HISTORY       (12)           /* 笆篈狾玂 12 掸菌癘魁 */
#define MAX_CROSSNUM      (9) 	         /* 程crosspostΩ计 */
#define MAX_QUERYLINES    (16)           /* 陪ボ Query/Plan 癟程︽计 */
#define MAX_LOGIN_INFO    (128)          /* 程絬硄计 */
#define MAX_POST_INFO     (32)           /* 程穝ゅ彻硄计 */
#define MAX_NAMELIST      (128)          /* 程ㄤ疭虫计 */
#define MAX_KEEPMAIL      (200)          /* 程玂痙碭 MAIL */
#define MAX_NOTE          (20)           /* 程玂痙碭絞痙ē */
#define MAX_SIGLINES      (6)            /* 帽郎ま程︽计 */
#define MAX_CROSSNUM      (9) 	         /* 程crosspostΩ计 */
#define MAX_REVIEW        (7)		 /* 程瞴臮 */
#define NUMVIEWFILE       (14)           /* 秈礶程计 */
#define MAX_SWAPUSED      (0.7)          /* SWAP程蔼ㄏノ瞯 */
#define LOGINATTEMPTS     (3)            /* 程秈ア粇Ω计 */
#define WHERE                            /* 琌Τ珿秏 */
#undef  LOG_BOARD  			 /* 狾琌log */


#define LOGINASNEW              /* 蹦ノビ叫眀腹 */
#define NO_WATER_POST           /* ňゎBlahBlahΑ拈 */
#define USE_BSMTP               /* ㄏノopusBSMTP 盚Μ獺? */
#define HAVE_ANONYMOUS          /* 矗ㄑ Anonymous 狾 */
#define INTERNET_EMAIL          /* や穿 InterNet Email ( Forward) */
#define HAVE_ORIGIN             /* 陪ボ author ㄓ矪 */
#undef  HAVE_INFO               /* 陪ボ祘Αセ弧 */
#undef  HAVE_LICENSE            /* 陪ボ GNU 舦礶 */
#define FAST_LOGIN		/* Login ぃ浪琩环狠ㄏノ */
#undef  HAVE_REPORT             /* ╰参發萝厨 */
#undef  NEWUSER_LIMIT           /* 穝も隔ぱ */
#undef  HAVE_X_BOARDS

#define SHOWUID                 /* ǎㄏノ UID */
#define SHOWBOARD               /* ǎㄏノ狾 */
#define SHOWPID                 /* ǎㄏノ PID */

#define DOTIMEOUT
#ifdef  DOTIMEOUT
#define IDLE_TIMEOUT    (43200) /* 薄猵ぇ timeout (12hr) */
#define SHOW_IDLE_TIME          /* 陪ボ盯竚丁 */
#endif

#define SEM_ENTER      -1      /* enter semaphore */
#define SEM_LEAVE      1       /* leave semaphore */
#define SEM_RESET      0       /* reset semaphore */

#define SHM_KEY         1228

#define PASSWDSEM_KEY   2010	/* semaphore key */

#define NEW_CHATPORT    3838
#define CHATPORT        5722

#define MAX_ROOM         16              /* 程Τ碭丁碵 */

#define EXIT_LOGOUT     0
#define EXIT_LOSTCONN   -1
#define EXIT_CLIERROR   -2
#define EXIT_TIMEDOUT   -3
#define EXIT_KICK       -4

#define CHAT_LOGIN_OK       "OK"
#define CHAT_LOGIN_EXISTS   "EX"
#define CHAT_LOGIN_INVALID  "IN"
#define CHAT_LOGIN_BOGUS    "BG"
#define BADCIDCHARS " *"        /* Chat Room い窽ノ nick じ */

#define BN_ALLPOST "ALLPOST"
#define BN_ALLHIDPOST "ALLHIDPOST"

#define MAXTAGS	255
#define WRAPMARGIN (511)

#ifdef USE_MASKED_FROMHOST
#define FROMHOST    fromhost_masked
#else
#define FROMHOST    fromhost
#endif

#endif
