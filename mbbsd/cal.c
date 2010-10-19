/* $Id$ */
#include "bbs.h"

/* 防堵 Multi play */
static int
is_playing(int unmode)
{
    register int    i;
    register userinfo_t *uentp;
    unsigned int p = StringHash(cuser.userid) % USHM_SIZE;

    for (i = 0; i < USHM_SIZE; i++, p++) { // XXX linear search
	if (p == USHM_SIZE)
	    p = 0;
	uentp = &(SHM->uinfo[p]);
	if (uentp->mode == DEBUGSLEEPING)
	    continue;
	if (uentp->uid == usernum &&
	    uentp->lockmode == unmode)
	    return 1;
    }
    return 0;
}

int
lockutmpmode(int unmode, int state)
{
    int             errorno = 0;

    if (currutmp->lockmode)
	errorno = LOCK_THIS;
    else if (state == LOCK_MULTI && is_playing(unmode))
	errorno = LOCK_MULTI;

    if (errorno) {
	clear();
	move(10, 20);
	if (errorno == LOCK_THIS)
	    prints("請先離開 %s 才能再 %s ",
		   ModeTypeTable[currutmp->lockmode],
		   ModeTypeTable[unmode]);
	else
	    prints("抱歉! 您已有其他線相同的ID正在%s",
		   ModeTypeTable[unmode]);
	pressanykey();
	return errorno;
    }
    setutmpmode(unmode);
    currutmp->lockmode = unmode;
    return 0;
}

int
unlockutmpmode(void)
{
    currutmp->lockmode = 0;
    return 0;
}

static int 
do_pay(int uid, int money, const char *item, const char *reason)  
{ 
    int oldm, newm; 
    const char *userid; 
 
    assert(money != 0); 
    userid = getuserid(uid); 
    assert(userid); 
    assert(reason); 
 
    // if we cannot find user, abort 
    if (!userid) 
        return -1; 
 
    oldm = moneyof(uid); 
    newm = deumoney(uid, -money); 
    if (uid == usernum) 
        reload_money(); 
 
    {
        char buf[PATHLEN]; 
        sethomefile(buf, userid, FN_RECENTPAY); 
        rotate_text_logfile(buf, SZ_RECENTPAY, 0.2); 
        syncnow(); 
        log_payment(buf, money, oldm, newm, reason);
    }

    return newm; 
} 
 
int 
pay_as_uid(int uid, int money, const char *item, ...) 
{ 
    va_list ap; 
    char reason[STRLEN*3] =""; 
 
    if (!money) 
        return 0; 
 
    if (item) { 
        va_start(ap, item); 
        vsnprintf(reason, sizeof(reason)-1, item, ap); 
        va_end(ap); 
    } 
 
    return do_pay(uid, money, item, reason); 
} 
 
int 
pay(int money, const char *item, ...) 
{ 
    va_list ap; 
    char reason[STRLEN*3] =""; 
 
    if (!money) 
        return 0; 
 
    if (item) { 
        va_start(ap, item); 
        vsnprintf(reason, sizeof(reason)-1, item, ap); 
        va_end(ap); 
    } 
 
    return do_pay(usernum, money, item, reason); 
}

static int
inmailbox(int m)
{
    pwcuAddExMailBox(m);
    return cuser.exmailbox;
}


int
p_from(void)
{
    char tmp_from[sizeof(currutmp->from)];

    if (vans("確定要改故鄉?[y/N]") != 'y')
	return 0;

    strlcpy(tmp_from, currutmp->from, sizeof(tmp_from)); 
    if (getdata(b_lines - 1, 0, "請輸入新故鄉:",
		tmp_from, sizeof(tmp_from), DOECHO) &&
	strcmp(tmp_from, currutmp->from) != 0) 
    {
	strlcpy(currutmp->from, tmp_from, sizeof(currutmp->from));
    }
    return 0;
}

