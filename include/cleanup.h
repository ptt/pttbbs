#ifndef PTTBBS_CLEANUP_H_
#define PTTBBS_CLEANUP_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/file.h>

#if defined(__GNUC__) || defined(__clang__)

/*
 * File descriptor cleanup: automatically close(fd) when exiting scope.
 * If fd < 0, does nothing. Sets *pfd = -1 upon close.
 */
static inline void cleanup_close_fd(int *pfd) {
    if (pfd && *pfd >= 0) {
        close(*pfd);
        *pfd = -1;
    }
}
#define autoclose __attribute__((cleanup(cleanup_close_fd), unused))

/*
 * Standard I/O FILE cleanup: automatically fclose(fp) when exiting scope.
 * If fp == NULL, does nothing. Sets *pfp = NULL upon close.
 */
static inline void cleanup_fclose(FILE **pfp) {
    if (pfp && *pfp) {
        fclose(*pfp);
        *pfp = NULL;
    }
}
#define autofclose __attribute__((cleanup(cleanup_fclose), unused))

/*
 * Dynamic memory cleanup: automatically free(*pptr) when exiting scope.
 * If *pptr == NULL, does nothing. Sets *pptr = NULL upon free.
 */
static inline void cleanup_free(void *ptr) {
    void **pptr = (void **)ptr;
    if (pptr && *pptr) {
        free(*pptr);
        *pptr = NULL;
    }
}
#define autofree __attribute__((cleanup(cleanup_free), unused))

/*
 * flock unlock on scope exit for an active file descriptor.
 */
static inline void cleanup_flock(int *pfd) {
    if (pfd && *pfd >= 0) {
        flock(*pfd, LOCK_UN);
    }
}
#define autounlock_flock __attribute__((cleanup(cleanup_flock), unused))

/*
 * flock guard: tracks locked state and automatically unlocks on scope exit.
 */
typedef struct {
    int fd;
    int locked;
} flock_guard_t;

static inline void cleanup_flock_guard(flock_guard_t *g) {
    if (g && g->locked && g->fd >= 0) {
        flock(g->fd, LOCK_UN);
        g->locked = 0;
    }
}
#define autoflock_guard __attribute__((cleanup(cleanup_flock_guard), unused)) flock_guard_t

/*
 * Unlink rollback guard: automatically unlinks path if active == 1 on scope exit.
 * Set active = 0 once the operation succeeds to disarm the rollback.
 */
typedef struct {
    const char *path;
    int active;
} unlink_guard_t;

static inline void cleanup_unlink_guard(unlink_guard_t *g) {
    if (g && g->active && g->path) {
        unlink(g->path);
        g->active = 0;
    }
}
#define autounlink_guard __attribute__((cleanup(cleanup_unlink_guard), unused)) unlink_guard_t

/*
 * shm_unlink rollback guard: automatically shm_unlink(name) if active == 1 on scope exit.
 * Set active = 0 once the operation succeeds to disarm the rollback.
 */
extern int shm_unlink(const char *name);
typedef struct {
    const char *name;
    int active;
} shm_unlink_guard_t;

static inline void cleanup_shm_unlink_guard(shm_unlink_guard_t *g) {
    if (g && g->active && g->name) {
        shm_unlink(g->name);
        g->active = 0;
    }
}
#define autoshm_unlink_guard __attribute__((cleanup(cleanup_shm_unlink_guard), unused)) shm_unlink_guard_t

/*
 * SQLite statement cleanup: automatically sqlite3_finalize(stmt) on scope exit.
 */
struct sqlite3_stmt;
extern int sqlite3_finalize(struct sqlite3_stmt *pStmt);
static inline void cleanup_sqlite3_stmt(struct sqlite3_stmt **pstmt) {
    if (pstmt && *pstmt) {
        sqlite3_finalize(*pstmt);
        *pstmt = NULL;
    }
}
#define auto_sqlite3_finalize __attribute__((cleanup(cleanup_sqlite3_stmt), unused))

/*
 * libevent evbuffer cleanup: automatically evbuffer_free(evb) on scope exit.
 */
struct evbuffer;
extern void evbuffer_free(struct evbuffer *buf);
static inline void _cleanup_evbuffer(struct evbuffer **pevb) {
    if (pevb && *pevb) {
        evbuffer_free(*pevb);
        *pevb = NULL;
    }
}
#define auto_evbuffer_free __attribute__((cleanup(_cleanup_evbuffer), unused))

/*
 * Automatic string buffer with Stack SSO (Small String Optimization):
 * Formats into a 512-byte stack buffer for the common case, falling back to
 * heap allocation only for oversized strings. Heap memory is automatically
 * freed on scope exit. The variable 's' is directly a (char *).
 */
typedef struct {
    char *heap;
    int len;
    char buf[512];
} autostring_t;

static inline void autostring_cleanup(autostring_t *as) {
    if (as && as->heap) {
        free(as->heap);
        as->heap = NULL;
    }
}

#if defined(GCC_CHECK_FORMAT)
static inline char *autostring_init_vformat(autostring_t *as, const char *fmt, va_list ap)
    GCC_CHECK_FORMAT(2, 0);
#else
static inline char *autostring_init_vformat(autostring_t *as, const char *fmt, va_list ap)
    __attribute__((format(printf, 2, 0)));
#endif

static inline char *autostring_init_vformat(autostring_t *as, const char *fmt, va_list ap) {
    as->heap = NULL;
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(as->buf, sizeof(as->buf), fmt, ap);

    if (len < 0) {
        va_end(ap_copy);
        as->buf[0] = '\0';
        as->len = 0;
        return as->buf;
    }
    as->len = len;
    if ((size_t)len < sizeof(as->buf)) {
        va_end(ap_copy);
        return as->buf;
    }

    as->heap = (char *)malloc((size_t)len + 1);
    if (!as->heap) {
        va_end(ap_copy);
        as->len = sizeof(as->buf) - 1;
        return as->buf;
    }
    vsnprintf(as->heap, (size_t)len + 1, fmt, ap_copy);
    va_end(ap_copy);
    return as->heap;
}

#if defined(GCC_CHECK_FORMAT)
static inline char *autostring_init_format(autostring_t *as, const char *fmt, ...)
    GCC_CHECK_FORMAT(2, 3);
#else
static inline char *autostring_init_format(autostring_t *as, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#endif

static inline char *autostring_init_format(autostring_t *as, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *ret = autostring_init_vformat(as, fmt, ap);
    va_end(ap);
    return ret;
}

#define AUTO_STR(s, fmt, ...) \
    autostring_t _as_##s __attribute__((cleanup(autostring_cleanup))); \
    __attribute__((unused)) char *s = autostring_init_format(&_as_##s, fmt, ##__VA_ARGS__); \
    __attribute__((unused)) int s##_len = _as_##s.len

#define AUTO_VSTR(s, fmt, ap) \
    autostring_t _as_##s __attribute__((cleanup(autostring_cleanup))); \
    __attribute__((unused)) char *s = autostring_init_vformat(&_as_##s, fmt, ap); \
    __attribute__((unused)) int s##_len = _as_##s.len

#else /* !__GNUC__ && !__clang__ */

#define autoclose
#define autofclose
#define autofree
#define autounlock_flock
#define autoflock_guard flock_guard_t
#define autounlink_guard unlink_guard_t
#define auto_sqlite3_finalize
#define auto_evbuffer_free
#define AUTO_STR(s, fmt, ...) char *s = NULL; int s##_len = 0
#define AUTO_VSTR(s, fmt, ap) char *s = NULL; int s##_len = 0

#endif

#endif /* PTTBBS_CLEANUP_H_ */
