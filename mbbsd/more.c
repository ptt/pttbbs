/* $Id$ */
#include "bbs.h"

/* use new pager: piaip's more. */
int more(char *fpath, int promptend)
{
    int r = pmore(fpath, promptend);

    switch(r)
    {

	case RET_DOSYSOPEDIT:
	    r = FULLUPDATE;

	    if (!HasUserPerm(PERM_SYSOP) ||
		strcmp(fpath, "etc/ve.hlp") == 0)
		break;

#ifdef GLOBAL_SECURITY
	    if (strcmp(currboard, GLOBAL_SECURITY) == 0)
		break;
#endif // GLOBAL_SECURITY

	    log_filef("log/security", LOG_CREAT,
		    "%u %24.24s %d %s admin edit file=%s\n", 
		    (int)now, ctime4(&now), getpid(), cuser.userid, fpath);
	    vedit(fpath, NA, NULL);
	    break;

	case RET_DOCHESSREPLAY:
	    r = FULLUPDATE;
	    ChessReplayGame(fpath);
	    break;

	case RET_DOBBSLUA:
	    r = FULLUPDATE;
	    bbslua(fpath);
	    break;
    }

    return r;
}

