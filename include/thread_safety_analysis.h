#ifndef THREAD_SAFETY_ANALYSIS_H__
#define THREAD_SAFETY_ANALYSIS_H__

/* Clang's thread-safety analysis attributes, checked with -Wthread-safety
 * (on for clang builds, see pttbbs.mk). A capability models a lock, or
 * anything else that is taken and given back; each declaration using these
 * says what it models. Compilers without them get nothing: gcc, and a
 * clang before 3.5, which knows only the older names (lockable and the
 * like, as FreeBSD's sys/cdefs.h spells them). */
#if defined(__has_extension) && defined(__has_attribute)
#if __has_extension(c_thread_safety_attributes) && \
    __has_attribute(acquire_capability)
#define TSA_SUPPORTED 1
#endif
#endif

#ifdef TSA_SUPPORTED
#define TSA_CAPABILITY(x)	__attribute__ ((capability(x)))
#define TSA_ACQUIRE(...)	__attribute__ ((acquire_capability(__VA_ARGS__)))
#define TSA_RELEASE(...)	__attribute__ ((release_capability(__VA_ARGS__)))
#define TSA_EXCLUDES(...)	__attribute__ ((locks_excluded(__VA_ARGS__)))
#define TSA_NO_ANALYSIS		__attribute__ ((no_thread_safety_analysis))
#else
#define TSA_CAPABILITY(x)
#define TSA_ACQUIRE(...)
#define TSA_RELEASE(...)
#define TSA_EXCLUDES(...)
#define TSA_NO_ANALYSIS
#endif

#endif
