/* $Id$ */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/types.h>
#include <pwd.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "config.h"

#ifdef Solaris
    #include <utmpx.h>
    #define U_FILE UTMPX_FILE
#else
    #include <utmp.h>
    #define U_FILE UTMP_FILE
#endif

#ifdef __FreeBSD__
    #define UTMP_FILE _PATH_UTMP
#endif

#ifndef Solaris
    #if MAXHOSTNAMELEN < UT_HOSTSIZE
	#define MAX_HOMENAME_LEN MAXHOSTNAMELEN
    #else
	#define MAX_HOMENAME_LEN UT_HOSTSIZE
    #endif
#else
    /* according to /usr/include/utmpx.h ... */
    #define MAX_HOMENAME_LEN 256
#endif

/* fill the hid with from hostname */
void gethid(char *hid, char *tty)
{
    char frombuf[100];

    if (getenv("SSH_CLIENT"))
	sscanf(getenv("SSH_CLIENT"), "%s", frombuf);
    else
	strcpy(frombuf, "127.0.0.1");

    if (strrchr(frombuf, ':'))
	strncpy(hid, strrchr(frombuf, ':') + 1, MAX_HOMENAME_LEN);
    else
	strncpy(hid, frombuf, MAX_HOMENAME_LEN);
}

/*
   show ban file
   if filename exist, print it out, sleep 1 second, and return 0;
   otherwise, return -1.
 */
int showbanfile(char *filename)
{
    FILE *fp;
    char buf[256];

    fp = fopen(filename, "r");
    if (fp)
    {
	while (fgets(buf, sizeof(buf), fp))
	    fputs(buf, stdout);
	printf("\n============================="
	       "=============================\n");
	fclose(fp);
	sleep(10);
    }
    return fp ? 0 : -1;
}

int main(void)
{
    int uid, rtv = 0;
    char *tty, ttybuf[32], hid[MAX_HOMENAME_LEN + 1];

#ifndef Solaris
    openlog("bbsrf", LOG_PID | LOG_PERROR, LOG_USER);
#else
    openlog("bbsrf", LOG_PID, LOG_USER);
#endif
    chdir(BBSHOME);
    uid = getuid();

    while (1)
    {
	if (!showbanfile(BAN_FILE))
	{
	    rtv = 1;
	    break;
	}
	else if (uid != BBSUID)
	{
	    syslog(LOG_ERR, "UID DOES NOT MATCH");
	    rtv = -1;
	    break;
	}
	else if (!getpwuid(uid))
	{
	    syslog(LOG_ERR, "YOU DONT EXIST");
	    rtv = -1;
	    break;
	}
	else
	{
	    tty = ttyname(0);
	    if (tty)
	    {
		strcpy(ttybuf, tty);
		gethid(hid, ttybuf);
	    }
	    else
	    {
		strcpy(ttybuf, "notty");
		strcpy(hid, "unknown");
	    }
	    execl(BBSPROG, "mbbsd", hid, ttybuf, NULL);
	    syslog(LOG_ERR, "execl(): %m");
	    sleep(3); // prevent flooding
	    rtv = -1;
	}
	break;
    }
    return rtv;
}
