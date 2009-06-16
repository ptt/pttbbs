///////////////////////////////////////////////////////////////////////
// Login Daemon Data

#ifndef _BBS_LOGIND_H

#include "bbs.h"

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

#endif // _BBS_LOGIND_H

// vim:et
