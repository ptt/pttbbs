#define _UTIL_C_
#include "bbs.h"

#define CLEAR(x) memset(&(u->x), 0, sizeof(u->x))

int check(void *data, int n, userec_t *u)
{
    if (!u->userid[0])
        return 0;
    fprintf(stderr, "%d\r", n);

    // clear unused data
    CLEAR(_unused);
    CLEAR(_unused1);
    CLEAR(_unused3);
    CLEAR(_unused4);
    CLEAR(_unused5);
    CLEAR(_unused6);
    CLEAR(_unused7);
    CLEAR(_unused8);
    CLEAR(_unused9);
    CLEAR(_unused10);
    CLEAR(_unused11);
    CLEAR(_unused12);
    // flush
    passwd_update(n+1, u);
    return 0;
}

int main(int argc, char **argv)
{
    now = time(NULL);
    chdir(BBSHOME);

    attach_SHM();
    if(passwd_init())
        exit(1);
    passwd_apply(NULL, check);

    return 0;
}
