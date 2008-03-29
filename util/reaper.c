/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

time4_t now;

#undef MAX_GUEST_LIFE
#undef MAX_LIFE

// override max life
#define MAX_GUEST_LIFE     (365L * 24 * 60 * 60)
#define MAX_LIFE           (365L *10 * 24 * 60 * 60)

static int free_accs = 0;
int check_free(void *data, int n, userec_t *u)
{
    if (u->userid[0] == 0)
	free_accs++;
    return 0;
}

int check(void *data, int n, userec_t *u) {
    time4_t d;
    char buf[256];
    (void)data;

    if (u->userid[0] == 0 && u->userid[1])
    {
	// expired user, clean record!
	syslog(LOG_ERR, "reset record (%d)", n+1);
	memset(u, 0, sizeof(userec_t));
	passwd_update(n+1, u);
	return 0;
    }

    if(u->userid[0] != '\0') {
	if(!is_validuserid(u->userid)) {
	    syslog(LOG_ERR, "bad userid(%d): %s", n, u->userid);
	    u->userid[0] = '\0';
	} else {
	    // test PASSWD synchronization
	    /*
	    int unum = searchuser(u->userid, u->userid);
	    if (unum == 0)
	    {
		strcpy(buf, ctime4(&u->lastlogin));
		syslog(LOG_NOTICE, "invalid user record (%d): %s (%s) %s", n+1, 
			u->userid, (u->userlevel & PERM_LOGINOK) ? "regok" : "unreg",
			buf
			);
		u->userid[0] = '\0';
		passwd_update(n+1, u);
	    }
	    return 0;
	    */

	    d = now - u->lastlogin;

	    // ignore regged accounts now.
	    if (u->userlevel & PERM_LOGINOK)
		return 0;
	    if (u->userlevel & PERM_SYSOP)
		return 0;
	    if (u->userlevel & PERM_XEMPT)
		return 0;

	    // if((d > MAX_GUEST_LIFE && (u->userlevel & PERM_LOGINOK) == 0) 
	    // || (d > MAX_LIFE && (u->userlevel & PERM_XEMPT) == 0)) {
	    if(d > MAX_GUEST_LIFE) {
		/* expired */
		int unum;
		
		unum = searchuser(u->userid, u->userid);
		strcpy(buf, ctime4(&u->lastlogin));
		if (unum != n+1)
		{
		    syslog(LOG_NOTICE, "out-of-sync user(%d/%d): %s %s", unum, n, 
			    u->userid, buf);
		} 
		else if (unum != 0)
		{
		    syslog(LOG_NOTICE, "kill user(%d): %s %s", unum, 
			    // (u->userlevel & PERM_LOGINOK) ? "regok" : "guest",
			    u->userid, buf);

		    log_filef(FN_USIES, LOG_CREAT,
			    "%s %s %-12s %s",
			    Cdate(&now), "CLEAN(EXPIRE)", u->userid, buf);

		    sprintf(buf, "mv home/%c/%s tmp/", u->userid[0], u->userid);
		    if(system(buf))
			syslog(LOG_ERR, "can't move user home: %s", u->userid);
		    u->userid[0] = '\0';
		    setuserid(unum, u->userid);

		    // flush into passwd
		    memset(u, 0, sizeof(userec_t));
		    passwd_update(unum, u);
		} else {
		    /*
		    static int changed = 0;

		    if (changed ++ > 10)
			exit(0);
			*/
    
		    syslog(LOG_NOTICE, "clean user(%d): %s %s", n+1, 
			    u->userid, buf);

		    log_filef(FN_USIES, LOG_CREAT,
			    "%s %s %-12s %s",
			    Cdate(&now), "CLEAN(CLEAR)", u->userid, buf);
		    u->userid[0] = '\0';
		    memset(u, 0, sizeof(userec_t));
		    passwd_update(n+1, u);
		}
	    }
	}
    }
    return 0;
}

int check_last_login(void *data, int n, userec_t *u) {
    char buf[256];

    if (u->userid[0] == 0)
        return 0;

    // ignore all XEMPTY users
    if (u->userlevel & PERM_XEMPT)
        return 0;

    strcpy(buf, Cdate(&u->firstlogin));

    if (u->lastlogin > now + 86400 || u->lastlogin < 0) // should not be newer than now plus one day.
    {
        // invalid record
        printf("使用者 %-*s (登入%3d 次, %s%s, %s [%04X])\n 最後登入日期異常 [%04X]: %s",
                IDLEN, u->userid, u->numlogins,
                (u->userlevel & PERM_LOGINOK) ? "已過認證" : "未過認證",
                (u->userlevel & PERM_SYSOP) ? "[SYSOP]" : "",
                buf,
                (unsigned)u->firstlogin,
                (unsigned)u->lastlogin,
                ctime4(&u->lastlogin));
        // fix it
        u->lastlogin = u->firstlogin;

        if (u->lastlogin > now)
        {
            printf(" (首次登入日期也異常)");
            u->lastlogin = u->firstlogin = 0x362CFC7E;
        }

        printf(" 已修正登入日期為: %s\n", ctime4(&u->lastlogin));

        // flush
        passwd_update(n+1, u);
    }

    return 0;
}


int main(int argc, char **argv)
{
    now = time(NULL);
#ifdef Solaris
    openlog("reaper", LOG_PID, SYSLOG_FACILITY);
#else
    openlog("reaper", LOG_PID | LOG_PERROR, SYSLOG_FACILITY);
#endif
    chdir(BBSHOME);

    attach_SHM();
    if(passwd_init())
	exit(1);
    passwd_apply(NULL, check);
    // passwd_apply(NULL, check_free);
    // printf("free accounts=%d\n", free_accs);
    
    return 0;
}
