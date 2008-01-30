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

	case RET_COPY2TMP:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
	    {
			char buf[10];
			getdata(b_lines - 1, 0, "把這篇文章收入到暫存檔？[y/N] ",
					buf, 4, LCECHO);
			if (buf[0] != 'y')
				break;
			setuserfile(buf, ask_tmpbuf(b_lines - 1));
			Copy(fpath, buf);
	    }
	    break;

	case RET_DOCHESSREPLAY:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
		{
			ChessReplayGame(fpath);
		}
	    break;

#if defined(USE_BBSLUA)
	case RET_DOBBSLUA:
	    r = FULLUPDATE;
	    if (HasUserPerm(PERM_BASIC))
		{
			bbslua(fpath);
		}
	    break;
#endif
    }

    return r;
}

