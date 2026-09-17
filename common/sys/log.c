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
    char sbuf[512];
    time4_t now = (time4_t)time(NULL);
    int prefix_len = snprintf(sbuf, sizeof(sbuf), "%s ", Cdatelite(&now));
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(sbuf))
        return -1;

    va_list ap_copy;
    va_copy(ap_copy, ap);
    int msg_len = vsnprintf(sbuf + prefix_len, sizeof(sbuf) - prefix_len, fmt, ap);
    if (msg_len < 0) {
        va_end(ap_copy);
        return -1;
    }
    int total_len = prefix_len + msg_len;
    if ((size_t)total_len < sizeof(sbuf)) {
        va_end(ap_copy);
        return file_append_len(fn, sbuf, total_len);
    }

    char *hbuf = (char *)malloc((size_t)total_len + 1);
    if (!hbuf) {
        va_end(ap_copy);
        return -1;
    }
    memcpy(hbuf, sbuf, prefix_len);
    vsnprintf(hbuf + prefix_len, (size_t)msg_len + 1, fmt, ap_copy);
    va_end(ap_copy);

    int res = file_append_len(fn, hbuf, total_len);
    free(hbuf);
    return res;
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
