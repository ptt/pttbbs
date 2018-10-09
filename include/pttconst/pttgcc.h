/* $Id$ */
#ifndef PTTGCC_H
#define PTTGCC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
#define GCC_CHECK_FORMAT(a,b) __attribute__ ((format (printf, a, b)))
#define GCC_NORETURN          __attribute__ ((__noreturn__))
#define GCC_UNUSED            __attribute__ ((__unused__))
#else
#define GCC_CHECK_FORMAT(a,b)
#define GCC_NORETURN
#define GCC_UNUSED
#endif

#ifdef __cplusplus
}
#endif

#endif /* PTTGCC_H */

