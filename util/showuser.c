/* $Id: reaper.c 4826 2009-09-10 10:05:12Z piaip $ */
#define _UTIL_C_
#include "bbs.h"

int check(void *data, int n, userec_t *u) {

    if (!u->userid[0])
        return 0;

    printf("%-*s\n", IDLEN, u->userid);
    return 0;
}

int main(int argc, char **argv)
{
    chdir(BBSHOME);
    attach_SHM();

    if(passwd_init())
	exit(1);

    passwd_apply(NULL, check);
    return 0;
}
