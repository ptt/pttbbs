#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "perm.h"
#include "util.h"

time_t now;

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
    time_t d;
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
		
		unum = searchuser(u->userid);
		strcpy(buf, ctime(&u->lastlogin));
		strtok(buf, "\n");
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

int main() {
    now = time(NULL);
    openlog("reaper", LOG_PID | LOG_PERROR, SYSLOG_FACILITY);
    chdir(BBSHOME);
    
    if(passwd_mmap())
	exit(1);
    passwd_apply2(check);
    
    return 0;
}
