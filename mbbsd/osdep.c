/* $Id$ */
#include "bbs.h"

#ifdef NEED_STRLCAT

#include <sys/types.h>
#include <string.h>

/*   size_t
 *   strlcat(char *dst, const char *src, size_t size);
 */
/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat(dst, src, siz)
    char *dst;
    const char *src;
    size_t siz;
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end */
    while (n-- != 0 && *d != '\0')
	d++;
    dlen = d - dst;
    n = siz - dlen;

    if (n == 0)
	return(dlen + strlen(s));
    while (*s != '\0') {
	if (n != 1) {
	    *d++ = *s;
	    n--;
	}
	s++;
    }
    *d = '\0';

    return(dlen + (s - src));	/* count does not include NUL */
}

#endif

#ifdef NEED_TIMEGM

#include <time.h>
#include <stdlib.h>

time_t timegm (struct tm *tm)
{
    time_t ret;
    char *tz;

    tz = getenv("TZ");
    putenv("TZ=");
    tzset();
    ret = mktime(tm);

    if (tz){
	char *buff = malloc( strlen(tz) + 10);
	sprintf( buff, "TZ=%s", tz);
	putenv(buff);
	free(buff);
    }
    else
	unsetenv("TZ");
    tzset();

    return ret;
}

#endif

#ifdef NEED_STRLCPY

/* ------------------------------------------------------------------------ */

/*   size_t
 *   strlcpy(char *dst, const char *src, size_t size);
 */

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t strlcpy(dst, src, siz)
    char *dst;
    const char *src;
    size_t siz;
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
	do {
	    if ((*d++ = *s++) == 0)
		break;
	} while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
	if (siz != 0)
	    *d = '\0';		/* NUL-terminate dst */
	while (*s++)
	    ;
    }

    return(s - src - 1);	/* count does not include NUL */
}

#endif

#ifdef NEED_STRCASESTR

char           *
strcasestr(const char *big, const char *little)
{
    char           *ans = (char *)big;
    int             len = strlen(little);
    char           *endptr = (char *)big + strlen(big) - len;

    while (ans <= endptr)
	if (!strncasecmp(ans, little, len))
	    return ans;
	else
	    ans++;
    return 0;
}

#endif

#ifdef NEED_SCANDIR

/*
 * Scan the directory dirname calling select to make a list of selected
 * directory entries then sort using qsort and compare routine dcomp.
 * Returns the number of entries and a pointer to a list of pointers to
 * struct dirent (through namelist). Returns -1 if there were any errors.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

/*
 * The DIRSIZ macro is the minimum record length which will hold the directory
 * entry. This requires the amount of space in struct dirent without the
 * d_name field, plus enough space for the name and a terminating nul byte
 * (dp->d_namlen + 1), rounded up to a 4 byte boundary.
 */
#undef DIRSIZ
#define DIRSIZ(dp) \
    ((sizeof(struct dirent) - sizeof(dp)->d_name) + \
     ((strlen((dp)->d_name) + 1 + 3) &~ 3))
#if 0
((sizeof(struct dirent) - sizeof(dp)->d_name) + \
 (((dp)->d_namlen + 1 + 3) &~ 3))
#endif

