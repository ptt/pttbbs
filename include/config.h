#ifndef INCLUDE_CONFIG_H
#define INCLUDE_CONFIG_H

#include <syslog.h>
#include "../pttbbs.conf"

/////////////////////////////////////////////////////////////////////////////
// Configuration Helper: IS_ENABLED()
//
// 仿 Linux Kernel 的 IS_ENABLED 巨集，可以 if (IS_ENABLED(CONFIG_XXX))
// 進行編譯期條件判斷，讓編譯器執行 Dead Code Elimination,
// 同時保留語法與型別檢查，避免 #ifdef 造成的不可預期性。
//
// 行為規則：
//   - 未定義 (Undefined)              -> 0 (false)
//   - 定義為 0 (#define CONFIG_XXX 0)  -> 0 (false)
//   - 空定義   (#define CONFIG_XXX)    -> 1 (true)
//   - 定義為 1 (#define CONFIG_XXX 1)  -> 1 (true)
//
// 注意由於我們沒有使用 Kconfig, 在這裡 #define CONFIG_XXX 的行為與 Linux
// 不同。 Linux kernel 會視為 false, 這裡視為 true。
//
#define __ARG_PLACEHOLDER_          0,
#define __ARG_PLACEHOLDER_1         0,
#define _config_val(val)            __ARG_PLACEHOLDER_##val
#define _config_test(arg1_or_junk)  ___config_test(arg1_or_junk 1, 0)
#define ___config_test(__ignored, val, ...) val
#define _config_enabled(cfg)        _config_test(_config_val(cfg))
#define IS_ENABLED(option)          _config_enabled(option)

#ifndef CONFIG_RATING
#define CONFIG_RATING 1
#endif


#define BBSPROG         BBSHOME "/bin/mbbsd"         /* 主程式 */
#define BAN_FILE        "BAN"                        /* 關站通告檔 */
#define LOAD_FILE       "/proc/loadavg"              /* for Linux */

/////////////////////////////////////////////////////////////////////////////
// System Name Configuration 系統名稱設定

/* 系統名(郵件用)，建議別超過 3 個字元。 詳見 sample/pttbbs.conf */
#ifndef BBSMNAME
#define BBSMNAME        "Ptt"
#endif

/* 系統名(選單用)，建議剛好 4 個字元。 詳見 sample/pttbbs.conf */
#ifndef BBSMNAME2
#define BBSMNAME2       "Ｐtt"
#endif

/* 錢幣名，建議剛好 3 個字元。 詳見 sample/pttbbs.conf */
#ifndef MONEYNAME
#define MONEYNAME       BBSMNAME "幣"
#endif

/* AID 顯示的站台名稱。 若 IP 太長請另行定義。 */
#ifndef AID_HOSTNAME
#define AID_HOSTNAME    MYHOSTNAME
#endif

/////////////////////////////////////////////////////////////////////////////
// Themes 主題配色

#ifndef TITLE_COLOR 
#define TITLE_COLOR             ANSI_COLOR(0;1;37;46)   /* 主畫面上方標題列 */
#endif

#ifndef HLP_CATEGORY_COLOR
#define HLP_CATEGORY_COLOR      ANSI_COLOR(0;1;32)  /* 說明表格內分類項 */
#endif

#ifndef HLP_DESCRIPTION_COLOR
#define HLP_DESCRIPTION_COLOR   ANSI_COLOR(0)       /* 說明表格內說明項 */
#endif

#ifndef HLP_KEYLIST_COLOR
#define HLP_KEYLIST_COLOR       ANSI_COLOR(0;1;36)  /* 說明表格內按鍵項 */
#endif

/////////////////////////////////////////////////////////////////////////////
// OS Settings 作業系統相關設定

#ifndef BBSUSER
#define BBSUSER "bbs"
#endif

#ifndef BBSUID
#define BBSUID (9999)
#endif

#ifndef BBSGID
#define BBSGID (99)
#endif

