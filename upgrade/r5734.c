#define _UTIL_C_
#include "bbs.h"

#define CLEAR(x) { \
    for (i = 0; i < sizeof(u->x); i++) \
        if (((char*)&(u->x))[i] != 0) \
            need_clear = 1; \
    memset(&(u->x), 0, sizeof(u->x)); \
}

int check(void *data, int n, userec_t *u)
{
    int i = 0;
    int need_clear = 0;
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
    if (need_clear) {
        printf("Clear: %s\n", u->userid);
        passwd_update(n+1, u);
    }
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
