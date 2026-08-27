#include "bbs.h"
#include "cmbbs.h"
#include "config.h"
#include "common.h"

// common log with formats for BBS (utils and clients)

int
log_payment(const char *filename GCC_UNUSED, int money GCC_UNUSED,
            int oldm GCC_UNUSED, int newm GCC_UNUSED,
            const char *reason GCC_UNUSED, time4_t now GCC_UNUSED)
{
#if defined(USE_RECENTPAY) || defined(LOG_RECENTPAY)
    return log_filef(filename,
                     "%s %s $%d ($%d => $%d) %s\n",
                     Cdatelite(&now),
                     money >= 0 ? "支出" : "收入",
                     money >= 0 ? money : -money,
                     oldm,
                     newm,
                     reason);
#else
    return 0;
#endif
}

int log_user_security(const char *user, const char *fmt, ...)
{
    assert(user && *user);

    if (!user || !*user)
        return -1;

    char fn[PATHLEN];
    sethomefile(fn, user, FN_USERSECURITY);

    va_list ap;
    va_start(ap, fmt);
    char *buf = NULL;
    if (vasprintf(&buf, fmt, ap) < 0 || !buf) {
        va_end(ap);
        return -1;
    }
    va_end(ap);

    int res = file_appendf(fn, "%s %s", Cdatelite(&now), buf);
    free(buf);
    return res;
}

