#include <time.h>
#include <stdio.h>
#include "cmsys.h"

static char cdate_buffer[32];

/**
 * 閏年
 */
int is_leap_year(int year)
{
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

/**
 * 給日期求星座
 *
 * @return	1..12
 */
int getHoroscope(int m, int d)
{
    if (m > 12 || m < 1)
	return 1;

    // 摩羯 水瓶 雙魚 牡羊 金牛 雙子 巨蟹 獅子 處女 天秤 天蠍 射手
    const int firstday[12] = {
	/* Jan. */ 20, 19, 21, 20, 21, 21, 23, 23, 23, 23, 22, 22
    };
    if (d >= firstday[m - 1]) {
	if (m == 12)
	    return 1;
	else
	    return m + 1;
    }
    else
	return m;
}

/**
 * 23+1 bytes, "12/31/2007 00:00:00 Mon\0"
 */
const char*
Cdate(const time4_t *clock)
{
    time_t          temp = (time_t)*clock;
    struct tm       mytm;

    localtime_r(&temp, &mytm);
    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d/%Y %T %a", &mytm);
    return cdate_buffer;
}

/**
 * 19+1 bytes, "12/31/2007 00:00:00\0"
 */
const char*
Cdatelite(const time4_t *clock)
{
    time_t          temp = (time_t)*clock;
    struct tm       mytm;

    localtime_r(&temp, &mytm);
    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d/%Y %T", &mytm);
    return cdate_buffer;
}

/**
 * 10+1 bytes, "12/31/2007\0"
 */
const char*
Cdatedate(const time4_t * clock)
{
    time_t          temp = (time_t)*clock;
    struct tm       mytm;

    localtime_r(&temp, &mytm);
    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d/%Y", &mytm);
    return cdate_buffer;
}

/**
 * 11+1 bytes, "12/31 10:01\0"
 */
const char*
Cdate_mdHM(const time4_t * clock)
{
    time_t          temp = (time_t)*clock;
    struct tm       mytm;

    localtime_r(&temp, &mytm);
    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d %H:%M", &mytm);
    return cdate_buffer;
}

#ifdef TIMET64
char           *
ctime4(const time4_t *clock)
{
    time_t temp = (time_t)*clock;
    
    return ctime(&temp);
}

// XXX TODO change this to localtime_r style someday.
struct tm *localtime4(const time4_t *t)
{
    if( t == NULL )
	return localtime(NULL);
    else {
	time_t  temp = (time_t)*t;
	return localtime(&temp);
    }
}

time4_t time4(time4_t *ptr)
{
    if( ptr == NULL )
	return time(NULL);
    else
	return *ptr = (time4_t)time(NULL);
}
#endif

const char*
my_ctime(const time4_t * t, char *ans, int len)
{
    time_t          temp = (time_t)*clock;
    struct tm       tp;

    localtime_r(&temp, &tp);
    snprintf(ans, len,
	     "%02d/%02d/%02d %02d:%02d:%02d", (tp.tm_year % 100),
	     tp.tm_mon + 1, tp.tm_mday, tp.tm_hour, tp.tm_min, tp.tm_sec);
    return ans;
}
