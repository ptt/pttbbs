/* $Id$ */

/**********************************************/
/*這個程式是用來計算賭骰子賺得錢跟賠的錢的程式 */
/*用法就是直接打 countalldice 就可以針對所有人 */
/*來計算他總共賺了多少 賠了多少............... */
/*作者:Heat 於1997/10/2                       */
/**********************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include "config.h"

#define DICE_WIN  BBSHOME "/etc/windice.log"
#define DICE_LOST BBSHOME "/etc/lostdice.log"

int total = 0;

typedef struct dice
{
    char id[14];
    int win;
    int lost;
}
dice;

dice table[1024];

int find(char *name)
{
    int i = 0;
    if (total == 0)
    {
	total++;
	return 0;
    }
    for (i = 0; i < total; i++)
	if (!strcmp(name, table[i].id))
	    return i;
    memset(&table[total++], 0, sizeof(dice));
    return total - 1;
}

int main() {
    int index, win = 0, lost = 0;
    FILE *fpwin, *fplost;
    char buf[256], *ptr, buf0[256], *name = (char *) malloc(15), *mon = (char *) malloc(5);

    fpwin = fopen(DICE_WIN, "r");
    fplost = fopen(DICE_LOST, "r");

    if (!fpwin || !fplost)
	perror("error open file");

    while (fgets(buf, 255, fpwin))
    {
	strcpy(buf0, buf);
	name = strtok(buf, " ");
	mon = strstr(buf0, "淨賺:");
	if ((ptr = strchr(mon, '\n')))
	    *ptr = 0;
	index = find(name);
	strcpy(table[index].id, name);
	table[index].win += atoi(mon + 5);
    }
    fclose(fpwin);

    while (fgets(buf, 255, fplost))
    {
	strcpy(buf0, buf);
	name = strtok(buf, " ");
	mon = strstr(buf0, "輸了 ");
	if ((ptr = strchr(mon, '\n')))
	    *ptr = 0;
	if ((index = find(name)) == total - 1)
	    strcpy(table[index].id, name);
	table[index].lost += atoi(mon + 5);
    }

    for (index = 0; index < total; index++)
    {
	printf("%-15s 贏了 %-8d 塊錢， 輸掉 %-8d 塊錢\n", table[index].id
	       ,table[index].win, table[index].lost);
	win += table[index].win;
	lost += table[index].lost;
    }
    index = win + lost;
    printf("\n人數: %d\n總贏錢=%d 總輸錢=%d 總金額:%d\n", total, win, lost, index);
    printf("贏的比例:%f 輸的比例:%f\n", (float) win / index, (float) lost / index);
    printf("\n備註：輸贏是以使用者的觀點來看\n");
    fclose(fplost);
    return 0;
}
