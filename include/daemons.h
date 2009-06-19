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

#endif // _BBS_DAEMONS_H

// vim:et
