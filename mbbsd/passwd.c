/* $Id$ */
#include "bbs.h"

#ifdef _BBS_UTIL_C_
#error sorry, mbbsd/passwd.c does not support utility mode anymore. please use libcmbbs instead.
#endif

static uint32_t        latest_numposts;

void
passwd_force_update(int flag)
{
    if(!currutmp || (currutmp->alerts & ALERT_PWD) == 0)
	return;
    currutmp->alerts &= ~flag;
}

int 
initcuser(const char *userid)
{
    usernum = passwd_load_user(userid, &cuser);
    return usernum;
}

// XXX I don't like the stupid synchronization here,
// but simply following previous work here...
int
passwd_sync_update(int num, userec_t * buf)
{
    int alerts;

    if (num < 1 || num > MAX_USERS)
	return -1;

    if(usernum == num && currutmp && ((alerts = currutmp->alerts)  & ALERT_PWD))
    {
	userec_t u;
	buf->money = moneyof(num);
	if (passwd_sync_query(num, &u) != 0)
	    return -1;

	if(alerts & ALERT_PWD_BADPOST)
	   cuser.badpost = buf->badpost = u.badpost;
	if(alerts & ALERT_PWD_GOODPOST)
	   cuser.goodpost = buf->goodpost = u.goodpost;
        if(alerts & ALERT_PWD_PERM)	
	   cuser.userlevel = buf->userlevel = u.userlevel;
        if(alerts & ALERT_PWD_JUSTIFY)	
	{
	    memcpy(buf->justify,  u.justify, sizeof(u.justify));
	    memcpy(cuser.justify, u.justify, sizeof(u.justify));
	    memcpy(buf->email,  u.email, sizeof(u.email));
	    memcpy(cuser.email, u.email, sizeof(u.email));
	}
	cuser.numposts += u.numposts - latest_numposts;
	currutmp->alerts &= ~ALERT_PWD;

	// ALERT_PWD_RELOAD: reload all! No need to write.
	if (alerts & ALERT_PWD_RELOAD)
	{
	    memcpy(&cuser, &u, sizeof(u));
	    return 0;
	}
    }

    if (passwd_update(num, buf) != 0)
	return -1;

    if (currutmp && usernum > 0 &&
	latest_numposts != cuser.numposts)
    {
	sendalert_uid(usernum, ALERT_PWD_POSTS);
	latest_numposts = cuser.numposts;
    }

    return 0;
}

// XXX I don't like the stupid synchronization here,
// but simply following previous work here...
int
passwd_sync_query(int num, userec_t * buf)
{
    if (passwd_query(num, buf) < 0)
	return -1;

    if (buf == &cuser)
	latest_numposts = cuser.numposts;

    return 0;
}
