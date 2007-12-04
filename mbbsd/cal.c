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
	if (uentp->uid == usernum)
	    if (uentp->lockmode == unmode)
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

/* 使用錢的函數 */
#define VICE_NEW   "vice.new"

const char*
money_level(int money)
{
    int i = 0;

    static const char *money_msg[] =
    {
	"債台高築", "赤貧", "清寒", "普通", "小康",
	"小富", "中富", "大富翁", "富可敵國", "比爾蓋\天", NULL
    };
    while (money_msg[i] && money > 10)
	i++, money /= 10;

    if(!money_msg[i])
	i--;
    return money_msg[i];
}

/* Heat:發票 */
int
vice(int money, const char *item)
{
   char            buf[128];
   unsigned int viceserial = (currutmp->lastact % 10000) * 10000 + random() % 10000;

    demoney(-money);
    if(money>=100) 
	{
          setuserfile(buf, VICE_NEW);
          log_file(buf, LOG_CREAT | LOG_VF, "%8.8d\n", viceserial);
	}
    snprintf(buf, sizeof(buf),
	     "%s 花了$%d 編號[%08d]", item, money, viceserial);
    mail_id(cuser.userid, buf, "etc/vice.txt", BBSMNAME "經濟部");
    return 0;
}

#define lockreturn(unmode, state) if(lockutmpmode(unmode, state)) return
#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
#define lockbreak(unmode, state) if(lockutmpmode(unmode, state)) break
#define SONGBOOK  "etc/SONGBOOK"
#define OSONGPATH "etc/SONGO"

