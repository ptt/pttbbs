/* $Id: convert.c 1374 2003-11-27 14:11:40Z victor $ */
#include "bbs.h"

#ifdef CONVERT

extern read_write_type write_type;
extern read_write_type read_type;

unsigned char *gb2big(unsigned char *, int *, int);
unsigned char *big2gb(unsigned char *, int *, int);
unsigned char *utf8_uni(unsigned char *, int *, int);
unsigned char *uni_utf8(unsigned char *, int *, int);
unsigned char *uni2big(unsigned char *, int *, int);
unsigned char *big2uni(unsigned char *, int *, int);

static ssize_t gb_read(int fd, void *buf, size_t count)
{
    int      icount = (int)read(fd, buf, count);
    if (count > 0)
	gb2big((char *)buf, &icount, 0);
    return (size_t)icount;
}

static ssize_t gb_write(int fd, void *buf, size_t count)
{
    int     icount = (int)count;
    big2gb((char *)buf, &icount, 0);
    return write(fd, buf, (size_t)icount);
}

static ssize_t utf8_read(int fd, void *buf, size_t count)
{
    count = read(fd, buf, count);
    if (count > 0) {
	int     icount = (int)count;
	utf8_uni(buf, &icount, 0);
	uni2big(buf, &icount, 0);
	((char *)buf)[icount] = 0;
	return (size_t)icount;
    }
    return count;
}

static ssize_t utf8_write(int fd, void *buf, size_t count)
{
    int     icount = (int)count;
    big2uni(buf, &icount, 0);
    uni_utf8(buf, &icount, 0);
    ((char *)buf)[icount] = 0;
    return write(fd, buf, (size_t)icount);
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
