#include "bbs.h"

#ifdef ASSESS

/* do (*num) + n, n is integer. */
inline static void inc(unsigned char *num, int n)
{
    if (n >= 0 && SALE_MAXVALUE - *num <= n)
	(*num) = SALE_MAXVALUE;
    else if (n < 0 && *num < -n)
	(*num) = 0;
    else
	(*num) += n;
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
    if (!(xuser.badpost % 10)) {
    	post_violatelaw(xuser.userid, "Ptt 系統警察", "劣文累計十篇", "罰單一張");
 	mail_violatelaw(xuser.userid, "Ptt 系統警察", "劣文累計十篇", "罰單一張");
	xuser.userlevel |= PERM_VIOLATELAW;
    }
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
#endif