#ifndef TAR_PATH
#define TAR_PATH "tar"
#endif

#ifndef MUTT_PATH
#define MUTT_PATH "mutt"
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN (256)
#endif

#ifndef PATHLEN
#define PATHLEN (256)
#endif

#ifndef DEFAULT_FOLDER_CREATE_PERM
#define DEFAULT_FOLDER_CREATE_PERM (0755)
#endif

#ifndef DEFAULT_FILE_CREATE_PERM
#define DEFAULT_FILE_CREATE_PERM (0644)
#endif

#ifndef SHM_KEY
#define SHM_KEY         1228
#endif

#ifndef SHM_NAME
#define SHM_NAME        "/bbs_" BBSMNAME
#endif

#ifndef PASSWDSEM_KEY
#define PASSWDSEM_KEY   2010    /* semaphore key */
#endif

#ifndef SYSLOG_FACILITY
#define SYSLOG_FACILITY   LOG_LOCAL0
#endif

#ifndef MEM_CHECK
#define MEM_CHECK 0x98761234
#endif

#ifndef RELAY_SERVER_IP                     /* 寄站外信的 mail server */
#define RELAY_SERVER_IP    "127.0.0.1"
#endif

#ifndef XCHATD_ADDR
#define XCHATD_ADDR     ":3838"
#endif

/////////////////////////////////////////////////////////////////////////////
// Default Board Names 預設看板名稱

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

#ifndef BN_DELETED
#define BN_DELETED "deleted"
#endif

#ifndef BN_JUNK
#define BN_JUNK "junk"
#endif 

#ifndef BN_POLICELOG
#define BN_POLICELOG    "PoliceLog"
#endif

#ifndef BN_UNANONYMOUS
#define BN_UNANONYMOUS "UnAnonymous"
#endif

#ifndef BN_NEWIDPOST
#define BN_NEWIDPOST "NEWIDPOST"
#endif

#ifndef BN_ALLPOST
#define BN_ALLPOST "ALLPOST"
#endif

#ifndef BN_ALLHIDPOST
#define BN_ALLHIDPOST "ALLHIDPOST"
#endif

/////////////////////////////////////////////////////////////////////////////
// Performance Parameters 效能參數

#ifndef MAX_USERS                        /* 最高註冊人數 */
#define MAX_USERS         (150000)
#endif

#ifndef MAX_ACTIVE
#define MAX_ACTIVE        (1024)         /* 最多同時上站人數 */
#endif

#ifndef USHM_SIZE
#define USHM_SIZE         ((MAX_ACTIVE) * 41 / 40)
#endif

#ifndef MAX_GUEST
#define MAX_GUEST         (100)          /* 最多 guest 上站人數 */
#endif

#ifndef MAX_CPULOAD
#define MAX_CPULOAD       (70)           /* CPU 最高load */
#endif

#ifndef DEBUGSLEEP_SECONDS
#define DEBUGSLEEP_SECONDS (3600)        /* debug 等待時間 */
#endif

#ifndef MAX_BOARD
#define MAX_BOARD         (8192)         /* 最大開板個數 */
#endif

#ifndef HASH_BITS
#define HASH_BITS         (16)           /* userid->uid hashing bits */
#endif

#ifndef USERLIST_SNAPSHOT_EXPIRE_SECS
#define USERLIST_SNAPSHOT_EXPIRE_SECS (10)       /* seconds before userlist snapshot is refreshed */
#endif

#ifndef OVERLOADBLOCKFDS
#define OVERLOADBLOCKFDS  (0)            /* 超載後會保留這麼多個 fd */
#endif

#ifndef MAX_FRIEND
#define MAX_FRIEND        (256)          /* 載入 cache 之最多朋友數目 */
#endif

#ifndef MAX_REJECT
#define MAX_REJECT        (32)           /* 載入 cache 之最多壞人數目 */
#endif


#ifndef MAX_MSGS
#define MAX_MSGS          (10)           /* 水球(熱訊)忍耐上限 */
#endif

