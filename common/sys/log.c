#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "cmsys.h"

int
log_filef(const char *fn, const char *fmt,...)
{
    va_list ap;
    va_start(ap, fmt);
    char *buf = NULL;
    if (vasprintf(&buf, fmt, ap) < 0 || !buf) {
        va_end(ap);
        return -1;
    }
    va_end(ap);

    int res = log_file(fn, buf);
    free(buf);
    return res;
}

int
log_file(const char *fn, const char *msg)
{
    time4_t now = (time4_t)time(NULL);
    return file_appendf(fn, "%s %s", Cdatelite(&now), msg);
}
