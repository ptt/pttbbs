
#ifndef __OSDEP_H__
#define __OSDEP_H__

/* os dependant include file, define */
#ifdef __FreeBSD__
    #if __FreeBSD__ >= 5
        #include <sys/limits.h>
    #else
        #include <machine/limits.h>
    #endif

    #include <machine/param.h>

    #define HAVE_SETPROCTITLE

#elif defined(__linux__)

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE	       /* for strcasestr */
#endif
    #include <sys/ioctl.h>
    #include <sys/file.h>      /* for flock() */
    #include <strings.h>       /* for strcasecmp() */

    #define NEED_STRLCPY
    #define NEED_STRLCAT

#else

    #error "Unknown OSTYPE"

#endif


#define Signal (signal)

#ifdef NEED_STRLCPY
    size_t strlcpy(char *dst, const char *src, size_t size);
#endif
#ifdef NEED_STRLCAT
    size_t strlcat(char *dst, const char *src, size_t size);
#endif


#endif
