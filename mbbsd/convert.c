/* $Id: convert.c 1374 2003-11-27 14:11:40Z victor $ */
#include "bbs.h"
/*
 * The following code is copied and modified from "autoconvert" with GPL.
 */

#ifdef GB_CONVERT

#include "convert.h"

#define BtoG_bad1 0xa1
#define BtoG_bad2 0xf5
#define GtoB_bad1 0xa1
#define GtoB_bad2 0xbc

char *hzconvert(char *, int *, char *, void (*dbcvrt)());

extern unsigned char GtoB[], BtoG[];

#define	c1	(unsigned char)(s[0])
#define	c2	(unsigned char)(s[1])

static void g2b(char *s)
{
    unsigned int i;

    if ((c2 >= 0xa1) && (c2 <= 0xfe)) {
	if ((c1 >= 0xa1) && (c1 <= 0xa9)) {
	    i = ((c1 - 0xa1) * 94 + (c2 - 0xa1)) * 2;
	    s[0] = GtoB[i++];  s[1] = GtoB[i];
	    return;
	} else if ((c1 >= 0xb0) && (c1 <= 0xf7)) {
	    i = ((c1 - 0xb0 + 9) * 94 + (c2 - 0xa1)) * 2;
	    s[0] = GtoB[i++];  s[1] = GtoB[i];
	    return;
	}
    }
    s[0] = GtoB_bad1;  s[1] = GtoB_bad2;
}

static void b2g(char *s)
{
    int i;

    if ((c1 >= 0xa1) && (c1 <= 0xf9)) {
	if ((c2 >= 0x40) && (c2 <= 0x7e)) {
	    i = ((c1 - 0xa1) * 157 + (c2 - 0x40)) * 2;
	    s[0] = BtoG[i++];  s[1] = BtoG[i];
	    return;
	} else if ((c2 >= 0xa1) && (c2 <= 0xfe)) {
	    i = ((c1 - 0xa1) * 157 + (c2 - 0xa1) + 63) * 2;
	    s[0] = BtoG[i++];  s[1] = BtoG[i];
	    return;
	}
    }
    s[0] = BtoG_bad1;  s[1] = BtoG_bad2;
}

unsigned char *gb2big(unsigned char *s, int plen)
{
    unsigned char c = 0;
    return hzconvert(s, &plen, &c, g2b);
}

unsigned char *big2gb(unsigned char *s, int plen)
{
    unsigned char c = 0;
    return hzconvert(s, &plen, &c, b2g);
}
#endif
