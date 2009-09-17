/* $Id$ */
#define PWCU_IMPL
#include "bbs.h"
#include "time.h"

#ifdef _BBS_UTIL_C_
#error sorry, mbbsd/passwd.c does not support utility mode anymore. please use libcmbbs instead.
#endif

#ifndef NO_CONST_CUSER
 #undef  cuser
 #define cuser pwcuser
#endif

int 
initcuser(const char *userid)
{
    usernum = passwd_load_user(userid, &cuser);
    return usernum;
}

int
passwd_sync_update(int num, userec_t * buf)
{
    if (num < 1 || num > MAX_USERS)
	return -1;

    // money update should be done before everything.
    buf->money = moneyof(num);
    if (passwd_update(num, buf) != 0)
	return -1;

    if (num == usernum)
	cuser.money = moneyof(num);

    return 0;
}

int
passwd_sync_query(int num, userec_t * buf)
{
    if (passwd_query(num, buf) < 0)
	return -1;

    buf->money = moneyof(num);

    if (num == usernum)
	cuser.money = moneyof(num);

    return 0;
}

// pwcu*: current user password helpers

static int
pwcuInitCUser(userec_t *u)
{
    assert(usernum > 0 && usernum <= MAX_USERS);
    if (passwd_sync_query(usernum, u) != 0)
	return -1;
    assert(strncmp(u->userid, cuser.userid, IDLEN) == 0);
    if    (strncmp(u->userid, cuser.userid, IDLEN) != 0)
	return -1;
    return 0;
}

static int
pwcuFinalCUser(userec_t *u)
{
    assert(usernum > 0 && usernum <= MAX_USERS);
    assert(strcmp(u->userid, cuser.userid) == 0);
    if (passwd_sync_update(usernum, u) != 0)
	return -1;
    return 0;
}

#define PWCU_START()  userec_t u; if(pwcuInitCUser (&u) != 0) return -1
#define PWCU_END()    if (pwcuFinalCUser(&u) != 0) return -1; return 0

#define _ENABLE_BIT( var,mask) var |=  (mask)
#define _DISABLE_BIT(var,mask) var &= ~(mask)
#define _SETBY_BIT(var,mask,val) if (val) { _ENABLE_BIT(var, (mask)); } else { _DISABLE_BIT(var, (mask)); }

int pwcuBitEnableLevel	(unsigned int mask)
{
    PWCU_START();
    _ENABLE_BIT(    u.userlevel, mask);
    _ENABLE_BIT(cuser.userlevel, mask);
    PWCU_END();
}

int pwcuBitDisableLevel	(unsigned int mask)
{
    PWCU_START();
    _DISABLE_BIT(    u.userlevel, mask);
    _DISABLE_BIT(cuser.userlevel, mask);
    PWCU_END();
}

int 
pwcuIncNumPost()
{
    PWCU_START();
    cuser.numposts =  ++u.numposts;
    PWCU_END();
}

int 
pwcuDecNumPost()
{
    PWCU_START();
    if (u.numposts > 0)
	u.numposts--;
    cuser.numposts = u.numposts;
    PWCU_END();
}

int 
pwcuViolateLaw	()
{
    PWCU_START();
    _ENABLE_BIT(    u.userlevel, PERM_VIOLATELAW);
    _ENABLE_BIT(cuser.userlevel, PERM_VIOLATELAW);
    u.timeviolatelaw     = now;
    cuser.timeviolatelaw = u.timeviolatelaw;
    u.vl_count++;
    cuser.vl_count = u.vl_count;
    PWCU_END();
}

int
pwcuSaveViolateLaw()
{
    PWCU_START();
    _DISABLE_BIT(    u.userlevel, PERM_VIOLATELAW);
    _DISABLE_BIT(cuser.userlevel, PERM_VIOLATELAW);
    PWCU_END();
}

