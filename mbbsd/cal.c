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
	errorno = 1;
    else if (is_playing(unmode))
	errorno = 2;

    if (errorno && !(state == LOCK_THIS && errorno == LOCK_MULTI)) {
	clear();
	move(10, 20);
	if (errorno == 1)
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

/* Heat:發票 */
int
vice(int money, char *item)
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
    mail_id(cuser.userid, buf, "etc/vice.txt", "Ptt經濟部");
    return 0;
}

#define lockreturn(unmode, state) if(lockutmpmode(unmode, state)) return
#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
#define lockbreak(unmode, state) if(lockutmpmode(unmode, state)) break
#define SONGBOOK  "etc/SONGBOOK"
#define OSONGPATH "etc/SONGO"

static int
osong(char *defaultid)
{
    char            destid[IDLEN + 1], buf[200], genbuf[200], filename[256],
                    say[51];
    char            receiver[45], ano[3];
    FILE           *fp, *fp1;
    //*fp2;
    fileheader_t    mail;
    int             nsongs;

    strlcpy(buf, Cdatedate(&now), sizeof(buf));

    lockreturn0(OSONG, LOCK_MULTI);

    /* Jaky 一人一天點一首 */
    if (!strcmp(buf, Cdatedate(&cuser.lastsong)) && !HAS_PERM(PERM_SYSOP)) {
	move(22, 0);
	vmsg("你今天已經點過囉，明天再點吧....");
	unlockutmpmode();
	return 0;
    }
    if (cuser.money < 200) {
	move(22, 0);
	vmsg("點歌要200銀唷!....");
	unlockutmpmode();
	return 0;
    }
    move(12, 0);
    clrtobot();
    prints("親愛的 %s 歡迎來到歐桑自動點歌系統\n", cuser.userid);
    trans_buffer[0] = 0;
    if (!defaultid) {
	getdata(13, 0, "要點給誰呢:[可直接按 Enter 先選歌]",
		destid, sizeof(destid), DOECHO);
	while (!destid[0]) {
	    a_menu("點歌歌本", SONGBOOK, 0);
	    clear();
	    getdata(13, 0, "要點給誰呢:[可按 Enter 重新選歌]",
		    destid, sizeof(destid), DOECHO);
	}
    } else
	strlcpy(destid, defaultid, sizeof(destid));

    /* Heat:點歌者匿名功能 */
    getdata(14, 0, "要匿名嗎?[y/n]:", ano, sizeof(ano), LCECHO);

    if (!destid[0]) {
	unlockutmpmode();
	return 0;
    }
    getdata_str(14, 0, "想要要對他(她)說..:", say,
		sizeof(say), DOECHO, "我愛妳..");
    snprintf(save_title, sizeof(save_title),
	     "%s:%s", (ano[0] == 'y') ? "匿名者" : cuser.userid, say);
    getdata_str(16, 0, "寄到誰的信箱(可用E-mail)?",
		receiver, sizeof(receiver), LCECHO, destid);

    if (!trans_buffer[0]) {
	outs("\n接著要選歌囉..進入歌本好好的選一首歌吧..^o^");
	pressanykey();
	a_menu("點歌歌本", SONGBOOK, 0);
    }
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
    snprintf(mail.title, sizeof(mail.title),
	     "◇ %s 點給 %s ",
	     (ano[0] == 'y') ? "匿名者" : cuser.userid, destid);

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
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf),
		     "%s%s%s", buf,
		     (ano[0] == 'y') ? "匿名者" : cuser.userid, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	while ((po = strstr(buf, "<~Des~>"))) {
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s", buf, destid, po + 7);
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


    if (append_record(OSONGPATH "/.DIR", &mail, sizeof(mail)) != -1) {
	cuser.lastsong = now;
	/* Jaky 超過 500 首歌就開始砍 */
	nsongs = get_num_records(OSONGPATH "/.DIR", sizeof(mail));
	if (nsongs > 500) {
	    delete_range(OSONGPATH "/.DIR", 1, nsongs - 500);
	}
	/* 把第一首拿掉 */
	vice(200, "點歌");
    }
    snprintf(save_title, sizeof(save_title),
	     "%s:%s", (ano[0] == 'y') ? "匿名者" : cuser.userid, say);
    hold_mail(filename, destid);

    if (receiver[0]) {
#ifndef USE_BSMTP
	bbs_sendmail(filename, save_title, receiver);
#else
	bsmtp(filename, save_title, receiver, 0);
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
    osong(NULL);
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
    if (getans("確定要改故鄉?[y/N]") != 'y')
	return 0;
    reload_money();
    if (cuser.money < 49)
	return 0;
    if (getdata_buf(b_lines - 1, 0, "請輸入新故鄉:",
		    currutmp->from, sizeof(currutmp->from), DOECHO)) {
	vice(49, "更改故鄉");
	currutmp->from_alias = 0;
    }
    return 0;
}

int
p_exmail(void)
{
    char            ans[4], buf[200];
    int             n;
//   Ptt: what is it for?
//    assert(MAX_EXKEEPMAIL < (1<< (sizeof(cuser.exmailbox)*8-1) ));
    if (cuser.exmailbox >= MAX_EXKEEPMAIL) {
	vmsg("容量最多增加 %d 封，不能再買了。", MAX_EXKEEPMAIL);
	return 0;
    }
    snprintf(buf, sizeof(buf),
	     "您曾增購 %d 封容量，還要再買多少?", cuser.exmailbox);

    getdata_str(b_lines - 2, 0, buf, ans, sizeof(ans), LCECHO, "10");

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
mail_redenvelop(char *from, char *to, int money, char mode)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;
    snprintf(genbuf, sizeof(genbuf), "home/%c/%s", to[0], to);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: %s\n"
	    "標題: 招財進寶\n"
	    "時間: %s\n"
	    "\033[1;33m親愛的 %s ：\n\n\033[m"
	    "\033[1;31m    我包給你一個 %d 元的大紅包喔 ^_^\n\n"
	    "    禮輕情意重，請笑納...... ^_^\033[m\n",
	    from, ctime4(&now), to, money);
    fclose(fp);
    snprintf(fhdr.title, sizeof(fhdr.title), "招財進寶");
    strlcpy(fhdr.owner, from, sizeof(fhdr.owner));

    if (mode == 'y')
	vedit(genbuf, NA, NULL);
    snprintf(genbuf, sizeof(genbuf), "home/%c/%s/.DIR", to[0], to);
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

int
p_give(void)
{
    int             money, tax;
    char            id[IDLEN + 1], money_buf[20];
#ifdef PLAY_ANGEL
    userec_t        xuser;
#endif

    move(1, 0);
    usercomplete("這位幸運兒的id:", id);
    if (!id[0] || !strcmp(cuser.userid, id) ||
	!getdata(2, 0, "要給多少錢:", money_buf, 7, LCECHO))
	return 0;
    money = atoi(money_buf);
    reload_money();
    if (money > 0 && cuser.money >= money) {
	tax = give_tax(money);
	if (money - tax <= 0)
	    return 0;		/* 繳完稅就沒錢給了 */
	deumoney(searchuser(id), money - tax); // TODO if searchuser(id) return 0
	demoney(-money);
	log_file(FN_MONEY, LOG_CREAT | LOG_VF, "%s\t給%s\t%d\t%s",
                 cuser.userid, id, money - tax, ctime4(&now));
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
    }
    return 0;
}

int
p_sysinfo(void)
{
    char            *cpuloadstr;
    int             load;
    extern char    *compile_time;

    load = cpuload(NULL);
    cpuloadstr = (load < 5 ? "良好" : (load < 20 ? "尚可" : "過重"));

    clear();
    showtitle("系統資訊", BBSNAME);
    move(2, 0);
    prints("您現在位於 " TITLE_COLOR BBSNAME "\033[m (" MYIP ")\n"
	   "系統負載情況: %s\n"
	   "線上服務人數: %d/%d\n"
	   "編譯時間:     %s\n"
	   "起始時間:     %s\n",
	   cpuloadstr, SHM->UTMPnumber,
#ifdef DYMAX_ACTIVE
	   SHM->GV2.e.dymaxactive > 2000 ? SHM->GV2.e.dymaxactive : MAX_ACTIVE,
#else
	   MAX_ACTIVE,
#endif
	   compile_time, ctime4(&start_time));
    if (HAS_PERM(PERM_SYSOP)) {
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	prints("記憶體用量: "
#ifdef IA32
	       "sbrk: %d KB, "
#endif
	       "idrss: %d KB, isrss: %d KB\n",
#ifdef IA32
	       ((int)sbrk(0) - 0x8048000) / 1024,
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

