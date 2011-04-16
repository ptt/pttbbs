/* $Id: convert.h 1374 2003-11-27 14:11:40Z victor $ */

#ifndef _BBS_CONVERT_H
#define _BBS_CONVERT_H

#ifdef CONVERT

enum ConvertMode {
    CONV_NORMAL,
    CONV_UTF8,
};

extern int (*convert_write)(VBUF *v, char c);
extern int (*convert_read)(VBUF *v, const void* buf, size_t len);

extern void init_convert(void);
extern void set_converting_type(int which);

#endif // CONVERT
#endif // _BBS_CONVERT_H
