/* $Id$ */
#include "bbs.h"

#define FN_REGISTER_LOG  "register.log"	// global registration history
#define FN_REJECT_NOTIFY "justify.reject"
#define FN_NOTIN_WHITELIST_NOTICE "etc/whitemail.notice"

// Regform1 file name (deprecated)
#define fn_register	"register.new"

// New style (Regform2) file names:
#define FN_REGFORM	"regform"	// registration form in user home
#define FN_REGFORM_LOG	"regform.log"	// regform history in user home
#define FN_REQLIST	"reg.wait"	// request list file, in global directory (replacing fn_register)

#define FN_REJECT_NOTES	"etc/reg_reject.notes"

#define FN_JOBSPOOL_DIR	"jobspool/"

// #define DBG_DISABLE_CHECK	// disable all input checks
// #define DBG_DRYRUN	// Dry-run test (mainly for RegForm2)

#define MSG_ERR_MAXTRIES "您嘗試錯誤的輸入次數太多，請下次再來吧"
#define MSG_ERR_TOO_OLD  "年份可能有誤。 若這是您的真實生日請另行通知站務處理"
#define MSG_ERR_TOO_YOUNG "年份有誤。 嬰兒/未出生應該無法使用 BBS..."
#define DATE_SAMPLE	 "1911/2/29"

////////////////////////////////////////////////////////////////////////////
// Value Validation
////////////////////////////////////////////////////////////////////////////
static int 
HaveRejectStr(const char *s, const char **rej)
{
    int     i;
    char    *rejectstr[] =
	{"幹", "不", "你媽", "某", "笨", "呆", "..", "xx",
	 "你管", "管我", "猜", "天才", "超人", 
	 /* "阿",  (某些住址有) */
	 "ㄅ", "ㄆ", "ㄇ", "ㄈ", "ㄉ", "ㄊ", "ㄋ", "ㄌ", "ㄍ", "ㄎ", "ㄏ",
	 "ㄐ", "ㄑ", "ㄒ", "ㄓ", "ㄔ", "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ",
	 "ㄧ", "ㄨ", "ㄩ", "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ", "ㄡ",
	 "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ", NULL};

    if( rej != NULL )
	for( i = 0 ; rej[i] != NULL ; ++i )
	    if( DBCS_strcasestr(s, rej[i]) )
		return 1;

    for( i = 0 ; rejectstr[i] != NULL ; ++i )
	if( DBCS_strcasestr(s, rejectstr[i]) )
	    return 1;

    return 0;
}

static int
removespace(char *s)
{
    int             i, index;

    for (i = 0, index = 0; s[i]; i++) {
	if (s[i] != ' ')
	    s[index++] = s[i];
    }
    s[index] = '\0';
    return index;
}

int
reserved_user_id(const char *userid)
{
    if (file_exist_record(FN_CONF_RESERVED_ID, userid))
       return 1;
    return 0;
}

int
bad_user_id(const char *userid)
{
    if(!is_validuserid(userid))
	return 1;

#if defined(STR_REGNEW)
    if (strcasecmp(userid, STR_REGNEW) == 0)
	return 1;
#endif

#if defined(STR_GUEST) && !defined(NO_GUEST_ACCOUNT_REG)
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

static char *
isvalidname(char *rname)
{
#ifdef FOREIGN_REG
    return NULL;
#else
    const char    *rejectstr[] =
	{"肥", "胖", "豬頭", "小白", "小明", "路人", "老王", "老李", "寶貝",
	 "先生", "帥哥", "老頭", "小姊", "小姐", "美女", "小妹", "大頭", 
	 "公主", "同學", "寶寶", "公子", "大頭", "小小", "小弟", "小妹",
	 "妹妹", "嘿", "嗯", "爺爺", "大哥", "無",
	 NULL};
    if( removespace(rname) && rname[0] < 0 &&
	strlen(rname) >= 4 &&
	!HaveRejectStr(rname, rejectstr) &&
	strncmp(rname, "小", 2) != 0   && //起頭是「小」
	strncmp(rname, "我是", 4) != 0 && //起頭是「我是」
	!(strlen(rname) == 4 && strncmp(&rname[2], "兒", 2) == 0) &&
	!(strlen(rname) >= 4 && strncmp(&rname[0], &rname[2], 2) == 0))
	return NULL;
    return "您的輸入似乎不正確";
#endif

}

static char *
isvalidcareer(char *career)
{
#ifndef FOREIGN_REG
    const char    *rejectstr[] = {NULL};
    if (!(career[0] < 0 && strlen(career) >= 6) ||
	strcmp(career, "家裡") == 0 || HaveRejectStr(career, rejectstr) )
	return "您的輸入似乎不正確";
    if (strcmp(&career[strlen(career) - 2], "大") == 0 ||
	strcmp(&career[strlen(career) - 4], "大學") == 0 ||
	strcmp(career, "學生大學") == 0)
	return "麻煩請加學校系所";
    if (strcmp(career, "學生高中") == 0)
	return "麻煩輸入學校名稱";
#else
    if( strlen(career) < 6 )
	return "您的輸入不正確";
#endif
    if (DBCS_strcasestr(career, "學") && 
	DBCS_strcasestr(career, "系") &&
	DBCS_strcasestr(career, "級") == 0 && 
	(DBCS_strcasestr(career, "畢") == 0 && 
	 DBCS_strcasestr(career, "肄") == 0))
	return "請加上年級";
    return NULL;
}

static int
strlen_without_space(const char *s)
{
    int i = 0;
    while (*s)
	if (*s++ != ' ') i++;
    return i;
}

static char *
isvalidaddr(char *addr)
{
    const char    *rejectstr[] =
	{"地球", "銀河", "火星", NULL};

#ifdef DBG_DISABLE_CHECK
    return NULL;
#endif // DBG_DISABLE_CHECK

    // addr[0] > 0: check if address is starting by Chinese.
    if (DBCS_strcasestr(addr, "信箱") != 0 || DBCS_strcasestr(addr, "郵政") != 0) 
	return "抱歉我們不接受郵政信箱";
    if (strlen_without_space(addr) < 15 ||
	(DBCS_strcasestr(addr, "市") == 0 && 
	 DBCS_strcasestr(addr, "巿") == 0 &&
	 DBCS_strcasestr(addr, "縣") == 0 && 
	 DBCS_strcasestr(addr, "室") == 0) ||
	strcmp(&addr[strlen(addr) - 2], "段") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "路") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "巷") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "弄") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "區") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "市") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "街") == 0 )
	return "這個地址似乎並不完整";
    if (HaveRejectStr(addr, rejectstr))
	return "這個地址似乎有誤";
    return NULL;
}

static char *
isvalidphone(char *phone)
{
    int     i;

#ifdef DBG_DISABLE_CHECK
    return NULL;
#endif // DBG_DISABLE_CHECK

    for( i = 0 ; phone[i] != 0 ; ++i )
	if( !isdigit((int)phone[i]) )
	    return "請不要加分隔符號";
    if (!removespace(phone) || 
	strlen(phone) < 9 || 
	strstr(phone, "00000000") != NULL ||
	strstr(phone, "22222222") != NULL    ) {
	return "這個電話號碼並不正確(請含區碼)" ;
    }
    return NULL;
}


////////////////////////////////////////////////////////////////////////////
// Account Expiring
////////////////////////////////////////////////////////////////////////////

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

#ifdef STR_REGNEW
    /* new user should register in 30 mins */
    // XXX 目前 new acccount 並不會在 utmp 裡放 STR_REGNEW...
    if (strcmp(urec->userid, STR_REGNEW) == 0)
	return 30 - value;
#endif

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
check_and_expire_account(int uid, const userec_t * urec, int expireRange)
{
    char            genbuf[200];
    int             val;
    if ((val = compute_user_value(urec, now)) < 0) {
	snprintf(genbuf, sizeof(genbuf), "#%d %-12s %s %d %d %d",
		 uid, urec->userid, Cdatelite(&(urec->lastlogin)),
		 urec->numlogins, urec->numposts, val);

	// 若超過 expireRange 則砍人，
	// 不然就 return 0
	if (-val > expireRange)
	{
	    log_usies("DATED", genbuf);
	    // log_usies("CLEAN", genbuf);
	    kill_user(uid, urec->userid);
	} else val = 0;
    }
    return val;
}

////////////////////////////////////////////////////////////////////////////
// Regcode Support
////////////////////////////////////////////////////////////////////////////

#define REGCODE_INITIAL "v6" // always 2 characters

static char *
getregfile(char *buf)
{
    // not in user's home because s/he could zip his/her home
    snprintf(buf, PATHLEN, FN_JOBSPOOL_DIR ".regcode.%s", cuser.userid);
    return buf;
}

static char *
makeregcode(char *buf)
{
    char    fpath[PATHLEN];
    int     fd, i;
    // prevent ambigious characters: oOlI
    const char *alphabet = "qwertyuipasdfghjkzxcvbnmoQWERTYUPASDFGHJKLZXCVBNM";

    /* generate a new regcode */
    buf[13] = 0;
    buf[0] = REGCODE_INITIAL[0];
    buf[1] = REGCODE_INITIAL[1];
    for( i = 2 ; i < 13 ; ++i )
	buf[i] = alphabet[random() % strlen(alphabet)];

    getregfile(fpath);
    if( (fd = open(fpath, O_WRONLY | O_CREAT, 0600)) == -1 ){
	perror("open");
	exit(1);
    }
    write(fd, buf, 13);
    close(fd);

    return buf;
}

static char *
getregcode(char *buf)
{
    int     fd;
    char    fpath[PATHLEN];

    getregfile(fpath);
    if( (fd = open(fpath, O_RDONLY)) == -1 ){
	buf[0] = 0;
	return buf;
    }
    read(fd, buf, 13);
    close(fd);
    buf[13] = 0;
    return buf;
}

void
delregcodefile(void)
{
    char    fpath[PATHLEN];
    getregfile(fpath);
    unlink(fpath);
}


////////////////////////////////////////////////////////////////////////////
// Figlet Captcha System
////////////////////////////////////////////////////////////////////////////
#ifdef USE_FIGLET_CAPTCHA

