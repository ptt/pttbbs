/* $Id */
#ifndef OSDEP_H__
#define OSDEP_H__

#ifdef __GNUC__
#define GCC_CHECK_FORMAT(a,b) __attribute__ ((format (printf, a, b)))
#else
#define GCC_CHECK_FORMAT(a,b)
#endif

/* os dependant include file, define */
#ifdef __linux__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE	       /* for strcasestr */
#endif

#include <sys/ioctl.h>
#include <sys/file.h>      /* for flock() */
#include <strings.h>       /* for strcasecmp() */

#if !defined(__has_builtin) || __has_builtin(strlcpy)
#define NEED_STRLCPY
#endif
#if !defined(__has_builtin) || __has_builtin(strlcat)
#define NEED_STRLCAT
#endif

#define NEED_SETPROCTITLE

#elif ! defined(__FreeBSD__) || ! defined( DARWIN )
//#error "Unknown OSTYPE"
#endif

#define Signal (signal)

#ifdef NEED_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

#ifdef NEED_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#ifdef NEED_SETPROCTITLE
#include <stdarg.h>
extern void initsetproctitle(int argc, char **argv, char **envp);
extern void setproctitle(const char* format, ...) GCC_CHECK_FORMAT(1,2);
#else
#define initsetproctitle(argc, argv, envp) do { (void)argc; (void)argv; (void)envp; } while (0)
#endif

extern int cpuload(char *str);

extern void get_memusage(int buflen, char *buf);
#endif
