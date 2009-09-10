/* $Id$ */
#include "bbs.h"

#ifdef ASSESS

/* do (*num) + n, n is integer. */
inline static void inc(unsigned char *num, int n)
{
    if (n >= 0 && UCHAR_MAX - *num <= n)
	(*num) = UCHAR_MAX;
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
	inc(&xuser._attr, num); \
	passwd_sync_update(uid, &xuser); \
	return xuser._attr; }\
    return 0;\
}

modify_column(badpost);  /* inc_badpost */

#endif // ASSESS