static int
gen_captcha(char *buf, int szbuf, char *fpath)
{
    // do not use: GQV
    const char *alphabet = "ABCDEFHIJKLMNOPRSTUWXYZ";
    int calphas = strlen(alphabet);
    char cmd[PATHLEN], opts[PATHLEN];
    int i, coptSpace, coptFont;
    static const char *optSpace[] = {
	"-S", "-s", "-k", "-W", 
	// "-o",
	NULL
    };
    static const char *optFont[] = {
	"banner", "big", "slant", "small",
	"smslant", "standard",
	// block family
	"block", "lean",
	// shadow family
	// "shadow", "smshadow",
	// mini (better with large spacing)
	// "mini", 
	NULL
    };

    // fill captcha code
    for (i = 0; i < szbuf-1; i++)
	buf[i] = alphabet[random() % calphas];
    buf[i] = 0; // szbuf-1

    // decide options
    coptSpace = coptFont = 0;
    while (optSpace[coptSpace]) coptSpace++;
    while (optFont[coptFont]) coptFont++;
    snprintf(opts, sizeof(opts),
	    "%s -f %s", 
	    optSpace[random() % coptSpace],
	    optFont [random() % coptFont ]);

    // create file
    snprintf(fpath, PATHLEN, FN_JOBSPOOL_DIR ".captcha.%s", buf);
    snprintf(cmd, sizeof(cmd), FIGLET_PATH " %s %s > %s",
	    opts, buf, fpath);

    // vmsg(cmd);
    if (system(cmd) != 0)
	return 0;

    return 1;
}


static int
_vgetcb_data_upper(int key, VGET_RUNTIME *prt, void *instance)
{
    if (key >= 'a' && key <= 'z')
	key = toupper(key);
    if (key < 'A' || key > 'Z')
    {
	bell();
	return VGETCB_NEXT;
    }
    return VGETCB_NONE;
}

static int
_vgetcb_data_post(int key, VGET_RUNTIME *prt, void *instance)
{
    char *s = prt->buf;
    while (*s)
    {
	if (isascii(*s) && islower(*s))
	    *s = toupper(*s);
	s++;
    }
    return VGETCB_NONE;
}


static int verify_captcha()
{
    char captcha[7] = "", code[STRLEN];
    char fpath[PATHLEN];
    VGET_CALLBACKS vge = { NULL, _vgetcb_data_upper, _vgetcb_data_post };
    int tries = 0, i;

    do {
	// create new captcha
	if (tries % 2 == 0 || !captcha[0])
	{
	    // if generation failed, skip captcha.
	    if (!gen_captcha(captcha, sizeof(captcha), fpath) ||
		    !dashf(fpath))
		return 1;

	    // prompt user about captcha
	    vs_hdr("CAPTCHA 認證程序");
	    outs("為了確認您的註冊程序，請輸入下面圖樣顯示的文字。\n"
		    "圖樣只會由大寫的 A-Z 英文字母組成。\n\n");
	    show_file(fpath, 4, b_lines-5, SHOWFILE_ALLOW_ALL);
	    unlink(fpath);
	}

	// each run waits 10 seconds.
	for (i = 10; i > 0; i--)
	{
	    move(b_lines-1, 0); clrtobot();
	    prints("請仔細檢查上面的圖形， %d 秒後即可輸入...", i);
	    // flush out current input
	    doupdate(); 
	    peek_input(0.1f, Ctrl('C'));
	    drop_input();
	    sleep(1);
	}

	// input captcha
	move(b_lines-1, 0); clrtobot();
	prints("請輸入圖樣顯示的 %d 個英文字母: ", (int)strlen(captcha));
	vgetstring(code, strlen(captcha)+1, 0, "", &vge, NULL);

	if (code[0] && strcasecmp(code, captcha) == 0)
	    break;

	// error case.
	if (++tries >= 10)
	    return 0;

	// error
	vmsg("輸入錯誤，請重試。注意組成文字全是大寫英文字母。");

    } while (1);

    clear();
    return 1;
}
#else // !USE_FIGLET_CAPTCHA

static int 
verify_captcha()
{
    return 1;
}

#endif // !USE_FIGLET_CAPTCHA

////////////////////////////////////////////////////////////////////////////
// Justify Utilities
////////////////////////////////////////////////////////////////////////////

static void 
email_justify(const userec_t *muser)
{
	char buf[256], genbuf[256];
	/* 
	 * It is intended to use BBSENAME instead of BBSNAME here.
	 * Because recently many poor users with poor mail clients
	 * (or evil mail servers) cannot handle/decode Chinese 
	 * subjects (BBSNAME) correctly, so we'd like to use 
	 * BBSENAME here to prevent subject being messed up.
	 * And please keep BBSENAME short or it may be truncated
	 * by evil mail servers.
	 */
	snprintf(buf, sizeof(buf),
		 " " BBSENAME " - [ %s ]", makeregcode(genbuf));

#ifdef HAVEMOBILE
	if (strcmp(muser->email, "m") == 0 || strcmp(muser->email, "M") == 0)
	    mobile_message(mobile, buf);
	else
#endif
	    bsmtp("etc/registermail", buf, muser->email, "non-exist");
        move(20,0);
        clrtobot();
	outs("我們即將寄出認證信 (您應該會在 10 分鐘內收到)\n"
	     "收到後您可以根據認證信標題的認證碼\n"
	     "輸入到 (U)ser -> (R)egister 後就可以完成註冊");
	pressanykey();
	return;
}


/* 使用者填寫註冊表格 */
static void
getfield(int line, const char *info, const char *desc, char *buf, int len)
{
    char            prompt[STRLEN];
    char            genbuf[200];

    // clear first
    move(line+1, 0); clrtoeol();
    move(line, 0); clrtoeol();
    prints("  原先設定：%-30.30s (%s)", buf, info);
    snprintf(prompt, sizeof(prompt), "  %s：", desc);
    if (getdata_str(line + 1, 0, prompt, genbuf, len, DOECHO, buf))
	strcpy(buf, genbuf);
    move(line+1, 0); clrtoeol();
    move(line, 0); clrtoeol();
    prints("  %s：%s", desc, buf);
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

    // XXX race condition...
    if (dosearchuser(user->userid, NULL))
    {
	vmsg("手腳不夠快，別人已經搶走了！");
	exit(1);
    }

    /* Lazy method : 先找尋已經清除的過期帳號 */
    if ((uid = dosearchuser("", NULL)) == 0) {
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
		passwd_sync_query(uid, &utmp);
		// tolerate for one year.
		check_and_expire_account(uid, &utmp, 365*12*60);
	    }
	}
    }

    /* initialize passwd semaphores */
    if (passwd_init())
	exit(1);

    passwd_lock();

    uid = dosearchuser("", NULL);
    if ((uid <= 0) || (uid > MAX_USERS)) {
	passwd_unlock();
	vmsg("抱歉，使用者帳號已經滿了，無法註冊新的帳號");
	exit(1);
    }

    setuserid(uid, user->userid);
    snprintf(genbuf, sizeof(genbuf), "uid %d", uid);
    log_usies("APPLY", genbuf);

    SHM->money[uid - 1] = user->money;

    if (passwd_sync_update(uid, (userec_t *)user) == -1) {
	passwd_unlock();
	vmsg("客滿了，再見！");
	exit(1);
    }

    passwd_unlock();

    return uid;
}

/////////////////////////////////////////////////////////////////////////////
// New Registration (Phase 1: Create Account)
/////////////////////////////////////////////////////////////////////////////

void
new_register(void)
{
    userec_t        newuser;
    char            passbuf[STRLEN];
    int             try, id, uid;
    char 	   *errmsg = NULL;

#ifdef HAVE_USERAGREEMENT
    int haveag = more(HAVE_USERAGREEMENT, YEA);
    while( haveag != -1 ){
	int c = vans("請問您接受這份使用者條款嗎? (yes/no) ");
	if (c == 'y')
	    break;
	else if (c == 'n')
	{
	    vmsg("抱歉, 您須要接受使用者條款才能註冊帳號享受我們的服務唷!");
	    exit(1);
	}
	vmsg("請輸入 y表示接受, n表示不接受");
    }
#endif

    // setup newuser
    memset(&newuser, 0, sizeof(newuser));
    newuser.version = PASSWD_VERSION;
    newuser.userlevel = PERM_DEFAULT;
    newuser.uflag = BRDSORT_FLAG | MOVIE_FLAG;
    newuser.uflag2 = 0;
    newuser.firstlogin = newuser.lastlogin = now;
    newuser.pager = PAGER_ON;
    strlcpy(newuser.lasthost, fromhost, sizeof(newuser.lasthost));

#ifdef DBCSAWARE
    if(u_detectDBCSAwareEvilClient())
	newuser.uflag &= ~DBCSAWARE_FLAG;
    else
	newuser.uflag |= DBCSAWARE_FLAG;
#endif

    more("etc/register", NA);
    try = 0;
    while (1) {
        userec_t xuser;
	int minute;

	if (++try >= 6) {
	    vmsg(MSG_ERR_MAXTRIES);
	    exit(1);
	}
	getdata(17, 0, msg_uid, newuser.userid,
		sizeof(newuser.userid), DOECHO);
        strcpy(passbuf, newuser.userid);

	if (bad_user_id(passbuf))
	    outs("無法接受這個代號，請使用英文字母，並且不要包含空格\n");
	else if ((id = getuser(passbuf, &xuser)) &&
		// >=: see check_and_expire_account definition
		 (minute = check_and_expire_account(id, &xuser, 0)) >= 0) 
	{
	    if (minute == 999999) // XXX magic number.  It should be greater than MAX_USERS at least.
		outs("此代號已經有人使用 是不死之身");
	    else {
		prints("此代號已經有人使用 還有 %d 天才過期 \n", 
			minute / (60 * 24) + 1);
	    }
	} 
	else if (reserved_user_id(passbuf))
	    outs("此代號已由系統保留，請使用別的代號\n");
	else // success
	    break;
    }

    // XXX 記得最後 create account 前還要再檢查一次 acc

    try = 0;
    while (1) {
	if (++try >= 6) {
	    vmsg(MSG_ERR_MAXTRIES);
	    exit(1);
	}
	move(20, 0); clrtoeol();
	outs(ANSI_COLOR(1;33) 
    "為避免被偷看，您的密碼並不會顯示在畫面上，直接輸入完後按 Enter 鍵即可。\n"
    "另外請注意密碼只有前八個字元有效，超過的將自動忽略。"
	ANSI_RESET);
	if ((getdata(18, 0, "請設定密碼：", passbuf,
		     sizeof(passbuf), NOECHO) < 3) ||
	    !strcmp(passbuf, newuser.userid)) {
	    outs("密碼太簡單，易遭入侵，至少要 4 個字，請重新輸入\n");
	    continue;
	}
	strlcpy(newuser.passwd, passbuf, PASSLEN);
	getdata(19, 0, "請檢查密碼：", passbuf, sizeof(passbuf), NOECHO);
	if (strncmp(passbuf, newuser.passwd, PASSLEN)) {
	    move(19, 0);
	    outs("密碼輸入錯誤, 請重新輸入密碼.\n");
	    continue;
	}
	passbuf[8] = '\0';
	strlcpy(newuser.passwd, genpasswd(passbuf), PASSLEN);
	break;
    }
    // set-up more information.
    move(19, 0); clrtobot();

    // warning: because currutmp=NULL, we can simply pass newuser.* to getdata.
    // DON'T DO THIS IF YOUR currutmp != NULL.
    try = 0;
    while (strlen(newuser.nickname) < 2)
    {
	if (++try > 10) {
	    vmsg(MSG_ERR_MAXTRIES);
	    exit(1);
	}
	getdata(19, 0, "綽號暱稱：", newuser.nickname,
		sizeof(newuser.nickname), DOECHO);
    }

    try = 0;
    while (strlen(newuser.realname) < 4)
    {
	if (++try > 10) {
	    vmsg(MSG_ERR_MAXTRIES);
	    exit(1);
	}
	getdata(20, 0, "真實姓名：", newuser.realname,
		sizeof(newuser.realname), DOECHO);

	if ((errmsg = isvalidname(newuser.realname)))
	{
	    memset(newuser.realname, 0, sizeof(newuser.realname));
	    vmsg(errmsg); 
	}
    }

    try = 0;
    while (strlen(newuser.address) < 8)
    {
	// do not use isvalidaddr to check,
	// because that requires foreign info.
	if (++try > 10) {
	    vmsg(MSG_ERR_MAXTRIES);
	    exit(1);
	}
	getdata(21, 0, "聯絡地址：", newuser.address,
		sizeof(newuser.address), DOECHO);
    }

    try = 0;
    while (newuser.year < 40) // magic number 40: see user.c
    {
	char birthday[sizeof("mmmm/yy/dd ")];
	int y, m, d;
	struct tm tm;

	localtime4_r(&now, &tm);

	if (++try > 20) {
	    vmsg(MSG_ERR_MAXTRIES);
	    exit(1);
	}
	getdata(22, 0, "生日 (西元年/月/日, 如 " DATE_SAMPLE ")：", birthday,
		sizeof(birthday), DOECHO);

	if (strcmp(birthday, DATE_SAMPLE) == 0) {
	    vmsg("不要複製範例！ 請輸入你真實生日");
	    continue;
	}

	if (ParseDate(birthday, &y, &m, &d)) {
	    vmsg("日期格式不正確");
	    continue;
	} else if (y < 1930) {
	    vmsg(MSG_ERR_TOO_OLD);
	    continue;
	} else if (y+3 > tm.tm_year+1900) {
	    vmsg(MSG_ERR_TOO_YOUNG);
	    continue;
	}
	newuser.year  = (unsigned char)(y-1900);
	newuser.month = (unsigned char)m;
	newuser.day   = (unsigned char)d;
    }

    if (!verify_captcha())
    {
	vmsg(MSG_ERR_MAXTRIES);
	exit(1);
    }

    setupnewuser(&newuser);

    if( (uid = initcuser(newuser.userid)) < 0) {
	vmsg("無法建立帳號");
	exit(1);
    }
    log_usies("REGISTER", fromhost);
}

