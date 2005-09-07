/* $Id$ */
#include "bbs.h"

char           *
genpasswd(char *pw)
{
    if (pw[0]) {
	char            saltc[2], c;
	int             i;

	i = 9 * getpid();
	saltc[0] = i & 077;
	saltc[1] = (i >> 6) & 077;

	for (i = 0; i < 2; i++) {
	    c = saltc[i] + '.';
	    if (c > '9')
		c += 7;
	    if (c > 'Z')
		c += 6;
	    saltc[i] = c;
	}
	return crypt(pw, saltc);
    }
    return "";
}

// NOTE it will clean string in "plain"
int
checkpasswd(const char *passwd, char *plain)
{
    int             ok;
    char           *pw;

    ok = 0;
    pw = crypt(plain, passwd);
    if(pw && strcmp(pw, passwd)==0)
	ok = 1;
    memset(plain, 0, strlen(plain));

    return ok;
}

/* 檢查 user 註冊情況 */
int
bad_user_id(const char *userid)
{
    int             len, i;
    len = strlen(userid);

    if (len < 2)
	return 1;

    if (not_alpha(userid[0]))
	return 1;
    for (i = 1; i < len; i++)
	/* DickG: 修正了只比較 userid 第一個字元的 bug */
	if (not_alnum(userid[i]))
	    return 1;

    if (strcasecmp(userid, str_new) == 0)
	return 1;

#ifdef NO_GUEST_ACCOUNT_REG
    if (strcasecmp(userid, STR_GUEST) == 0)
	return 1;
#endif

    /* in2: 原本是用strcasestr,
            不過有些人中間剛剛好出現這個字應該還算合理吧? */
    if( strncasecmp(userid, "fuck", 4) == 0 ||
        strncasecmp(userid, "shit", 4) == 0 )
	return 1;

    /*
     * while((ch = *(++userid))) if(not_alnum(ch)) return 1;
     */
    return 0;
}

/* -------------------------------- */
/* New policy for allocate new user */
/* (a) is the worst user currently  */
/* (b) is the object to be compared */
/* -------------------------------- */
static int
compute_user_value(const userec_t * urec, time4_t clock)
{
    int             value;

    /* if (urec) has XEMPT permission, don't kick it */
    if ((urec->userid[0] == '\0') || (urec->userlevel & PERM_XEMPT)
    /* || (urec->userlevel & PERM_LOGINOK) */
	|| !strcmp(STR_GUEST, urec->userid))
	return 999999;
    value = (clock - urec->lastlogin) / 60;	/* minutes */

    /* new user should register in 30 mins */
    if (strcmp(urec->userid, str_new) == 0)
	return 30 - value;
#if 0
    if (!urec->numlogins)	/* 未 login 成功者，不保留 */
	return -1;
    if (urec->numlogins <= 3)	/* #login 少於三者，保留 20 天 */
	return 20 * 24 * 60 - value;
#endif
    /* 未完成註冊者，保留 15 天 */
    /* 一般情況，保留 120 天 */
    return (urec->userlevel & PERM_LOGINOK ? 120 : 15) * 24 * 60 - value;
}

int
check_and_expire_account(int uid, const userec_t * urec)
{
    char            genbuf[200];
    int             val;
    if ((val = compute_user_value(urec, now)) < 0) {
	snprintf(genbuf, sizeof(genbuf), "#%d %-12s %15.15s %d %d %d",
		 uid, urec->userid, ctime4(&(urec->lastlogin)) + 4,
		 urec->numlogins, urec->numposts, val);
	if (val > -1 * 60 * 24 * 365) {
	    log_usies("CLEAN", genbuf);
            kill_user(uid, urec->userid);
	} else {
	    val = 0;
	    log_usies("DATED", genbuf);
	}
    }
    return val;
}


int
setupnewuser(const userec_t *user)
{
    char            genbuf[50];
    char           *fn_fresh = ".fresh";
    userec_t        utmp;
    time_t          clock;
    struct stat     st;
    int             fd, uid;

    clock = now;

    /* Lazy method : 先找尋已經清除的過期帳號 */
    if ((uid = searchuser("", NULL)) == 0) {
	/* 每 1 個小時，清理 user 帳號一次 */
	if ((stat(fn_fresh, &st) == -1) || (st.st_mtime < clock - 3600)) {
	    if ((fd = open(fn_fresh, O_RDWR | O_CREAT, 0600)) == -1)
		return -1;
	    write(fd, ctime(&clock), 25);
	    close(fd);
	    log_usies("CLEAN", "dated users");

	    fprintf(stdout, "尋找新帳號中, 請稍待片刻...\n\r");

	    if ((fd = open(fn_passwd, O_RDWR | O_CREAT, 0600)) == -1)
		return -1;

	    /* 不曉得為什麼要從 2 開始... Ptt:因為SYSOP在1 */
	    for (uid = 2; uid <= MAX_USERS; uid++) {
		passwd_query(uid, &utmp);
		check_and_expire_account(uid, &utmp);
	    }
	}
    }

    passwd_lock();

    uid = searchuser("", NULL);
    if ((uid <= 0) || (uid > MAX_USERS)) {
	passwd_unlock();
	vmsg("抱歉，使用者帳號已經滿了，無法註冊新的帳號");
	exit(1);
    }

    setuserid(uid, user->userid);
    snprintf(genbuf, sizeof(genbuf), "uid %d", uid);
    log_usies("APPLY", genbuf);

    SHM->money[uid - 1] = user->money;

    if (passwd_update(uid, (userec_t *)user) == -1) {
	passwd_unlock();
	vmsg("客滿了，再見！");
	exit(1);
    }

    passwd_unlock();

    return uid;
}

