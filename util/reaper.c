/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

time4_t now;

int invalid(char *userid) {
    int i;
    
    if(!isalpha(userid[0]))
	return 1;
    
    for(i = 1; i < IDLEN && userid[i]; i++)
	if(!isalpha(userid[i]) && !isdigit(userid[i]))
	    return 1;
    return 0;
}

int check(int n, userec_t *u) {
    time4_t d;
    char buf[256];
    
    if(u->userid[0] != '\0') {
	if(invalid(u->userid)) {
	    syslog(LOG_ERR, "bad userid(%d): %s", n, u->userid);
	    u->userid[0] = '\0';
	} else {
	    d = now - u->lastlogin;
	    if((d > MAX_GUEST_LIFE && (u->userlevel & PERM_LOGINOK) == 0) ||
	       (d > MAX_LIFE && (u->userlevel & PERM_XEMPT) == 0)) {
		/* expired */
		int unum;
		
		unum = searchuser(u->userid, u->userid);
		strcpy(buf, ctime4(&u->lastlogin));
		syslog(LOG_NOTICE, "kill user(%d): %s %s", unum, u->userid, buf);
		sprintf(buf, "mv home/%c/%s tmp/", u->userid[0], u->userid);
		if(system(buf))
		    syslog(LOG_ERR, "can't move user home: %s", u->userid);
		u->userid[0] = '\0';
		setuserid(unum, u->userid);
	    }
	}
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
    passwd_apply(check);
    
    return 0;
}
