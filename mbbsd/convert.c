/* $Id$ */
#include "bbs.h"

#ifdef CONVERT

extern unsigned char *gb2big(unsigned char *, int *, int);
extern unsigned char *big2gb(unsigned char *, int *, int);
extern unsigned char *utf8_uni(unsigned char *, int *, int);
extern unsigned char *uni_utf8(unsigned char *, int *, int);
extern unsigned char *uni2big(unsigned char *, int *, int);
extern unsigned char *big2uni(unsigned char *, int *, int);

static ssize_t 
gb_input(void *buf, ssize_t icount)
{
    /* is sizeof(ssize_t) == sizeof(int)? not sure */
    int ic = (int) icount;
    gb2big((unsigned char *)buf, &ic, 0);
    return (ssize_t)ic;
}

static ssize_t 
gb_read(int fd, void *buf, size_t count)
{
    ssize_t icount = read(fd, buf, count);
    if (icount > 0)
	    icount = gb_input(buf, icount);
    return icount;
}

static ssize_t 
gb_write(int fd, void *buf, size_t count)
{
    int     icount = (int)count;
    big2gb((unsigned char *)buf, &icount, 0);
    if(icount > 0)
	return write(fd, buf, (size_t)icount);
    else
	return count; /* fake */
}

static ssize_t 
utf8_input  (void *buf, ssize_t icount) 
{
    /* is sizeof(ssize_t) == sizeof(int)? not sure */
    int ic = (int) icount;
    utf8_uni(buf, &ic, 0);
    uni2big(buf, &ic, 0);
    return (ssize_t)ic;
}

static ssize_t 
utf8_read(int fd, void *buf, size_t count)
{
    ssize_t icount = read(fd, buf, count);
    if (icount > 0)
	    icount = utf8_input(buf, icount);
    return icount;
}

static ssize_t 
utf8_write(int fd, void *buf, size_t count)
{
    int     icount = (int)count;
    static unsigned char *mybuf = NULL;
    static int   cmybuf = 0;

    /* utf8 output is a special case because 
     * we need larger buffer which can be 
     * tripple or more in size.
     * Current implementation uses 128 for each block.
     */

    if(cmybuf < count * 4) {
	cmybuf = (count*4+0x80) & (~0x7f) ;
	mybuf = (unsigned char*) realloc (mybuf, cmybuf);
    }
    memcpy(mybuf, buf, count);
    big2uni(mybuf, &icount, 0);
    uni_utf8(mybuf, &icount, 0);
    if(icount > 0)
	return write(fd, mybuf, (size_t)icount);
    else
	return count; /* fake */
}

static ssize_t 
norm_input(void *buf, ssize_t icount)
{
    return icount;
}

/* global function pointers */
read_write_type write_type = (read_write_type)write;
read_write_type read_type = read;
convert_type    input_type = norm_input;

void set_converting_type(int which)
{
    if (which == CONV_NORMAL) {
	read_type = read;
	write_type = (read_write_type)write;
	/* for speed up, NULL is better.. */
	input_type = NULL; /* norm_input; */
    }
    else if (which == CONV_GB) {
	read_type = gb_read;
	write_type = gb_write;
	input_type = gb_input;
    }
    else if (which == CONV_UTF8) {
	read_type = utf8_read;
	write_type = utf8_write;
	input_type = utf8_input;
    }
}

#endif
