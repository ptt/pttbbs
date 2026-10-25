#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "cmsys.h"

int
log_filev(const char *fn, const char *fmt, va_list ap)
{
    time4_t now = (time4_t)time(NULL);
    AUTO_VSTR(msg, fmt, ap);
    AUTO_STR(logline, "%s %s", Cdatelite(&now), msg);
    return file_append_len(fn, logline, logline_len);
}

int
log_filef(const char *fn, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int res = log_filev(fn, fmt, ap);
    va_end(ap);
    return res;
}

int
log_file(const char *fn, const char *msg)
{
    if (!msg)
        return -1;
    return log_filef(fn, "%s", msg);
}