int
p_exmail(void)
{
    char            ans[4], buf[200];
    int             n, oldm;

    if (cuser.exmailbox >= MAX_EXKEEPMAIL) {
	vmsgf("容量最多增加 %d 封，不能再買了。", MAX_EXKEEPMAIL);
	return 0;
    }
    snprintf(buf, sizeof(buf),
	     "您曾增購 %d 封容量，還要再買多少? ", cuser.exmailbox);

    // no need to create default prompt.
    // and people usually come this this by accident...
    getdata(b_lines - 2, 0, buf, ans, sizeof(ans), NUMECHO);

    oldm = cuser.exmailbox;
    n = atoi(ans);
    if (!ans[0] || n<=0)
	return 0;

    if (n + cuser.exmailbox > MAX_EXKEEPMAIL)
	n = MAX_EXKEEPMAIL - cuser.exmailbox;
    reload_money();
    if (cuser.money < n * 1000)
    {
	vmsg("你的錢不夠。");
	return 0;
    }

    if (vmsgf("你想購買 %d 封信箱 (要花 %d 元), 確定嗎？[y/N] ", 
		n, n*1000) != 'y')
	return 0;

    reload_money();
    pay(n * 1000, "購買信箱");
    inmailbox(n);
    log_filef("log/exmailbox.log", LOG_CREAT,
	    "%-13s %d+%d->%d %s\n", cuser.userid, 
	    oldm, n, cuser.exmailbox, Cdatelite(&now));

    vmsgf("已購買信箱。新容量上限: %d", cuser.exmailbox);
    return 0;
}

int
mail_redenvelop(const char *from, const char *to, int money, char *fpath)
{
    char            _fpath[PATHLEN], dirent[PATHLEN];
    fileheader_t    fhdr;
    FILE           *fp;

    if (!fpath) fpath = _fpath;

    sethomepath(fpath, to);
    stampfile(fpath, &fhdr);

    if (!(fp = fopen(fpath, "w")))
	return -1;

    fprintf(fp, "作者: %s\n"
	    "標題: 招財進寶\n"
	    "時間: %s\n"
	    ANSI_COLOR(1;33) "親愛的 %s ：\n\n" ANSI_RESET
	    ANSI_COLOR(1;31) "    我包給你一個 %d 元的大紅包喔 ^_^\n\n"
	    "    禮輕情意重，請笑納...... ^_^" ANSI_RESET "\n",
	    from, ctime4(&now), to, money);
    fclose(fp);

    // colorize topic to make sure this is issued by system.
    snprintf(fhdr.title, sizeof(fhdr.title), 
	    ANSI_COLOR(1;37;41) "[紅包]" ANSI_RESET " $%d", money);
    strlcpy(fhdr.owner, from, sizeof(fhdr.owner));
    sethomedir(dirent, to);
    append_record(dirent, &fhdr, sizeof(fhdr));
    return 0;
}

/* 給錢與贈與稅 */

int
give_tax(int money)
{
    int tax = money * 0.1;
    assert (money >= 0);
    if (money % 10)
	tax ++;
    return (tax < 1) ? 1 : tax;
}
int
cal_before_givetax(int taxed_money)
{
    int m = taxed_money / 9.0f * 10 + 1;
    if (m > 1 && taxed_money % 9 == 0)
	m--;
    return m;
}

int
cal_after_givetax(int money)
{
    return money - give_tax(money);
}

static int 
give_money_vget_changecb(int key, VGET_RUNTIME *prt, void *instance)
{
    int  m1 = atoi(prt->buf), m2 = m1;
    char c1 = ' ', c2 = ' ';
    int is_before_tax = *(int*)instance;

    if (is_before_tax)
	m2 = cal_after_givetax(m1),  c1 = '>';
    else
	m1 = cal_before_givetax(m2), c2 = '>';

    // adjust output
    if (m1 <= 0 || m2 <= 0)
	m1 = m2 = 0;

    move(4, 0);
    prints(" %c 你要付出 (稅前): %d\n", c1, m1);
    prints(" %c 對方收到 (稅後): %d\n", c2, m2);
    return VGETCB_NONE;
}

