#include "bbs.h"

inline static void inc(unsigned char *num)
{
    if (*num < SALE_MAXVALUE)
	(*num)++;
}

void inc_goodpost(int uid)
{
    passwd_query(uid, &xuser);
    inc(&xuser.goodpost);
    passwd_update(uid, &xuser);
}

void inc_badpost(int uid)
{
    passwd_query(uid, &xuser);
    inc(&xuser.badpost);
    passwd_update(uid, &xuser);
}

void inc_goodsale(int uid)
{
    passwd_query(uid, &xuser);
    inc(&xuser.goodsale);
    passwd_update(uid, &xuser);
}

void inc_badsale(int uid)
{
    passwd_query(uid, &xuser);
    inc(&xuser.badsale);
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
