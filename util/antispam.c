/* $Id: antispam.c,v 1.2 2003/07/20 00:55:34 in2 Exp $ */
/* 抓廣告信的程式 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "config.h"

#define WINDOW 100		/* 一次window多少個server */
#define LEVEL  21		/* 若幾次重復就算廣告信 */

#define mailog BBSHOME "/etc/mailog"
#define spamlog BBSHOME "/etc/spam"

typedef struct sendinfo
{
    char time[18];
    char from[50];
    char userid[20];
    int count;
}
sendinfo;

int
main(int argc, char **argv)
{
    char buf[200], *from, *userid;
    int num = -1, numb = -1, n, nb;
    FILE *fp = fopen(mailog, "r"), *fo;
    sendinfo data[WINDOW];
    sendinfo bad[WINDOW];

    unlink(spamlog);
    fo = fopen(spamlog, "a");
    memset(data, 0, sizeof(data));
    memset(bad, 0, sizeof(bad));

    if (!fp || !fo)
	return 0;

    while (fgets(buf, 200, fp))
    {
	strtok(buf, "\r\n");
	from = strchr(buf, '>') + 2;
	userid = strstr(buf, " =>");

	if (!from || !userid)
	    continue;

	*userid = 0;
	userid += 4;

	if (strstr(from, "MAILER-DAEMON")
	    || strstr(from, userid))
	    continue;		/* 退信通知不管 */
    /* 是否已是badhost */

	for (nb = 0; nb < WINDOW && bad[nb].from[0]; nb++)
	    if (!strcmp(bad[nb].from, from))
		break;

	if (nb < WINDOW && bad[nb].from[0])
	{
	    bad[nb].count++;
	    continue;
	}

    /* 簡查過去記錄 */

	for (n = 0; n < WINDOW && data[n].from[0]; n++)
	    if (!strcmp(data[n].from, from))
		break;

	if (n < WINDOW && data[n].from[0])
	{
	    if (!strncmp(data[n].userid, userid, 20))
		continue;
	/* 轉給同一個人就不管 */
	    strncpy(data[n].userid, userid, 20);
	    if (++data[n].count >= LEVEL)
	    {
	    /* 變成bad 移data到bad 空缺由後一筆資料補上 */
		if (nb >= WINDOW)
		{
		    numb = (numb + 1) % WINDOW;
		    nb = numb;
		    fprintf(fo, "%s %s 重覆寄 %d 次\n",
			    bad[nb].time, bad[nb].from, bad[nb].count);
/*                                 printf(" %s send %d times\n",
   bad[nb].from, bad[nb].count); */
		}
		memcpy(&bad[nb], &data[n], sizeof(sendinfo));
		memcpy(&data[n], &data[n + 1], sizeof(sendinfo) * (WINDOW - n - 1));
		if (num > n)
		    num--;
	    }
	}
	else
	{
	    if (n >= WINDOW)
	    {
		num = (num + 1) % WINDOW;
		n = num;
	    }
/*                printf("[%s] to [%s]\n", from, userid); */
	    buf[17] = 0;
	    strncpy(data[n].time, buf, 17);
	    strncpy(data[n].from, from, 50);
	    strncpy(data[n].userid, userid, 20);
	}
    }

    for (nb = 0; nb < WINDOW && bad[nb].from[0]; nb++)
    {
	fprintf(fo, "%s %s 重覆寄 %d 次\n", bad[nb].time,
		bad[nb].from, bad[nb].count);
/*         printf(" %s send %d times\n", bad[nb].from, bad[nb].count); */
    }
    fclose(fp);
    fclose(fo);
    return 0;
}