static int 
give_money_vget_peekcb(int key, VGET_RUNTIME *prt, void *instance)
{
    int *p_is_before_tax = (int*) instance;
    
    // digits will be refreshed later.
    if (key >= '0' && key <= '9')
	return VGETCB_NONE;
    if (key != KEY_TAB)
	return VGETCB_NONE;

    // TAB - toggle mode and update display
    assert(p_is_before_tax);
    *p_is_before_tax = !*p_is_before_tax;
    give_money_vget_changecb(key, prt, instance);
    return VGETCB_NEXT;
}

static int
do_give_money(char *id, int uid, int money, const char *myid)
{
    int tax;
    char prompt[STRLEN*2] = "";

    reload_money();
    if (money < 1 || cuser.money < money)
	return -1;

    tax = give_tax(money);
    if (money - tax <= 0)
	return -1;		/* 繳完稅就沒錢給了 */

    if (strcasecmp(myid, cuser.userid) != 0)  {
        snprintf(prompt, sizeof(prompt)-1,
                "以 %s 的名義轉帳給 %s (稅後 $%d)",
                myid, id, money - tax); 
    } else {
        snprintf(prompt, sizeof(prompt)-1, 
                "轉帳給 %s (稅後 $%d)", id, money - tax); 
    }

    // 實際給予金錢。 為避免程式故障/惡意斷線，一律先扣再發。
    pay(money, "%s", prompt);
    pay_as_uid(uid, -(money - tax), "來自 %s 的轉帳 (稅前 $%d)", 
               myid, money); 
    log_filef(FN_MONEY, LOG_CREAT, "%-12s 給 %-12s %d\t(稅後 %d)\t%s\n",
              cuser.userid, id, money, money - tax, Cdate(&now));

    // penalty
    if (money < 50) {
	usleep(2000000);    // 2 sec
    } else if (money < 200) {
	usleep(500000);	    // 0.5 sec
    } else {
	usleep(100000);	    // 0.1 sec
    }
    return 0;
}

int
p_give(void)
{
    give_money_ui(NULL);
    return -1;
}

