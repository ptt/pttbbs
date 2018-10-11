#define _XOPEN_SOURCE
#include "cmutil_time/util_timestamp.h"

/**
 * @brief Get current time in milli-timestamp
 * @details Get current time in milli-timestamp
 *
 * @param milli_timestamp milli-timestamp (to-compute)
 * @return Err
 */
Err
get_milli_timestamp(time64_t *milli_timestamp)
{
    struct timeval tv;

    int ret_code = gettimeofday(&tv, NULL);
    if (ret_code) return S_ERR;

    *milli_timestamp = ((time64_t)tv.tv_sec) * 1000L + ((time64_t)tv.tv_usec) / 1000L;

    return S_OK;
}

Err
milli_timestamp_to_year(time64_t milli_timestamp, int *year)
{
    struct tm tmp_tm = {};
    time64_t timestamp = 0;

    Err error_code = milli_timestamp_to_timestamp(milli_timestamp, &timestamp);
    if(error_code) return error_code;

    localtime_r(&timestamp, &tmp_tm);
    *year = tmp_tm.tm_year + 1900;
    return S_OK;
}

Err
milli_timestamp_to_timestamp(time64_t milli_timestamp, time64_t *timestamp)
{
    *timestamp = milli_timestamp / 1000;

    return S_OK;
}

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
 * @param tz time-zone (Asia/Taipei: 8)
 * @param timestamp [description]
 */
Err
datetime_to_timestamp(int year, int mm, int dd, int HH, int MM, int SS, int tz, time64_t *timestamp)
{
    char buf[MAX_BUF_TIMESTAMP_SIZE] = {};
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", year, mm, dd, HH, MM, SS);
    struct tm datetime = {};

    char *ret = strptime(buf, "%Y-%m-%d %H:%M:%S", &datetime);
    if(!ret) return S_ERR;

    time_t tmp_timestamp = mktime(&datetime) + MY_TZ * 3600 - tz * 3600;
    *timestamp = (time64_t) tmp_timestamp;

    return S_OK;
}

Err
add_timespec_with_nanosecs(struct timespec *a, int nanosecs)
{
    a->tv_nsec += nanosecs;
    if(a->tv_nsec < BILLION) return S_OK;

    a->tv_nsec -= BILLION;
    a->tv_sec++;

    return S_OK;
}