int
pwcuCancelBadpost()
{
    int day;
    PWCU_START();

    // no matter what, reload the timebomb
    cuser.badpost = u.badpost;
    cuser.timeremovebadpost = u.timeremovebadpost;

    // check timebomb again
    day = (now - u.timeremovebadpost ) / DAY_SECONDS;
    if (day < BADPOST_CLEAR_DURATION)
	return -1;
    if (u.badpost < 1)
	return -1;

    cuser.badpost = --u.badpost;
    cuser.timeremovebadpost = u.timeremovebadpost = now;

    PWCU_END();
}

int 
pwcuAddExMailBox(int m)
{
    PWCU_START();
    u.exmailbox    += m;
    cuser.exmailbox = u.exmailbox;
    PWCU_END();
}

int pwcuSetLastSongTime (time4_t clk)
{
    PWCU_START();
    u.lastsong     = clk;
    cuser.lastsong = clk;
    PWCU_END();
}

int pwcuSetMyAngel	(const char *angel_uid)
{
    PWCU_START();
    strlcpy(    u.myangel, angel_uid, sizeof(    u.myangel));
    strlcpy(cuser.myangel, angel_uid, sizeof(cuser.myangel));
    PWCU_END();
}

int pwcuSetNickname	(const char *nickname)
{
    PWCU_START();
    strlcpy(    u.nickname, nickname, sizeof(    u.nickname));
    strlcpy(cuser.nickname, nickname, sizeof(cuser.nickname));
    PWCU_END();
}

int 
pwcuToggleOutMail()
{
    PWCU_START();
    u.uflag2     ^=  REJ_OUTTAMAIL;
    _SETBY_BIT(cuser.uflag2, REJ_OUTTAMAIL,
	    u.uflag2 & REJ_OUTTAMAIL);
    PWCU_END();
}

int 
pwcuSetLoginView(unsigned int bits)
{
    PWCU_START();
    u.loginview     = bits;
    cuser.loginview = u.loginview;
    PWCU_END();
}

int 
pwcuRegCompleteJustify(const char *justify)
{
    PWCU_START();
    strlcpy(    u.justify, justify, sizeof(u.justify));
    strlcpy(cuser.justify, justify, sizeof(cuser.justify));
    _ENABLE_BIT(    u.userlevel, (PERM_POST | PERM_LOGINOK));
    _ENABLE_BIT(cuser.userlevel, (PERM_POST | PERM_LOGINOK));
    PWCU_END();
}

int
pwcuRegSetTemporaryJustify(const char *justify, const char *email)
{
    PWCU_START();
    strlcpy(    u.email, email, sizeof(u.email));
    strlcpy(cuser.email, email, sizeof(cuser.email));
    strlcpy(    u.justify, justify, sizeof(u.justify));
    strlcpy(cuser.justify, justify, sizeof(cuser.justify));
    _DISABLE_BIT(    u.userlevel, (PERM_POST | PERM_LOGINOK));
    _DISABLE_BIT(cuser.userlevel, (PERM_POST | PERM_LOGINOK));
    PWCU_END();
}

int pwcuRegisterSetInfo (const char *rname,
			 const char *addr,
			 const char *career,
			 const char *phone,
			 const char *email,
			 int         mobile,
			 uint8_t     sex,
			 uint8_t     year,
			 uint8_t     month,
			 uint8_t     day,
			 uint8_t     is_foreign)
{
    PWCU_START();
    strlcpy(u.realname, rname,  sizeof(u.realname));
    strlcpy(u.address,  addr,   sizeof(u.address));
    strlcpy(u.career,   career, sizeof(u.career));
    strlcpy(u.phone,    phone,  sizeof(u.phone));
    strlcpy(u.email,    email,  sizeof(u.email));
    u.mobile = mobile;
    u.sex    = sex;
    u.year   = year;
    u.month  = month;
    u.day    = day;
    _SETBY_BIT(u.uflag2, FOREIGN, is_foreign);

    // duplicate to cuser
    
    strlcpy(cuser.realname, rname,  sizeof(cuser.realname));
    strlcpy(cuser.address,  addr,   sizeof(cuser.address));
    strlcpy(cuser.career,   career, sizeof(cuser.career));
    strlcpy(cuser.phone,    phone,  sizeof(cuser.phone));
    strlcpy(cuser.email,    email,  sizeof(cuser.email));
    cuser.mobile = mobile;
    cuser.sex    = sex;
    cuser.year   = year;
    cuser.month  = month;
    cuser.day    = day;
    _SETBY_BIT(cuser.uflag2, FOREIGN, is_foreign);

    PWCU_END();
}