static int
osong(void)
{
    char            sender[IDLEN + 1], receiver[IDLEN + 1], buf[200],
		    genbuf[200], filename[256], say[51];
    char            trans_buffer[PATHLEN];
    char            address[45];
    FILE           *fp, *fp1;
    //*fp2;
    fileheader_t    mail;
    int             nsongs;

    strlcpy(buf, Cdatedate(&now), sizeof(buf));

    lockreturn0(OSONG, LOCK_MULTI);

    /* Jaky 一人一天點一首 */
    if (!strcmp(buf, Cdatedate(&cuser.lastsong)) && !HasUserPerm(PERM_SYSOP)) {
	move(22, 0);
	vmsg("你今天已經點過囉，明天再點吧....");
	unlockutmpmode();
	return 0;
    }

    while (1) {
	char ans[4];
	move(12, 0);
	clrtobot();
	prints("親愛的 %s 歡迎來到歐桑自動點歌系統\n", cuser.userid);
	getdata(13, 0, "請選擇 " ANSI_COLOR(1) "1)" ANSI_RESET " 開始點歌、"
		ANSI_COLOR(1) "2)" ANSI_RESET " 看歌本、"
		"或是 " ANSI_COLOR(1) "3)" ANSI_RESET " 離開: ",
		ans, sizeof(ans), DOECHO);

	if (ans[0] == '1')
	    break;
	else if (ans[0] == '2') {
	    a_menu("點歌歌本", SONGBOOK, 0, NULL);
	    clear();
	}
	else if (ans[0] == '3') {
	    vmsg("謝謝光臨 :)");
	    unlockutmpmode();
	    return 0;
	}
    }

    if (cuser.money < 200) {
	move(22, 0);
	vmsg("點歌要200銀唷!....");
	unlockutmpmode();
	return 0;
    }

    getdata_str(14, 0, "點歌者(可匿名): ", sender, sizeof(sender), DOECHO, cuser.userid);
    getdata(15, 0, "點給(可匿名): ", receiver, sizeof(receiver), DOECHO);

    getdata_str(16, 0, "想要要對他(她)說..:", say,
		sizeof(say), DOECHO, "我愛妳..");
    snprintf(save_title, sizeof(save_title),
	     "%s:%s", sender, say);
    getdata_str(17, 0, "寄到誰的信箱(真實 ID 或 E-mail)?",
		address, sizeof(address), LCECHO, receiver);
    outs("\n接著要選歌囉..進入歌本好好的選一首歌吧..^o^");
    pressanykey();
    a_menu("點歌歌本", SONGBOOK, 0, trans_buffer);
    if (!trans_buffer[0] || strstr(trans_buffer, "home") ||
	strstr(trans_buffer, "boards") || !(fp = fopen(trans_buffer, "r"))) {
	unlockutmpmode();
	return 0;
    }
    strlcpy(filename, OSONGPATH, sizeof(filename));

    stampfile(filename, &mail);

    unlink(filename);

    if (!(fp1 = fopen(filename, "w"))) {
	fclose(fp);
	unlockutmpmode();
	return 0;
    }
    strlcpy(mail.owner, "點歌機", sizeof(mail.owner));
    snprintf(mail.title, sizeof(mail.title), "◇ %s 點給 %s ", sender, receiver);

    while (fgets(buf, sizeof(buf), fp)) {
	char           *po;
	if (!strncmp(buf, "標題: ", 6)) {
	    clear();
	    move(10, 10);
	    outs(buf);
	    pressanykey();
	    fclose(fp);
	    fclose(fp1);
	    unlockutmpmode();
	    return 0;
	}
	while ((po = strstr(buf, "<~Src~>"))) {
	    const char *dot = "";
	    if (is_validuserid(sender) && strcmp(sender, cuser.userid) != 0)
		dot = ".";
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s%s", buf, sender, dot, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	while ((po = strstr(buf, "<~Des~>"))) {
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s", buf, receiver, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	while ((po = strstr(buf, "<~Say~>"))) {
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s", buf, say, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	fputs(buf, fp1);
    }
    fclose(fp1);
    fclose(fp);

    log_file("etc/osong.log",  LOG_CREAT | LOG_VF, "id: %-12s ◇ %s 點給 %s : \"%s\", 轉寄至 %s\n", cuser.userid, sender, receiver, say, address, ctime4(&now));

    if (append_record(OSONGPATH "/.DIR", &mail, sizeof(mail)) != -1) {
	cuser.lastsong = now;
	/* Jaky 超過 MAX_MOVIE 首歌就開始砍 */
	nsongs = get_num_records(OSONGPATH "/.DIR", sizeof(mail));
	if (nsongs > MAX_MOVIE) {
	    // XXX race condition
	    delete_range(OSONGPATH "/.DIR", 1, nsongs - MAX_MOVIE);
	}
	snprintf(genbuf, sizeof(genbuf), "%s says \"%s\" to %s.", sender, say, receiver);
	log_usies("OSONG", genbuf);
	/* 把第一首拿掉 */
	vice(200, "點歌");
    }
    snprintf(save_title, sizeof(save_title), "%s:%s", sender, say);
    hold_mail(filename, receiver);

    if (address[0]) {
#ifndef USE_BSMTP
	bbs_sendmail(filename, save_title, address);
#else
	bsmtp(filename, save_title, address);
#endif
    }
    clear();
    outs(
	 "\n\n  恭喜您點歌完成囉..\n"
	 "  一小時內動態看板會自動重新更新\n"
	 "  大家就可以看到您點的歌囉\n\n"
	 "  點歌有任何問題可以到Note板的精華區找答案\n"
	 "  也可在Note板精華區看到自己的點歌記錄\n"
	 "  有任何保貴的意見也歡迎到Note板留話\n"
	 "  讓親切的板主為您服務\n");
    pressanykey();
    sortsong();
    topsong();

    unlockutmpmode();
    return 1;
}

int
ordersong(void)
{
    osong();
    return 0;
}

static int
inmailbox(int m)
{
    userec_t xuser;
    passwd_query(usernum, &xuser);
    cuser.exmailbox = xuser.exmailbox + m;
    passwd_update(usernum, &cuser);
    return cuser.exmailbox;
}


#if !HAVE_FREECLOAK
/* 花錢選單 */
int
p_cloak(void)
{
    if (getans(currutmp->invisible ? "確定要現身?[y/N]" : "確定要隱身?[y/N]") != 'y')
	return 0;
    if (cuser.money >= 19) {
	vice(19, "付費隱身");
	currutmp->invisible %= 2;
	vmsg((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
    }
    return 0;
}
#endif

int
p_from(void)
{
    char tmp_from[sizeof(currutmp->from)];
    if (getans("確定要改故鄉?[y/N]") != 'y')
	return 0;
    reload_money();
    if (cuser.money < 49)
	return 0;
    if (getdata_buf(b_lines - 1, 0, "請輸入新故鄉:",
		    tmp_from, sizeof(tmp_from), DOECHO)) {
	vice(49, "更改故鄉");
	strlcpy(currutmp->from, tmp_from, sizeof(currutmp->from));
	currutmp->from_alias = 0;
    }
    return 0;
}

int
p_exmail(void)
{
    char            ans[4], buf[200];
    int             n;

    if (cuser.exmailbox >= MAX_EXKEEPMAIL) {
	vmsgf("容量最多增加 %d 封，不能再買了。", MAX_EXKEEPMAIL);
	return 0;
    }
    snprintf(buf, sizeof(buf),
	     "您曾增購 %d 封容量，還要再買多少?", cuser.exmailbox);

    getdata_str(b_lines - 2, 0, buf, ans, sizeof(ans), LCECHO, "1");

    n = atoi(ans);
    if (!ans[0] || n<=0)
	return 0;
    if (n + cuser.exmailbox > MAX_EXKEEPMAIL)
	n = MAX_EXKEEPMAIL - cuser.exmailbox;
    reload_money();
    if (cuser.money < n * 1000)
	return 0;
    vice(n * 1000, "購買信箱");
    inmailbox(n);
    return 0;
}

void
mail_redenvelop(const char *from, const char *to, int money, char mode)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;

    sethomepath(genbuf, to);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: %s\n"
	    "標題: 招財進寶\n"
	    "時間: %s\n"
	    ANSI_COLOR(1;33) "親愛的 %s ：\n\n" ANSI_RESET
	    ANSI_COLOR(1;31) "    我包給你一個 %d 元的大紅包喔 ^_^\n\n"
	    "    禮輕情意重，請笑納...... ^_^" ANSI_RESET "\n",
	    from, ctime4(&now), to, money);
    fclose(fp);
    snprintf(fhdr.title, sizeof(fhdr.title), "招財進寶");
    strlcpy(fhdr.owner, from, sizeof(fhdr.owner));

    if (mode == 'y')
	vedit(genbuf, NA, NULL);
    sethomedir(genbuf, to);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

/* 計算贈與稅 */
int
give_tax(int money)
{
    int             i, tax = 0;
    int      tax_bound[] = {1000000, 100000, 10000, 1000, 0};
    double   tax_rate[] = {0.4, 0.3, 0.2, 0.1, 0.08};
    for (i = 0; i <= 4; i++)
	if (money > tax_bound[i]) {
	    tax += (money - tax_bound[i]) * tax_rate[i];
	    money -= (money - tax_bound[i]);
	}
    return (tax <= 0) ? 1 : tax;
}

int do_give_money(char *id, int uid, int money)
{
    int tax;
#ifdef PLAY_ANGEL
    userec_t        xuser;
#endif

    reload_money();
    if (money > 0 && cuser.money >= money) {
	tax = give_tax(money);
	if (money - tax <= 0)
	    return -1;		/* 繳完稅就沒錢給了 */
	deumoney(uid, money - tax);
	demoney(-money);
	log_file(FN_MONEY, LOG_CREAT | LOG_VF, "%-12s 給 %-12s %d\t(稅後 %d)\t%s",
                 cuser.userid, id, money, money - tax, ctime4(&now));
#ifdef PLAY_ANGEL
	getuser(id, &xuser);
	if (!strcmp(xuser.myangel, cuser.userid)){
	    mail_redenvelop(
		    getkey("他是你的小主人，是否匿名？[Y/n]") == 'n' ?
		    cuser.userid : "小天使", id, money - tax,
			getans("要自行書寫紅包袋嗎？[y/N]"));
	} else
#endif
	mail_redenvelop(cuser.userid, id, money - tax,
		getans("要自行書寫紅包袋嗎？[y/N]"));
	if (money < 50) {
	    usleep(2000000);
	} else if (money < 200) {
	    usleep(500000);
	} else {
	    usleep(100000);
	}
	return 0;
    }
    return -1;
}

int
p_give(void)
{
    int             uid;
    char            id[IDLEN + 1], money_buf[20];

    move(1, 0);
    usercomplete("這位幸運兒的id:", id);
    if (!id[0] || !strcmp(cuser.userid, id) ||
	!getdata(2, 0, "要給多少錢:", money_buf, 7, LCECHO)) {
	vmsg("交易取消!");
	return -1;
    }
    if ((uid = searchuser(id, id)) == 0) {
	vmsg("查無此人!");
	return -1;
    }
    return do_give_money(id, uid, atoi(money_buf));
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
	   SHM->GV2.e.dymaxactive > 2000 ? SHM->GV2.e.dymaxactive : MAX_ACTIVE,
#else
	   MAX_ACTIVE,
#endif
#ifdef DETECT_CLIENT
	   client_code,
#endif
	   compile_time, ctime4(&start_time));
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
	       "sbrk: %d KB, "
#endif
#ifdef __linux__
	       "VmData: %d KB, VmStk: %d KB, "
#endif
	       "idrss: %d KB, isrss: %d KB\n",
#ifdef IA32
	       ((int)sbrk(0) - 0x8048000) / 1024,
#endif
#ifdef __linux__
	       vmdata, vmstk,
#endif
	       (int)ru.ru_idrss, (int)ru.ru_isrss);
	prints("CPU 用量:   %ld.%06ldu %ld.%06lds",
	       ru.ru_utime.tv_sec, ru.ru_utime.tv_usec,
	       ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
#ifdef CPULIMIT
	prints(" (limit %d secs)", (int)(CPULIMIT * 60));
#endif
	outs("\n特別參數:"
#ifdef CRITICAL_MEMORY
		" CRITICAL_MEMORY"
#endif
#ifdef OUTTACACHE
		" OUTTACACHE"
#endif
		);
    }
    pressanykey();
    return 0;
}

