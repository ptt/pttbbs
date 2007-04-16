/*     ¹Ø¬Pµ{¦¡               96 10/11            */

#define _UTIL_C_
#include "bbs.h"

#define OUTFILE    BBSHOME "/etc/birth.today"

struct userec_t user;

int bad_user_id(const char *userid) {
    register char ch;
    int j;
    if (strlen(user.userid) < 2 || !isalpha(user.userid[0]))
	return 1;
    if (user.numlogins == 0 || user.numlogins > 15000)
	return 1;
    if (user.numposts > 15000)
	return 1;
    for (j = 1; (ch = user.userid[j]); j++)
    {
	if (!isalnum(ch))
	    return 1;
    }
    return 0;
}

int Link(const char *src, const char *dst) {
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
    //int i, day = 0;
    time_t now;
    struct tm *ptime;
    int j;

    attach_SHM();
    now = time(NULL);		/* back to ancent */
    ptime = localtime(&now);

    if(passwd_init())
	exit(1);

    printf("*»sªí\n");
    fp1 = fopen(OUTFILE, "w");

    fprintf(fp1, "\n                      "
	    "[1m¡¹[35m¡¹[34m¡¹[33m¡¹[32m¡¹[31m¡¹[45;33m ¹Ø¬P¤jÆ[ "
	    "[40m¡¹[32m¡¹[33m¡¹[34m¡¹[35m¡¹[1m¡¹[m \n\n");
    fprintf(fp1, "[33m¡i[1;45m¥»¤é¹Ø¬P[40;33m¡j[m \n");
    int horoscope = getHoroscope(ptime->tm_mon + 1, ptime->tm_mday);
    for(j = 1; j <= MAX_USERS; j++) {
	passwd_query(j, &user);
	if (bad_user_id(NULL))
	    continue;
	if (user.month == ptime->tm_mon + 1 && user.day == ptime->tm_mday) {
	    char genbuf[200];
	    sprintf(genbuf, BBSHOME "/home/%c/%s", user.userid[0], user.userid);
	    stampfile(genbuf, &mymail);
	    strcpy(mymail.owner, BBSNAME);
	    strcpy(mymail.title, "!! ¥Í¤é§Ö¼Ö !!");
	    unlink(genbuf);
	    Link(BBSHOME "/etc/Welcome_birth.%d", genbuf, horoscope);
	    sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", user.userid[0], user.userid);
	    append_record(genbuf, &mymail, sizeof(mymail));
	    if ((user.numlogins + user.numposts) < 20)
		continue;

	    fprintf(fp1,
		    "   [33m[%2d/%-2d] %-14s[0m %-24s login:%-5d post:%-5d\n",
		    ptime->tm_mon + 1, ptime->tm_mday, user.userid,
		    user.nickname, user.numlogins, user.numposts);
	}
    }
    fclose(fp1);
    return 0;
}