#include "chess.h"
int 
pwcuChessResult(int sigType, ChessGameResult r)
{
    uint16_t *utmp_win = NULL, *cuser_win = NULL, *u_win = NULL,
	     *utmp_lose= NULL, *cuser_lose= NULL, *u_lose= NULL,
	     *utmp_tie = NULL, *cuser_tie = NULL, *u_tie = NULL;

    PWCU_START();

    // verify variable size
    assert(sizeof(* utmp_win) == sizeof(currutmp->chc_win));
    assert(sizeof(*cuser_lose)== sizeof(   cuser.five_lose));
    assert(sizeof(*    u_tie) == sizeof(       u.go_tie));

    // determine variables
    switch(sigType)
    {
	case SIG_CHC:
	    utmp_win  = &(currutmp->chc_win);
	    utmp_lose = &(currutmp->chc_lose);
	    utmp_tie  = &(currutmp->chc_tie);
	    cuser_win = &(    cuser.chc_win);
	    cuser_lose= &(    cuser.chc_lose);
	    cuser_tie = &(    cuser.chc_tie);
	    u_win     = &(        u.chc_win);
	    u_lose    = &(        u.chc_lose);
	    u_tie     = &(        u.chc_tie);
	    break;

	case SIG_GO:
	    utmp_win  = &(currutmp->go_win);
	    utmp_lose = &(currutmp->go_lose);
	    utmp_tie  = &(currutmp->go_tie);
	    cuser_win = &(    cuser.go_win);
	    cuser_lose= &(    cuser.go_lose);
	    cuser_tie = &(    cuser.go_tie);
	    u_win     = &(        u.go_win);
	    u_lose    = &(        u.go_lose);
	    u_tie     = &(        u.go_tie);
	    break;

	case SIG_GOMO:
	    utmp_win  = &(currutmp->five_win);
	    utmp_lose = &(currutmp->five_lose);
	    utmp_tie  = &(currutmp->five_tie);
	    cuser_win = &(    cuser.five_win);
	    cuser_lose= &(    cuser.five_lose);
	    cuser_tie = &(    cuser.five_tie);
	    u_win     = &(        u.five_win);
	    u_lose    = &(        u.five_lose);
	    u_tie     = &(        u.five_tie);
	    break;

	default:
	    assert(!"unknown sigtype");
	    break;
    }

    // perform action
    switch(r)
    {
	case CHESS_RESULT_WIN:
	    *utmp_win = *cuser_win = 
		++(*u_win);
	    // recover init lose
	    if (*u_lose > 0)
		*utmp_lose = *cuser_lose = 
		    --(*u_lose);
	    break;

	case CHESS_RESULT_TIE:
	    *utmp_tie = *cuser_tie = 
		++*u_tie;
	    // recover init lose
	    if (*u_lose > 0)
		*utmp_lose = *cuser_lose = 
		    --(*u_lose);
	    break;

	case CHESS_RESULT_LOST:
	    *utmp_lose = *cuser_lose = 
		++(*u_lose);
	    break;

	default:
	    assert(!"unknown result");
	    return -1;
    }

    PWCU_END();
}

int 
pwcuSetChessEloRating(uint16_t elo_rating)
{
    PWCU_START();
    cuser.chess_elo_rating = u.chess_elo_rating = elo_rating;
    PWCU_END();
}