#ifndef MAX_ADBANNER
#define MAX_ADBANNER      (500)          /* 最多動態看板數 */
#endif

#ifndef MAX_SWAPUSED
#define MAX_SWAPUSED      (0.7)          /* SWAP最高使用率 */
#endif

#ifndef HOTBOARDCACHE
#define HOTBOARDCACHE     (0)            /* 熱門看板快取 */
#endif

#ifndef MAX_HOTBOARDS
#define MAX_HOTBOARDS     (128)          /* 熱門看板最大數量 */
#endif

#ifndef TARQUEUE_TIME_STR
#define TARQUEUE_TIME_STR   "深夜"       // 看板備份時間訊息 (應與 contab 一致)
#endif

#ifndef MAX_TERM_COLS
#define MAX_TERM_COLS       200
#endif

#ifndef MIN_TERM_COLS
#define MIN_TERM_COLS       20
#endif

// Warning: MAX_TERM_ROWS will be used in `term.c` for macro expansion, so this
// must be a pure digit without ().
#ifndef MAX_TERM_ROWS
#define MAX_TERM_ROWS       150
#endif

#ifndef MIN_TERM_ROWS
#define MIN_TERM_ROWS       24
#endif

#ifndef DEFAULT_TERM_COLS
#define DEFAULT_TERM_COLS   80
#endif

#ifndef DEFAULT_TERM_ROWS
#define DEFAULT_TERM_ROWS   24
#endif

#define VALID_TERM_COLS(x)  MAX(MIN_TERM_COLS, MIN(MAX_TERM_COLS, (x)))
#define VALID_TERM_ROWS(x)  MAX(MIN_TERM_ROWS, MIN(MAX_TERM_ROWS, (x)))

/////////////////////////////////////////////////////////////////////////////
// More system messages 系統訊息

#ifndef RECYCLE_BIN_NAME
#define RECYCLE_BIN_NAME "資源回收筒" // "垃圾桶"
#endif

#ifndef RECYCLE_BIN_OWNER
#define RECYCLE_BIN_OWNER "[" RECYCLE_BIN_NAME "]"
#endif

#ifndef TIME_CAPSULE_NAME
#define TIME_CAPSULE_NAME "Magical Index" // "Time Capsule"
#endif

/////////////////////////////////////////////////////////////////////////////
// Site settings 站台功能設定

#ifndef MAX_POST_MONEY                      /* 發表文章稿費的上限 */
#define MAX_POST_MONEY          (100)
#endif

#ifndef MAX_CHICKEN_MONEY                   /* 養雞場獲利上限 */
#define MAX_CHICKEN_MONEY       (100)
#endif

#ifndef MAX_GUEST_LIFE                      /* 最長未認證使用者保留時間(秒) */
#define MAX_GUEST_LIFE          (3 * 24 * 60 * 60)
#endif

#ifndef MAX_EDIT_LINE
#define MAX_EDIT_LINE           (2048)      /* 文章最長編輯長度 */
#endif 

#ifndef MAX_EDIT_LINE_LARGE                 // 大檔最長編輯長度
#define MAX_EDIT_LINE_LARGE     (32000)
#endif

#ifndef MAX_LIFE                            /* 最長使用者保留時間(秒) */
#define MAX_LIFE                (120 * 24 * 60 * 60)
#endif

#ifndef KEEP_DAYS_REGGED
#define KEEP_DAYS_REGGED        (120)       /* 已註冊使用者保留多久 */
#endif

#ifndef KEEP_DAYS_UNREGGED
#define KEEP_DAYS_UNREGGED      (15)        /* 未註冊使用者保留多久 */
#endif

#ifndef MAX_FROM
#define MAX_FROM                (300)       /* 最多故鄉數 */
#endif

#ifndef THREAD_SEARCH_RANGE
#define THREAD_SEARCH_RANGE     (500)       /* 系列文章搜尋上限 */
#endif


