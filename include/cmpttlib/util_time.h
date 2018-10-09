/* $Id$ */
#ifndef UTIL_TIME_H
#define UTIL_TIME_H

#include "pttconst.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

// XXX hack for time64_t
typedef long time64_t;

#define TZ_TAIPEI 28800 // 8 * 3600

Err GetMilliTimestamp(time64_t *milli_timestamp);

Err MilliTimestampToYear(const time64_t milli_timestamp, int *year);

Err MilliTimestampToTimestamp(const time64_t milli_timestamp, time64_t *timestamp);

time64_t MilliTimestampToTimestamp_ne(const time64_t milli_timestamp);

/**
 * @brief [brief description]
 * @details [long description]
 *
 * @param year [description]
 * @param mm [description]
 * @param dd [description]
 * @param HH [description]
 * @param MM [description]
 * @param SS [description]
 * @param tz timezone (in sec, CST as 28800, EST as -18000)
 * @param timestamp [description]
 * @return [description]
 */
Err DatetimeToTimestamp(const int year, const int mm, const int dd, const int HH, const int MM, const int SS, const int tz, time64_t *timestamp);

const char* MilliTimestampToCdate_ne(const time64_t milli_timestamp);

const char* MilliTimestampToCdateLite_ne(const time64_t milli_timestamp);

const char* MilliTimestampToCdateDate_ne(const time64_t milli_timestamp);

const char* MilliTimestampToCdateMd_ne(const time64_t milli_timestamp);

const char* MilliTimestampToCdateMdHM_ne(const time64_t milli_timestamp);

const char* MilliTimestampToCdateMdHMS_ne(const time64_t milli_timestamp);

Err AddTimespecWithNanosecs(struct timespec *a, int nanosecs);


#ifdef __cplusplus
}
#endif

#endif /* UTIL_TIME_H */
