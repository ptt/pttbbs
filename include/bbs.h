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
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/sem.h>

/* our header */
#include "config.h"

/* common library */
#include "cmsys.h"
#include "cmbbs.h"

#ifdef __dietlibc__
#include "cmdiet.h"
#endif

#include "ansi.h"
#include "vtkbd.h"
#include "visio.h"
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

#ifndef INCLUDE_VAR_H
    #include "var.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BBS_H */