#ifndef FORCE_PROCESS_REGISTER_FORM
#define FORCE_PROCESS_REGISTER_FORM     (0)
#endif

#ifndef HBFLexpire
#define HBFLexpire        (432000)      /* 5 days */
#endif

#ifndef MAX_EXKEEPMAIL
#define MAX_EXKEEPMAIL    (1000)        /* 最多信箱加大多少封 */
#endif

#ifndef ADD_EXMAILBOX
#define ADD_EXMAILBOX     0             /* 贈送信箱 */
#endif


#ifndef BADPOST_CLEAR_DURATION
#define BADPOST_CLEAR_DURATION  (180)   // 消劣文時間限制
#endif

#ifndef BADPOST_MIN_CLEAR_DURATION
#define BADPOST_MIN_CLEAR_DURATION (3)  // 劣文首消時間限制
#endif

#ifndef MAX_CROSSNUM
#define MAX_CROSSNUM      (9)           /* 最多crosspost次數 */
#endif

/* (deprecated) more.c 中文章頁數上限(lines/22), +4 for safe */
#ifndef MAX_PAGES
#define MAX_PAGES         (MAX_EDIT_LINE / 22 + 4)
#endif

#ifndef MAX_ADBANNER_SECTION
#define MAX_ADBANNER_SECTION (10)       /* 最多動態看板類別 */
#endif

#ifndef MAX_ADBANNER_HEIGHT
#define MAX_ADBANNER_HEIGHT  (11)       /* 最大動態看板內容高度 */
#endif

#ifndef MAX_QUERYLINES
#define MAX_QUERYLINES    (16)          /* 顯示 Query/Plan 訊息最大行數 */
#endif

#ifndef MAX_LOGIN_INFO
#define MAX_LOGIN_INFO    (128)         /* 最多上線通知人數 */
#endif

#ifndef MAX_POST_INFO
#define MAX_POST_INFO     (32)          /* 最多新文章通知人數 */
#endif

#ifndef MAX_NAMELIST
#define MAX_NAMELIST      (128)         /* 最多其他特別名單人數 */
#endif

#ifndef MAX_NOTE
#define MAX_NOTE          (20)          /* 最多保留幾篇留言？ */
#endif

#ifndef MAX_SIGLINES
#define MAX_SIGLINES      (6)           /* 簽名檔引入最大行數 */
#endif

#ifndef MAX_REVIEW
#define MAX_REVIEW        (7)           /* 最多水球回顧 */
#endif

#ifndef NUMVIEWFILE
#define NUMVIEWFILE       (14)          /* 進站畫面最多數 */
#endif

#ifndef LOGINATTEMPTS
#define LOGINATTEMPTS     (3)           /* 最大進站失誤次數 */
#endif

#ifndef MAX_KEEPMAIL
#define MAX_KEEPMAIL            (200)   /* 一般 user 最多保留幾封 MAIL？ */
#endif

#ifndef MAX_KEEPMAIL_SOFTLIMIT
#define MAX_KEEPMAIL_SOFTLIMIT  (2500)  /* 除 admin 外，無法寄給此人 */
#endif

#ifndef MAX_KEEPMAIL_HARDLIMIT
#define MAX_KEEPMAIL_HARDLIMIT  (20000) /* 信箱數量的上限，超過就不給寄信 */
#endif

#ifndef BADCIDCHARS
#define BADCIDCHARS     " *"            /* Chat Room 中禁用於 nick 的字元 */
#endif

#ifndef MAX_ROOM
#define MAX_ROOM        (16)            /* 聊天室最多有幾間包廂？ */
#endif

#ifndef MAXTAGS
#define MAXTAGS         (255)           /* t(tag) 的最大數量 */
#endif

#ifndef WRAPMARGIN
#define WRAPMARGIN      (511)           /* 編輯器 wrap 長度 */
#endif

#ifndef PW_PLAIN_MIN
#define PW_PLAIN_MIN    (4)             /* 密碼最小長度 */
#endif

