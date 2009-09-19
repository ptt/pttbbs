#define _UTIL_C_
#include "bbs.h"
#include <time.h>

int transform(userec_t *new, userec_t *old, int i)
{
    userec_t *u = new;

    memcpy(new, old, sizeof(userec_t));

    if (!u->userid[0])
	return 0;

    u->uflag |= ADBANNER_USONG_FLAG;

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
	 (fdw = open(FN_PASSWD".trans.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0 ) {
	perror("open");
	exit(-1);
    }

    while (read(fd, &old, sizeof(old)) > 0) {
	transform(&new, &old, ++i);
	write(fdw, &new, sizeof(new));
    }

    close(fd);
    close(fdw);

    // printf("total %d records converted.\n", accs);
    return 0;
}
