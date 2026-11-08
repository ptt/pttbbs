#define _UTIL_C_
#include "bbs.h"

/* Update user online info: count today in numlogindays for the users who
 * have been online since an earlier day (and so did not log in today). */

static int verbose = 0;

/* The beginning (0:00am) of the day of firstlogin, the base of the days of
 * a user, as in pwcuLoginSave() of mbbsd/passwd.c. */
static time4_t
day_base(time4_t firstlogin)
{
    struct tm tm;

    if (!localtime4_r(&firstlogin, &tm))
        return firstlogin;
    tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
    return time_to_time4(mktime(&tm));
}

void fastcheck()
{
    int i;
    static char seen_uid[MAX_USERS + 1];
    userec_t urec;
    userinfo_t u;
    time4_t base;

    memset(seen_uid, 0, sizeof(seen_uid));

    for (i = 0; i < USHM_SIZE; i++) {
        memcpy(&u, SHM->uinfo + i, sizeof(u));
        if (!u.userid[0])
            continue;
        if (verbose > 2)
            fprintf(stderr, "scanning: %s (UID:%d, PID:%d)\n", u.userid, u.uid, u.pid);
        if (u.mode == DEBUGSLEEPING ||
            (u.userlevel & PERM_VIOLATELAW) ||
            !(u.userlevel & PERM_LOGINOK) ||
            u.pid <= 0 ||
            kill(u.pid, 0) != 0)
            continue;
        if (u.uid <= 0 || u.uid > MAX_USERS || seen_uid[u.uid])
            continue;
        seen_uid[u.uid] = 1;

        // Found new online user.
        passwd_query(u.uid, &urec);
        if (strcmp(urec.userid, u.userid) != 0) {
            if (verbose)
                fprintf(stderr, "warning: UTMP (%s) does not match PW(%s).\n",
                        u.userid, urec.userid);
            continue;
        }
        if (verbose > 1)
            fprintf(stderr, "checking: %s (%s)\n", urec.userid, Cdatelite(&urec.lastlogin));

        // Count a day once, like logins do (pwcuLoginSave): only if the
        // last login (or update) was on an earlier day. So it doesn't matter
        // when or how many times a day this runs.
        base = day_base(urec.firstlogin);
        if (time4_days_elapsed(now, base) <= time4_days_elapsed(urec.lastlogin, base))
            continue;

        if (verbose)
            fprintf(stderr, "update: %s (%s, %d) ->", urec.userid,
                    Cdatelite(&urec.lastlogin), urec.numlogindays);
        /* user still online, let's mock it. */
        urec.lastlogin = now;
        urec.numlogindays++;
        if (verbose)
            fprintf(stderr, "(%s, %d).\n", Cdatelite(&urec.lastlogin), urec.numlogindays);
        passwd_update(u.uid, &urec);
    }
}

int main(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    const char *prog = argv[0];
    while (argc > 1) {
        if (strcmp(argv[1], "-v") == 0) {
            verbose++;
            argc--, argv++;
        } else {
            fprintf(stderr, "usage: %s [-v]\n", prog);
            return -1;
        }
    }
    now = time(NULL);
    chdir(BBSHOME);

    attach_SHM();
    fastcheck();
    return 0;
}
