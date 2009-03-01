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
 * 5+1 bytes, "12/31\0"
 */
const char*
Cdate_md(const time4_t * clock)
{
    time_t          temp = (time_t)*clock;
    struct tm       mytm;

    localtime_r(&temp, &mytm);
    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d", &mytm);
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

char *
ctime4_r(const time4_t *t, char *buf)
{
    time_t temp = (time_t)*t;

    return ctime_r(&temp, buf);
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

struct tm*
localtime4_r(const time4_t *t, struct tm *pt)
{
    if (t)
    {
	time_t temp  = (time_t)*t;
	localtime_r(&temp, pt);
    }
    else
	localtime_r(NULL, pt);
    return pt;
}


time4_t time4(time4_t *ptr)
{
    if( ptr == NULL )
	return time(NULL);
    else
	return *ptr = (time4_t)time(NULL);
}
#endif
