/* $Id: kaede.c,v 1.1 2002/03/07 15:13:48 in2 Exp $ */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "proto.h"

extern struct utmpfile_t *utmpshm;
extern userec_t cuser;

char *Ptt_prints(char *str, int mode) {
    char *po , strbuf[256];

    while((po = strstr(str, "\033*s"))) {
	po[0] = 0;
	sprintf(strbuf, "%s%s%s", str, cuser.userid, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*t"))) {
	time_t now = time(0);

	po[0] = 0;
	sprintf(strbuf, "%s%s", str, Cdate(&now));
	str[strlen(strbuf)-1] = 0;
	strcat(strbuf, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*u"))) {
	int attempts;

	attempts = utmpshm->number;
	po[0] = 0;
	sprintf(strbuf, "%s%d%s", str, attempts, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*b"))) {
	po[0] = 0;
	sprintf(strbuf, "%s%d/%d%s", str, cuser.month, cuser.day, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*l"))) {
	po[0] = 0;
	sprintf(strbuf, "%s%d%s", str, cuser.numlogins, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*p"))) {
	po[0] = 0;
	sprintf(strbuf, "%s%d%s", str, cuser.numposts, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*n"))) {
	po[0] = 0;
	sprintf(strbuf, "%s%s%s", str, cuser.username, po + 3);
	strcpy(str, strbuf);
    }
    while((po = strstr(str, "\033*m"))) {
	po[0] = 0;
	sprintf(strbuf, "%s%d%s", str, cuser.money, po + 3);
	strcpy(str, strbuf);
    }
    strip_ansi(str, str ,mode);
    return str;
}

int Rename(char* src, char* dst) {
    if(rename(src, dst) == 0)
	return 0;    
    return -1;
}

int Link(char* src, char* dst) {
    char cmd[200];
    
    if(strcmp(src, BBSHOME "/home") == 0)
    	return 1;
    if(link(src, dst) == 0)
	return 0;
    
    sprintf(cmd, "/bin/cp -R %s %s", src, dst);
    return system(cmd);
}

char *my_ctime(const time_t *t) {
    struct tm *tp;
    static char ans[100];

    tp = localtime(t);
    sprintf(ans, "%02d/%02d/%02d %02d:%02d:%02d", (tp->tm_year % 100),
	    tp->tm_mon + 1,tp->tm_mday, tp->tm_hour, tp->tm_min, tp->tm_sec);
    return ans;
}