int
scandir(dirname, namelist, select, dcomp)
    const char *dirname;
    struct dirent ***namelist;
    int (*select) (struct dirent *);
    int (*dcomp) (const void *, const void *);
{
    register struct dirent *d, *p, **names;
    register size_t nitems;
    struct stat stb;
    long arraysz;
    DIR *dirp;

    if ((dirp = opendir(dirname)) == NULL)
	return(-1);
    if (fstat(dirp->dd_fd, &stb) < 0)
	return(-1);

    /*
     * estimate the array size by taking the size of thedirectory file
     * and dividing it by a multiple of the minimum sizeentry.
     */
    arraysz = (stb.st_size / 24);
    names = (struct dirent **)malloc(arraysz * sizeof(struct dirent *));
    if (names == NULL)
	return(-1);

    nitems = 0;
    while ((d = readdir(dirp)) != NULL) {
	if (select != NULL && !(*select)(d))
	    continue; /* just selected names */
	/*
	 * Make a minimum size copy of the data
	 */
	p = (struct dirent *)malloc(DIRSIZ(d));
	if (p == NULL)
	    return(-1);
	p->d_ino = d->d_ino;
	p->d_off = d->d_off;
	p->d_reclen = d->d_reclen;
	memcpy(p->d_name, d->d_name, strlen(d->d_name) +1);
#if 0
	p->d_fileno = d->d_fileno;
	p->d_type = d->d_type;
	p->d_reclen = d->d_reclen;
	p->d_namlen = d->d_namlen;
	bcopy(d->d_name, p->d_name, p->d_namlen + 1);
#endif
	/*
	 * Check to make sure the array has space left and
	 * realloc the maximum size.
	 */
	if (++nitems >= arraysz) {
	    if (fstat(dirp->dd_fd, &stb) < 0)
		return(-1); /* just might have grown */
	    arraysz = stb.st_size / 12;
	    names = (struct dirent **)realloc((char*)names,
		    arraysz * sizeof(struct dirent*));
	    if (names == NULL)
		return(-1);
	}
	names[nitems-1] = p;
    }
    closedir(dirp);
    if (nitems && dcomp != NULL)
	qsort(names, nitems, sizeof(struct dirent *),dcomp);
    *namelist = names;
    return(nitems);
}

/*
 * Alphabetic order comparison routine for those who want it.
 */
int
alphasort(d1, d2)
    const void *d1;
    const void *d2;
{
    return(strcmp((*(struct dirent **)d1)->d_name,
		(*(struct dirent **)d2)->d_name));
} 

#endif

#ifdef NEED_FLOCK

int
flock (int fd, int f)
{
    if( f == LOCK_EX )
	return lockf(fd, F_LOCK, 0L);

    if( f == LOCK_UN )
	return lockf(fd, F_ULOCK, 0L);

    return -1;
}

#endif

#ifdef NEED_UNSETENV

void
unsetenv(name)
    char *name;
{
    extern char **environ;
    register char **pp;
    int len = strlen(name);

    for (pp = environ; *pp != NULL; pp++)
    {
	if (strncmp(name, *pp, len) == 0 &&
		((*pp)[len] == '=' || (*pp)[len] == '\0'))
	    break;
    }

    for (; *pp != NULL; pp++)
	*pp = pp[1];
}

#endif

#ifdef NEED_INET_PTON

#include <arpa/nameser.h>

