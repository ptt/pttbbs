#include "bbs.h"


inline static void inc(unsigned char *num, int n)
{
    if (SALE_MAXVALUE - *num >= n)
	(*num) += n;
    else
	(*num) = SALE_MAXVALUE;
}

inline static void dec(unsigned char *num, int n)
{
    if (*num < n)
	(*num) -= n;
    else
	(*num) = 0;
}

void inc_goodpost(int uid, int num)
{
    passwd_query(uid, &xuser);
    inc(&xuser.goodpost, num);
    passwd_update(uid, &xuser);
}

void inc_badpost(int uid, int num)
{
    passwd_query(uid, &xuser);
    inc(&xuser.badpost, num);
    passwd_update(uid, &xuser);
}

void inc_goodsale(int uid, int num)
{
    passwd_query(uid, &xuser);
    inc(&xuser.goodsale, num);
    passwd_update(uid, &xuser);
}

void inc_badsale(int uid, int num)
{
    passwd_query(uid, &xuser);
    inc(&xuser.badsale, num);
    passwd_update(uid, &xuser);
}

void set_assess(int uid, unsigned char num, int type)
{
    passwd_query(uid, &xuser);
    switch (type){
	case GOODPOST:
	    xuser.goodpost = num;
	    break;
	case BADPOST:
	    xuser.badpost = num;
	    break;
	case GOODSALE:
	    xuser.goodsale = num;
	    break;
	case BADSALE:
	    xuser.badsale = num;
	    break;
    }
    passwd_update(uid, &xuser);
}
