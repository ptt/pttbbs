#define _UTIL_C_
#include "bbs.h"

int check(void *data, int n, userec_t *u) {
    (void)data;
    (void)n;

    if (!u->userid[0])
        return 0;

    printf("%-*s\n", IDLEN, u->userid);
    return 0;
}

int main(void)
{
    chdir(BBSHOME);
    attach_SHM();

    if(passwd_init())
	exit(1);

    passwd_apply(NULL, check);
    return 0;
}
