
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

    #define _XOPEN_SOURCE
    #define _ISOC99_SOURCE
    
    #if BYTE_ORDER == BIG_ENDIAN
	#define _BIG_ENDIAN
    #else
	#define _LITTLE_ENDIAN
    #endif

#elif defined(__linux__)

    #define _GNU_SOURCE

    #include <sys/ioctl.h>
    #include <sys/file.h>      /* for flock() */
    #include <strings.h>       /* for strcasecmp() */
    #include <endian.h>        /* for __BYTE_ORDER */

    #if __BYTE_ORDER == __BIG_ENDIAN
	#define _BIG_ENDIAN
    #else
	#define _LITTLE_ENDIAN
    #endif

    #define NEED_STRCASESTR
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

    #define NEED_FLOCK
    #define NEED_UNSETENV
    #define NEED_SCANDIR
    #define NEED_STRCASESTR
    #define NEED_TIMEGM

    #if __OS_MAJOR_VERSION__ == 5 && __OS_MINOR_VERSION__ < 8
	#define NEED_STRLCPY
	#define NEED_STRLCAT
	#define NEED_INET_PTON
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


#endif