int
check_regmail(char *email)
{
    FILE           *fp;
    char            buf[128], *c;
    int allow = 0;

    c = strchr(email, '@');
    if (c == NULL) return 0;

    // reject multiple '@'
    if (c != strrchr(email, '@')) 
    {
	vmsg("E-Mail 的格式不正確。");
	return 0;
    }

    // domain tolower
    str_lower(c, c);

    // allow list
    allow = 0;
    if ((fp = fopen("etc/whitemail", "rt")))
    {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (buf[0] == '#')
		continue;
	    chomp(buf);
	    c = buf+1;
	    // vmsgf("%c %s %s",buf[0], c, email);
	    switch(buf[0])
	    {
		case 'A': if (strcasecmp(c, email) == 0)	allow = 1; break;
		case 'P': if (strcasestr(email, c))	allow = 1; break;
		case 'S': if (strcasecmp(strstr(email, "@") + 1, c) == 0) allow = 1; break;
		case '%': allow = 1; break; // allow all
	        // domain match (*@c | *@*.c)
		case 'D': if (strlen(email) > strlen(c))
			  {
			      // c2 points to starting of possible c.
			      const char *c2 = email + strlen(email) - strlen(c);
			      if (strcasecmp(c2, c) != 0)
				  break;
			      c2--;
			      if (*c2 == '.' || *c2 == '@')
				  allow = 1;
			  }
			  break;
	    }
	    if (allow) break;
	}
	fclose(fp);
	if (!allow) 
	{
	    // show whitemail notice if it exists.
	    if (dashf(FN_NOTIN_WHITELIST_NOTICE))
	    {
		VREFSCR scr = vscr_save();
		more(FN_NOTIN_WHITELIST_NOTICE, NA);
		pressanykey();
		vscr_restore(scr);
	    } else vmsg("抱歉，目前不接受此 Email 的註冊申請。");
	    return 0;
	}
    }

    // reject list
    allow = 1;
    if ((fp = fopen("etc/banemail", "r"))) {
	while (allow && fgets(buf, sizeof(buf), fp)) {
	    if (buf[0] == '#')
		continue;
	    chomp(buf);
	    c = buf+1;
	    switch(buf[0])
	    {
		case 'A': if (strcasecmp(c, email) == 0)
			  {
			      allow = 0;
			      // exact match
			      vmsg("此電子信箱已被禁止註冊");
			  }
			  break;
		case 'P': if (strcasestr(email, c))
			  {
			      allow = 0;
			      vmsg("此信箱已被禁止用於註冊 (可能是免費信箱)");
			  }
			  break;
		case 'S': if (strcasecmp(strstr(email, "@") + 1, c) == 0)
			  {
			      allow = 0;
			      vmsg("此信箱已被禁止用於註冊 (可能是免費信箱)");
			  }
			  break;
		case 'D': if (strlen(email) > strlen(c))
			  {
			      // c2 points to starting of possible c.
			      const char *c2 = email + strlen(email) - strlen(c);
			      if (strcasecmp(c2, c) != 0)
				  break;
			      c2--;
			      if (*c2 == '.' || *c2 == '@')
			      {
				  vmsg("此信箱的網域已被禁止用於註冊 (可能是免費信箱)");
				  allow = 0;
			      }
			  }
			  break;
	    }
	}
	fclose(fp);
    }
    return allow;
}

void 
check_birthday(void)
{
    // check birthday
    int changed = 0;
    time_t t = (time_t)now;
    struct tm tm;

    localtime_r(&t, &tm);
    while ( cuser.year < 40 || // magic number 40: see user.c
	    cuser.year+3 > tm.tm_year) 
    {
	char birthday[sizeof("mmmm/yy/dd ")];
	int y, m, d;

	clear();
	vs_hdr("輸入生日");
	move(2,0);
	outs("本站為配合實行內容分級制度，請您輸入正確的生日資訊。");

	getdata(5, 0, "生日 (西元年/月/日, 如 " DATE_SAMPLE ")：", birthday,
		sizeof(birthday), DOECHO);

	if (strcmp(birthday, DATE_SAMPLE) == 0) {
	    vmsg("不要複製範例！ 請輸入你真實生日");
	    continue;
	} 
	if (ParseDate(birthday, &y, &m, &d)) {
	    vmsg("日期格式不正確");
	    continue;
	} else if (y < 1930) {
	    vmsg(MSG_ERR_TOO_OLD);
	    continue;
	} else if (y+3 > tm.tm_year+1900) {
	    vmsg(MSG_ERR_TOO_YOUNG);
	    continue;
	}

	cuser.year  = (unsigned char)(y-1900);
	cuser.month = (unsigned char)m;
	cuser.day   = (unsigned char)d;
	changed = 1;
    }

    if (changed) {
	clear();
	resolve_over18();
    }
}

/////////////////////////////////////////////////////////////////////////////
// User Registration (Phase 2: Validation)
/////////////////////////////////////////////////////////////////////////////

void
check_register(void)
{
    char fn[PATHLEN];

    // 已經通過的就不用了
    if (HasUserPerm(PERM_LOGINOK) || HasUserPerm(PERM_SYSOP))
	return;

    // 基本權限被拔應該是要讓他不能註冊用。
    if (!HasUserPerm(PERM_BASIC))
	return;

    /* 
     * 避免使用者被退回註冊單後，在知道退回的原因之前，
     * 又送出一次註冊單。
     */ 
    setuserfile(fn, FN_REJECT_NOTIFY);
    if (dashf(fn))
    {
	int xun = 0, abort = 0;
	userec_t u = {0};
	char buf[PATHLEN] = "";
	FILE *fp = fopen(fn, "rt");

	// load reference
	fgets(buf, sizeof(buf), fp);
	fclose(fp);

	// parse reference
	if (buf[0] == '#')
	{
	    xun = atoi(buf+1);
	    if (xun <= 0 || xun > MAX_USERS ||
		passwd_sync_query(xun, &u) < 0 ||
		!(u.userlevel & (PERM_ACCOUNTS | PERM_ACCTREG)))
		memset(&u, 0, sizeof(u));
	    // now u is valid only if reference is loaded with account sysop.
	}

	// show message.
	more(fn, YEA);
	move(b_lines-4, 0); clrtobot();
	outs("\n" ANSI_COLOR(1;31) 
	     "前次註冊單審查失敗。 (本記錄已備份於您的信箱中)\n"
	     "請重新申請並照上面指示正確填寫註冊單。\n");

	if (u.userid[0])
	    outs("如有任何問題或需要與站務人員聯絡請按 r 回信。");

	outs(ANSI_RESET "\n");


	// force user to confirm.
	while (!abort)
	{
	    switch(vans(u.userid[0] ?
		    "請輸入 y 繼續或輸入 r 回信給站務: " : 
		    "請輸入 y 繼續: "))
	    {
		case 'y':
		    abort = 1;
		    break;

		case 'r':
		    if (!u.userid[0])
			break;

		    // mail to user
		    setuserfile(quote_file, FN_REJECT_NOTIFY);
		    strlcpy(quote_user, "[退註通知]", sizeof(quote_user));
		    clear();
		    do_innersend(u.userid, NULL, "[註冊問題] 退註相關問題", NULL);
		    abort = 1;
		    // quick return to avoid confusing user
		    unlink(fn);
		    return;
		    break;

		default:
		    bell();
		    break;
	    }
	}

	unlink(fn);
    } 

    /* 回覆過身份認證信函，或曾經 E-mail post 過 */
    clear();
    move(9, 0);

    // 無法使用註冊碼 = 被退註的，提示一下?
    // (u_register 裡面會 vmsg)
    if (HasUserPerm(PERM_NOREGCODE))
    {
    }

    outs("  您目前尚未通過註冊認證程序，請細詳填寫" 
	    ANSI_COLOR(32) "註冊申請單" ANSI_RESET "，\n"
	 "  通告站長以獲得進階使用權力。\n\n");

    outs("  如果您之前曾使用 email 等認證方式通過註冊認證但又看到此訊息，\n"
	 "  代表您的認證由於資料不完整已被取消。\n");

    u_register();

#ifdef NEWUSER_LIMIT
    if (cuser.lastlogin - cuser->firstlogin < 3 * DAY_SECONDS)
	cuser.userlevel &= ~PERM_POST;
    more("etc/newuser", YEA);
#endif
}

