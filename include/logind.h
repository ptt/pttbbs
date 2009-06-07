///////////////////////////////////////////////////////////////////////
// Login Daemon Data

#ifndef _BBS_LOGIND_H

#include "bbs.h"

typedef struct login_data
{
    // terminal information
    int  t_lines, t_cols;
    int  encoding;

    // user authentication
    char userid[IDLEN+1];
	char hostip[32+1];

}   login_data;

#endif // _BBS_LOGIND_H

// vim:ts=4:sw=4
