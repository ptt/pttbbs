/* $Id: toolkit.c,v 1.1 2002/03/07 15:13:48 in2 Exp $ */
#include <ctype.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"

unsigned StringHash(unsigned char *s) {
    unsigned int v=0;
    while(*s) {
	v = (v << 8) | (v >> 24);
	v ^= toupper(*s++);	/* note this is case insensitive */
    }
    return (v * 2654435769UL) >> (32 - HASH_BITS);
}
