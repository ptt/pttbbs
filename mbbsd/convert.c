/* $Id: convert.c 1374 2003-11-27 14:11:40Z victor $ */
#include "bbs.h"
/*
 * The following code is copied and modified from "autoconvert" with GPL.
 */

#ifdef CONVERT

extern read_write_type write_type;
extern read_write_type read_type;

static int gb_read(int fd, void *buf, size_t count)
{
    int len = read(fd, buf, count);
    if (len > 0)
	gb2big(buf, len);
    return len;
}

static int gb_write(int fd, void *buf, size_t count)
{
    big2gb(buf, count);
    return write(fd, buf, count);
}

static int utf8_read(int fd, void *buf, size_t count)
{
    count = read(fd, buf, count);
    if (count > 0) {
	strcpy(buf, utf8_uni(buf, &count, 0));
	uni2big(buf, &count, 0);
	((char *)buf)[count] = 0;
    }
    return count;
}

static int utf8_write(int fd, void *buf, size_t count)
{
    strcpy(buf, big2uni(buf, &count, 0));
    uni_utf8(buf, &count, 0);
    ((char *)buf)[count] = 0;
    return write(fd, buf, count);
}

void set_converting_type(int which)
{
    if (which == CONV_NORMAL) {
	read_type = read;
	write_type = (read_write_type)write;
    }
    else if (which == CONV_GB) {
	read_type = gb_read;
	write_type = gb_write;
    }
    else if (which == CONV_UTF8) {
	read_type = utf8_read;
	write_type = utf8_write;
    }
}

#endif
