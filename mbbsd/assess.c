/* $Id$ */
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

/* FIXME race occurs when he's online.
 * passwd_update in mbbsd.c:u_exit() override the later value. */
#define modify_column(name) \
int inc_##name(char *userid, int num) \
{ \
    int uid = getuser(userid);\
    if(uid>0 ){ \
       inc(&xuser.name, num); \
       passwd_update(uid, &xuser); \
       return xuser.name; }\
    return 0;\
}

modify_column(goodpost); /* inc_goodpost */
modify_column(badpost);  /* inc_badpost */
modify_column(goodsale); /* inc_goodsale */
modify_column(badsale);  /* inc_badsale */

#if 0 //unused function
void set_assess(char *userid, unsigned char num, int type)
{
    int uid = getuser(userid);
    if(uid<=0) return;
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

#endif
