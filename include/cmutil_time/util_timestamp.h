/* $Id$ */
#ifndef UTIL_TIMESTAMP_H
#define UTIL_TIMESTAMP_H

#include "ptterr.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include "config.h"    

// XXX hack for time64_t
typedef long time64_t;

#define BILLION 1000000000

#define TIMEZONE_TAIPEI 8
    
#define MAX_BUF_TIMESTAMP_SIZE 20

Err get_milli_timestamp(time64_t *milli_timestamp);

Err milli_timestamp_to_year(time64_t milli_timestamp, int *year);

Err milli_timestamp_to_timestamp(time64_t milli_timestamp, time64_t *timestamp);

Err datetime_to_timestamp(int year, int mm, int dd, int HH, int MM, int SS, int tz, time64_t *timestamp);

Err add_timespec_with_nanosecs(struct timespec *a, int nanosecs);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_TIMESTAMP_H */
