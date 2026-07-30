#include "bbs.h"
#include "cmbbs.h"

// common log with formats for BBS (utils and clients)

int
log_payment(const char *filename GCC_UNUSED, int money GCC_UNUSED,
            int oldm GCC_UNUSED, int newm GCC_UNUSED,
            const char *reason GCC_UNUSED, time4_t now GCC_UNUSED)
{
#if defined(USE_RECENTPAY) || defined(LOG_RECENTPAY)
    return log_filef(filename,
                     LOG_CREAT,
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

