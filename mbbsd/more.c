/* $Id$ */
#include "bbs.h"

/* use new pager: piaip's more. */
int more(char *fpath, int promptend)
{
    int r = pmore(fpath, promptend);

    if (r == RET_DOSYSOPEDIT)
    {
	if (HasUserPerm(PERM_SYSOP) &&
#ifdef GLOBAL_SECURITY
	    strcmp(currboard, GLOBAL_SECURITY) != 0 &&
#endif // GLOBAL_SECURITY
	    strcmp(fpath, "etc/ve.hlp") != 0 &&
	    1)
	{
	    time4_t t = time4(NULL);
	    log_filef("log/security", LOG_CREAT,
		    "%d %24.24s %d %s admin edit file=%s\n", 
		    t, ctime4(&t), getpid(), cuser.userid, fpath);
	    vedit(fpath, NA, NULL);
	}
	r = FULLUPDATE;
    }

    return r;
}

