#define _UTIL_C_
#include "bbs.h"

int resolve_over18_user(const userec_t *u)
{
    uint8_t month = u->_unused_birth[0];
    uint8_t day = u->_unused_birth[1];
    uint8_t year = u->_unused_birth[2];

    struct tm ptime;

    /* get local time */
    time4_t now = time4(NULL);
    localtime4_r(&now, &ptime);

    // 照實歲計算，沒生日的當作未滿 18
    if (year < 1 || month < 1)
        return 0;
    else if (ptime.tm_year - year > 18)
        return 1;
    else if (ptime.tm_year - year < 18)
        return 0;
    else if ((ptime.tm_mon+1) > month)
        return 1;
    else if ((ptime.tm_mon+1) < month)
        return 0;
    else if (ptime.tm_mday >= day )
        return 1;
    return 0;
}


int check(void *data, int n, userec_t *u)
{
    int over_18;
    int need_write = 0;
    if (!u->userid[0])
        return 0;
    fprintf(stderr, "%d\r", n);

    over_18 = resolve_over18_user(u);
    if (over_18 != u->over_18) {
        u->over_18 = over_18;
        need_write = 1;
    }

    // flush
    if (need_write) {
        printf("Write: %s\n", u->userid);
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
