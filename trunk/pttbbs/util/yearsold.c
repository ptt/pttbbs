/* $Id: yearsold.c,v 1.1 2002/03/07 15:13:46 in2 Exp $ */
/* ¯¸¤W¦~ÄÖ²Î­p */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "util.h"

#define MAX_LINE        16

struct userec_t cuser;

void
 outs(fp, buf, mode)
FILE *fp;
char buf[], mode;
{
    static char state = '0';

    if (state != mode)
	fprintf(fp, "[3%cm", state = mode);
    if (buf[0])
    {
	fprintf(fp, buf);
	buf[0] = 0;
    }
}

int main()
{
    int i, j, k;
    char buf[256];
    FILE *fp;
    int year, max, item, maxyear;
    long totalyear;
    int act[25];
    time_t now;
    struct tm *ptime;

    now = time(NULL);
    ptime = localtime(&now);

    if(passwd_mmap())
	exit(1);
    
    memset(act, 0, sizeof(act));
    for(k = 1; k <= MAX_USERS; k++) {
	passwd_query(k, &cuser);
	if (((ptime->tm_year - cuser.year) < 10) || ((ptime->tm_year - cuser.year) >
						     33))
	    continue;

	act[ptime->tm_year - cuser.year - 10]++;
	act[24]++;
    }
    
    for (i = max = totalyear = maxyear = 0; i < 24; i++)
    {
	totalyear += act[i] * (i + 10);
	if (act[i] > max)
	{
	    max = act[i];
	    maxyear = i;
	}
    }

    item = max / MAX_LINE + 1;

    if ((fp = fopen(BBSHOME"/etc/yearsold", "w")) == NULL)
    {
	printf("cann't open etc/yearsold\n");
	return 1;
    }

    fprintf(fp, "\t\t\t [1;33;45m " BBSNAME
	    " ¦~ÄÖ²Î­p [%02d/%02d/%02d] [40m\n\n",
	    ptime->tm_year % 100, ptime->tm_mon, ptime->tm_mday);
    for (i = MAX_LINE + 1; i > 0; i--)
    {
	strcpy(buf, "   ");
	for (j = 0; j < 24; j++)
	{
	    max = item * i;
	    year = act[j];
	    if (year && (max > year) && (max - item <= year))
	    {
		outs(fp, buf, '7');
		fprintf(fp, "%-3d", year);
	    }
	    else if (max <= year)
	    {
		outs(fp, buf, '4');
		fprintf(fp, "¢i ");
	    }
	    else
		strcat(buf, "   ");
	}
	fprintf(fp, "\n");
    }


    fprintf(fp, "   [32m"
	    "10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33\n\n"
	    "\t\t      [36m¦³®Ä²Î­p¤H¦¸¡G[37m%-9d[36m¥­§¡¦~ÄÖ¡G[37m%d[40;0m\n"
	    ,act[24], (int)totalyear / act[24]);
    fclose(fp);
    return 0;
}
