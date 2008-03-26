#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "cmsys.h"

int
log_filef(const char *fn, int log_flag, const char *fmt,...)
{
    char       msg[256];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    return log_file(fn, log_flag, msg);
}

int
log_file(const char *fn, int log_flag, const char *msg)
{
    int        fd;
    int flag = O_APPEND | O_WRONLY;
    int mode = 0664;

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

