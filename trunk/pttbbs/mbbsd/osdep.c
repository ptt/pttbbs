/* $Id: osdep.c,v 1.4 2002/07/05 17:10:27 in2 Exp $ */
#include "bbs.h"

#if __FreeBSD__

#include <kvm.h>

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

double 
swapused(long *total, long *used)
{
    double          percent = -1;
    kvm_t          *kd;
    struct kvm_swap swapinfo;
    int             pagesize;

    kd = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL);
    if (kd) {
	if (kvm_getswapinfo(kd, &swapinfo, 1, 0) == 0) {
	    pagesize = getpagesize();
	    *total = swapinfo.ksw_total * pagesize;
	    *used = swapinfo.ksw_used * pagesize;
	    if (*total != 0)
		percent = (double)*used / (double)*total;
	}
	kvm_close(kd);
    }
    return percent;
}

#else
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

double 
swapused(long *total, long *used)
{
    double          percent = -1;
    char            buf[101];
    FILE           *fp;

    if ((fp = fopen("/proc/meminfo", "r"))) {
	while (fgets(buf, 100, fp) && buf[0] != 'S');
	if (sscanf(buf + 6, "%ld %ld", total, used) == 2)
	    if (*total != 0)
		percent = (double)*used / (double)*total;
	fclose(fp);
    }
    return percent;
}

#endif