/*
 * Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

int
inet_pton(int af, const char *src, void *dst)
{
    static const char digits[] = "0123456789";
    int saw_digit, octets, ch;
    u_char tmp[INADDRSZ], *tp;

    saw_digit = 0;
    octets = 0;
    *(tp = tmp) = 0;
    while ((ch = *src++) != '\0') {
	const char *pch;

	if ((pch = strchr(digits, ch)) != NULL) {
	    u_int new = *tp * 10 + (pch - digits);

	    if (new > 255)
		return (0);
	    *tp = new;
	    if (! saw_digit) {
		if (++octets > 4)
		    return (0);
		saw_digit = 1;
	    }
	} else if (ch == '.' && saw_digit) {
	    if (octets == 4)
		return (0);
	    *++tp = 0;
	    saw_digit = 0;
	} else
	    return (0);
    }
    if (octets < 4)
	return (0);

    memcpy(dst, tmp, INADDRSZ);

    return (1);
}
#endif

#ifdef NEED_BSD_SIGNAL

void (*bsd_signal(int sig, void (*func)(int)))(int)
{
    struct sigaction act, oact;

    act.sa_handler = func;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, sig);
    if (sigaction(sig, &act, &oact) == -1)
	return(SIG_ERR);
    return(oact.sa_handler);
}


#endif


#ifdef Solaris

#include <sys/stat.h>
#include <sys/swap.h>


double swapused(int *total, int *used)
{
    double          percent = -1;
    register int cnt, i;
    register int free;
    struct swaptable *swt;
    struct swapent *ste;
    static char path[256]; // does it really need 'static' ?
    cnt = swapctl(SC_GETNSWP, 0);
    swt = (struct swaptable *)malloc(sizeof(int) +
	    cnt * sizeof(struct swapent));
    if (swt == NULL)
    {
	return 0;
    }
    swt->swt_n = cnt;

    /* fill in ste_path pointers: we don't care about the paths, so we point
       them all to the same buffer */
    ste = &(swt->swt_ent[0]);
    i = cnt;
    while (--i >= 0)
    {
	ste++->ste_path = path;
    }
    /* grab all swap info */
    swapctl(SC_LIST, swt);

    /* walk thru the structs and sum up the fields */
    *total = free = 0;
    ste = &(swt->swt_ent[0]);
    i = cnt;
    while (--i >= 0)
    {
	/* dont count slots being deleted */
	if (!(ste->ste_flags & ST_INDEL) &&
		!(ste->ste_flags & ST_DOINGDEL))
	{
	    *total += ste->ste_pages;
	    free += ste->ste_free;
	}
	ste++;
    }

    *used = *total - free;
    if( total != 0)
	percent = (double)*used / (double)*total;
    else
	percent = 0;

    return percent;
}
#endif

#if __FreeBSD__

#include <kvm.h>


double
swapused(int *total, int *used)
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

#endif

#if __FreeBSD__

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
#endif


#ifdef Solaris

#include <kstat.h>
#include <sys/param.h>

#define loaddouble(la) ((double)(la) / FSCALE)

int
cpuload(char *str)
{
    kstat_ctl_t *kc;
    kstat_t *ks;
    kstat_named_t *kn;
    double l[3] = {-1, -1, -1};

    kc = kstat_open();

    if( !kc ){
	strcpy(str, "(unknown) ");
	return -1;
    }

    ks = kstat_lookup( kc, "unix", 0, "system_misc");

    if( kstat_read( kc, ks, 0) == -1){
	strcpy( str, "( unknown ");
	return -1;
    }

    kn = kstat_data_lookup( ks, "avenrun_1min" );

    if( kn ) {
	l[0] = loaddouble(kn->value.ui32);
    }

    kn = kstat_data_lookup( ks, "avenrun_5min" );

    if( kn ) {
	l[1] = loaddouble(kn->value.ui32);
    }

    kn = kstat_data_lookup( ks, "avenrun_15min" );

    if( kn ) {
	l[2] = loaddouble(kn->value.ui32);
    }

    if (str) {

	if (l[0] != -1)
	    sprintf(str, " %.2f %.2f %.2f", l[0], l[1], l[2]);
	else
	    strcpy(str, " (unknown) ");
    }

    kstat_close(kc);
    return (int)l[0];
}

#endif

#ifdef __linux__
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
	    snprintf(str, sizeof(str), " %.2f %.2f %.2f", l[0], l[1], l[2]);
	else
	    strcpy(str, " (unknown) ");
    }
    return (int)l[0];
}

double
swapused(int *total, int *used)
{
    double          percent = -1;
    char            buf[101];
    FILE           *fp;

    if ((fp = fopen("/proc/meminfo", "r"))) {
	while (fgets(buf, 100, fp) && strstr(buf, "SwapTotal:") == NULL);
	sscanf(buf, "%*s %d", total);
	fgets(buf, 100, fp);
	sscanf(buf, "%*s %d", used);
	*used = *total - *used;
	if (*total != 0)
	    percent = (double)*used / (double)*total;
	fclose(fp);
    }
    return percent;
}

#endif