static int
create_regform_request()
{
    FILE *fn;

    char fname[PATHLEN];
    setuserfile(fname, FN_REGFORM);
    fn = fopen(fname, "wt");	// regform 2: replace model

    if (!fn)
	return 0;

    // create request data
    fprintf(fn, "uid: %s\n",    cuser.userid);
    fprintf(fn, "name: %s\n",   cuser.realname);
    fprintf(fn, "career: %s\n", cuser.career);
    fprintf(fn, "addr: %s\n",   cuser.address);
    fprintf(fn, "phone: %s\n",  cuser.phone);
    fprintf(fn, "email: %s\n",  "x"); // email is apparently 'x' here.
    fprintf(fn, "----\n");
    fclose(fn);

    // regform2 must update request list
    file_append_record(FN_REQLIST, cuser.userid);

    // save justify information
    snprintf(cuser.justify, sizeof(cuser.justify),
	    "<Manual>");
    return 1;
}

static void
toregister(char *email)
{
    clear();
    vs_hdr("認證設定");
    if (cuser.userlevel & PERM_NOREGCODE){
	strcpy(email, "x");
	goto REGFORM2;
    }
    move(1, 0);
    outs("您好, 本站認證認證的方式有:\n"
	 "  1.若您有 E-Mail  (本站不接受 yahoo, kimo等免費的 E-Mail)\n"
	 "    請輸入您的 E-Mail , 我們會寄發含有認證碼的信件給您\n"
	 "    收到後請到 (U)ser => (R)egister 輸入認證碼, 即可通過認證\n"
	 "\n"
	 "  2.若您沒有 E-Mail 或是一直無法收到認證信, 請輸入 x \n"
	 "  會有站長親自人工審核註冊資料，" ANSI_COLOR(1;33)
	   "但注意這可能會花上數週或更多時間。" ANSI_RESET "\n"
	 "**********************************************************\n"
	 "* 注意!                                                  *\n"
	 "* 通常應該會在輸入完成後十分鐘內收到認證信, 若過久未收到 *\n"
	 "* 請到郵件垃圾桶檢查是否被當作垃圾信(SPAM)了，另外若是   *\n"
	 "* 輸入後發生認證碼錯誤請重填一次 E-Mail                  *\n"
	 "**********************************************************\n");

#ifdef HAVEMOBILE
    outs("  3.若您有手機門號且想採取手機簡訊認證的方式 , 請輸入 m \n"
	 "    我們將會寄發含有認證碼的簡訊給您 \n"
	 "    收到後請到(U)ser => (R)egister 輸入認證碼, 即可通過認證\n");
#endif

    while (1) {
	email[0] = 0;
	getfield(15, "身分認證用", "E-Mail Address", email, 50);
	strip_blank(email, email);
	if (strcmp(email, "X") == 0) email[0] = 'x';
	if (strcmp(email, "x") == 0)
	    break;
#ifdef HAVEMOBILE
	else if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0) {
	    if (isvalidmobile(mobile)) {
		char            yn[3];
		getdata(16, 0, "請再次確認您輸入的手機號碼正確嘛? [y/N]",
			yn, sizeof(yn), LCECHO);
		if (yn[0] == 'y')
		    break;
	    } else {
		move(15, 0); clrtobot();
		move(17, 0);
		outs("指定的手機號碼不正確,"
		       "若您無手機門號請選擇其他方式認證");
	    }

	}
#endif
	else if (check_regmail(email)) {
	    char            yn[3];
#ifdef USE_EMAILDB
	    int email_count;

	    // before long waiting, alert user
	    move(18, 0); clrtobot();
	    outs("正在確認 email, 請稍候...\n");
	    doupdate();
	    
	    email_count = emaildb_check_email(email, strlen(email));

	    if (email_count < 0) {
		move(15, 0); clrtobot();
		move(17, 0);
		outs("email 認證系統發生問題, 請稍後再試，或輸入 x 採手動認證。\n");
		pressanykey();
		return;
	    } else if (email_count >= EMAILDB_LIMIT) { 
		move(15, 0); clrtobot();
		move(17, 0);
		outs("指定的 E-Mail 已註冊過多帳號, 請使用其他 E-Mail, 或輸入 x 採手動認證\n");
		outs("但注意手動認證通常會花上數週以上的時間。\n");
	    } else {
#endif
	    move(17, 0);
	    outs(ANSI_COLOR(1;31) 
	    "\n提醒您: 如果之後發現您輸入的註冊資料有問題，不僅註冊會被取消，\n"
	    "原本認證用的 E-mail 也不能再用來認證。\n" ANSI_RESET);
	    getdata(16, 0, "請再次確認您輸入的 E-Mail 位置正確嘛? [y/N]",
		    yn, sizeof(yn), LCECHO);
	    clrtobot();
	    if (yn[0] == 'y')
		break;
#ifdef USE_EMAILDB
	    }
#endif
	} else {
	    move(15, 0); clrtobot();
	    move(17, 0);
	    outs("指定的 E-Mail 不正確。可能你輸入的是免費的Email，\n");
	    outs("或曾有使用者以本 E-Mail 認證後被取消資格。\n\n");
	    outs("若您無 E-Mail 請輸入 x 由站長手動認證，\n");
	    outs("但注意手動認證通常會花上數週以上的時間。\n");
	}
    }
#ifdef USE_EMAILDB
    // XXX for 'x', the check will be made later... let's simply ignore it.
    if (strcmp(email, "x") != 0 &&
	emaildb_update_email(cuser.userid, strlen(cuser.userid), email, strlen(email)) < 0) {
	move(15, 0); clrtobot();
	move(17, 0);
	outs("email 認證系統發生問題, 請稍後再試，或輸入 x 採手動認證。\n");
	pressanykey();
	return;
    }
#endif
    strlcpy(cuser.email, email, sizeof(cuser.email));
 REGFORM2:
    if (strcasecmp(email, "x") == 0) {	/* 手動認證 */
	if (!create_regform_request())
	{
	    vmsg("註冊申請單建立失敗。請至 " BN_BUGREPORT " 報告。");
	}
    } else {
	// register by mail or mobile
	snprintf(cuser.justify, sizeof(cuser.justify), "<Email>");
#ifdef HAVEMOBILE
	if (phone != NULL && email[1] == 0 && tolower(email[0]) == 'm')
	    snprintf(cuser.justify, sizeof(cuser.justify),
		    "<Mobile>");
#endif
       email_justify(&cuser);
    }
}


