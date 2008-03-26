/* $id$ */

#ifndef INCLUDE_BBS_H
#define INCLUDE_BBS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "osdep.h"

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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

/* our header */
#include "config.h"

/* common library */
#include "cmsys.h"
#include "cmbbs.h"

#include "ansi.h"
#include "statistic.h"
#include "uflags.h"
#include "pttstruct.h"
#include "fav.h"
#include "common.h"
#include "perm.h"
#include "modes.h"
#include "chess.h"
#include "proto.h"
#include "fnv_hash.h"

#ifdef ASSESS
    #include "assess.h"
#endif

#ifdef CONVERT
    #include "convert.h"
#endif

#ifdef _UTIL_C_
    #include "util.h"
#endif

#ifndef INCLUDE_VAR_H
    #include "var.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BBS_H */
