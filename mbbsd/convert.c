/* $Id: convert.c 1374 2003-11-27 14:11:40Z victor $ */
#include "bbs.h"
/*
 * The following code is copied and modified from "autoconvert" with GPL.
 */

#ifdef GB_CONVERT

#include "convert.h"

char *hzconvert(char *, int *, char *, void (*dbcvrt)());

extern const unsigned char GtoB[], BtoG[];

#define	c1	(unsigned char)(s[0])
#define	c2	(unsigned char)(s[1])

static void g2b(char *s)
{
    unsigned int i;

    if ((c1 >= 0x81) && (c1 <= 0xfe) && (c2 >= 0x40) && (c2 <= 0xfe) && (c2 != 0x7f)) {
	/*
	 * c1c2 in the table 
	 */
	if (c2 < 0x7f)
	    i = ((c1 - 0x81) * 190 + (c2 - 0x40)) * 2;
	else
	    i = ((c1 - 0x81) * 190 + (c2 - 0x41)) * 2;
	s[0] = GtoB[i++];
	s[1] = GtoB[i];
    } else {                    /* c1c2 not in the table */
	if ((char) s[1] >= 0)   /* half HANZI-character */
	    s[0] = '?';
	else {                  /* invalid gbk-character */
	    s[0] = GtoB_bad1;
	    s[1] = GtoB_bad2;
	}
    }
}

static void b2g(char *s)
{
    unsigned int i;

    if ((c1 >= 0x81) && (c1 <= 0xfe) && (c2 >= 0x40) && (c2 <= 0xfe) && (c2 != 0x7f)) {
        /*
         * c1c2 in the table 
         */
        if (c2 < 0x7f)
            i = ((c1 - 0x81) * 190 + (c2 - 0x40)) * 2;
        else
            i = ((c1 - 0x81) * 190 + (c2 - 0x41)) * 2;
        s[0] = BtoG[i++];
        s[1] = BtoG[i];
    } else {                    /* c1c2 not in the table */
        if ((char) s[1] >= 0)   /* half HANZI-character */
            s[0] = '?';
        else {                  /* invalid big5-character */
            s[0] = BtoG_bad1;
            s[1] = BtoG_bad2;
        }
    }
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
