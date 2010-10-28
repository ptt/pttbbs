#define _UTIL_C_
#include "bbs.h"
#include <time.h>

int accs = 0;

int transform(userec_t *new, userec_t *old, int i)
{
    int mind = 1, maxd = 1;
    userec_t *u = new;

    memcpy(new, old, sizeof(userec_t));

    if (!u->userid[0])
	return 0;

    accs ++;

    // save old login number
    u->old_numlogins = u->numlogindays;

    // adjust new 'lastseen'
    u->lastseen = u->lastlogin;

    // check validate firstlogin
    // if (u->firstlogin < SITE_CREATION_DATE)
    //    u->firstlogin = SITE_CREATION_DATE;

    // calculate max logindays
    maxd = (u->lastlogin - u->firstlogin) / DAY_SECONDS;
    if (maxd < mind)
	maxd = mind;
    if (u->numlogindays > maxd)
	u->numlogindays = maxd;

    printf("%-13s: numlogin: %d -> %d\n", 
	    u->userid, u->old_numlogins, u->numlogindays);

    // force convert!
    // passwd_update(n+1, u);

    return 0;
}

int main(void)
{
    int fd, fdw;
    userec_t new;
    userec_t old;
    int i = 0;

    printf("sizeof(userec_t)=%u\n", (unsigned int)sizeof(userec_t));
    printf("You're going to convert your .PASSWDS\n");
    printf("The new file will be named .PASSWDS.trans.tmp\n");

    if (chdir(BBSHOME) < 0) {
	perror("chdir");
	exit(-1);
    }

    if ((fd = open(FN_PASSWD, O_RDONLY)) < 0 ||
	 (fdw = open(FN_PASSWD".trans.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0 ) {
	perror("open");
	exit(-1);
    }

    while (read(fd, &old, sizeof(old)) > 0) {
	transform(&new, &old, ++i);
	write(fdw, &new, sizeof(new));
    }

    close(fd);
    close(fdw);

    printf("total %d records converted.\n", accs);
    return 0;
}
