/* $Id: convert.c 1374 2003-11-27 14:11:40Z victor $ */
#include "bbs.h"
/*
 * The following code is copied and modified from "autoconvert" with GPL.
 */

#ifdef CONVERT

#include "convert.h"
#include "iconv.h"


char *hzconvert(char *, int *, char *, void (*dbcvrt)());

extern const unsigned char GtoB[], BtoG[];

#define	c1	(unsigned char)(s[0])
#define	c2	(unsigned char)(s[1])

/* Convert: Big5 <-> GB */

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

int gb_converting_read(int fd, void *buf, size_t count)
{
    int len = read(fd, buf, count);
    if (len >= 0)
	gb2big(buf, len);
    return len;
}

int gb_converting_write(int fd, void *buf, size_t count)
{
    big2gb(buf, count);
    return write(fd, buf, count);
}

/* Convert: Big5 <-> Unicode */

/**************************/
static iconv_t big2ucs, ucs2big;

int ucs_converting_init(void)
{
    big2ucs = iconv_open("utf8", "big5");
    ucs2big = iconv_open("big5", "utf8");
    return (big2ucs < 0 || ucs2big < 0);
}

int str_iconv(iconv_t desc, char *src, int srclen)
{
    int iconv_ret;
    /* Start translation */
    while (srclen > 0) {
	iconv_ret = iconv(desc, (const char* *)&src, &srclen, &src, &srclen);
	// deslen - 1 ?? iconv_ret = iconv(desc, (const char* *)&src, &srclen, &src, &srclen);
	if (iconv_ret  != 0) {
	    switch(errno) {
		case EILSEQ:
		    /* forward that byte */
		    src++; srclen--;
		    break;
		    /* incomplete multibyte happened */
		case EINVAL:
		    /* forward that byte (maybe wrong)*/
		    src++; srclen--;
		    break;
		    /* des no rooms */
		case E2BIG:
		    /* break out the while loop */
		    srclen = 0;
		    break;
	    }
	}
    }
    *src= '\0';
    return srclen;
}

//int str_iconv(iconv_t desc, char *src, int srclen)
//{
//    deslen--;	/* keep space for '\0' */
//    deslen_old = deslen;
//
//    /* Start translation */
//    while (srclen > 0 && deslen > 0) {
//	iconv_ret = iconv(desc, (const char* *)&src, &srclen, &des, &deslen);
//	if (iconv_ret  != 0) {
//	    switch(errno) {
//		case EILSEQ:
//		    /* forward that byte */
//		    *des = *src;
//		    src++; srclen--;
//		    des++; deslen--;
//		    break;
//		    /* incomplete multibyte happened */
//		case EINVAL:
//		    /* forward that byte (maybe wrong)*/
//		    *des = *src;
//		    src++; srclen--;
//		    des++; deslen--;
//		    break;
//		    /* des no rooms */
//		case E2BIG:
//		    /* break out the while loop */
//		    srclen = 0;
//		    break;
//	    }
//	}
//    }
//    *des = '\0';
//    return deslen_old - deslen;
//}

int ucs_converting_read(int fd, void *buf, size_t count)
{
    int len;
    if ((len = read(fd, buf, count)) < 0)
	str_iconv(ucs2big, buf, count);
    return len;
}

int ucs_converting_write(int fd, void *buf, size_t count)
{
    str_iconv(big2ucs, buf, count);
    return write(fd, buf, count);
}

/* we don't need it since one cannot use it after stop mbbsd
void ucs_finish_converting(void)
{
    iconv_close(big2ucs);
    iconv_close(ucs2big);
}
*/
#endif
