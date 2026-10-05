#ifndef STATISTIC_H_
#define STATISTIC_H_
#define STAT(X, OP) do { \
    if(SHM && SHM->version==SHM_VERSION && 0<=(X) && (X)<STAT_MAX) \
      SHM->statistic[X] OP; \
} while(0)
#define STATADD(X, val) do { \
    if(SHM && SHM->version==SHM_VERSION && 0<=(X) && (X)<STAT_MAX) \
      __sync_fetch_and_add(&SHM->statistic[X], (val)); \
} while(0)
#define STATINC(X) STATADD(X, 1)

#ifdef CPU_STATS

#include <sys/time.h>
#include <sys/resource.h>

#define BEGINSTAT(name) struct rusage name ## _start; getrusage(RUSAGE_SELF, &(name ## _start));

#define TVALDIFF_TO_MS(start, end) ((end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)/1000)

#define ENDSTAT(name) do { \
    struct rusage *_start = &( name ## _start), _end; \
    getrusage(RUSAGE_SELF, &_end); \
    STATADD(name ## _SCPU, TVALDIFF_TO_MS(_start->ru_stime, _end.ru_stime));          \
    STATADD(name ## _UCPU, TVALDIFF_TO_MS(_start->ru_utime, _end.ru_utime));          \
    STATINC(name); \
} while(0);

typedef struct {
    int id;
    int scpu;
    int ucpu;
    struct rusage start;
} stat_scope_t;

void cleanup_stat(stat_scope_t *ss);

#define SCOPED_STAT(name) \
    stat_scope_t _stat_##name __attribute__((cleanup(cleanup_stat))) = { \
        .id = name, \
        .scpu = name##_SCPU, \
        .ucpu = name##_UCPU, \
    }; \
    getrusage(RUSAGE_SELF, &_stat_##name.start)

#else
#define BEGINSTAT(name) STATINC(name)
#define ENDSTAT(name)

typedef int stat_scope_t;
static inline void cleanup_stat(stat_scope_t *ss) { (void)ss; }
#define SCOPED_STAT(name) \
    STATINC(name); \
    stat_scope_t _stat_##name __attribute__((cleanup(cleanup_stat), unused)) = 0

#endif


enum { // XXX description in shmctl.c
    STAT_LOGIN,
    STAT_SHELLLOGIN,
    STAT_VEDIT,
    STAT_TALKREQUEST,
    STAT_WRITEREQUEST,
    STAT_MORE,
    STAT_SYSWRITESOCKET,
    STAT_SYSSELECT,
    STAT_SYSREADSOCKET,
    STAT_DOSEND,
    STAT_SEARCHUSER,
    STAT_THREAD,
    STAT_SELECTREAD,
    STAT_QUERY,
    STAT_DOTALK,
    STAT_FRIENDDESC,
    STAT_FRIENDDESC_FILE,
    STAT_PICKMYFRIEND,
    STAT_PICKBFRIEND,
    STAT_GAMBLE,
    STAT_DOPOST,
    STAT_READPOST,
    STAT_COMMENT,
    STAT_TODAYLOGIN_MIN,
    STAT_TODAYLOGIN_MAX,
    STAT_SIGINT,
    STAT_SIGQUIT,
    STAT_SIGILL,
    STAT_SIGABRT,
    STAT_SIGFPE,
    STAT_SIGBUS,
    STAT_SIGSEGV,
    STAT_READPOST_12HR,
    STAT_READPOST_1DAY,
    STAT_READPOST_3DAY,
    STAT_READPOST_7DAY,
    STAT_READPOST_OLD,
    STAT_SIGXCPU,
    STAT_BOARDREC,
    STAT_BOARDREC_SCPU,
    STAT_BOARDREC_UCPU,
    STAT_DOCOMMENT,
    STAT_DOCOMMENT_SCPU,
    STAT_DOCOMMENT_UCPU,
    STAT_QUERY_SCPU,
    STAT_QUERY_UCPU,
    STAT_LOGIND_NEWCONN,
    STAT_LOGIND_OVERLOAD,
    STAT_LOGIND_BANNED,
    STAT_LOGIND_AUTHFAIL,
    STAT_LOGIND_SERVSTART,
    STAT_LOGIND_SERVFAIL,
    STAT_LOGIND_PASSWDPROMPT,
    STAT_MBBSD_ENTER,
    STAT_MBBSD_EXIT,
    STAT_MBBSD_ABORTED,
    /* insert here. don't forget update shmctl.c */
    STAT_NUM,
    STAT_MAX=512
};
#endif
