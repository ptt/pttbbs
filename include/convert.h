/* $Id: convert.h 1374 2003-11-27 14:11:40Z victor $ */

#ifndef _BBS_CONVERT_H
#define _BBS_CONVERT_H

#ifdef CONVERT

enum ConvertMode {
    CONV_NORMAL,
    CONV_GB,
    CONV_UTF8
};

typedef ssize_t (*read_write_type)(int, void *, size_t);
typedef ssize_t (*convert_type)(void *, ssize_t);

extern read_write_type write_type;
extern read_write_type read_type;
extern convert_type    input_type;

extern void init_convert();

#endif // CONVERT
#endif // _BBS_CONVERT_H
