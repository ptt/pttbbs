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

#define modify_column(name) \
int inc_##name(int uid, int num) \
{ \
    passwd_query(uid, &xuser); \
    inc(&xuser.name, num); \
    userinfo_t *user = search_ulist(uid); \
    if (user != NULL) \
	user->name = xuser.name; \
    passwd_update(uid, &xuser); \
    return xuser.name; \
}

modify_column(goodpost);
modify_column(badpost);
modify_column(goodsale);
modify_column(badsale);


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
