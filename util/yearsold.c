/* $Id$ */
/* ¯¸¤W¦~ÄÖ²Î­p */
#define _UTIL_C_
#include "bbs.h"

#define YEARSOLD_MAX_LINE        16

struct userec_t user;

void
fouts(fp, buf, mode)
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

int main(int argc, char **argv)
{
    int i, j, k;
    char buf[256];
    FILE *fp;
    int year, max, item, maxyear, totalyear;
    int act[25];
    time4_t now;
    struct tm *ptime;

    now = time(NULL);
    ptime = localtime4(&now);

    attach_SHM();
    if(passwd_init())
	exit(1);
    
    memset(act, 0, sizeof(act));
    for(k = 1; k <= MAX_USERS; k++) {
	passwd_query(k, &user);
	if (((ptime->tm_year - user.year) < 10) || ((ptime->tm_year - user.year) >
						     33))
	    continue;

	act[ptime->tm_year - user.year - 10]++;
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

    item = max / YEARSOLD_MAX_LINE + 1;

    if ((fp = fopen(BBSHOME"/etc/yearsold", "w")) == NULL)
    {
	printf("cann't open etc/yearsold\n");
	return 1;
    }

    fprintf(fp, "\t\t\t [1;33;45m " BBSNAME
	    " ¦~ÄÖ²Î­p [%02d/%02d/%02d] [40m\n\n",
	    ptime->tm_year % 100, ptime->tm_mon + 1, ptime->tm_mday);
    for (i = YEARSOLD_MAX_LINE + 1; i > 0; i--)
    {
	strcpy(buf, "   ");
	for (j = 0; j < 24; j++)
	{
	    max = item * i;
	    year = act[j];
	    if (year && (max > year) && (max - item <= year))
	    {
		fouts(fp, buf, '7');
		fprintf(fp, "%-3d", year);
	    }
	    else if (max <= year)
	    {
		fouts(fp, buf, '4');
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