int 
pwcuToggleUserFlag	(unsigned int mask)
{
    PWCU_START();
    u.uflag ^= mask;
    _SETBY_BIT(cuser.uflag,  mask,
	           u.uflag & mask);
    PWCU_END();
}

int 
pwcuToggleUserFlag2	(unsigned int mask)
{
    PWCU_START();
    u.uflag2 ^= mask;
    _SETBY_BIT(cuser.uflag2,  mask,
	           u.uflag2 & mask);
    PWCU_END();
}

int 
pwcuToggleSortBoard ()
{
    // XXX if this is executed too often,
    // put it into 'non-important variable list'.
    return pwcuToggleUserFlag(BRDSORT_FLAG);
}

int 
pwcuToggleFriendList()
{
    // XXX if this is executed too often,
    // put it into 'non-important variable list'.
    return pwcuToggleUserFlag(FRIEND_FLAG);
}

// non-important variables (only save on exit)

int 
pwcuSetWaterballMode(unsigned int bm)
{
    // XXX you MUST save this variable in pwcuExitSave();
    cuser.pager_ui_type = bm % PAGER_UI_TYPES;
    return 0;
}

int
pwcuSetSignature(unsigned char newsig)
{
    // XXX you MUST save this variable in pwcuExitSave();
    cuser.signature = newsig;
    return 0;
}

// session save

// XXX this is a little different - only invoked at login,
// which we should update/calculate every variables to log.
int pwcuLoginSave	()
{
    // XXX because LoginSave was called very long after
    // login_start_time, so we must reload passwd again
    // here to prevent race condition. 
    // If you want to remove this reload, make sure
    // pwcuLoginSave is called AFTER login_start_time
    // was decided.
    int regdays = 0, prev_regdays = 0;
    int reftime = login_start_time;
    time4_t   baseref = 0;
    struct tm baseref_tm = {0};

    PWCU_START();

    // new host from 'fromhost'
    strlcpy(    u.lasthost, fromhost, sizeof(    u.lasthost));
    strlcpy(cuser.lasthost, fromhost, sizeof(cuser.lasthost));

    // this must be valid.
    assert(login_start_time > 0);

    // adjust base reference by rounding to beginning of each day (0:00am)
    baseref = u.firstlogin;
    if (localtime4_r(&baseref, &baseref_tm))
    {
	baseref_tm.tm_sec = baseref_tm.tm_min = baseref_tm.tm_hour = 0;
	baseref = mktime(&baseref_tm);
    }

    // invalid session?
    if (reftime < u.lastlogin)
	reftime = u.lastlogin;

    regdays =      (    reftime - baseref) / DAY_SECONDS;
    prev_regdays = (u.lastlogin - baseref) / DAY_SECONDS;
    // assert(regdays >= prev_regdays);

    if (u.numlogindays > prev_regdays)
	u.numlogindays = prev_regdays;

    // calculate numlogindays (only increase one per each key)
    if (regdays > prev_regdays)
    {
	++u.numlogindays;
	is_first_login_of_today = 1;
    }
    cuser.numlogindays = u.numlogindays;

    // update last login time
    cuser.lastlogin = u.lastlogin = reftime;

    if (!PERM_HIDE(currutmp))
	cuser.lastseen = u.lastseen = reftime;

    PWCU_END();
}

