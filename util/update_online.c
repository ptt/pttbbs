#define _UTIL_C_
#include "bbs.h"

/* Update user online info. */

int check(void *data GCC_UNUSED, int n, userec_t *u)
{
    time4_t now;
    userinfo_t *utmp;
    if (!u->userid[0] ||
        (u->userlevel & PERM_VIOLATELAW) ||
        !(u->userlevel & PERM_LOGINOK))
        return 0;

    utmp = search_ulistn(n + 1, 1);
    if (utmp == NULL)
        return 0;

    // If the process does not exist...
    if (utmp->pid <= 0 || kill(utmp->pid, 0) != 0)
        return 0;

    // TODO how to make sure the UTMP has already logged?
    now = (time4_t)time(0);

    /* user still online, let's mock it. */
    if (u->lastlogin + DAY_SECONDS <= now)
        return 0;

    fprintf(stderr, ".");
    u->lastlogin = now;
    u->numlogindays++;
    passwd_update(n + 1, u);
    return 0;
}

int main(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    now = time(NULL);
    chdir(BBSHOME);

    attach_SHM();
    if(passwd_init())
	exit(1);
    passwd_fast_apply(NULL, check);
    return 0;
}
