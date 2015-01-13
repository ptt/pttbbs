#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "cmsys.h"
#include "config.h" // for DEFAULT_FILE_CREATE_PERM

int
log_filef(const char *fn, int log_flag, const char *fmt,...)
{
    char buf[1024], *msg = buf;
    int len = sizeof(buf), ret;

    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(msg, len, fmt, ap);
    if (ret >= len) {
        len = ret + 1;
        msg = (char *)malloc(len);
        if (!msg) {
            va_end(ap);
            return -1;
        }
        vsnprintf(msg, len, fmt, ap);
    }
    va_end(ap);

    ret = log_file(fn, log_flag, msg);
    if (msg != buf)
        free(msg);

    return ret;
}

int
log_file(const char *fn, int log_flag, const char *msg)
{
    int        fd;
    int flag = O_APPEND | O_WRONLY;
    int mode = DEFAULT_FILE_CREATE_PERM;

    if (log_flag & LOG_CREAT)
	flag |= O_CREAT;

    fd = open(fn, flag, mode);
    if( fd < 0 )
	return -1;

    if( write(fd, msg, strlen(msg)) < 0 ){
	close(fd);
	return -1;
    }
    close(fd);
    return 0;
}

