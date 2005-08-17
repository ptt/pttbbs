/* $Id$ */
#ifndef INCLUDE_CONFIG_H
#define INCLUDE_CONFIG_H

#include <syslog.h>
#include "../pttbbs.conf"

#define BBSPROG         BBSHOME "/bin/mbbsd"         /* 主程式 */
#define BAN_FILE        "BAN"                        /* 關站通告檔 */
#define LOAD_FILE       "/proc/loadavg"              /* for Linux */

#ifndef BBSUSER
#define BBSUSER "bbs"
#endif

#ifndef BBSUID
#define BBSUID (9999)
#endif

#ifndef BBSGID
#define BBSGID (99)
#endif

#ifndef RELAY_SERVER_IP                     /* 寄站外信的 mail server */
#define RELAY_SERVER_IP    "127.0.0.1"
#endif

#ifndef MAX_USERS                           /* 最高註冊人數 */
#define MAX_USERS          (150000)
#endif

#ifndef MAX_ACTIVE
#define MAX_ACTIVE        (1024)         /* 最多同時上站人數 */
#endif

#ifndef MAX_CPULOAD
#define MAX_CPULOAD       (70)           /* CPU 最高load */
#endif

#ifndef MAX_LANG
#define MAX_LANG          (1)			 /* 最多使用語言 */
#endif

#ifndef MAX_STRING
#define MAX_STRING        (8000)         /* 系統最多使用字串 */
#endif

#ifndef MAX_POST_MONEY                      /* 發表文章稿費的上限 */
#define MAX_POST_MONEY     (100)
#endif

#ifndef MAX_CHICKEN_MONEY                   /* 養雞場獲利上限 */
#define MAX_CHICKEN_MONEY  (100)
#endif

#ifndef MAX_GUEST_LIFE                      /* 最長未認證使用者保留時間(秒) */
#define MAX_GUEST_LIFE     (3 * 24 * 60 * 60)
#endif

#ifndef MAX_EDIT_LINE
#define MAX_EDIT_LINE 2048                  /* 文章最長編輯長度 */
#endif 

#ifndef MAX_LIFE                            /* 最長使用者保留時間(秒) */
#define MAX_LIFE           (120 * 24 * 60 * 60)
#endif

#ifndef MAX_FROM
#define MAX_FROM           (300)            /* 最多故鄉數 */
#endif

#ifndef THREAD_SEARCH_RANGE
#define THREAD_SEARCH_RANGE (500)
#endif

#ifndef HAVE_JCEE                           /* 大學聯考查榜系統 */
#define HAVE_JCEE 0
#endif

#ifndef MEM_CHECK
#define MEM_CHECK 0x98761234
#endif

#ifndef FOREIGN_REG_DAY                     /* 外籍使用者試用日期上限 */
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
#define MAX_BOARD         (8192)         /* 最大開板個數 */
#endif

#ifndef MAX_EXKEEPMAIL
#define MAX_EXKEEPMAIL    (1000)         /* 最多信箱加大多少封 */
#endif

#ifndef OVERLOADBLOCKFDS
#define OVERLOADBLOCKFDS  (0)            /* 超載後會保留這麼多個 fd */
#endif

#ifndef HOTBOARDCACHE
#define HOTBOARDCACHE     (0)            /* 熱門看板快取 */
#endif

#ifndef INNTIMEZONE
#define INNTIMEZONE       "+0800 (CST)"
#endif

#ifndef ADD_EXMAILBOX
#define ADD_EXMAILBOX     0              /* 贈送信箱 */
#endif

#ifndef HASH_BITS
#define HASH_BITS         16             /* userid->uid hashing bits */
#endif

/* more.c 中文章頁數上限(lines/22), +4 for safe */
#define MAX_PAGES         (MAX_EDIT_LINE / 22 + 4)

/* 以下還未整理 */
#define MAX_FRIEND        (256)          /* 載入 cache 之最多朋友數目 */
#define MAX_REJECT        (32)           /* 載入 cache 之最多壞人數目 */
#define MAX_MSGS          (10)           /* 水球(熱訊)忍耐上限 */
#define MAX_MOVIE         (999)          /* 最多動態看板數 */
#define MAX_MOVIE_SECTION (10)		 /* 最多動態看板類別 */
#define MAX_ITEMS         (1000)         /* 一個目錄最多有幾項 */
#define MAX_HISTORY       (12)           /* 動態看板保持 12 筆歷史記錄 */
#define MAX_CROSSNUM      (9) 	         /* 最多crosspost次數 */
#define MAX_QUERYLINES    (16)           /* 顯示 Query/Plan 訊息最大行數 */
#define MAX_LOGIN_INFO    (128)          /* 最多上線通知人數 */
#define MAX_POST_INFO     (32)           /* 最多新文章通知人數 */
#define MAX_NAMELIST      (128)          /* 最多其他特別名單人數 */
#define MAX_KEEPMAIL      (200)          /* 最多保留幾封 MAIL？ */
#define MAX_NOTE          (20)           /* 最多保留幾篇留言？ */
#define MAX_SIGLINES      (6)            /* 簽名檔引入最大行數 */
#define MAX_CROSSNUM      (9) 	         /* 最多crosspost次數 */
#define MAX_REVIEW        (7)		 /* 最多水球回顧 */
#define NUMVIEWFILE       (14)           /* 進站畫面最多數 */
#define MAX_SWAPUSED      (0.7)          /* SWAP最高使用率 */
#define LOGINATTEMPTS     (3)            /* 最大進站失誤次數 */
#define WHERE                            /* 是否有故鄉功能 */
#undef  LOG_BOARD  			 /* 看板是否log */


