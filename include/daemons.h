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

}   login_data;

///////////////////////////////////////////////////////////////////////
// Angel Beats! Daemon

#ifndef ANGELBEATS_ADDR
#define ANGELBEATS_ADDR   ":5132"
#endif

enum ANGELBEATS_OPERATIONS {
    ANGELBEATS_REQ_INVALID = 0,
    ANGELBEATS_REQ_REPORT,
    ANGELBEATS_REQ_RELOAD,
    ANGELBEATS_REQ_SUGGEST,
    ANGELBEATS_REQ_SUGGEST_AND_LINK,
    ANGELBEATS_REQ_REMOVE_LINK,
};

typedef struct {
    short cb;           // size of current structure
    short operation;

    int angel_uid;
    int master_uid;
}   angel_beats_data ;

typedef struct {
    short cb;
    short total_angels;
    short total_online_angels;
    short total_active_angels;
    short low_average_masters;
    short high_average_masters;
    short my_active_masters;
}   angel_beats_report ;

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
