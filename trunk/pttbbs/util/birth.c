/*     ¹Ø¬Pµ{¦¡               96 10/11            */

#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "config.h"
#include "pttstruct.h"
#include "util.h"
#include "common.h"

#define OUTFILE    BBSHOME "/etc/birth.today"

struct userec_t cuser;

int bad_user_id() {
    register char ch;
    int j;
    if (strlen(cuser.userid) < 2 || !isalpha(cuser.userid[0]))
	return 1;
    if (cuser.numlogins == 0 || cuser.numlogins > 15000)
	return 1;
    if (cuser.numposts > 15000)
	return 1;
    for (j = 1; (ch = cuser.userid[j]); j++)
    {
	if (!isalnum(ch))
	    return 1;
    }
    return 0;
}

int Link(char *src, char *dst) {
    char cmd[200];

    if (link(src, dst) == 0)
	return 0;

    sprintf(cmd, "/bin/cp -R %s %s", src, dst);
    return system(cmd);
}

int main(argc, argv)
    int argc;
    char **argv;
{
    FILE *fp1;
    fileheader_t mymail;
    int i, day = 0;
    time_t now;
    struct tm *ptime;
    int j;

    now = time(NULL);		/* back to ancent */
    ptime = localtime(&now);

    if(passwd_mmap())
	exit(1);

    printf("*»sªí\n");
    fp1 = fopen(OUTFILE, "w");

    fprintf(fp1, "\n                      "
	    "[1m¡¹[35m¡¹[34m¡¹[33m¡¹[32m¡¹[31m¡¹[45;33m ¹Ø¬P¤jÆ[ "
	    "[40m¡¹[32m¡¹[33m¡¹[34m¡¹[35m¡¹[1m¡¹[m \n\n");
    fprintf(fp1, "[33m¡i[1;45m¥»¤é¹Ø¬P[40;33m¡j[m \n");
    for(j = 1; j <= MAX_USERS; j++) {
	passwd_query(j, &cuser);
	if (bad_user_id())
	    continue;
	if (cuser.month == ptime->tm_mon + 1)
	{
	    if (cuser.day == ptime->tm_mday)
	    {
		char genbuf[200];
		sprintf(genbuf, BBSHOME "/home/%c/%s", cuser.userid[0], cuser.userid);
		stampfile(genbuf, &mymail);
		strcpy(mymail.owner, BBSNAME);
		strcpy(mymail.title, "!! ¥Í¤é§Ö¼Ö !!");
		mymail.savemode = 0;
		unlink(genbuf);
		Link(BBSHOME "/etc/Welcome_birth", genbuf);
		sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", cuser.userid[0], cuser.userid);
		append_record(genbuf, &mymail, sizeof(mymail));
		if ((cuser.numlogins + cuser.numposts) < 20)
		    continue;

                fprintf(fp1,
		 "   [33m[%2d/%-2d] %-14s[0m %-24s login:%-5d post:%-5d\n",
		    ptime->tm_mon + 1, ptime->tm_mday, cuser.userid,
			cuser.username, cuser.numlogins, cuser.numposts);
	    }
	}
    }
    fclose(fp1);
    return 0;
}
