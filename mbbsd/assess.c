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

#define modify_column(_attr) \
int inc_##_attr(const char *userid, int num) \
{ \
    userec_t xuser; \
    int uid = getuser(userid, &xuser);\
    if( uid > 0 ){ \
	userinfo_t *uinfo = search_ulist(uid); \
	if (uinfo != NULL) \
	    inc(&uinfo->_attr, num); \
	inc(&xuser._attr, num); \
	passwd_update(uid, &xuser); \
	return xuser._attr; }\
    return 0;\
}

modify_column(goodpost); /* inc_goodpost */
modify_column(badpost);  /* inc_badpost */
modify_column(goodsale); /* inc_goodsale */
modify_column(badsale);  /* inc_badsale */

#if 0 //unused function
void set_assess(const char *userid, unsigned char num, int type)
{
    userec_t xuser;
    int uid = getuser(userid, &xuser);
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
