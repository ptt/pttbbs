#ifndef INNBBSCONF_H
#define INNBBSCONF_H
#include <stdio.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/param.h>
#include <sys/wait.h>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#ifndef BSD44
#include <malloc.h>
#endif
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>

/* #include "bbs.h" */
#if defined(AIX)
#include <sys/select.h>
#endif

/*
 * BBS home directory It has been overridden in Makefile
 */
#ifndef _PATH_BBSHOME
#define _PATH_BBSHOME "/u/staff/bbsroot/csie_util/bntpd/home"
/* # define _PATH_BBSHOME "/home/bbs" */
#endif

#ifndef EXPIREDAYS
#define EXPIREDAYS 7
#endif

#ifndef DEFAULT_HIST_SIZE
#define DEFAULT_HIST_SIZE 100000
#endif

/*
 * Maximum number of connections accepted by innbbsd
 */
#ifndef MAXCLIENT
#define MAXCLIENT 500
#endif

/*
 * Maximum number of articles received for a newsgroup by bbsnnrp each time
 */
#ifndef MAX_ARTS
#define MAX_ARTS 100
#endif

/*
 * Maximum size of articles received
 */
#ifndef MAX_ART_SIZE
#define MAX_ART_SIZE 1000000L
#endif


/*
 * Maximum number of articles stated for a newsgroup by bbsnnrp each time
 */
#ifndef MAX_STATS
#define MAX_STATS 1000
#endif

/*
 * Mininum wait interval for bbsnnrp
 */
#ifndef MIN_WAIT
#define MIN_WAIT 60
#endif


#ifndef DefaultINNBBSPort
#define DefaultINNBBSPort "7777"
#endif

/*
 * time to maintain history database
 */
#ifndef HIS_MAINT
#define HIS_MAINT
#define HIS_MAINT_HOUR 4
#define HIS_MAINT_MIN  30
#endif

#ifndef ChannelSize
#define ChannelSize 4096
#endif

#ifndef ReadSize
#define ReadSize 1024
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef CLX_IOCTL
#define CLX_IOCTL
#endif

#define DEFAULTSERVER "your.favorite.news.server"
#define DEFAULTPORT "nntp"
#define DEFAULTPROTOCOL "tcp"
#define DEFAULTPATH ".innbbsd"

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

/*
 * # ifndef ARG #  ifdef __STDC__ #   define ARG(x) (x) #  else #   define
 * ARG(x) () #  endif # endif
 */
/* machine dependend */
#if defined(__linux)
#ifndef LINUX
#define LINUX
#endif
#endif

#if !defined(__svr4__) || defined(sun)
#define WITH_TM_GMTOFF
#endif
#if (defined(__svr4__) && defined(sun)) || defined(Solaris)
#ifndef Solaris
#define Solaris
#endif
#define NO_getdtablesize
//#define NO_bcopy
//#define NO_bzero
//#define NO_flock
#define WITH_lockf
#endif

#if defined(AIX)
#define NO_flock
#define WITH_lockf
#endif

#if defined(HPUX)
#define NO_getdtablesize
#define NO_flock
#define WITH_lockf
#endif

#ifdef NO_bcopy
#ifndef bcopy
#define bcopy(a,b,c) memcpy(b,a,c)
#endif
#endif

#ifdef NO_bzero
#ifndef bzero
#define bzero(mem, size) memset(mem,'\0',size)
#endif
#endif

#ifndef LOCK_EX
#define LOCK_EX         2	/* exclusive lock */
#define LOCK_UN         8	/* unlock */
#endif

#ifdef DEC_ALPHA
#define ULONG unsigned int
#else
#define ULONG unsigned long
#endif

#ifdef PalmBBS
#undef WITH_RECORD_O
#endif

#endif
