/* $Id: bbsrf.c,v 1.1 2002/03/07 15:13:45 in2 Exp $ */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/types.h>
#include <utmp.h>
#include <pwd.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "config.h"

/* fill the hid with from hostname */
void gethid(char *hid, char *tty)
{
    int fd;
    char *tp;
    struct utmp data;

    gethostname(hid, MAXHOSTNAMELEN);
    hid[MAXHOSTNAMELEN] = '\0';
    tp = strrchr(tty, '/') + 1;
    if (tp && strlen(tp) == 5)
    {
	fd = open(_PATH_UTMP, O_RDONLY);
	if (fd < 0)
	    syslog(LOG_ERR, "%s: %m", _PATH_UTMP);
	else
	{
	    while (read(fd, &data, sizeof(data)) == sizeof(data))
		if (strcmp(data.ut_line, tp) == 0)
		{
		    if (data.ut_host[0]) {
#if MAXHOSTNAMELEN < UT_HOSTSIZE
			strncpy(hid, data.ut_host, MAXHOSTNAMELEN);
			hid[MAXHOSTNAMELEN] = '\0';
#else
			strncpy(hid, data.ut_host, UT_HOSTSIZE);
			hid[UT_HOSTSIZE] = '\0';
#endif
		    }
		    break;
		}
	    close(fd);
	}
    }
}

/*
   get system load averages 
   return 0 if success; otherwise, return -1.
 */
int getload(double load[3])
{
    int rtv = -1;
#if defined(linux)
    FILE *fp;

    fp = fopen(LOAD_FILE, "r");
    if (fp)
    {
	if (fscanf(fp, "%lf %lf %lf", &load[0], &load[1], &load[2]) == 3)
	    rtv = 0;
	fclose(fp);
    }
#elif defined(__FreeBSD__)
    if (getloadavg(load, 3) == 3)
	rtv = 0;
#endif
    return rtv;
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
	sleep(1);
    }
    return fp ? 0 : -1;
}

int main(void)
{
    int uid, rtv = 0;
    char *tty, ttybuf[32], hid[MAXHOSTNAMELEN + 1];

    openlog("bbsrf", LOG_PID | LOG_PERROR, LOG_USER);
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
	    rtv = -1;
	}
	break;
    }
    return rtv;
}
