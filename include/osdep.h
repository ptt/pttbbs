
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

#elif defined(Solaris)

    #include <alloca.h>
    #include <crypt.h>
    #include <sys/param.h>
    #include <sys/ioctl.h>
    #include <limits.h>
    #include <strings.h>       /* for strcasecmp() */             
								      
    #define _ISOC99_SOURCE

    #define NEED_TIMEGM

    #if __OS_MAJOR_VERSION__ == 5 && __OS_MINOR_VERSION__ < 8
	#define NEED_STRLCPY
	#define NEED_STRLCAT
    #endif

    #if __OS_MAJOR_VERSION__ == 5 && __OS_MAJOR_VERSION__ < 6
	#define NEED_BSD_SIGNAL
    #endif

#else

    #error "Unknown OSTYPE"

#endif


#ifdef Solaris
    #define Signal (bsd_signal)
#else
    #define Signal (signal)
#endif

#ifdef NEED_STRLCPY
    size_t strlcpy(char *dst, const char *src, size_t size);
#endif
#ifdef NEED_STRLCAT
    size_t strlcat(char *dst, const char *src, size_t size);
#endif


#endif