int 
give_money_ui(const char *userid)
{
    int             uid;
    char            id[IDLEN + 1], money_buf[20];
    char	    passbuf[PASSLEN];
    int		    m = 0, mtax = 0, tries = 3, skipauth = 0;
    static time4_t  lastauth = 0;
    const char	    *myid = cuser.userid;
    const char	    *uid_prompt = "這位幸運兒的id: ";

    // TODO prevent macros, we should check something here,
    // like user pw/id/...
    vs_hdr("給予金錢");

    if (!userid || !*userid || strcmp(userid, cuser.userid) == 0)
	userid = NULL;

    // XXX should we prevent editing if userid is already assigned?
    usercomplete2(uid_prompt, id, userid);
    // redraw highlighted prompt
    move(1, 0); clrtobot();
    outs(uid_prompt); outs(ANSI_COLOR(1)); outs(id);
    outs(ANSI_RESET "\n");

    if (!id[0] || strcasecmp(cuser.userid, id) == 0)
    {
	vmsg("交易取消!");
	return -1;
    }

    if ((uid = searchuser(id, id)) == 0) {
	vmsg("查無此人!");
	return -1;
    }

    m = 0;
    money_buf[0] = 0;
    outs("要給他多少錢呢? (可按 TAB 切換輸入稅前/稅後金額, 稅率固定 10%)\n");
    outs(" 請輸入金額: ");  // (3, 0)
    {
	int is_before_tax = 1;
	const VGET_CALLBACKS cb = { 
	    give_money_vget_peekcb, 
	    NULL, 
	    give_money_vget_changecb,
	};
	if (vgetstring(money_buf, 7, VGET_DIGITS, "", &cb, &is_before_tax))
	    m = atoi(money_buf);
	if (m > 0 && !is_before_tax)
	    m = cal_before_givetax(m);
    }
    if (m < 2) {
	vmsg("金額過少，交易取消!");
	return -1;
    }

    reload_money();
    if (cuser.money < m) {
	vmsg("你沒有那麼多錢喔!");
	return -1;
    }

    mtax = give_tax(m);
    move(4, 0);
    prints( "交易內容: %s 將給予 %s : [未稅] $%d (稅金 $%d )\n"
	    "對方實得: $%d\n",
	    cuser.userid, id, m, mtax, m-mtax);

    // safe context starts at (6, 0).
#ifdef PLAY_ANGEL
    if (HasUserPerm(PERM_ANGEL))
    {
	userec_t xuser = {0};
	getuser(id, &xuser);

	if (strcmp(xuser.myangel, cuser.userid) == 0)
	{
	    char yn[3];
	    outs("他是你的小主人，是否匿名？[Y/n]: ");
	    vgets(yn, sizeof(yn), VGET_LOWERCASE);
	    if (yn[0] != 'n')
		myid = "小天使";
	}
    }
#endif // PLAY_ANGEL

    // safe context starts at (7, 0)
    move(7, 0);
    if (now - lastauth >= 15*60) // valid through 15 minutes
    {
	outs(ANSI_COLOR(1;31) "為了避免誤按或是惡意詐騙，"
		"在完成交易前要重新確認您的身份。" ANSI_RESET);
    } else {
	outs("你的認證尚未過期，可暫時跳過密碼認證程序。\n");
	// auth is valid.
	if (vans("確定進行交易嗎？ (y/N): ") == 'y')
	    skipauth = 1;
	else
	    tries = -1;
    }

    // safe context starts at (7, 0)
    while (!skipauth && tries-- > 0)
    {
	getdata(8, 0, MSG_PASSWD,
		passbuf, sizeof(passbuf), NOECHO);
	passbuf[8] = '\0';
	if (checkpasswd(cuser.passwd, passbuf))
	{
	    lastauth = now;
	    break;
	}
	// if we show '%d chances left', some user may think
	// they will be locked out...
	if (tries > 0 &&
	    vmsg("密碼錯誤，請重試或按 n 取消交易。") == 'n')
	    return -1;
    }

    if (tries < 0)
    {
	vmsg("交易取消!");
	return -1;
    }

    outs("\n交易正在進行中，請稍候...\n"); 
    refresh();

    if(do_give_money(id, uid, m, myid) < 0)
    {
	outs(ANSI_COLOR(1;31) "交易失敗！" ANSI_RESET "\n");
	vmsg("交易失敗。");
	return -1;
    }

    outs(ANSI_COLOR(1;33) "交易完成！" ANSI_RESET "\n");

    // transaction complete.
    {
	char fpath[PATHLEN];
	if (mail_redenvelop( myid, id, m - mtax, fpath) < 0)
	{
#ifdef USE_RECENTPAY
            outs("您可於下列位置找到最近的交易記錄:\n" 
                    "主選單 => (U)ser個人設定 => (L)MyLogs 個人記錄 => " 
                    "(P)Recent Pay 最近交易記錄\n"); 
#endif
	    vmsg("交易完成。");
	    return 0;
	}

	// TODO 若是壞人，禁止編輯內文？
	if (vans("交易已完成，要修改紅包袋嗎？[y/N] ") == 'y')
	{
	    veditfile(fpath);
	}
	sendalert(id, ALERT_NEW_MAIL);
    }
    return 0;
}

int 
resolve_over18_user(const userec_t *u)
{
    /* get local time */
    struct tm ptime;

    localtime4_r(&now, &ptime);
    // 照實歲計算，沒生日的當作未滿 18
    if (u->year < 1 || u->month < 1)
	return 0;
    else if( (ptime.tm_year - u->year) > 18)
	return 1;
    else if (ptime.tm_year - u->year < 18)
	return 0;
    else if ((ptime.tm_mon+1) > u->month)
	return 1;
    else if ((ptime.tm_mon+1) < u->month)
	return 0;
    else if (ptime.tm_mday >= u->day )
	return 1;
    return 0;
}

void
resolve_over18(void)
{
    over18 = resolve_over18_user(cuser_ref);
}

