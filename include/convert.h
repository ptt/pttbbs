/* $Id: convert.h 1374 2003-11-27 14:11:40Z victor $ */

#ifdef CONVERT

#define CONV_NORMAL	0
#define CONV_GB		1
#define CONV_UTF8	2

typedef ssize_t (*read_write_type)(int, void *, size_t);
typedef ssize_t (*convert_type)(void *, ssize_t);

#endif
