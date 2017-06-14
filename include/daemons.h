#ifndef _BBS_DAEMONS_H

#include "pttstruct.h"

///////////////////////////////////////////////////////////////////////
// From Host Nick Daemon (fromd)

#ifndef FROMD_ADDR
#define FROMD_ADDR      ":5130"
#endif

///////////////////////////////////////////////////////////////////////
// Register E-Mail Database Daemon (regmaild)

#ifndef REGMAILD_ADDR
#define REGMAILD_ADDR   ":5131"
#endif

enum {
    REGMAILDB_REQ_COUNT = 1,
    REGMAILDB_REQ_SET,
    REGCHECK_REQ_AMBIGUOUS,
};

typedef struct
{
    size_t cb;
    int  operation;
    char userid   [IDLEN+1];
    char email    [50]; // TODO define const in pttstruct.h
}   regmaildb_req;


///////////////////////////////////////////////////////////////////////
// Login Daemon (logind)

typedef struct login_data
{
    // size of current structure
    size_t cb;
    void   *ack;

    // terminal information
    int  t_lines, t_cols;
    int  encoding;
    Fnv32_t client_code;

    // user authentication
    char userid[IDLEN+1];
    char hostip[IPV4LEN+1];
    char port  [IDLEN+1];

    // connection information
    uint32_t flags;

}   login_data;

typedef struct conn_data
{
    // size of current structure
    uint32_t cb;
    uint32_t encoding;
    uint32_t raddr_len;
    uint8_t  raddr[16];
    uint16_t rport;
    uint16_t lport;
    uint32_t flags;
} PACKSTRUCT conn_data;

#define CONN_FLAG_SECURE (1 << 0)

///////////////////////////////////////////////////////////////////////
// Angel Beats! Daemon

#ifndef ANGELBEATS_ADDR
#define ANGELBEATS_ADDR   ":5132"
#endif

// Merge perf in every X seconds.
#ifndef ANGELBEATS_PERF_MIN_PERIOD
#define ANGELBEATS_PERF_MIN_PERIOD  (600)
#endif

enum ANGELBEATS_OPERATIONS {
    ANGELBEATS_REQ_INVALID = 0,
    ANGELBEATS_REQ_REPORT,
    ANGELBEATS_REQ_RELOAD,
    ANGELBEATS_REQ_SUGGEST,
    ANGELBEATS_REQ_SUGGEST_AND_LINK,
    ANGELBEATS_REQ_REMOVE_LINK,
    ANGELBEATS_REQ_HEARTBEAT,
    ANGELBEATS_REQ_GET_ONLINE_LIST,
    ANGELBEATS_REQ_EXPORT_PERF,
    ANGELBEATS_REQ_REG_NEW,
    ANGELBEATS_REQ_BLAME,
    ANGELBEATS_REQ_SAVE_STATE,
    ANGELBEATS_REQ_MAX,
};

typedef struct {
    short cb;           // size of current structure
    short operation;

    int angel_uid;
    int master_uid;
}   angel_beats_data;

typedef struct {
    short cb;
    short total_angels;
    short total_online_angels;
    short total_active_angels;

    short min_masters_of_online_angels;
    short max_masters_of_online_angels;
    short min_masters_of_active_angels;
    short max_masters_of_active_angels;

    short my_index;
    short my_active_index;
    short my_active_masters;
    short inactive_days;

    time4_t last_assigned;
    int     missed_assign;
    int     last_assigned_master;

}   angel_beats_report ;

#define ANGELBEATS_UID_LIST_SIZE    (20)

typedef struct {
    short cb;
    short angels;
    int uids[ANGELBEATS_UID_LIST_SIZE];
}   angel_beats_uid_list;

///////////////////////////////////////////////////////////////////////
// Brc Storage Daemon

#ifndef BRCSTORED_ADDR
#define BRCSTORED_ADDR   ":5133"
#endif

enum BRCSTORED_OPERATIONS {
    BRCSTORED_REQ_INVALID = 0,
    BRCSTORED_REQ_READ = 'r',
    BRCSTORED_REQ_WRITE = 'w',
};

///////////////////////////////////////////////////////////////////////
// Comments Daemon

#ifndef COMMENTD_ADDR
#define COMMENTD_ADDR   ":5134"
#endif

#ifndef COMMENTLEN
#define COMMENTLEN (80)
#endif

enum {
    COMMENTD_REQ_ADD = 1,
    COMMENTD_REQ_QUERY_COUNT,
    COMMENTD_REQ_QUERY_BODY,
    COMMENTD_REQ_MARK_DELETED,
};

typedef struct CommentBodyReq {
    time4_t time;
    time4_t ipv4;
    uint32_t userref; /* user.ctime */
    int32_t type;
    char userid[IDLEN + 1];
    char msg[COMMENTLEN + 1];
} PACKSTRUCT CommentBodyReq;

typedef struct CommentKeyReq {
    char board[IDLEN + 1];
    char file[FNLEN + 1];
} PACKSTRUCT CommentKeyReq;

typedef struct {
    short cb;
    short operation;
    CommentBodyReq comment;
    CommentKeyReq key;
} PACKSTRUCT CommentAddRequest;

typedef struct {
    short cb;
    short operation;
    uint32_t start;
    CommentKeyReq key;
} PACKSTRUCT CommentQueryRequest;

///////////////////////////////////////////////////////////////////////
// Posts Daemon
//
#ifndef POSTD_ADDR
#define POSTD_ADDR   ":5135"
#endif

enum {
    POSTD_REQ_ADD = 1,
    POSTD_REQ_IMPORT,
    POSTD_REQ_GET_CONTENT,
    POSTD_REQ_IMPORT_REMOTE,
};

typedef struct {
    uint32_t userref; /* user.ctime */
    uint32_t ctime;   /* post ctime */
    uint32_t ipv4;    /* user.fromhost */
    char userid[IDLEN + 1];
} PACKSTRUCT PostAddExtraInfoReq;

typedef struct {
    char board[IDLEN + 1];
    char file[FNLEN + 1];
} PACKSTRUCT PostKeyReq;

typedef struct {
    short cb;
    short operation;
    fileheader_t header;
    PostAddExtraInfoReq extra;
    PostKeyReq key;
} PACKSTRUCT PostAddRequest;

typedef struct {
    short cb;
    short operation;
    PostKeyReq key;
} PACKSTRUCT PostGetContentRequest;

///////////////////////////////////////////////////////////////////////
// online friend relation daemon
//
typedef struct {
    int     index; // 在 SHM->uinfo[index]
    int     uid;   // 避免在 cache server 上不同步, 再確認用.
    int     friendstat;
    int     rfriendstat;
} ocfs_t;

#endif // _BBS_DAEMONS_H

// vim:et