int
p_sysinfo(void)
{
    char            *cpuloadstr;
    int             load;
    extern char    *compile_time;
#ifdef DETECT_CLIENT
    extern Fnv32_t  client_code;
#endif

    load = cpuload(NULL);
    cpuloadstr = (load < 5 ? "良好" : (load < 20 ? "尚可" : "過重"));

    clear();
    showtitle("系統資訊", BBSNAME);
    move(2, 0);
    prints("您現在位於 " TITLE_COLOR BBSNAME ANSI_RESET " (" MYIP ")\n"
	   "系統負載情況: %s\n"
	   "線上服務人數: %d/%d\n"
#ifdef DETECT_CLIENT
	   "client code:  %8.8X\n"
#endif
	   "編譯時間:     %s\n"
	   "起始時間:     %s\n",
	   cpuloadstr, SHM->UTMPnumber,
#ifdef DYMAX_ACTIVE
	   // XXX check the related logic in mbbsd.c
	   (SHM->GV2.e.dymaxactive > 2000 && SHM->GV2.e.dymaxactive < MAX_ACTIVE) ? 
	    SHM->GV2.e.dymaxactive : MAX_ACTIVE,
#else
	   MAX_ACTIVE,
#endif
#ifdef DETECT_CLIENT
	   client_code,
#endif
	   compile_time, Cdatelite(&start_time));

#ifdef REPORT_PIAIP_MODULES
    outs("\n" ANSI_COLOR(1;30)
	    "Modules powered by piaip:\n"
	    "\ttelnet/vkbd protocol, vtuikit, ALOHA fixer, BRC v3\n"
	    "\tpiaip's Common Chat Window (CCW)\n"
#if defined(USE_PIAIP_MORE) || defined(USE_PMORE)
	    "\tpmore (piaip's more) 2007 w/Movie\n"
#endif
#ifdef HAVE_GRAYOUT
	    "\tGrayout Advanced Control 淡入淡出特效系統\n"
#endif 
#ifdef EDITPOST_SMARTMERGE
	    "\tSmart Merge 修文自動合併\n"
#endif
#ifdef EXP_EDIT_UPLOAD
	    "\t(EXP) Editor Uploader 長文上傳\n"
#endif
#if defined(USE_PFTERM)
	    "\t(EXP) pfterm (piaip's flat terminal, Perfect Term)\n"
#endif
#if defined(USE_BBSLUA)
	    "\t(EXP) BBS-Lua\n"
#endif
	    ANSI_RESET
	    );
#endif // REPORT_PIAIP_MODULES

    if (HasUserPerm(PERM_SYSOP)) {
	struct rusage ru;
#ifdef __linux__
	int vmdata=0, vmstk=0;
	FILE * fp;
	char buf[128];
	if ((fp = fopen("/proc/self/status", "r"))) {
	    while (fgets(buf, 128, fp)) {
		sscanf(buf, "VmData: %d", &vmdata);
		sscanf(buf, "VmStk: %d", &vmstk);
	    }
	    fclose(fp);
	}
#endif
	getrusage(RUSAGE_SELF, &ru);
	prints("記憶體用量: "
#ifdef IA32
	       "sbrk: %u KB, "
#endif
#ifdef __linux__
	       "VmData: %d KB, VmStk: %d KB, "
#endif
	       "idrss: %d KB, isrss: %d KB\n",
#ifdef IA32
	       ((unsigned int)sbrk(0) - 0x8048000) / 1024,
#endif
#ifdef __linux__
	       vmdata, vmstk,
#endif
	       (int)ru.ru_idrss, (int)ru.ru_isrss);
	prints("CPU 用量:   %ld.%06ldu %ld.%06lds",
	       (long int)ru.ru_utime.tv_sec, 
	       (long int)ru.ru_utime.tv_usec,
	       (long int)ru.ru_stime.tv_sec, 
	       (long int)ru.ru_stime.tv_usec);
#ifdef CPULIMIT_PER_DAY
	prints(" (limit %d secs per day)", CPULIMIT_PER_DAY);
#endif
	outs("\n特別參數:"
#ifdef CRITICAL_MEMORY
		" CRITICAL_MEMORY"
#endif
#ifdef UTMPD
		" UTMPD"
#endif
#ifdef FROMD
		" FROMD"
#endif
		);
    }
    pressanykey();
    return 0;
}