#ifdef USE_MASKED_FROMHOST
#define FROMHOST    fromhost_masked
#else
#define FROMHOST    fromhost
#endif

/////////////////////////////////////////////////////////////////////////////
// Logging 記錄設定

#ifndef LOG_CONF_KEYWORD        // 記錄搜尋的關鍵字
#define LOG_CONF_KEYWORD        (0)
#endif
#ifndef LOG_CONF_INTERNETMAIL   // 記錄 internet outgoing mail
#define LOG_CONF_INTERNETMAIL   (0)
#endif
#ifndef LOG_CONF_PUSH           // 記錄推文
#define LOG_CONF_PUSH           (0)
#endif
#ifndef LOG_CONF_EDIT_CALENDAR  // 記錄編輯行事曆
#define LOG_CONF_EDIT_CALENDAR  (0)
#endif
#ifndef LOG_CONF_POST           // 記錄發文
#define LOG_CONF_POST           (0)
#endif
#ifndef LOG_CONF_CRAWLER        // 記錄 crawlers
#define LOG_CONF_CRAWLER        (0)
#endif
#ifndef LOG_CONF_CROSSPOST      // 記錄轉錄
#define LOG_CONF_CROSSPOST      (0)
#endif
#ifndef LOG_CONF_BAD_REG_CODE   // 記錄打錯的註冊碼
#define LOG_CONF_BAD_REG_CODE   (0)
#endif
#ifndef LOG_CONF_VALIDATE_REG   // 記錄審核註冊單
#define LOG_CONF_VALIDATE_REG   (0)
#endif
#ifndef LOG_CONF_MASS_DELETE    // 記錄大量刪除檔案
#define LOG_CONF_MASS_DELETE    (0)
#endif
#ifndef LOG_CONF_OSONG_VERBOSE  // 詳細點播記錄
#define LOG_CONF_OSONG_VERBOSE  (0)
#endif
#ifndef LOG_CONF_EDIT_TITLE     // 編輯標題記錄
#define LOG_CONF_EDIT_TITLE     (0)
#endif

/////////////////////////////////////////////////////////////////////////////
// Default Configurations 預設參數

// 正向 CONFIG_* (預設開啟)。若在 pttbbs.conf 定義舊式 NO_XXX，則對應的 CONFIG_* 為 0。
#if !defined(NO_LOGINASNEW) && !defined(CONFIG_LOGINASNEW)
#define CONFIG_LOGINASNEW 1           /* 採用上站申請帳號制度 */
#endif
#if IS_ENABLED(CONFIG_LOGINASNEW)
#define LOGINASNEW 1                  /* legacy compat */
#endif

#if !defined(NO_DOTIMEOUT) && !defined(CONFIG_TIMEOUT)
#define CONFIG_TIMEOUT 1              /* 處理閒置時間 */
#endif

#if !defined(NO_INTERNET_EMAIL) && !defined(CONFIG_INTERNET_EMAIL)
#define CONFIG_INTERNET_EMAIL 1       /* 支援 InterNet Email 功能(含 Forward) */
#endif
#if IS_ENABLED(CONFIG_INTERNET_EMAIL)
#define INTERNET_EMAIL 1              /* legacy compat */
#endif

#if !defined(NO_SHOWUID) && !defined(CONFIG_SHOWUID)
#define CONFIG_SHOWUID 1              /* 站長可看見使用者 UID */
#endif
#if IS_ENABLED(CONFIG_SHOWUID)
#define SHOWUID 1                     /* legacy compat */
#endif

#if !defined(NO_SHOWBOARD) && !defined(CONFIG_SHOWBOARD)
#define CONFIG_SHOWBOARD 1            /* 站長可看見使用者看板 */
#endif
#if IS_ENABLED(CONFIG_SHOWBOARD)
#define SHOWBOARD 1                   /* legacy compat */
#endif

