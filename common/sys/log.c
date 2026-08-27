#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "cmsys.h"

int
log_filef(const char *fn, const char *fmt,...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = file_appendv(fn, fmt, ap);
    va_end(ap);
    return ret;
}

int
log_file(const char *fn, const char *msg)
{
    return file_append(fn, msg);
}
