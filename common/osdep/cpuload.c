/* $Id$ */
#include <stdio.h>
#include <string.h>
#include "osdep.h"

#if defined( __FreeBSD__) || defined(_BSD_SOURCE)
#include <stdlib.h>
int
cpuload(char *str)
{
    double          l[3] = {-1, -1, -1};
    if (getloadavg(l, 3) != 3)
	l[0] = -1;

    if (str) {
	if (l[0] != -1)
	    sprintf(str, " %.2f %.2f %.2f", l[0], l[1], l[2]);
	else
	    strcpy(str, " (unknown) ");
    }
    return (int)l[0];
}
#elif defined(__linux__)
int
cpuload(char *str)
{
    double          l[3] = {-1, -1, -1};
    FILE           *fp;

    if ((fp = fopen("/proc/loadavg", "r"))) {
	if (fscanf(fp, "%lf %lf %lf", &l[0], &l[1], &l[2]) != 3)
	    l[0] = -1;
	fclose(fp);
    }
    if (str) {
	if (l[0] != -1)
	    sprintf(str, " %.2f %.2f %.2f", l[0], l[1], l[2]);
	else
	    strcpy(str, " (unknown) ");
    }
    return (int)l[0];
}
#else
int
cpuload(char *str)
{
    strcpy(str, " (unknown) ");
    return -1;
}
#endif