#if !defined(NO_SHOWPID) && !defined(CONFIG_SHOWPID)
#define CONFIG_SHOWPID 1              /* 站長可看見使用者 PID */
#endif
#if IS_ENABLED(CONFIG_SHOWPID)
#define SHOWPID 1                     /* legacy compat */
#endif

#if !defined(NO_HAVE_ANONYMOUS) && !defined(CONFIG_ANONYMOUS)
#define CONFIG_ANONYMOUS 1            /* 提供 Anonymous 板 */
#endif
#if IS_ENABLED(CONFIG_ANONYMOUS)
#define HAVE_ANONYMOUS 1              /* legacy compat */
#endif

#if !defined(NO_HAVE_ORIGIN) && !defined(CONFIG_ORIGIN)
#define CONFIG_ORIGIN 1               /* 顯示 author 來自何處 */
#endif
#if IS_ENABLED(CONFIG_ORIGIN)
#define HAVE_ORIGIN 1                 /* legacy compat */
#endif

#ifndef SITE_MAIL_FACTOR
#define SITE_MAIL_FACTOR (1.0)
#endif

#ifndef POST_KEYWORDS_MAX
#define POST_KEYWORDS_MAX (0)
#endif

#if !defined(NO_REJECT_FLOOD_POST) && !defined(CONFIG_REJECT_FLOOD_POST)
#define CONFIG_REJECT_FLOOD_POST 1    /* 防止BlahBlah式灌水 */
#endif
#if IS_ENABLED(CONFIG_REJECT_FLOOD_POST)
#define REJECT_FLOOD_POST 1           /* legacy compat */
#endif

// 其他預設開啟之功能，若定義了 NO_XXX 則不定義 CONFIG_XXX (即 false)
#if !defined(NO_GAMBLE) && !defined(CONFIG_GAMBLE)
#define CONFIG_GAMBLE 1
#endif

#if !defined(NO_SYSOP_ACCOUNT) && !defined(CONFIG_SYSOP_ACCOUNT)
#define CONFIG_SYSOP_ACCOUNT 1
#endif

#if !defined(NO_GUEST_ACCOUNT_REG) && !defined(CONFIG_GUEST_ACCOUNT_REG)
#define CONFIG_GUEST_ACCOUNT_REG 1
#endif

#if !defined(NO_INSCREEN) && !defined(CONFIG_INSCREEN)
#define CONFIG_INSCREEN 1
#endif

#if !defined(NO_CHECK_AMBIGUOUS_USERID) && !defined(CONFIG_CHECK_AMBIGUOUS_USERID)
#define CONFIG_CHECK_AMBIGUOUS_USERID 1
#endif

#if !defined(NO_ADJUST_CPU_LIMITS) && !defined(CONFIG_ADJUST_CPU_LIMITS)
#define CONFIG_ADJUST_CPU_LIMITS 1
#endif

#if !defined(NO_CONST_CUSER) && !defined(CONFIG_CONST_CUSER)
#define CONFIG_CONST_CUSER 1
#endif

// #define  HAVE_INFO               /* 顯示程式版本說明 */
// #define  HAVE_LICENSE            /* 顯示 GNU 版權畫面 */
// #define  HAVE_REPORT             /* (轉信)系統追蹤報告 */

#if IS_ENABLED(CONFIG_TIMEOUT)
# define IDLE_TIMEOUT    (43200) /* 一般情況之 timeout (12hr) */
# define SHOW_IDLE_TIME          /* 顯示閒置時間 */
# define DOTIMEOUT       1       /* legacy compat */
#endif

#if defined(PLAY_ANGEL) && !defined(CONFIG_ANGEL)
#define CONFIG_ANGEL 1
#endif

#if defined(ALL_REEDIT_LOG) && !defined(CONFIG_ALL_REEDIT_LOG)
#define CONFIG_ALL_REEDIT_LOG 1
#endif

#if defined(DOTIMEOUT) && !defined(CONFIG_TIMEOUT)
#define CONFIG_TIMEOUT 1
#endif

