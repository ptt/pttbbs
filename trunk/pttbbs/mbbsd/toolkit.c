/* $Id: toolkit.c,v 1.3 2002/07/05 17:10:28 in2 Exp $ */
#include "bbs.h"

unsigned 
StringHash(unsigned char *s)
{
    unsigned int    v = 0;
    while (*s) {
	v = (v << 8) | (v >> 24);
	v ^= toupper(*s++);	/* note this is case insensitive */
    }
    return (v * 2654435769UL) >> (32 - HASH_BITS);
}