// XXX this is a little different - only invoked at exist,
// so no need to sync back to cuser.
int 
pwcuExitSave	()
{
    int dirty = 0;
    uint32_t uflag, uflag2, withme;
    uint8_t  invisible, pager, signature, pager_ui_type;
    int32_t  money;

    PWCU_START();

    // XXX if PWCU_START uses sync_query, then money is
    // already changed... however, maybe not a problem here,
    // since every deumoney() should write difference.

    // save variables for dirty check
    uflag     = u.uflag;
    uflag2    = u.uflag2;

    withme    = u.withme;
    pager     = u.pager;
    invisible = u.invisible;

    money     = u.money;
    signature = u.signature;
    pager_ui_type = u.pager_ui_type;

    // configure new utmp values
    u.withme    = currutmp->withme;
    u.pager     = currutmp->pager;
    u.invisible = currutmp->invisible;

    u.signature = cuser.signature;
    u.money     = moneyof(usernum);
    u.pager_ui_type = cuser.pager_ui_type;

    // XXX 當初設計的人把 mind 設計成非 NULL terminated 的...
    // assert(sizeof(u.mind) == sizeof(currutmp->mind));
    if (memcmp(u.mind, currutmp->mind, sizeof(u.mind)) != 0)
    {
	memcpy(u.mind,currutmp->mind, sizeof(u.mind));
	dirty = 1;
    }

    // check dirty
    if (!dirty && (
	uflag  != u.uflag ||
	uflag2 != u.uflag2||
	withme != u.withme||
	pager  != u.pager ||
	money  != u.money ||
	pager_ui_type != u.pager_ui_type ||
	signature != u.signature||
	invisible != u.invisible))
    {
	dirty = 1;
    }

#ifdef DEBUG
    log_filef("log/pwcu_exitsave.log", LOG_CREAT,
	    "%s exit %s at %s\n", u.userid,
	    dirty ? "DIRTY" : "CLEAN",
	    Cdatelite(&now));
#endif

    // no need to save data.
    if (!dirty)
	return 0;

    PWCU_END();
}

int 
pwcuReload	()
{
    int r = passwd_sync_query(usernum, &cuser);
    // XXX TODO verify cuser structure?
    return r;
}

int
pwcuReloadMoney ()
{
    cuser.money=moneyof(usernum);
    return 0;
}

int
pwcuDeMoney	(int money)
{
    deumoney(usernum, money);
    cuser.money = moneyof(usernum);
    return 0;
}

// Initialization

void pwcuInitZero	()
{
    bzero(&cuser, sizeof(cuser));
}

int pwcuInitAdminPerm	()
{
    PWCU_START();
    cuser.userlevel = PERM_BASIC | PERM_CHAT | PERM_PAGE |
	PERM_POST | PERM_LOGINOK | PERM_MAILLIMIT |
	PERM_CLOAK | PERM_SEECLOAK | PERM_XEMPT |
	PERM_SYSOPHIDE | PERM_BM | PERM_ACCOUNTS |
	PERM_CHATROOM | PERM_BOARD | PERM_SYSOP | PERM_BBSADM;
    PWCU_END();
}

void pwcuInitGuestPerm	()
{
    cuser.userlevel = 0;
    cuser.uflag = BRDSORT_FLAG;
    cuser.uflag2= 0; // we don't need FAVNEW_FLAG or anything else.
    cuser.pager = PAGER_OFF;
# ifdef GUEST_DEFAULT_DBCS_NOINTRESC
    _ENABLE_BIT(cuser.uflag, DBCS_NOINTRESC);
# endif
}

#undef  DIM
#define DIM(x) (sizeof(x)/sizeof(x[0]))

void pwcuInitGuestInfo	()
{
    int i;
    char *nick[] = {
	"椰子", "貝殼", "內衣", "寶特瓶", "翻車魚",
	"樹葉", "浮萍", "鞋子", "潛水艇", "魔王",
	"鐵罐", "考卷", "大美女"
    };

    i = random() % DIM(nick);
    snprintf(cuser.nickname, sizeof(cuser.nickname),
	    "海邊漂來的%s", nick[i]);
    strlcpy(currutmp->nickname, cuser.nickname,
	    sizeof(currutmp->nickname));
    strlcpy(cuser.realname, "guest", sizeof(cuser.realname));
    memset (cuser.mind, 0, sizeof(cuser.mind));
    cuser.sex = i % 8;
}
