#include "innbbsconf.h"

#ifdef NO_getdtablesize
#include <sys/time.h>
#include <sys/resource.h>
getdtablesize()
{
    struct rlimit   limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) >= 0) {
	return limit.rlim_cur;
    }
    return -1;
}
#endif

#if 0
#if defined(SYSV) && !defined(WITH_RECORD_O)
#include <fcntl.h>
flock(fd, op)
    int             fd, op;
{
    switch (op) {
    case LOCK_EX:
	op = F_LOCK;
	break;
    case LOCK_UN:
	op = F_ULOCK;
	break;
    default:
	return -1;
    }
    return lockf(fd, op, 0L);
}
#endif
#endif
