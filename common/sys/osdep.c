/* $Id$ */
#include "osdep.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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

#ifdef HAVE_SETPROCTITLE

void
initsetproctitle(int argc, char **argv, char **envp)
{
}

#else


static char **Argv = NULL;	/* pointer to argument vector */
static int argv_size;		/* end of argv */

extern char **environ;

void
initsetproctitle(int argc, char **argv, char **envp)
{
    register int    i;
    int len=0,nenv=0;


    /*
     * Move the environment so setproctitle can use the space at the top of
     * memory.
     */
    for (i = 0; envp[i]; i++)
      len+=strlen(envp[i])+1;
    nenv=i+1;
    len+=sizeof(char*)*nenv;
    environ = malloc(len);
    len=0;
    for (i = 0; envp[i]; i++) {
        environ[i] = (char*)environ+nenv*sizeof(char*)+len;
	strcpy(environ[i], envp[i]);
	len+=strlen(envp[i])+1;
    }
    environ[i] = NULL;

    /* Save start and extent of argv for setproctitle. */
    Argv = argv;
    if (i > 0)
	argv_size = envp[i - 1] + strlen(envp[i - 1]) - Argv[0];
    else
	argv_size = argv[argc - 1] + strlen(argv[argc - 1]) - Argv[0];
}

static void
do_setproctitle(const char *cmdline)
{
    int             len;

    len = strlen(cmdline) + 1; // +1 for '\0'
    if(len > argv_size - 2) // 2 ??
        len = argv_size - 2;
    memset(Argv[0], 0, argv_size);
    strlcpy(Argv[0], cmdline, len);
    Argv[1] = NULL;
}

void
setproctitle(const char *format,...)
{
    char            buf[256];
    va_list         args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    do_setproctitle(buf);
    va_end(args);
}

#endif

#ifdef __FreeBSD__

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
	    sprintf(str, " %.2f %.2f %.2f", l[0], l[1], l[2]);
	else
	    strcpy(str, " (unknown) ");
    }
    return (int)l[0];
}

#endif
