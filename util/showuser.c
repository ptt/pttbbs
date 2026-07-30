#define _UTIL_C_
#include "bbs.h"

int check(void *data GCC_UNUSED, int n GCC_UNUSED, userec_t *u) {

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