int
u_register(void)
{
    char            rname[20], addr[50], mobile[16];
#ifdef FOREIGN_REG
    char            fore[2];
#endif
    char            phone[20], career[40], email[50], birthday[11], sex_is[2];
    unsigned char   year, mon, day;
    char            inregcode[14], regcode[50];
    char            ans[3], *errcode;
    int		    i = 0;

    if (cuser.userlevel & PERM_LOGINOK) {
	outs("您的身份確認已經完成，不需填寫申請表");
	return XEASY;
    }

    // TODO REGFORM 2 checks 2 parts.
    i = file_find_record(FN_REQLIST, cuser.userid);

    if (i > 0)
    {
	vs_hdr("註冊單尚在處理中");
	move(3, 0);
	prints("   您的註冊申請單尚在處理中(處理順位: %d)，請耐心等候\n\n", i);
	outs("   * 如果您之前曾使用 email 等認證方式通過註冊認證但又看到此訊息，\n"
	     "     代表您的認證由於資料不完整已被取消。\n\n"
	     "   * 如果您已收到註冊碼卻看到這個畫面，代表您在使用 Email 註冊後\n"
	     "     " ANSI_COLOR(1;31) "又另外申請了站長直接人工審核的註冊申請單。" 
		ANSI_RESET "\n"
	     "     進入人工審核程序後 Email 註冊碼自動失效，要等到審核完成\n"
	     "      (會多花很多時間，數天到數週是正常的) ，所以請耐心等候。\n\n");
	vmsg("您的註冊申請單尚在處理中");
	return FULLUPDATE;
    }

    strlcpy(rname, cuser.realname, sizeof(rname));
    strlcpy(addr,  cuser.address,  sizeof(addr));
    strlcpy(email, cuser.email,    sizeof(email));
    strlcpy(career,cuser.career,   sizeof(career));
    strlcpy(phone, cuser.phone,    sizeof(phone));

    if (cuser.mobile)
	snprintf(mobile, sizeof(mobile), "0%09d", cuser.mobile);
    else
	mobile[0] = 0;

    if (cuser.month == 0 && cuser.day == 0 && cuser.year == 0)
	birthday[0] = 0;
    else
	snprintf(birthday, sizeof(birthday), "%04i/%02i/%02i",
		 1900 + cuser.year, cuser.month, cuser.day);

    sex_is[0] = (cuser.sex % 8) + '1';
    sex_is[1] = 0;

    if (cuser.userlevel & PERM_NOREGCODE) {
	vmsg("您不被允許\使用認證碼認證。請填寫註冊申請單");
	goto REGFORM;
    }

    // getregcode(regcode);

    // XXX why check by year? 
    // birthday is moved to earlier, so let's check email instead.
    if (cuser.email[0] && // cuser.year != 0 &&	/* 已經第一次填過了~ ^^" */
	strcmp(cuser.email, "x") != 0 &&	/* 上次手動認證失敗 */
	strcmp(cuser.email, "X") != 0) 
    {
	vs_hdr("EMail認證");
	move(2, 0);

	prints("請輸入您的認證碼。(由 %s 開頭無空白的十三碼)\n"
	       "或輸入 x 來重新填寫 E-Mail 或改由站長手動認證\n", REGCODE_INITIAL);
	inregcode[0] = 0;

	do{
	    getdata(10, 0, "您的認證碼：",
		    inregcode, sizeof(inregcode), DOECHO);
	    if( strcmp(inregcode, "x") == 0 || strcmp(inregcode, "X") == 0 )
		break;
	    if( strlen(inregcode) != 13 || inregcode[0] == ' ')
		vmsg("認證碼輸入不完整，總共應有十三碼，沒有空白字元。");
	    else if( inregcode[0] != REGCODE_INITIAL[0] || inregcode[1] != REGCODE_INITIAL[1] ) {
		/* old regcode */
		vmsg("輸入的認證碼錯誤，" // "或因系統昇級已失效，"
		     "請輸入 x 重填一次 E-Mail");
	    }
	    else
		break;
	} while( 1 );

	// make it case insensitive.
	if (strcasecmp(inregcode, getregcode(regcode)) == 0) {
	    int             unum;
	    delregcodefile();
	    if ((unum = searchuser(cuser.userid, NULL)) == 0) {
		vmsg("系統錯誤，查無此人！");
		u_exit("getuser error");
		exit(0);
	    }
	    mail_muser(cuser, "[註冊成功\囉]", "etc/registeredmail");
#if FOREIGN_REG_DAY > 0
	    if(cuser.uflag2 & FOREIGN)
		mail_muser(cuser, "[出入境管理局]", "etc/foreign_welcome");
#endif
	    cuser.userlevel |= (PERM_LOGINOK | PERM_POST);
	    outs("\n註冊成功\, 重新上站後將取得完整權限\n"
		   "請按下任一鍵跳離後重新上站~ :)");
	    snprintf(cuser.justify, sizeof(cuser.justify),
		     "<E-Mail>: %s", Cdate(&now));
	    pressanykey();
	    u_exit("registed");
	    exit(0);
	    return QUIT;
	} else if (strcasecmp(inregcode, "x") != 0) {
	    if (regcode[0])
	    {
		vmsg("認證碼錯誤！");
		return FULLUPDATE;
	    }
	    else 
	    {
		vmsg("認證碼已過期，請重新註冊。");
		toregister(email);
		return FULLUPDATE;
	    }
	} else {
	    toregister(email);
	    return FULLUPDATE;
	}
    }

    REGFORM:
    getdata(b_lines - 1, 0, "您確定要填寫註冊單嗎(Y/N)？[N] ",
	    ans, 3, LCECHO);
    if (ans[0] != 'y')
	return FULLUPDATE;

    move(2, 0);
    clrtobot();
    while (1) {
	clear();
	move(1, 0);
	prints("%s(%s) 您好，請據實填寫以下的資料:",
	       cuser.userid, cuser.nickname);
#ifdef FOREIGN_REG
	fore[0] = 'y';
	fore[1] = 0;
	getfield(2, "Y/n", "是否現在住在台灣", fore, 2);
    	if (fore[0] == 'n')
	    fore[0] |= FOREIGN;
	else
	    fore[0] = 0;
#endif
	while (1) {
	    getfield(8, 
#ifdef FOREIGN_REG
                     "請用本名",
#else
                     "請用中文",
#endif
                     "真實姓名", rname, 20);
	    if( (errcode = isvalidname(rname)) == NULL )
		break;
	    else
		vmsg(errcode);
	}

	move(11, 0);
	outs("  請盡量詳細的填寫您的服務單位，大專院校請麻煩"
	     "加" ANSI_COLOR(1;33) "系所" ANSI_RESET "，公司單位請加" ANSI_COLOR(1;33) "職稱" ANSI_RESET "，\n"
	     "  暫無工作請麻煩填寫" ANSI_COLOR(1;33) "畢業學校" ANSI_RESET "。\n");
	while (1) {
	    getfield(9, "(畢業)學校(含" ANSI_COLOR(1;33) "系所年級" ANSI_RESET ")或單位職稱",
		     "服務單位", career, 40);
	    if( (errcode = isvalidcareer(career)) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	move(10, 0); clrtobot();
	while (1) {
	    getfield(10, "含" ANSI_COLOR(1;33) "縣市" ANSI_RESET "及門寢號碼"
		     "(台北請加" ANSI_COLOR(1;33) "行政區" ANSI_RESET ")",
		     "目前住址", addr, sizeof(addr));
	    if( (errcode = isvalidaddr(addr)) == NULL
#ifdef FOREIGN_REG
                || fore[0] 
#endif
		)
		break;
	    else
		vmsg(errcode);
	}
	while (1) {
	    getfield(11, "不加-(), 包括長途區號", "連絡電話", phone, 11);
	    if( (errcode = isvalidphone(phone)) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	getfield(12, "只輸入數字 如:0912345678 (可不填)",
		 "手機號碼", mobile, 20);
	while (1) {
	    getfield(13, "西元/月月/日日 如: " DATE_SAMPLE , 
		    "生日", birthday, sizeof(birthday));
	    if (birthday[0] == 0) {
		snprintf(birthday, sizeof(birthday), "%04i/%02i/%02i",
			 1900 + cuser.year, cuser.month, cuser.day);
		mon = cuser.month;
		day = cuser.day;
		year = cuser.year;
	    } else {
		int y, m, d;
		if (strcmp(birthday, DATE_SAMPLE) == 0) {
		    vmsg("不要複製範例！ 請輸入你真實生日");
		    continue;
		}
		if (ParseDate(birthday, &y, &m, &d)) {
		    vmsg("您的輸入不正確");
		    continue;
		}
		mon = (unsigned char)m;
		day = (unsigned char)d;
		year = (unsigned char)(y - 1900);
	    }
	    if (year < 40) {
		vmsg("您的輸入不正確");
		continue;
	    }
	    break;
	}
	getfield(14, "1.♂男 2.♀女 ", "性別", sex_is, 2);
	getdata(20, 0, "以上資料是否正確(Y/N)？(Q)取消註冊 [N] ",
		ans, 3, LCECHO);
	if (ans[0] == 'q')
	    return 0;
	if (ans[0] == 'y')
	    break;
    }

    // copy values to cuser
    strlcpy(cuser.realname, rname,  sizeof(cuser.realname));
    strlcpy(cuser.address,  addr,   sizeof(cuser.address));
    strlcpy(cuser.email,    email,  sizeof(cuser.email));
    strlcpy(cuser.career,   career, sizeof(cuser.career));
    strlcpy(cuser.phone,    phone,  sizeof(cuser.phone));

    cuser.mobile = atoi(mobile);
    cuser.sex = (sex_is[0] - '1') % 8;
    cuser.month = mon;
    cuser.day = day;
    cuser.year = year;

#ifdef FOREIGN_REG
    if (fore[0])
	cuser.uflag2 |= FOREIGN;
    else
	cuser.uflag2 &= ~FOREIGN;
#endif

    // if reach here, email is apparently 'x'.
    toregister(email);

    // update cuser
    passwd_sync_update(usernum, &cuser);

    return FULLUPDATE;
}

////////////////////////////////////////////////////////////////////////////
// Regform Utilities
////////////////////////////////////////////////////////////////////////////

// TODO define and use structure instead, even in reg request file.
typedef struct {
    // current format:
    // (optional) num: unum, date
    // [0] uid: xxxxx	(IDLEN=12)
    // [1] name: RRRRRR (20)
    // [2] career: YYYYYYYYYYYYYYYYYYYYYYYYYY (40)
    // [3] addr: TTTTTTTTT (50)
    // [4] phone: 02DDDDDDDD (20)
    // [5] email: x (50) (deprecated)
    // [6] mobile: (deprecated)
    // [7] ----
    userec_t	u;	// user record
    char	online;

} RegformEntry;

// regform format utilities

// regform loader: deprecated.
// now we use real user record.
/*
int
load_regform_entry(RegformEntry *pre, FILE *fp)
{
    char buf[STRLEN];
    char *v;

    memset(pre, 0, sizeof(RegformEntry));
    while (fgets(buf, sizeof(buf), fp))
    {
	if (buf[0] == '-')
	    break;
	buf[sizeof(buf)-1] = 0;
	v = strchr(buf, ':');
	if (v == NULL)
	    continue;
	*v++ = 0;
	if (*v == ' ') v++;
	chomp(v);

	if (strcmp(buf, "uid") == 0)
	    strlcpy(pre->u.userid, v, sizeof(pre->u.userid));
	else if (strcmp(buf, "name") == 0)
	    strlcpy(pre->u.realname, v, sizeof(pre->u.realname));
	else if (strcmp(buf, "career") == 0)
	    strlcpy(pre->u.career, v, sizeof(pre->u.career));
	else if (strcmp(buf, "addr") == 0)
	    strlcpy(pre->u.address, v, sizeof(pre->u.address));
	else if (strcmp(buf, "phone") == 0)
	    strlcpy(pre->u.phone, v, sizeof(pre->u.phone));
    }
    return pre->u.userid[0] ? 1 : 0;
}
*/

static int
print_regform_entry(const RegformEntry *pre, FILE *fp, int close)
{
    fprintf(fp, "uid: %s\n",	pre->u.userid);
    fprintf(fp, "name: %s\n",	pre->u.realname);
    fprintf(fp, "career: %s\n", pre->u.career);
    fprintf(fp, "addr: %s\n",	pre->u.address);
    fprintf(fp, "phone: %s\n",	pre->u.phone);
    if (close)
	fprintf(fp, "----\n");
    return 1;
}

static int
concat_regform_entry_localized(const RegformEntry *pre, char *result, int maxlen)
{
    int len = strlen(result);
    len += snprintf(result + len, maxlen - len, "使用者ID: %s\n", pre->u.userid);
    len += snprintf(result + len, maxlen - len, "真實姓名: %s\n", pre->u.realname);
    len += snprintf(result + len, maxlen - len, "職業學校: %s\n", pre->u.career);
    len += snprintf(result + len, maxlen - len, "目前住址: %s\n", pre->u.address);
    len += snprintf(result + len, maxlen - len, "電話號碼: %s\n", pre->u.phone);
    len += snprintf(result + len, maxlen - len, "上站位置: %s\n", pre->u.lasthost);
    len += snprintf(result + len, maxlen - len, "----\n");
    return 1;
}

static int
print_regform_entry_localized(const RegformEntry *pre, FILE *fp)
{
    char buf[STRLEN * 6];
    buf[0] = '\0';
    concat_regform_entry_localized(pre, buf, sizeof(buf));
    fputs(buf, fp);
    return 1;
}

int
append_regform(const RegformEntry *pre, const char *logfn, const char *ext)
{
    FILE *fout = fopen(logfn, "at");
    if (!fout)
	return 0;

    print_regform_entry(pre, fout, 0);
    if (ext)
    {
	syncnow();
	fprintf(fout, "Date: %s\n", Cdate(&now));
	if (*ext)
	    fprintf(fout, ext);
    }
    // close it
    fprintf(fout, "----\n");
    fclose(fout);
    return 1;
}

// prototype declare
static void regform_print_reasons(const char *reason, FILE *fp);
static void regform_concat_reasons(const char *reason, char *result, int maxlen);

int regform_estimate_queuesize()
{
    return dashs(FN_REQLIST) / IDLEN;
}

/////////////////////////////////////////////////////////////////////////////
// Administration (SYSOP Validation)
/////////////////////////////////////////////////////////////////////////////

#define REJECT_REASONS	(6)
#define REASON_LEN	(60)
static const char *reasonstr[REJECT_REASONS] = {
    "請輸入真實姓名",
    "請詳填(畢業)學校『系』『級』或服務單位(含所屬縣市及職稱)",
    "請填寫完整住址 (含縣市/鄉鎮市區, 台北市請記得加行政區域)",
    "請詳填連絡電話 (含區碼, 中間不加 '-', '(', ')' 等符號)",
    "請精確並完整填寫註冊申請表",
    "請用中文填寫申請單",
};

#define REASON_FIRSTABBREV '0'
#define REASON_IN_ABBREV(x) \
    ((x) >= REASON_FIRSTABBREV && (x) - REASON_FIRSTABBREV < REJECT_REASONS)
#define REASON_EXPANDABBREV(x)	 reasonstr[(x) - REASON_FIRSTABBREV]

void
regform_log2board(const RegformEntry *pre, char accepted, 
	const char *reason, int priority)
{
#ifdef BN_ID_RECORD
    char title[STRLEN];
    char *title2 = NULL;
    char msg[STRLEN * REJECT_REASONS];

    snprintf(title, sizeof(title), 
	    "[審核] %s: %s (%s: %s)", 
	    accepted ? "○通過":"╳退回", pre->u.userid, 
	    priority ? "指定審核" : "審核者",
	    cuser.userid);

    // reduce mail header title
    title2 = strchr(title, ' ');
    if (title2) title2++;


    // construct msg
    strlcpy(msg, title2 ? title2 : title, sizeof(msg));
    strlcat(msg, "\n", sizeof(msg));
    if (!accepted) {
	regform_concat_reasons(reason, msg, sizeof(msg));
    }
    strlcat(msg, "\n", sizeof(msg));
    concat_regform_entry_localized(pre, msg, sizeof(msg));

    post_msg(BN_ID_RECORD, title, msg, "[註冊系統]");

#endif  // BN_ID_RECORD
}

void 
regform_accept(const char *userid, const char *justify)
{
    char buf[PATHLEN];
    int unum = 0;
    userec_t muser;

    unum = getuser(userid, &muser);
    if (unum == 0)
	return; // invalid user

    muser.userlevel |= (PERM_LOGINOK | PERM_POST);
    strlcpy(muser.justify, justify, sizeof(muser.justify));
    // manual accept sets email to 'x'
    strlcpy(muser.email, "x", sizeof(muser.email)); 

    // handle files
    sethomefile(buf, muser.userid, FN_REJECT_NOTIFY);
    unlink(buf);

    // update password file
    passwd_sync_update(unum, &muser);

    // alert online users?
    if (search_ulist(unum))
    {
	sendalert(muser.userid,  ALERT_PWD_PERM|ALERT_PWD_JUSTIFY); // force to reload perm
	kick_all(muser.userid);
    }

    // According to suggestions by PTT BBS account sysops,
    // it is better to use anonymous here.
#if FOREIGN_REG_DAY > 0
    if(muser.uflag2 & FOREIGN)
	mail_log2id(muser.userid, "[System] Registration Complete ", "etc/foreign_welcome",
		"[SYSTEM]", 1, 0);
    else
#endif
    // last: send notification mail
    mail_log2id(muser.userid, "[系統通知] 註冊成功\ ", "etc/registered",
	    "[系統通知]", 1, 0);
}

void 
regform_reject(const char *userid, const char *reason, const RegformEntry *pre)
{
    char buf[PATHLEN];
    FILE *fp = NULL;
    int unum = 0;
    userec_t muser;

    unum = getuser(userid, &muser);
    if (unum == 0)
	return; // invalid user

    muser.userlevel &= ~(PERM_LOGINOK | PERM_POST);

    // handle files

    // update password file
    passwd_sync_update(unum, &muser);

    // alert online users?
    if (search_ulist(unum))
    {
	sendalert(muser.userid,  ALERT_PWD_PERM); // force to reload perm
	kick_all(muser.userid);
    }

    // last: send notification
    mkuserdir(muser.userid);
    sethomefile(buf, muser.userid, FN_REJECT_NOTIFY);
    fp = fopen(buf, "wt");
    assert(fp);
    syncnow();

    // log reference for mail-reply.
    fprintf(fp, "#%010d\n\n", usernum);

    if(pre) print_regform_entry_localized(pre, fp);
    fprintf(fp, "%s 註冊失敗。\n", Cdate(&now));


    // prompt user for how to contact if they have problem
    // (deprecated because we allow direct reply now)
    // fprintf(fp, ANSI_COLOR(1;31) "如有任何問題或需要與站務人員聯絡請至"
    // 	    BN_ID_PROBLEM "看板。" ANSI_RESET "\n"); 

    // multiple abbrev loop
    regform_print_reasons(reason, fp);
    fprintf(fp, "--\n");
    fclose(fp);

    // if current site has extra notes
    if (dashf(FN_REJECT_NOTES))
	AppendTail(FN_REJECT_NOTES, buf, 0);

    // According to suggestions by PTT BBS account sysops,
    // it is better to use anonymous here.
    //
    // XXX how to handle the notification file better?
    // mail_log2id: do not use move.
    // mail_muser(muser, "[註冊失敗]", buf);
    
    // use regform2! no need to set 'newmail'.
    mail_log2id(muser.userid, "[註冊失敗記錄]", buf, "[註冊系統]", 0, 0);
}

// New Regform UI
static void
prompt_regform_ui()
{
    vs_footer(" 審核 ",
	    " (y)接受(n)拒絕(d)丟掉 (s)跳過(u)復原 (空白/PgDn)儲存+下頁 (q/END)結束");
}

static void
regform_concat_reasons(const char *reason, char *result, int maxlen)
{
    int len = strlen(result);
    // multiple abbrev loop
    if (REASON_IN_ABBREV(reason[0]))
    {
	int i = 0;
	for (i = 0; reason[i] && REASON_IN_ABBREV(reason[i]); i++) {
	    len += snprintf(result + len, maxlen - len, "[退回原因] %s\n", REASON_EXPANDABBREV(reason[i]));
	}
    } else {
	len += snprintf(result + len, maxlen - len, "[退回原因] %s\n", reason);
    }
}

static void
regform_print_reasons(const char *reason, FILE *fp)
{
    char msg[STRLEN * REJECT_REASONS];
    msg[0] = '\0';
    regform_concat_reasons(reason, msg, sizeof(msg));
    fputs(msg, fp);
}

static void
resolve_reason(char *s, int y, int force)
{
    // should start with REASON_FIRSTABBREV
    const char *reason_prompt = 
	" (0)真實姓名 (1)詳填系級 (2)完整住址"
	" (3)詳填電話 (4)確實填寫 (5)中文填寫";

    s[0] = 0;
    move(y, 0);
    outs(reason_prompt); outs("\n");

    do {
	getdata(y+1, 0, "退回原因: ", s, REASON_LEN, DOECHO);

	// convert abbrev reasons (format: single digit, or multiple digites)
	if (REASON_IN_ABBREV(s[0]))
	{
	    if (s[1] == 0) // simple replace ment
	    {
		strlcpy(s, REASON_EXPANDABBREV(s[0]),
			REASON_LEN);
	    } else {
		// strip until all digites
		char *p = s;
		while (*p)
		{
		    if (!REASON_IN_ABBREV(*p))
			*p = ' ';
		    p++;
		}
		strip_blank(s, s);
		strlcat(s, " [多重原因]", REASON_LEN);
	    }
	} 

	if (!force && !*s)
	    return;

	if (strlen(s) < 4)
	{
	    if (vmsg("原因太短。 要取消退回嗎？ (y/N): ") == 'y')
	    {
		*s = 0;
		return;
	    }
	}
    } while (strlen(s) < 4);
}

////////////////////////////////////////////////////////////////////////////
// Regform2 API
////////////////////////////////////////////////////////////////////////////

// registration queue
int
regq_append(const char *userid)
{
    if (file_append_record(FN_REQLIST, userid) < 0)
	return 0;
    return 1;
}

int 
regq_find(const char *userid)
{
    return file_find_record(FN_REQLIST, userid);
}

int
regq_delete(const char *userid)
{
    return file_delete_record(FN_REQLIST, userid, 0);
}

// user home regform operation
int 
regfrm_exist(const char *userid)
{
    char fn[PATHLEN];
    sethomefile(fn, userid, FN_REGFORM);
    return  dashf(fn) ? 1 : 0;
}

int
regfrm_delete(const char *userid)
{
    char fn[PATHLEN];
    sethomefile(fn, userid, FN_REGFORM);

#ifdef DBG_DRYRUN
    // dry run!
    vmsgf("regfrm_delete (%s)", userid);
    return 1;
#endif

    // directly delete.
    unlink(fn);

    // remove from queue
    regq_delete(userid);
    return 1;
}

int
regfrm_load(const char *userid, RegformEntry *pre)
{
    // FILE *fp = NULL;
    char fn[PATHLEN];
    int unum = 0;

    memset(pre, 0, sizeof(RegformEntry));

    // first check if user exists.
    unum = getuser(userid, &(pre->u));

    // valid unum starts at 1.
    if (unum < 1)
	return 0;

    // check if regform exists.
    sethomefile(fn, userid, FN_REGFORM);
    if (!dashf(fn))
	return 0;

#ifndef DBG_DRYRUN
    // check if user is already registered
    if (pre->u.userlevel & PERM_LOGINOK)
    {
	regfrm_delete(userid);
	return 0;
    }
#endif

    // load regform
    // (deprecated in current version, we use real user data now)

    // fill RegformEntry data
    pre->online = search_ulist(unum) ? 1 : 0;

    return 1;
}

int 
regfrm_save(const char *userid, const RegformEntry *pre)
{
    FILE *fp = NULL;
    char fn[PATHLEN];
    int ret = 0;
    sethomefile(fn, userid, FN_REGFORM);

    fp = fopen(fn, "wt");
    if (!fp)
	return 0;
    ret = print_regform_entry(pre, fp, 1);
    fclose(fp);
    return ret;
}

int 
regfrm_trylock(const char *userid)
{
    int fd = 0;
    char fn[PATHLEN];
    sethomefile(fn, userid, FN_REGFORM);
    if (!dashf(fn)) return 0;
    fd = open(fn, O_RDONLY);
    if (fd < 0) return 0;
    if (flock(fd, LOCK_EX|LOCK_NB) == 0)
	return fd;
    close(fd);
    return 0;
}

int 
regfrm_unlock(int lockfd)
{
    int fd = lockfd;
    if (lockfd <= 0)
	return 0;
    lockfd =  flock(fd, LOCK_UN) == 0 ? 1 : 0;
    close(fd);
    return lockfd;
}

// regform processors
int
regfrm_accept(RegformEntry *pre, int priority)
{
    char justify[REGLEN+1], buf[STRLEN*2];
    char fn[PATHLEN], fnlog[PATHLEN];

#ifdef DBG_DRYRUN
    // dry run!
    vmsg("regfrm_accept");
    return 1;
#endif

    sethomefile(fn, pre->u.userid, FN_REGFORM);

    // build justify string
    snprintf(justify, sizeof(justify),
	    "[%s] %s", cuser.userid, Cdate(&now));

    // call handler
    regform_accept(pre->u.userid, justify);

    // log to user home
    sethomefile(fnlog, pre->u.userid, FN_REGFORM_LOG);
    append_regform(pre, fnlog, "");

    // log to global history
    snprintf(buf, sizeof(buf), "Approved: %s -> %s\n", 
	    cuser.userid, pre->u.userid);
    append_regform(pre, FN_REGISTER_LOG, buf);

    // log to board
    regform_log2board(pre, 1, NULL, priority);

    // remove from queue
    unlink(fn);
    regq_delete(pre->u.userid);
    return 1;
}

int
regfrm_reject(RegformEntry *pre, const char *reason, int priority)
{
    char buf[STRLEN*2];
    char fn[PATHLEN];

#ifdef DBG_DRYRUN
    // dry run!
    vmsg("regfrm_reject");
    return 1;
#endif

    sethomefile(fn, pre->u.userid, FN_REGFORM);

    // call handler
    regform_reject(pre->u.userid, reason, pre);

    // log to global history
    snprintf(buf, sizeof(buf), "Rejected: %s -> %s [%s]\n", 
	    cuser.userid, pre->u.userid, reason);
    append_regform(pre, FN_REGISTER_LOG, buf);

    // log to board
    regform_log2board(pre, 0, reason, priority);

    // remove from queue
    unlink(fn);
    regq_delete(pre->u.userid);
    return 1;
}

// working queue
FILE *
regq_init_pull()
{
    FILE *fp = tmpfile(), *src =NULL;
    char buf[STRLEN];
    if (!fp) return NULL;
    src = fopen(FN_REQLIST, "rt");
    if (!src) { fclose(fp); return NULL; }
    while (fgets(buf, sizeof(buf), src))
	fputs(buf, fp);
    fclose(src);
    rewind(fp);
    return fp;
}

int 
regq_pull(FILE *fp, char *uid)
{
    char buf[STRLEN];
    size_t idlen = 0;
    uid[0] = 0;
    if (fgets(buf, sizeof(buf), fp) == NULL)
	return 0;
    idlen = strcspn(buf, str_space);
    if (idlen < 1) return 0;
    if (idlen > IDLEN) idlen = IDLEN;
    strlcpy(uid, buf, idlen+1);
    return 1;
}

int
regq_end_pull(FILE *fp)
{
    // no need to unlink because fp is a tmpfile.
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

// UI part
int
ui_display_regform_single(
	const RegformEntry *pre, 
	int tid, char *reason)
{
    int c;
    const userec_t *xuser = &(pre->u);

    while (1)
    {
	move(1, 0);
	user_display(xuser, 1);
	move(14, 0);
	prints(ANSI_COLOR(1;32) 
		"--------------- 這是第 %2d 份註冊單 -----------------------" 
		ANSI_RESET "\n", tid);
	prints("  %-12s: %s %s\n",	"帳號", pre->u.userid,
		(xuser->userlevel & PERM_NOREGCODE) ? 
		ANSI_COLOR(1;31) "  [T:禁止使用認證碼註冊]" ANSI_RESET: 
		"");
	prints("0.%-12s: %s%s\n",	"真實姓名", pre->u.realname,
		xuser->uflag2 & FOREIGN ? " (外籍)" : 
		"");
	prints("1.%-12s: %s\n",	"服務單位", pre->u.career);
	prints("2.%-12s: %s\n",	"目前住址", pre->u.address);
	prints("3.%-12s: %s\n",	"連絡電話", pre->u.phone);

	move(b_lines, 0);
	outs("是否接受此資料(Y/N/Q/Del/Skip)？[S] ");

	// round to ASCII
	while ((c = igetch()) > 0xFF);
	c = tolower(c);

	if (c == 'y' || c == 'q' || c == 'd' || c == 's')
	    return c;
	if (c == 'n')
	{
	    int n = 0;
	    move(3, 0);
	    outs("\n" ANSI_COLOR(1;31) 
		    "請提出退回申請表原因，按 <Enter> 取消:\n" ANSI_RESET);
	    for (n = 0; n < REJECT_REASONS; n++)
		prints("%d) %s\n", n, reasonstr[n]);
	    outs("\n\n\n"); // preserved for prompt

	    getdata(3+2+REJECT_REASONS+1, 0,"退回原因: ",
		    reason, REASON_LEN, DOECHO);
	    if (reason[0] == 0)
		continue;
	    // interprete reason
	    return 'n';
	} 
	else if (REASON_IN_ABBREV(c))
	{
	    // quick set
	    sprintf(reason, "%c", c);
	    return 'n';
	}
	return 's';
    }
    // shall never reach here
    return 's';
}

void
regform2_validate_single(const char *xuid)
{
    int lfd = 0;
    int tid = 0;
    char uid[IDLEN+1];
    char rsn[REASON_LEN];
    FILE *fpregq = regq_init_pull();
    RegformEntry re;

    if (xuid && !*xuid)
	xuid = NULL;

    if (!fpregq)
	return;

    while (regq_pull(fpregq, uid))
    {
	int abort = 0;

	// if target assigned, loop until given target.
	if (xuid && strcasecmp(uid, xuid) != 0)
	    continue;

	// try to load regform.
	if (!regfrm_load(uid, &re))
	{
	    regq_delete(uid);
	    continue;
	}

	// try to lock
	lfd = regfrm_trylock(uid);
	if (lfd <= 0)
	    continue;

	tid ++;

	// display regform and process
	switch(ui_display_regform_single(&re, tid, rsn))
	{
	    case 'y': // accept
		regfrm_accept(&re, xuid ? 1 : 0);
		break;

	    case 'd': // delete
		regfrm_delete(uid);
		break;

	    case 'q': // quit
		abort = 1;
		break;

	    case 'n': // reject
		regfrm_reject(&re, rsn, xuid ? 1 : 0);
		break;

	    case 's': // skip
		// do nothing.
		break;

	    default: // shall never reach here
		assert(0);
		break;
	}
	
	// final processing
	regfrm_unlock(lfd);

	if (abort)
	    break;
    }
    regq_end_pull(fpregq);

    // finishing
    clear(); move(5, 0);
    if (xuid && tid == 0)
	prints("未發現 %s 的註冊單。", xuid);
    else
	prints("您檢視了 %d 份註冊單。", tid);
    pressanykey();
}

#define FORMS_IN_PAGE (10)

int
regform2_validate_page(int dryrun)
{
    int yMsg = FORMS_IN_PAGE*2+1;
    RegformEntry forms [FORMS_IN_PAGE];
    char ans	[FORMS_IN_PAGE];
    int  lfds	[FORMS_IN_PAGE];
    char rejects[FORMS_IN_PAGE][REASON_LEN];	// reject reason length
    char rsn	[REASON_LEN];
    int cforms = 0,	// current loaded forms
	ci = 0, // cursor index
	ch = 0,	// input key
	i;
    int tid = 0;
    char uid[IDLEN+1];
    FILE *fpregq = regq_init_pull();

    if (!fpregq)
	return 0;

    while (ch != 'q')
    {
	// initialize and prepare
	memset(ans,	0, sizeof(ans));
	memset(rejects, 0, sizeof(rejects));
	memset(forms,	0, sizeof(forms));
	memset(lfds,	0, sizeof(lfds));
	cforms = 0;
	clear();

	// load forms
	while (cforms < FORMS_IN_PAGE)
	{
	    if (!regq_pull(fpregq, uid))
		break;
	    i = cforms; // align index

	    // check if user exists.
	    if (!regfrm_load(uid, &forms[i]))
	    {
		regq_delete(uid);
		continue;
	    }

	    // try to lock
	    lfds[i] = regfrm_trylock(uid);
	    if (lfds[i] <= 0)
		continue;

	    // assign default answers
	    if (forms[i].u.userlevel & PERM_LOGINOK)
		ans[i] = 'd';
#ifdef REGFORM_DISABLE_ONLINE_USER
	    else if (forms[i].online)
		ans[i] = 's';
#endif // REGFORM_DISABLE_ONLINE_USER

	    // display regform
	    move(i*2, 0);
	    prints("  %2d%s %s%-12s " ANSI_RESET, 
		    i+1, 
		    ( (forms[i].u.userlevel & PERM_LOGINOK) ? 
		      ANSI_COLOR(1;33) "Y" : 
#ifdef REGFORM_DISABLE_ONLINE_USER
			  forms[i].online ? "s" : 
#endif
			  "."),
		    forms[i].online ?  ANSI_COLOR(1;35) : ANSI_COLOR(1),
		    forms[i].u.userid);

	    prints( ANSI_COLOR(1;31) "%19s " 
		    ANSI_COLOR(1;32) "%-40s" ANSI_RESET"\n", 
		    forms[i].u.realname, forms[i].u.career);

	    move(i*2+1, 0); 
	    prints("    %s %-50s%20s\n", 
		    (forms[i].u.userlevel & PERM_NOREGCODE) ? 
		    ANSI_COLOR(1;31) "T" ANSI_RESET : " ",
		    forms[i].u.address, forms[i].u.phone);

	    cforms++, tid ++;
	}

	// if no more forms then leave.
	if (cforms < 1)
	    break;

	// adjust cursor if required
	if (ci >= cforms)
	    ci = cforms-1;

	// display page info
	vbarf(ANSI_REVERSE "\t%s 已顯示 %d 份註冊單 ", // "(%2d%%)  ",
		    dryrun? "(測試模式)" : "",
		    tid);

	// handle user input
	prompt_regform_ui();
	ch = 0;
	while (ch != 'q' && ch != ' ') {
	    ch = cursor_key(ci*2, 0);
	    switch (ch)
	    {
		// nav keys
		case KEY_UP:
		case 'k':
		    if (ci > 0) ci--;
		    break;

		case KEY_DOWN:
		case 'j':
		    ch = 'j'; // go next
		    break;

		    // quick nav (assuming to FORMS_IN_PAGE=10)
		case '1': case '2': case '3': case '4': case '5':
		case '6': case '7': case '8': case '9':
		    ci = ch - '1';
		    if (ci >= cforms) ci = cforms-1;
		    break;
		case '0':
		    ci = 10-1;
		    if (ci >= cforms) ci = cforms-1;
		    break;

		case KEY_HOME: ci = 0; break;
		    /*
		case KEY_END:  ci = cforms-1; break;
		    */

		    // abort
		case KEY_END:
		case 'q':
		    ch = 'q';
		    if (vans("確定要離開了嗎？ (本頁變更將不會儲存) [y/N]: ") != 'y')
		    {
			prompt_regform_ui();
			ch = 0;
			continue;
		    }
		    break;

		    // prepare to go next page
		case KEY_PGDN:
		case ' ':
		    ch = ' ';

		    {
			int blanks = 0;
			// solving blank (undecided entries)
			for (i = 0, blanks = 0; i < cforms; i++)
			    if (ans[i] == 0) blanks ++;

			if (!blanks)
			    break;

			// have more blanks
			ch = vansf("尚未指定的 %d 個項目要: (S跳過/y通過/n拒絕/e繼續編輯): ", 
				blanks);
		    }

		    if (ch == 'e')
		    {
			prompt_regform_ui();
			ch = 0;
			continue;
		    }
		    if (ch == 'y') {
			// do nothing.
		    } else if (ch == 'n') {
			// query reject reason
			resolve_reason(rsn, yMsg, 1);
			if (*rsn == 0)
			    ch = 's';
		    } else ch = 's';

		    // filling answers
		    for (i = 0; i < cforms; i++)
		    {
			if (ans[i] != 0)
			    continue;
			ans[i] = ch;
			if (ch != 'n')
			    continue;
			strlcpy(rejects[i], rsn, REASON_LEN);
		    }

		    ch = ' '; // go to page mode!
		    break;

		    // function keys
		case 'y':	// accept
#ifdef REGFORM_DISABLE_ONLINE_USER
		    if (forms[ci].online)
		    {
			vmsg("暫不開放審核在線上使用者。");
			break;
		    }
#endif
		case 's':	// skip
		case 'd':	// delete
		case KEY_DEL:	//delete
		    if (ch == KEY_DEL) ch = 'd';

		    grayout(ci*2, ci*2+1, GRAYOUT_DARK);
		    move_ansi(ci*2, 4); outc(ch);
		    ans[ci] = ch;
		    ch = 'j'; // go next
		    break;

		case 'u':	// undo
#ifdef REGFORM_DISABLE_ONLINE_USER
		    if (forms[ci].online)
		    {
			vmsg("暫不開放審核在線上使用者。");
			break;
		    }
#endif
		    grayout(ci*2, ci*2+1, GRAYOUT_NORM);
		    move_ansi(ci*2, 4); outc('.');
		    ans[ci] = 0;
		    ch = 'j'; // go next
		    break;

		case 'n':	// reject
#ifdef REGFORM_DISABLE_ONLINE_USER
		    if (forms[ci].online)
		    {
			vmsg("暫不開放審核在線上使用者。");
			break;
		    }
#endif
		    // query for reason
		    resolve_reason(rejects[ci], yMsg, 0);
		    move(yMsg, 0); clrtobot();
		    prompt_regform_ui();

		    if (!rejects[ci][0])
			break;

		    move(yMsg, 0);
		    prints("退回 %s 註冊單原因:\n %s\n", 
			    forms[ci].u.userid, rejects[ci]);

		    // do reject
		    grayout(ci*2, ci*2+1, GRAYOUT_DARK);
		    move_ansi(ci*2, 4); outc(ch);
		    ans[ci] = ch;
		    ch = 'j'; // go next

		    break;
	    } // switch(ch)

	    // change cursor
	    if (ch == 'j' && ++ci >= cforms)
		ci = cforms -1;
	} // while(ch != QUIT/SAVE)

	// if exit, we still need to skip all read forms
	if (ch == 'q')
	{
	    for (i = 0; i < cforms; i++)
		ans[i] = 's';
	}

	// page complete (save).
	assert(ch == ' ' || ch == 'q');

	// save/commit if required.
	if (dryrun) 
	{
	    // prmopt for debug
	    clear();
	    vs_hdr("測試模式");
	    outs("您正在執行測試模式，所以剛審的註冊單並不會生效。\n"
		    "下面列出的是剛才您審完的結果:\n\n");

	    for (i = 0; i < cforms; i++)
	    {
		char justify[REGLEN+1];
		if (ans[i] == 'y')
		    snprintf(justify, sizeof(justify), // build justify string
			    "%s %s", cuser.userid, Cdate(&now));

		prints("%2d. %-12s - %c %s\n", i+1, forms[i].u.userid, ans[i],
			ans[i] == 'n' ? rejects[i] : 
			ans[i] == 'y' ? justify : "");
	    }
	    if (ch != 'q')
		pressanykey();
	} 
	else 
	{
	    // real functionality
	    for (i = 0; i < cforms; i++)
	    {
		switch(ans[i])
		{
		    case 'y': // accept
			regfrm_accept(&forms[i], 0);
			break;

		    case 'd': // delete
			regfrm_delete(uid);
			break;

		    case 'n': // reject
			regfrm_reject(&forms[i], rejects[i], 0);
			break;

		    case 's': // skip
			// do nothing.
			break;

		    default:
			assert(0);
			break;
		}
	    }
	} // !dryrun

	// unlock all forms
	for (i = 0; i < cforms; i++)
	    regfrm_unlock(lfds[i]);

    } // while (ch != 'q')

    regq_end_pull(fpregq);

    // finishing
    clear(); move(5, 0);
    prints("您檢視了 %d 份註冊單。", tid);
    pressanykey();
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Regform UI 
// 處理 Register Form
/////////////////////////////////////////////////////////////////////////////

/* Auto-Regform-Scan
 * FIXME 真是一團垃圾
 *
 * fdata 用了太多 magic number
 * return value 應該是指 reason (return index + 1)
 * ans[0] 指的是帳管選擇的「錯誤的欄位」 (Register 選單裡看到的那些)
 */
#if 0
static int
auto_scan(char fdata[][STRLEN], char ans[])
{
    int             good = 0;
    int             count = 0;
    int             i;
    char            temp[10];

    if (!strncmp(fdata[1], "小", 2) || DBCS_strcasestr(fdata[1], "丫")
	|| DBCS_strcasestr(fdata[1], "誰") || DBCS_strcasestr(fdata[1], "不")) {
	ans[0] = '0';
	return 1;
    }
    strlcpy(temp, fdata[1], 3);

    /* 疊字 */
    if (!strncmp(temp, &(fdata[1][2]), 2)) {
	ans[0] = '0';
	return 1;
    }
    if (strlen(fdata[1]) >= 6) {
	if (DBCS_strcasestr(fdata[1], "陳水扁")) {
	    ans[0] = '0';
	    return 1;
	}
	if (DBCS_strcasestr("趙錢孫李周吳鄭王", temp))
	    good++;
	else if (DBCS_strcasestr("杜顏黃林陳官余辛劉", temp))
	    good++;
	else if (DBCS_strcasestr("蘇方吳呂李邵張廖應蘇", temp))
	    good++;
	else if (DBCS_strcasestr("徐謝石盧施戴翁唐", temp))
	    good++;
    }
    if (!good)
	return 0;

    if (!strcmp(fdata[2], fdata[3]) ||
	!strcmp(fdata[2], fdata[4]) ||
	!strcmp(fdata[3], fdata[4])) {
	ans[0] = '4';
	return 5;
    }
    if (DBCS_strcasestr(fdata[2], "大")) {
	if (DBCS_strcasestr(fdata[2], "台") || DBCS_strcasestr(fdata[2], "淡") ||
	    DBCS_strcasestr(fdata[2], "交") || DBCS_strcasestr(fdata[2], "政") ||
	    DBCS_strcasestr(fdata[2], "清") || DBCS_strcasestr(fdata[2], "警") ||
	    DBCS_strcasestr(fdata[2], "師") || DBCS_strcasestr(fdata[2], "銘傳") ||
	    DBCS_strcasestr(fdata[2], "中央") || DBCS_strcasestr(fdata[2], "成") ||
	    DBCS_strcasestr(fdata[2], "輔") || DBCS_strcasestr(fdata[2], "東吳"))
	    good++;
    } else if (DBCS_strcasestr(fdata[2], "女中"))
	good++;

    if (DBCS_strcasestr(fdata[3], "地球") || DBCS_strcasestr(fdata[3], "宇宙") ||
	DBCS_strcasestr(fdata[3], "信箱")) {
	ans[0] = '2';
	return 3;
    }
    if (DBCS_strcasestr(fdata[3], "市") || DBCS_strcasestr(fdata[3], "縣")) {
	if (DBCS_strcasestr(fdata[3], "路") || DBCS_strcasestr(fdata[3], "街")) {
	    if (DBCS_strcasestr(fdata[3], "號"))
		good++;
	}
    }
    for (i = 0; fdata[4][i]; i++) {
	if (isdigit((int)fdata[4][i]))
	    count++;
    }

    if (count <= 4) {
	ans[0] = '3';
	return 4;
    } else if (count >= 7)
	good++;

    if (good >= 3) {
	ans[0] = 'y';
	return -1;
    } else
	return 0;
}
#endif

int
m_register(void)
{
    FILE           *fn;
    int             x, y, wid, len;
    char            ans[4];
    char            genbuf[200];

    if (dashs(FN_REQLIST) <= 0) {
	outs("目前並無新註冊資料");
	return XEASY;
    }
    fn = fopen(FN_REQLIST, "r");
    assert(fn);

    vs_hdr("審核使用者註冊資料");
    y = 2;
    x = wid = 0;

    while (fgets(genbuf, STRLEN, fn) && x < 65) {
	move(y++, x);
	outs(genbuf);
	len = strlen(genbuf);
	if (len > wid)
	    wid = len;
	if (y >= t_lines - 3) {
	    y = 2;
	    x += wid + 2;
	}
    }

    fclose(fn);

    getdata(b_lines - 1, 0, 
	    "開始審核嗎 (Y:單筆模式/N:不審/E:整頁模式/U:指定ID)？[N] ", 
	    ans, sizeof(ans), LCECHO);

    if (ans[0] == 'y')
	regform2_validate_single(NULL);
    else if (ans[0] == 'e')
	regform2_validate_page(0);
    else if (ans[0] == 'u') {
	vs_hdr("指定審核");
	usercomplete(msg_uid, genbuf);
	if (genbuf[0])
	    regform2_validate_single(genbuf);
    }

    return 0;
}

/* vim:sw=4
 */