#define LOGINASNEW              /* 採用上站申請帳號制度 */
#define NO_WATER_POST           /* 防止BlahBlah式灌水 */
#define USE_BSMTP               /* 使用opus的BSMTP 寄收信? */
#define HAVE_ANONYMOUS          /* 提供 Anonymous 板 */
#undef  POSTNOTIFY              /* 新文章通知功能 */
#define INTERNET_EMAIL          /* 支援 InterNet Email 功能(含 Forward) */
#define HAVE_ORIGIN             /* 顯示 author 來自何處 */
#undef  HAVE_MAILCLEAN          /* 清理所有使用者個人信箱 */
#undef  HAVE_SUICIDE            /* 提供使用者自殺功能 */
#undef  HAVE_REPORT             /* 系統追蹤報告 */
#undef  HAVE_INFO               /* 顯示程式版本說明 */
#undef  HAVE_LICENSE            /* 顯示 GNU 版權畫面 */
#define FAST_LOGIN		/* Login 不檢查遠端使用者 */
#define HAVE_CAL                /* 提供計算機 */
#undef  HAVE_REPORT             /* 系統追蹤報告 */
#undef  NEWUSER_LIMIT           /* 新手上路的三天限制 */
#undef  HAVE_X_BOARDS

#define USE_LYNX   	        /* 使用外部lynx dump ? */
#undef  USE_PROXY
#ifdef  USE_PROXY
#define PROXYSERVER "140.112.28.165"
#define PROXYPORT   3128
#endif
#define LOCAL_PROXY             /* 是否開啟local 的proxy */
#ifdef  LOCAL_PROXY
#define HPROXYDAY   1           /* local的proxy refresh天數 */
#endif

#define SHOWMIND                /* 看見心情 */
#define SHOWUID                 /* 看見使用者 UID */
#define SHOWBOARD               /* 看見使用者看板 */
#define SHOWPID                 /* 看見使用者 PID */

#define DOTIMEOUT
#ifdef  DOTIMEOUT
#define IDLE_TIMEOUT    (43200) /* 一般情況之 timeout (12hr) */
#define MONITOR_TIMEOUT (20*60) /* monitor 時之 timeout */
#define SHOW_IDLE_TIME          /* 顯示閒置時間 */
#endif

#define SEM_ENTER      -1      /* enter semaphore */
#define SEM_LEAVE      1       /* leave semaphore */
#define SEM_RESET      0       /* reset semaphore */

#define MAGIC_KEY       1234    /* 身分認證信函編碼 */

#define SHM_KEY         1228
#if 0
#define BRDSHM_KEY      1208
#define UHASH_KEY       1218	/* userid->uid hash */
#define UTMPSHM_KEY     2221
#define PTTSHM_KEY      1220    /* 動態看板 , 節日 */
#define FROMSHM_KEY     1223    /* whereis, 最多使用者 */
#endif

#define BRDSEM_KEY      2005    /* semaphore key */
#define PTTSEM_KEY      2000    /* semaphore key */
#define FROMSEM_KEY     2003    /* semaphore key */
#define PASSWDSEM_KEY   2010

#define NEW_CHATPORT    3838
#define CHATPORT        5722

#define MAX_ROOM         16              /* 最多有幾間包廂？ */

#define EXIT_LOGOUT     0
#define EXIT_LOSTCONN   -1
#define EXIT_CLIERROR   -2
#define EXIT_TIMEDOUT   -3
#define EXIT_KICK       -4

#define CHAT_LOGIN_OK       "OK"
#define CHAT_LOGIN_EXISTS   "EX"
#define CHAT_LOGIN_INVALID  "IN"
#define CHAT_LOGIN_BOGUS    "BG"
#define BADCIDCHARS " *"        /* Chat Room 中禁用於 nick 的字元 */

#define ALLPOST "ALLPOST"
#define ALLHIDPOST "ALLHIDPOST"

#define MAXTAGS	256
#define BRC_STRLEN      15	/* Length of board name */
#define BRC_MAXSIZE     24576
#define BRC_MAXNUM      80

#define WRAPMARGIN (511)

#endif