void
new_register(void)
{
    userec_t        newuser;
    char            passbuf[STRLEN];
    int             try, id, uid;

#ifdef HAVE_USERAGREEMENT
    more(HAVE_USERAGREEMENT, YEA);
    while( 1 ){
	getdata(b_lines - 1, 0, "請問您接受這份使用者條款嗎? (yes/no) ",
		passbuf, 4, LCECHO);
	if( passbuf[0] == 'y' )
	    break;
	if( passbuf[0] == 'n' ){
	    vmsg("抱歉, 您須要接受使用者條款才能註冊帳號享受我們的服務唷!");
	    exit(1);
	}
	vmsg("請輸入 y表示接受, n表示不接受");
    }
#endif
    memset(&newuser, 0, sizeof(newuser));
    more("etc/register", NA);
    try = 0;
    while (1) {
        userec_t xuser;
	int minute;

	if (++try >= 6) {
	    vmsg("您嘗試錯誤的輸入太多，請下次再來吧");
	    exit(1);
	}
	getdata(17, 0, msg_uid, newuser.userid,
		sizeof(newuser.userid), DOECHO);
        strcpy(passbuf, newuser.userid);

	if (bad_user_id(passbuf))
	    outs("無法接受這個代號，請使用英文字母，並且不要包含空格\n");
	else if ((id = getuser(passbuf, &xuser)) &&
		 (minute = check_and_expire_account(id, &xuser)) >= 0) {
	    if (minute == 999999) // XXX magic number
		outs("此代號已經有人使用 是不死之身");
	    else {
		prints("此代號已經有人使用 還有%d天才過期 \n", minute / (60 * 24));
	    }
	} else
	    break;
    }

    try = 0;
    while (1) {
	if (++try >= 6) {
	    vmsg("您嘗試錯誤的輸入太多，請下次再來吧");
	    exit(1);
	}
	move(18, 0); clrtoeol();
	outs(ANSI_COLOR(1;33) "為避免被偷看，您的密碼並不會顯示在畫面上，直接輸入完後按 Enter 鍵即可。" ANSI_RESET);
	if ((getdata(19, 0, "請設定密碼：", passbuf,
		     sizeof(passbuf), NOECHO) < 3) ||
	    !strcmp(passbuf, newuser.userid)) {
	    outs("密碼太簡單，易遭入侵，至少要 4 個字，請重新輸入\n");
	    continue;
	}
	strncpy(newuser.passwd, passbuf, PASSLEN);
	getdata(20, 0, "請檢查密碼：", passbuf, sizeof(passbuf), NOECHO);
	if (strncmp(passbuf, newuser.passwd, PASSLEN)) {
	    outs("密碼輸入錯誤, 請重新輸入密碼.\n");
	    continue;
	}
	passbuf[8] = '\0';
	strncpy(newuser.passwd, genpasswd(passbuf), PASSLEN);
	break;
    }
    newuser.version = PASSWD_VERSION;
    newuser.userlevel = PERM_DEFAULT;
    newuser.uflag = BRDSORT_FLAG | MOVIE_FLAG;
    newuser.uflag2 = 0;
    newuser.firstlogin = newuser.lastlogin = now;
    newuser.money = 0;
    newuser.pager = PAGER_ON;

#ifdef DBCSAWARE
    if(u_detectDBCSAwareEvilClient())
	newuser.uflag &= ~DBCSAWARE_FLAG;
    else
	newuser.uflag |= DBCSAWARE_FLAG;
#endif

    setupnewuser(&newuser);

    if( (uid = initcuser(newuser.userid)) < 0) {
	vmsg("無法建立帳號");
	exit(1);
    }
    log_usies("REGISTER", fromhost);
}


void
check_register(void)
{
    char           *ptr = NULL;

    if (HasUserPerm(PERM_LOGINOK))
	return;

    /* 
     * 避免使用者被退回註冊單後，在知道退回的原因之前，
     * 又送出一次註冊單。
     */ 
    if (currutmp->mailalert)
	m_read();

    stand_title("請詳細填寫個人資料");

    while (strlen(cuser.nickname) < 2)
	getdata(2, 0, "綽號暱稱：", cuser.nickname,
		sizeof(cuser.nickname), DOECHO);

    for (ptr = cuser.nickname; *ptr; ptr++) {
	if (*ptr == 9)		/* TAB convert */
	    *ptr = ' ';
    }
    while (strlen(cuser.realname) < 4)
	getdata(4, 0, "真實姓名：", cuser.realname,
		sizeof(cuser.realname), DOECHO);

    while (strlen(cuser.address) < 8)
	getdata(6, 0, "聯絡地址：", cuser.address,
		sizeof(cuser.address), DOECHO);


    if (!HasUserPerm(PERM_SYSOP)) {
	/* 回覆過身份認證信函，或曾經 E-mail post 過 */
	clear();
	move(9, 3);
	outs("請詳填寫" ANSI_COLOR(32) "註冊申請單" ANSI_RESET "，"
	       "通告站長以獲得進階使用權力。\n\n\n\n");
	u_register();

#ifdef NEWUSER_LIMIT
	if (cuser.lastlogin - cuser->firstlogin < 3 * 86400)
	    cuser.userlevel &= ~PERM_POST;
	more("etc/newuser", YEA);
#endif
    }
}
/* vim:sw=4
 */
