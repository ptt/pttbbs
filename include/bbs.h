/* $id$ */
#ifndef INCLUDE_BBS_H
#define INCLUDE_BBS_H

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

/* os dependant include file, define */
#ifdef FreeBSD
    #ifdef FreeBSD5
        #include <sys/limits.h>
    #else
        #include <machine/limits.h>
    #endif
    #include <machine/param.h>
#endif
#ifdef Linux
    #include <sys/param.h>
    #include <limits.h>
    #include <sys/file.h>      // for flock()
    size_t
    strlcpy(char *dst, const char *src, size_t size);

    size_t
    strlcat(char *dst, const char *src, size_t size);
#endif

/* our header */
#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "perm.h"
#include "modes.h"
#include "proto.h"
#include "gomo.h"

#ifdef _UTIL_C_
    #include "util.h"
#endif

#ifndef INCLUDE_VAR_H
    #include "var.h"
#endif

#endif /* INCLUDE_BBS_H */