#if defined(EDITPOST_SMARTMERGE) && !defined(CONFIG_EDITPOST_SMARTMERGE)
#define CONFIG_EDITPOST_SMARTMERGE 1
#endif
#if defined(MULTI_WELCOME_LOGIN) && !defined(CONFIG_MULTI_WELCOME_LOGIN)
#define CONFIG_MULTI_WELCOME_LOGIN 1
#endif
#if defined(OUTJOBSPOOL) && !defined(CONFIG_OUTJOBSPOOL)
#define CONFIG_OUTJOBSPOOL 1
#endif
#if defined(DYMAX_ACTIVE) && !defined(CONFIG_DYMAX_ACTIVE)
#define CONFIG_DYMAX_ACTIVE 1
#endif
#if defined(DEBUGSLEEP) && !defined(CONFIG_DEBUGSLEEP)
#define CONFIG_DEBUGSLEEP 1
#endif
#if defined(DEBUG_FWDADDRERR) && !defined(CONFIG_DEBUG_FWDADDRERR)
#define CONFIG_DEBUG_FWDADDRERR 1
#endif
#if defined(CRITICAL_MEMORY) && !defined(CONFIG_CRITICAL_MEMORY)
#define CONFIG_CRITICAL_MEMORY 1
#endif
#if defined(COLORDATE) && !defined(CONFIG_COLORDATE)
#define CONFIG_COLORDATE 1
#endif
#if defined(GUEST_DEFAULT_DBCS_NOINTRESC) && !defined(CONFIG_GUEST_DEFAULT_DBCS_NOINTRESC)
#define CONFIG_GUEST_DEFAULT_DBCS_NOINTRESC 1
#endif
#if defined(SAFE_ARTICLE_DELETE) && !defined(CONFIG_SAFE_ARTICLE_DELETE)
#define CONFIG_SAFE_ARTICLE_DELETE 1
#endif
#if defined(OLDRECOMMEND) && !defined(CONFIG_OLD_RECOMMEND)
#define CONFIG_OLD_RECOMMEND 1
#endif
#if defined(GUESTRECOMMEND) && !defined(CONFIG_GUESTRECOMMEND)
#define CONFIG_GUESTRECOMMEND 1
#endif
#if defined(DEFAULT_AUTOCPLOG) && !defined(CONFIG_DEFAULT_AUTOCPLOG)
#define CONFIG_DEFAULT_AUTOCPLOG 1
#endif
#if defined(FROMD) && !defined(CONFIG_FROMD)
#define CONFIG_FROMD 1
#endif
#if defined(REQUIRE_VERIFY_EMAIL_AT_REGISTER) && !defined(CONFIG_REQUIRE_VERIFY_EMAIL_AT_REGISTER)
#define CONFIG_REQUIRE_VERIFY_EMAIL_AT_REGISTER 1
#endif
#if defined(USEREC_EMAIL_IS_CONTACT) && !defined(CONFIG_USEREC_EMAIL_IS_CONTACT)
#define CONFIG_USEREC_EMAIL_IS_CONTACT 1
#endif
#if defined(ALLOW_REGISTER_WITH_ONLY_CONTACT_EMAIL) && !defined(CONFIG_ALLOW_REGISTER_WITH_ONLY_CONTACT_EMAIL)
#define CONFIG_ALLOW_REGISTER_WITH_ONLY_CONTACT_EMAIL 1
#endif
#if defined(REQUIRE_CONTACT_EMAIL_TO_CHANGE_PASSWORD) && !defined(CONFIG_REQUIRE_CONTACT_EMAIL_TO_CHANGE_PASSWORD)
#define CONFIG_REQUIRE_CONTACT_EMAIL_TO_CHANGE_PASSWORD 1
#endif

#ifdef DEBUG
#define IS_DEBUG 1
#else
#define IS_DEBUG 0
#endif

#endif // INCLUDE_CONFIG_H
