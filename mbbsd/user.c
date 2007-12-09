/* $Id$ */
#include "bbs.h"
static char    * const sex[8] = {
    MSG_BIG_BOY, MSG_BIG_GIRL, MSG_LITTLE_BOY, MSG_LITTLE_GIRL,
    MSG_MAN, MSG_WOMAN, MSG_PLANT, MSG_MIME
};

#ifdef CHESSCOUNTRY
static const char * const chess_photo_name[3] = {
    "photo_fivechess", "photo_cchess", "photo_go",
};

static const char * const chess_type[3] = {
    "五子棋", "象棋", "圍棋",
};
#endif

int
kill_user(int num, const char *userid)
{
    userec_t u;
    char src[256], dst[256];

    if(!userid || num<=0 ) return -1;
    sethomepath(src, userid);
    snprintf(dst, sizeof(dst), "tmp/%s", userid);
    friend_delete_all(userid, FRIEND_ALOHA);
    delete_allpost(userid);
    if (dashd(src) && Rename(src, dst) == 0) {
	snprintf(src, sizeof(src), "/bin/rm -fr home/%c/%s >/dev/null 2>&1", userid[0], userid);
	system(src);
    }

    memset(&u, 0, sizeof(userec_t));
    log_usies("KILL", getuserid(num));
    setuserid(num, "");
    passwd_update(num, &u);
    return 0;
}
int
u_loginview(void)
{
    int             i;
    unsigned int    pbits = cuser.loginview;

    clear();
    move(4, 0);
    for (i = 0; i < NUMVIEWFILE; i++)
	prints("    %c. %-20s %-15s \n", 'A' + i,
	       loginview_file[i][1], ((pbits >> i) & 1 ? "ˇ" : "Ｘ"));

    clrtobot();
    while ((i = getkey("請按 [A-N] 切換設定，按 [Return] 結束："))!='\r')
       {
	i = i - 'a';
	if (i >= NUMVIEWFILE || i < 0)
	    bell();
	else {
	    pbits ^= (1 << i);
	    move(i + 4, 28);
	    outs((pbits >> i) & 1 ? "ˇ" : "Ｘ");
	}
    }

    if (pbits != cuser.loginview) {
	cuser.loginview = pbits;
	passwd_update(usernum, &cuser);
    }
    return 0;
}
int u_cancelbadpost(void)
{
   int day;
   if(cuser.badpost==0)
     {vmsg("你並沒有劣文."); return 0;}
        
   if(search_ulistn(usernum,2))
     {vmsg("請登出其他視窗, 否則不受理."); return 0;}

   passwd_query(usernum, &cuser);
   day = 180 - (now - cuser.timeremovebadpost ) / 86400;
   if(day>0 && day<=180)
     {
      vmsgf("每 180 天才能申請一次, 還剩 %d 天.", day);
      vmsg("您也可以注意站方是否有勞動服務方式刪除劣文.");
      return 0;
     }

   if(
      getkey("我願意尊守站方規定,組規,以及板規[y/N]?")!='y' ||
      getkey("我願意尊重不歧視族群,不鬧板,尊重各板主權力[y/N]?")!='y' ||
      getkey("我願意謹慎發表有意義言論,不謾罵攻擊,不跨板廣告[y/N]?")!='y' )

     {vmsg("請您思考清楚後再來申請刪除."); return 0;}

   if(search_ulistn(usernum,2))
     {vmsg("請登出其他視窗, 否則不受理."); return 0;}
   if(cuser.badpost)
   {
       cuser.badpost--;
       cuser.timeremovebadpost = now;
       passwd_update(usernum, &cuser);
       log_file("log/cancelbadpost.log", LOG_VF|LOG_CREAT,
	        "%s %s 刪除一篇劣文\n", Cdate(&now), cuser.userid);
   }
   vmsg("恭喜您已經成功\刪除一篇劣文.");
   return 0;
}

void
user_display(const userec_t * u, int adminmode)
{
    int             diff = 0;
    char            genbuf[200];

    clrtobot();
    prints(
	   "        " ANSI_COLOR(30;41) "┴┬┴┬┴┬" ANSI_RESET "  " ANSI_COLOR(1;30;45) "    使 用 者"
	   " 資 料        "
	   "     " ANSI_RESET "  " ANSI_COLOR(30;41) "┴┬┴┬┴┬" ANSI_RESET "\n");
    prints("                代號暱稱: %s(%s)\n"
	   "                真實姓名: %s"
#if FOREIGN_REG_DAY > 0
	   " %s%s"
#elif defined(FOREIGN_REG)
	   " %s"
#endif
	   "\n"
	   "                居住住址: %s\n"
	   "                電子信箱: %s\n"
	   "                性    別: %s\n"
	   "                銀行帳戶: %d 銀兩\n",
	   u->userid, u->nickname, u->realname,
#if FOREIGN_REG_DAY > 0
	   u->uflag2 & FOREIGN ? "(外籍: " : "",
	   u->uflag2 & FOREIGN ?
		(u->uflag2 & LIVERIGHT) ? "永久居留)" : "未取得居留權)"
		: "",
#elif defined(FOREIGN_REG)
	   u->uflag2 & FOREIGN ? "(外籍)" : "",
#endif
	   u->address, u->email,
	   sex[u->sex % 8], u->money);

    sethomedir(genbuf, u->userid);
    prints("                私人信箱: %d 封  (購買信箱: %d 封)\n"
	   "                手機號碼: %010d\n"
	   "                生    日: %04i/%02i/%02i\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox, u->mobile,
	   u->year + 1900, u->month, u->day
	   );

#ifdef ASSESS
    prints("                優 劣 文: 優:%d / 劣:%d\n",
           u->goodpost, u->badpost);
#endif // ASSESS

    prints("                上站位置: %s\n", u->lasthost);

#ifdef PLAY_ANGEL
    if (adminmode)
	prints("                小 天 使: %s\n",
		u->myangel[0] ? u->myangel : "無");
#endif
    prints("                註冊日期: %s", ctime4(&u->firstlogin));
    prints("                前次光臨: %s", ctime4(&u->lastlogin));
    prints("                上站文章: %d 次 / %d 篇\n",
	   u->numlogins, u->numposts);

#ifdef CHESSCOUNTRY
    {
	int i, j;
	FILE* fp;
	for(i = 0; i < 2; ++i){
	    sethomefile(genbuf, u->userid, chess_photo_name[i]);
	    fp = fopen(genbuf, "r");
	    if(fp != NULL){
		for(j = 0; j < 11; ++j)
		    fgets(genbuf, 200, fp);
		fgets(genbuf, 200, fp);
		prints("%12s棋國自我描述: %s", chess_type[i], genbuf + 11);
		fclose(fp);
	    }
	}
    }
#endif

    if (adminmode) {
	strcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF");
	for (diff = 0; diff < 32; diff++)
	    if (!(u->userlevel & (1 << diff)))
		genbuf[diff] = '-';
	prints("                認證資料: %s\n"
	       "                user權限: %s\n",
	       u->justify, genbuf);
    } else {
	diff = (now - login_start_time) / 60;
	prints("                停留期間: %d 小時 %2d 分\n",
	       diff / 60, diff % 60);
    }

    /* Thor: 想看看這個 user 是那些板的板主 */
    if (u->userlevel >= PERM_BM) {
	int             i;
	boardheader_t  *bhdr;

	outs("                擔任板主: ");

	for (i = 0, bhdr = bcache; i < numboards; i++, bhdr++) {
	    if (is_uBM(bhdr->BM, u->userid)) {
		outs(bhdr->brdname);
		outc(' ');
	    }
	}
	outc('\n');
    }
    outs("        " ANSI_COLOR(30;41) "┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴"
	 "┬┴┬┴┬┴┬" ANSI_RESET);

    outs((u->userlevel & PERM_LOGINOK) ?
	 "\n您的註冊程序已經完成，歡迎加入本站" :
	 "\n如果要提昇權限，請參考本站公佈欄辦理註冊");

#ifdef NEWUSER_LIMIT
    if ((u->lastlogin - u->firstlogin < 3 * 86400) && !HasUserPerm(PERM_POST))
	outs("\n新手上路，三天後開放權限");
#endif
}

void
mail_violatelaw(const char *crime, const char *police, const char *reason, const char *result)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;

    sendalert(crime,  ALERT_PWD_PERM);

    sethomepath(genbuf, crime);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: [" BBSMNAME "警察局]\n"
	    "標題: [報告] 違法報告\n"
	    "時間: %s\n"
	    ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n     " ANSI_COLOR(1;32) "%s" ANSI_RESET
	    "因" ANSI_COLOR(1;35) "%s" ANSI_RESET "行為，\n違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET "，特此通知"
	    "\n請到 " GLOBAL_LAW " 查詢相關法規資訊，並到 Play-Pay-ViolateLaw 繳交罰單",
	    ctime4(&now), police, crime, reason, result);
    fclose(fp);
    strcpy(fhdr.title, "[報告] 違法判決報告");
    strcpy(fhdr.owner, "[" BBSMNAME "警察局]");
    sethomedir(genbuf, crime);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

void
kick_all(char *user)
{
   userinfo_t *ui;
   int num = searchuser(user, NULL), i=1;
   while((ui = (userinfo_t *) search_ulistn(num, i))>0)
       {
         if(ui == currutmp) i++;
         if ((ui->pid <= 0 || kill(ui->pid, SIGHUP) == -1))
                         purge_utmp(ui);
         log_usies("KICK ALL", user);
       }
}

void
violate_law(userec_t * u, int unum)
{
    char            ans[4], ans2[4];
    char            reason[128];
    move(1, 0);
    clrtobot();
    move(2, 0);
    outs("(1)Cross-post (2)亂發廣告信 (3)亂發連鎖信\n");
    outs("(4)騷擾站上使用者 (8)其他以罰單處置行為\n(9)砍 id 行為\n");
    getdata(5, 0, "(0)結束", ans, 3, DOECHO);
    switch (ans[0]) {
    case '1':
	strcpy(reason, "Cross-post");
	break;
    case '2':
	strcpy(reason, "亂發廣告信");
	break;
    case '3':
	strcpy(reason, "亂發連鎖信");
	break;
    case '4':
	while (!getdata(7, 0, "請輸入被檢舉理由以示負責：", reason, 50, DOECHO));
	strcat(reason, "[騷擾站上使用者]");
	break;
    case '8':
    case '9':
	while (!getdata(6, 0, "請輸入理由以示負責：", reason, 50, DOECHO));
	break;
    default:
	return;
    }
    getdata(7, 0, msg_sure_ny, ans2, 3, LCECHO);
    if (*ans2 != 'y')
	return;
    if (ans[0] == '9') {
	if (HasUserPerm(PERM_POLICE) && (u->numlogins > 100 || u->numposts > 100))
	    return;

        kill_user(unum, u->userid);
	post_violatelaw(u->userid, cuser.userid, reason, "砍除 ID");
    } else {
        kick_all(u->userid);
	u->userlevel |= PERM_VIOLATELAW;
	u->timeviolatelaw = now;
	u->vl_count++;
	passwd_update(unum, u);
	post_violatelaw(u->userid, cuser.userid, reason, "罰單處份");
	mail_violatelaw(u->userid, "站務警察", reason, "罰單處份");
    }
    pressanykey();
}

void Customize(void)
{
    char    done = 0;
    int     dirty = 0;
    int     key;

    /* cuser.uflag settings */
    static const unsigned int masks1[] = {
	MOVIE_FLAG,
	NO_MODMARK_FLAG	,
	COLORED_MODMARK,
#ifdef DBCSAWARE
	DBCSAWARE_FLAG,
#endif
	0,
    };

    static const char* desc1[] = {
	"動態看板",
	"隱藏文章修改符號(推文/修文) (~)",
	"改用色彩代替修改符號 (+)",
#ifdef DBCSAWARE
	"自動偵測雙位元字集(如全型中文)",
#endif
	0,
    };

    /* cuser.uflag2 settings */
    static const unsigned int masks2[] = {
	REJ_OUTTAMAIL,
	FAVNEW_FLAG,
	FAVNOHILIGHT,
	0,
    };

    static const char* desc2[] = {
	"拒收站外信",
	"新板自動進我的最愛",
	"單色顯示我的最愛",
	0,
    };

    while ( !done ) {
	int i = 0, ia = 0, ic = 0; /* general uflags */
	int iax = 0; /* extended flags */

	clear();
	showtitle("個人化設定", "個人化設定");
	move(2, 0);
	outs("您目前的個人化設定: ");
	move(4, 0);

	/* print uflag options */
	for (i = 0; masks1[i]; i++, ia++)
	{
	    clrtoeol();
	    prints( ANSI_COLOR(1;36) "%c" ANSI_RESET
		    ". %-40s%s\n",
		    'a' + ia, desc1[i],
		    (cuser.uflag & masks1[i]) ? 
		    ANSI_COLOR(1;36) "是" ANSI_RESET : "否");
	}
	ic = i;
	/* print uflag2 options */
	for (i = 0; masks2[i]; i++, ia++)
	{
	    clrtoeol();
	    prints( ANSI_COLOR(1;36) "%c" ANSI_RESET
		    ". %-40s%s" ANSI_RESET "\n",
		    'a' + ia, desc2[i],
		    (cuser.uflag2 & masks2[i]) ? 
		    ANSI_COLOR(1;36) "是" ANSI_RESET : "否");
	}
	/* extended stuff */
	{
	    char mindbuf[5];
	    const static char *wm[] = 
		{"一般", "進階", "未來", ""};

	    prints("%c. %-40s%s\n",
		    '1' + iax++,
		    "水球模式",
		    wm[(cuser.uflag2 & WATER_MASK)]);
	    memcpy(mindbuf, &currutmp->mind, 4);
	    mindbuf[4] = 0;
	    prints("%c. %-40s%s\n",
		    '1' + iax++,
		    "目前的心情",
		    mindbuf);
#ifdef PLAY_ANGEL
	    /* damn it, dirty stuff here */
	    if( HasUserPerm(PERM_ANGEL) )
	    {
		const char *am[4] = 
		{"男女皆可", "限女生", "限男生", "暫不接受新的小主人"};
		prints("%c. %-40s%10s\n",
			'1' + iax++,
			"開放小主人詢問",
			(REJECT_QUESTION ? "否" : "是"));
		prints("%c. %-40s%10s\n",
			'1' + iax++,
			"接受的小主人性別",
			am[ANGEL_STATUS()]);
	    }
#endif
	}

	/* input */
	key = getkey("請按 [a-%c,1-%c] 切換設定，其它任意鍵結束: ", 
		'a' + ia-1, '1' + iax -1);

	if (key >= 'a' && key < 'a' + ia)
	{
	    /* normal pref */
	    key -= 'a';
	    dirty = 1;

	    if(key < ic)
	    {
		cuser.uflag ^= masks1[key];
	    } else {
		key -= ic;
		cuser.uflag2 ^= masks2[key];
	    }
	    continue;
	}

	if (key < '1' || key >= '1' + iax)
	{
	    done = 1; continue;
	}
	/* extended keys */
	key -= '1';

	switch(key)
	{
	    case 0: 
		{
		    int     currentset = cuser.uflag2 & WATER_MASK;  
		    currentset = (currentset + 1) % 3;  
		    cuser.uflag2 &= ~WATER_MASK;  
		    cuser.uflag2 |= currentset;  
		    vmsg("修正水球模式後請正常離線再重新上線");  
		    dirty = 1;
		}
		continue;
	    case 1:
		{
		    char mindbuf[6] = "";
		    getdata(b_lines - 1, 0, "現在的心情? ",  
			    mindbuf, 5, DOECHO);  
		    if (strcmp(mindbuf, "通緝") == 0)  
			vmsg("不可以把自己設通緝啦!");  
		    else if (strcmp(mindbuf, "壽星") == 0)  
			vmsg("你不是今天生日欸!");  
		    else  
			memcpy(currutmp->mind, mindbuf, 4);  
		    dirty = 1;
		}
		continue;
	}
#ifdef PLAY_ANGEL
	if( HasUserPerm(PERM_ANGEL) ){
	    if (key == iax-2)
	    {
		SwitchBeingAngel();
		dirty = 1; continue;
	    } 
	    else if (key == iax-1)
	    {
		SwitchAngelSex(ANGEL_STATUS() + 1);
		dirty = 1; continue;
	    }
	}
#endif
	
    }

    if(dirty)
	passwd_update(usernum, &cuser);

    grayout_lines(0, b_lines, 0);
    vmsg("設定完成");
}

static char *
getregfile(char *buf)
{
    // not in user's home because s/he could zip his/her home
    snprintf(buf, PATHLEN, "jobspool/.regcode.%s", cuser.userid);
    return buf;
}

static char *
makeregcode(char *buf)
{
    char    fpath[PATHLEN];
    int     fd, i;
    const char *alphabet = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM";

    /* generate a new regcode */
    buf[13] = 0;
    buf[0] = 'v';
    buf[1] = '6';
    for( i = 2 ; i < 13 ; ++i )
	buf[i] = alphabet[random() % 52];

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

static void
delregcodefile(void)
{
    char    fpath[PATHLEN];
    getregfile(fpath);
    unlink(fpath);
}

#ifdef DEBUG
int
_debug_testregcode()
{
    char buf[16], rcode[16];
    char myid[16];
    int i = 1;

    clear();
    strcpy(myid, cuser.userid);
    do {
	getdata(0, 0, "輸入 id (空白結束): ",
		buf, IDLEN+1, DOECHO);
	if(buf[0])
	{
	    move(i++, 0);
	    i %= t_lines;
	    if(i == 0)
		i = 1;
	    strcpy(cuser.userid, buf);
	    prints("id: [%s], regcode: [%s]\n",
		    cuser.userid, getregcode(rcode));
	    move(i, 0);
	    clrtoeol();
	}
    } while (buf[0]);
    strcpy(cuser.userid, myid);

    pressanykey();
    return 0;
}
#endif

static void
justify_wait(char *userid, char *phone, char *career,
	char *rname, char *addr, char *mobile)
{
    char buf[PATHLEN];
    sethomefile(buf, userid, "justify.wait");
    if (phone[0] != 0) {
	FILE* fn = fopen(buf, "w");
	assert(fn);
	fprintf(fn, "%s\n%s\ndummy\n%s\n%s\n%s\n",
		phone, career, rname, addr, mobile);
	fclose(fn);
    }
}

static void email_justify(const userec_t *muser)
{
	char            tmp[IDLEN + 1], buf[256], genbuf[256];
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

	strlcpy(tmp, cuser.userid, sizeof(tmp));
	// XXX dirty, set userid=SYSOP
	strlcpy(cuser.userid, str_sysop, sizeof(cuser.userid));
#ifdef HAVEMOBILE
	if (strcmp(muser->email, "m") == 0 || strcmp(muser->email, "M") == 0)
	    mobile_message(mobile, buf);
	else
#endif
	    bsmtp("etc/registermail", buf, muser->email);
	strlcpy(cuser.userid, tmp, sizeof(cuser.userid));
        move(20,0);
        clrtobot();
	outs("我們即將寄出認證信 (您應該會在 10 分鐘內收到)\n"
	     "收到後您可以根據認證信標題的認證碼\n"
	     "輸入到 (U)ser -> (R)egister 後就可以完成註冊");
	pressanykey();
	return;
}

void
uinfo_query(userec_t *u, int adminmode, int unum)
{
    userec_t        x;
    register int    i = 0, fail, mail_changed;
    int             uid, ans;
    char            buf[STRLEN], *p;
    char            genbuf[200], reason[50];
    int money = 0;
    int             flag = 0, temp = 0, money_change = 0;

    fail = mail_changed = 0;

    memcpy(&x, u, sizeof(userec_t));
    ans = getans(adminmode ?
    "(1)改資料(2)密碼(3)權限(4)砍帳號(5)改ID(6)寵物(7)審判(M)信箱  [0]結束 " :
    "請選擇 (1)修改資料 (2)設定密碼 (M)修改信箱 (C) 個人化設定 ==> [0]結束 ");

    if (ans > '2' && ans != 'm' && ans != 'c' && !adminmode)
	ans = '0';

    if (ans == '1' || ans == '3' || ans == 'm') {
	clear();
	i = 1;
	move(i++, 0);
	outs(msg_uid);
	outs(x.userid);
    }
    switch (ans) {
    case 'c':
	Customize();
	return;
    case 'm':
	do {
	    getdata_str(i, 0, "電子信箱[變動要重新認證]：", buf, 50, DOECHO,
		    x.email);
	} while (!isvalidemail(buf) && vmsg("認證信箱不能用使用免費信箱"));
	i++;
	if (strcmp(buf, x.email) && strchr(buf, '@')) {
	    strlcpy(x.email, buf, sizeof(x.email));
	    mail_changed = 1 - adminmode;
	}
	break;
    case '7':
	violate_law(&x, unum);
	return;
    case '1':
	move(0, 0);
	outs("請逐項修改。");

	getdata_buf(i++, 0, " 暱 稱  ：", x.nickname,
		    sizeof(x.nickname), DOECHO);
	if (adminmode) {
	    getdata_buf(i++, 0, "真實姓名：",
			x.realname, sizeof(x.realname), DOECHO);
	    getdata_buf(i++, 0, "居住地址：",
			x.address, sizeof(x.address), DOECHO);
	}
	snprintf(buf, sizeof(buf), "%010d", x.mobile);
	getdata_buf(i++, 0, "手機號碼：", buf, 11, LCECHO);
	x.mobile = atoi(buf);
	snprintf(genbuf, sizeof(genbuf), "%i", (u->sex + 1) % 8);
	getdata_str(i++, 0, "性別 (1)葛格 (2)姐接 (3)底迪 (4)美眉 (5)薯叔 "
		    "(6)阿姨 (7)植物 (8)礦物：",
		    buf, 3, DOECHO, genbuf);
	if (buf[0] >= '1' && buf[0] <= '8')
	    x.sex = (buf[0] - '1') % 8;
	else
	    x.sex = u->sex % 8;

	while (1) {
	    snprintf(genbuf, sizeof(genbuf), "%04i/%02i/%02i",
		     u->year + 1900, u->month, u->day);
	    if (getdata_str(i, 0, "生日 西元/月月/日日：", buf, 11, DOECHO, genbuf) == 0) {
		x.month = u->month;
		x.day = u->day;
		x.year = u->year;
	    } else {
		int y, m, d;
		if (ParseDate(buf, &y, &m, &d))
		    continue;
		x.month = (unsigned char)m;
		x.day = (unsigned char)d;
		x.year = (unsigned char)(y - 1900);
	    }
	    if (!adminmode && x.year < 40)
		continue;
	    i++;
	    break;
	}

#ifdef PLAY_ANGEL
	if (adminmode) {
	    const char* prompt;
	    userec_t the_angel;
	    if (x.myangel[0] == 0 || x.myangel[0] == '-' ||
		    (getuser(x.myangel, &the_angel) &&
		     the_angel.userlevel & PERM_ANGEL))
		prompt = "小天使：";
	    else
		prompt = "小天使（此帳號已無小天使資格）：";
	    while (1) {
	        userec_t xuser;
		getdata_str(i, 0, prompt, buf, IDLEN + 1, DOECHO,
			x.myangel);
		if(buf[0] == 0 || strcmp(buf, "-") == 0 ||
			(getuser(buf, &xuser) &&
			    (xuser.userlevel & PERM_ANGEL)) ||
			strcmp(x.myangel, buf) == 0){
		    strlcpy(x.myangel, xuser.userid, IDLEN + 1);
		    ++i;
		    break;
		}

		prompt = "小天使：";
	    }
	}
#endif

#ifdef CHESSCOUNTRY
	{
	    int j, k;
	    FILE* fp;
	    for(j = 0; j < 2; ++j){
		sethomefile(genbuf, u->userid, chess_photo_name[j]);
		fp = fopen(genbuf, "r");
		if(fp != NULL){
		    FILE* newfp;
		    char mybuf[200];
		    for(k = 0; k < 11; ++k)
			fgets(genbuf, 200, fp);
		    fgets(genbuf, 200, fp);
		    chomp(genbuf);

		    snprintf(mybuf, 200, "%s棋國自我描述：", chess_type[j]);
		    getdata_buf(i, 0, mybuf, genbuf + 11, 80 - 11, DOECHO);
		    ++i;

		    sethomefile(mybuf, u->userid, chess_photo_name[j]);
		    strcat(mybuf, ".new");
		    if((newfp = fopen(mybuf, "w")) != NULL){
			rewind(fp);
			for(k = 0; k < 11; ++k){
			    fgets(mybuf, 200, fp);
			    fputs(mybuf, newfp);
			}
			fputs(genbuf, newfp);
			fputc('\n', newfp);

			fclose(newfp);

			sethomefile(genbuf, u->userid, chess_photo_name[j]);
			sethomefile(mybuf, u->userid, chess_photo_name[j]);
			strcat(mybuf, ".new");
			
			Rename(mybuf, genbuf);
		    }
		    fclose(fp);
		}
	    }
	}
#endif

	if (adminmode) {
	    int l;
	    if (HasUserPerm(PERM_BBSADM)) {
		snprintf(genbuf, sizeof(genbuf), "%d", x.money);
		if (getdata_str(i++, 0, "銀行帳戶：", buf, 10, DOECHO, genbuf))
		    if ((l = atol(buf)) != 0) {
			if (l != x.money) {
			    money_change = 1;
			    money = x.money;
			    x.money = l;
			}
		    }
	    }
	    snprintf(genbuf, sizeof(genbuf), "%d", x.exmailbox);
	    if (getdata_str(i++, 0, "購買信箱數：", buf, 6,
			    DOECHO, genbuf))
		if ((l = atol(buf)) != 0)
		    x.exmailbox = (int)l;

	    getdata_buf(i++, 0, "認證資料：", x.justify,
			sizeof(x.justify), DOECHO);
	    getdata_buf(i++, 0, "最近光臨機器：",
			x.lasthost, sizeof(x.lasthost), DOECHO);

	    // XXX 一變數不要多用途亂用 "fail"
	    snprintf(genbuf, sizeof(genbuf), "%d", x.numlogins);
	    if (getdata_str(i++, 0, "上線次數：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.numlogins = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->numposts);
	    if (getdata_str(i++, 0, "文章數目：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.numposts = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->goodpost);
	    if (getdata_str(i++, 0, "優良文章數:", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.goodpost = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->badpost);
	    if (getdata_str(i++, 0, "惡劣文章數:", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.badpost = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->vl_count);
	    if (getdata_str(i++, 0, "違法記錄：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.vl_count = fail;

	    snprintf(genbuf, sizeof(genbuf),
		     "%d/%d/%d", u->five_win, u->five_lose, u->five_tie);
	    if (getdata_str(i++, 0, "五子棋戰績 勝/敗/和：", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    char *strtok_pos;
		    p = strtok_r(buf, "/\r\n", &strtok_pos);
		    if (!p)
			break;
		    x.five_win = atoi(p);
		    p = strtok_r(NULL, "/\r\n", &strtok_pos);
		    if (!p)
			break;
		    x.five_lose = atoi(p);
		    p = strtok_r(NULL, "/\r\n", &strtok_pos);
		    if (!p)
			break;
		    x.five_tie = atoi(p);
		    break;
		}
	    snprintf(genbuf, sizeof(genbuf),
		     "%d/%d/%d", u->chc_win, u->chc_lose, u->chc_tie);
	    if (getdata_str(i++, 0, "象棋戰績 勝/敗/和：", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    char *strtok_pos;
		    p = strtok_r(buf, "/\r\n", &strtok_pos);
		    if (!p)
			break;
		    x.chc_win = atoi(p);
		    p = strtok_r(NULL, "/\r\n", &strtok_pos);
		    if (!p)
			break;
		    x.chc_lose = atoi(p);
		    p = strtok_r(NULL, "/\r\n", &strtok_pos);
		    if (!p)
			break;
		    x.chc_tie = atoi(p);
		    break;
		}
#ifdef FOREIGN_REG
	    if (getdata_str(i++, 0, "住在 1)台灣 2)其他：", buf, 2, DOECHO, x.uflag2 & FOREIGN ? "2" : "1"))
		if ((fail = atoi(buf)) > 0){
		    if (fail == 2){
			x.uflag2 |= FOREIGN;
		    }
		    else
			x.uflag2 &= ~FOREIGN;
		}
	    if (x.uflag2 & FOREIGN)
		if (getdata_str(i++, 0, "永久居留權 1)是 2)否：", buf, 2, DOECHO, x.uflag2 & LIVERIGHT ? "1" : "2")){
		    if ((fail = atoi(buf)) > 0){
			if (fail == 1){
			    x.uflag2 |= LIVERIGHT;
			    x.userlevel |= (PERM_LOGINOK | PERM_POST);
			}
			else{
			    x.uflag2 &= ~LIVERIGHT;
			    x.userlevel &= ~(PERM_LOGINOK | PERM_POST);
			}
		    }
		}
#endif
	    fail = 0;
	}
	break;

    case '2':
	i = 19;
	if (!adminmode) {
	    if (!getdata(i++, 0, "請輸入原密碼：", buf, PASSLEN, NOECHO) ||
		!checkpasswd(u->passwd, buf)) {
		outs("\n\n您輸入的密碼不正確\n");
		fail++;
		break;
	    }
	} else {
            FILE *fp;
	    char  witness[3][32], title[100];
	    for (i = 0; i < 3; i++) {
		if (!getdata(19 + i, 0, "請輸入協助證明之使用者：",
			     witness[i], sizeof(witness[i]), DOECHO)) {
		    outs("\n不輸入則無法更改\n");
		    fail++;
		    break;
		} else if (!(uid = searchuser(witness[i], NULL))) {
		    outs("\n查無此使用者\n");
		    fail++;
		    break;
		} else {
		    userec_t        atuser;
		    passwd_query(uid, &atuser);
		    if (now - atuser.firstlogin < 6 * 30 * 24 * 60 * 60) {
			outs("\n註冊未超過半年，請重新輸入\n");
			i--;
		    }
                    strcpy(witness[i], atuser.userid);
			// Adjust upper or lower case
		}
	    }
	    if (i < 3)
		break;

	    sprintf(title, "%s 的密碼重設通知 (by %s)",u->userid, cuser.userid);
            unlink("etc/updatepwd.log");
            if(! (fp = fopen("etc/updatepwd.log", "w")))
                     break;

            fprintf(fp, "%s 要求密碼重設:\n"
                        "見證人為 %s, %s, %s",
                         u->userid, witness[0], witness[1], witness[2] );
            fclose(fp);

            post_file(GLOBAL_SECURITY, title, "etc/updatepwd.log", "[系統安全局]");
	    mail_id(u->userid, title, "etc/updatepwd.log", cuser.userid);
	    for(i=0; i<3; i++)
	     {
	       mail_id(witness[i], title, "etc/updatepwd.log", cuser.userid);
             }
            i = 20;
	}

	if (!getdata(i++, 0, "請設定新密碼：", buf, PASSLEN, NOECHO)) {
	    outs("\n\n密碼設定取消, 繼續使用舊密碼\n");
	    fail++;
	    break;
	}
	strlcpy(genbuf, buf, PASSLEN);

	getdata(i++, 0, "請檢查新密碼：", buf, PASSLEN, NOECHO);
	if (strncmp(buf, genbuf, PASSLEN)) {
	    outs("\n\n新密碼確認失敗, 無法設定新密碼\n");
	    fail++;
	    break;
	}
	buf[8] = '\0';
	strlcpy(x.passwd, genpasswd(buf), sizeof(x.passwd));
	break;

    case '3':
	i = setperms(x.userlevel, str_permid);
	if (i == x.userlevel)
	    fail++;
	else {
	    flag = 1;
	    temp = x.userlevel;
	    x.userlevel = i;
	}
	break;

    case '4':
	i = QUIT;
	break;

    case '5':
	if (getdata_str(b_lines - 3, 0, "新的使用者代號：", genbuf, IDLEN + 1,
			DOECHO, x.userid)) {
	    if (searchuser(genbuf, NULL)) {
		outs("錯誤! 已經有同樣 ID 的使用者");
		fail++;
	    } else
		strlcpy(x.userid, genbuf, sizeof(x.userid));
	}
	break;
    case '6':
	if (x.mychicken.name[0])
	    x.mychicken.name[0] = 0;
	else
	    strlcpy(x.mychicken.name, "[死]", sizeof(x.mychicken.name));
	break;
    default:
	return;
    }

    if (fail) {
	pressanykey();
	return;
    }
    if (getans(msg_sure_ny) == 'y') {
	if (flag) {
	    post_change_perm(temp, i, cuser.userid, x.userid);
#ifdef PLAY_ANGEL
	    if (i & ~temp & PERM_ANGEL)
		mail_id(x.userid, "翅膀長出來了！", "etc/angel_notify", "[上帝]");
#endif
	}
	if (strcmp(u->userid, x.userid)) {
	    char            src[STRLEN], dst[STRLEN];

	    sethomepath(src, u->userid);
	    sethomepath(dst, x.userid);
	    Rename(src, dst);
	    setuserid(unum, x.userid);
	}
	if (mail_changed) {
	    char justify_tmp[REGLEN + 1];
	    char *phone, *career;
	    char *strtok_pos;
	    strlcpy(justify_tmp, u->justify, sizeof(justify_tmp));

	    x.userlevel &= ~(PERM_LOGINOK | PERM_POST);

	    phone  = strtok_r(justify_tmp, ":", &strtok_pos);
	    career = strtok_r(NULL, ":", &strtok_pos);

	    if (phone  == NULL) phone  = "";
	    if (career == NULL) career = "";

	    snprintf(buf, sizeof(buf), "%d", x.mobile);

	    justify_wait(x.userid, phone, career, x.realname, x.address, buf);
            email_justify(&x);
	}
	memcpy(u, &x, sizeof(x));
	if (i == QUIT) {
            kill_user(unum, x.userid);
	    return;
	} else
	    log_usies("SetUser", x.userid);
	if (money_change) {
	    char title[TTLEN+1];
	    char msg[200];
	    clrtobot();
	    clear();
	    while (!getdata(5, 0, "請輸入理由以示負責：",
			    reason, sizeof(reason), DOECHO));

	    snprintf(msg, sizeof(msg),
		    "   站長" ANSI_COLOR(1;32) "%s" ANSI_RESET "把" ANSI_COLOR(1;32) "%s" ANSI_RESET "的錢"
		    "從" ANSI_COLOR(1;35) "%d" ANSI_RESET "改成" ANSI_COLOR(1;35) "%d" ANSI_RESET "\n"
		    "   " ANSI_COLOR(1;37) "站長%s修改錢理由是：%s" ANSI_RESET,
		    cuser.userid, x.userid, money, x.money,
		    cuser.userid, reason);
	    snprintf(title, sizeof(title),
		    "[公安報告] 站長%s修改%s錢報告", cuser.userid,
		    x.userid);
	    post_msg(GLOBAL_SECURITY, title, msg, "[系統安全局]");
	    setumoney(unum, x.money);
	}
	passwd_update(unum, &x);
	if(flag)
    	  sendalert(x.userid,  ALERT_PWD_PERM); // force to reload perm
    }
}

int
u_info(void)
{
    move(2, 0);
    user_display(&cuser, 0);
    uinfo_query(&cuser, 0, usernum);
    strlcpy(currutmp->nickname, cuser.nickname, sizeof(currutmp->nickname));
    return 0;
}

int
u_cloak(void)
{
    outs((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
    return XEASY;
}

void
showplans_userec(userec_t *user)
{
    char            genbuf[200];

    if(user->userlevel & PERM_VIOLATELAW)
    {
	outs(" " ANSI_COLOR(1;31) "此人違規 尚未繳交罰單" ANSI_RESET);
	return;
    }

#ifdef CHESSCOUNTRY
    if (user_query_mode) {
	int    i = 0;
	FILE  *fp;

       sethomefile(genbuf, user->userid, chess_photo_name[user_query_mode - 1]);
	if ((fp = fopen(genbuf, "r")) != NULL)
	{
	    char   photo[6][256];
	    int    kingdom_bid = 0;
	    int    win = 0, lost = 0;

	    move(7, 0);
	    while (i < 12 && fgets(genbuf, 256, fp))
	    {
		chomp(genbuf);
		if (i < 6)  /* 讀照片檔 */
		    strcpy(photo[i], genbuf);
		else if (i == 6)
		    kingdom_bid = atoi(genbuf);
		else
		    prints("%s %s\n", photo[i - 7], genbuf);

		i++;
	    }
	    fclose(fp);

	    if (user_query_mode == 1) {
		win = user->five_win;
		lost = user->five_lose;
	    } else if(user_query_mode == 2) {
		win = user->chc_win;
		lost = user->chc_lose;
	    }
	    prints("%s <總共戰績> %d 勝 %d 敗\n", photo[5], win, lost);


	    /* 棋國國徽 */
	    setapath(genbuf, bcache[kingdom_bid - 1].brdname);
	    strlcat(genbuf, "/chess_ensign", sizeof(genbuf));
	    show_file(genbuf, 13, 10, ONLY_COLOR);
	    return;
	}
    }
#endif /* defined(CHESSCOUNTRY) */

    sethomefile(genbuf, user->userid, fn_plans);
    if (!show_file(genbuf, 7, MAX_QUERYLINES, ONLY_COLOR))
	prints("《個人名片》%s 目前沒有名片", user->userid);
}

void
showplans(const char *uid)
{
    userec_t user;
    if(getuser(uid, &user))
       showplans_userec(&user);
}
/*
 * return value: how many items displayed */
int
showsignature(char *fname, int *j, SigInfo *si)
{
    FILE           *fp;
    char            buf[256];
    int             i, lines = scr_lns;
    char            ch;

    clear();
    move(2, 0);
    lines -= 3;

    setuserfile(fname, "sig.0");
    *j = strlen(fname) - 1;
    si->total = 0;
    si->max = 0;

    for (ch = '1'; ch <= '9'; ch++) {
	fname[*j] = ch;
	if ((fp = fopen(fname, "r"))) {
	    si->total ++;
	    si->max = ch - '1';
	    if(lines > 0 && si->max >= si->show_start)
	    {
		prints(ANSI_COLOR(36) "【 簽名檔.%c 】" ANSI_RESET "\n", ch);
		lines--;
		if(lines > MAX_SIGLINES/2)
		    si->show_max = si->max;
		for (i = 0; lines > 0 && i < MAX_SIGLINES && 
			fgets(buf, sizeof(buf), fp) != NULL; i++)
		    outs(buf), lines--;
	    }
	    fclose(fp);
	}
    }
    if(lines > 0)
	si->show_max = si->max;
    return si->max;
}

int
u_editsig(void)
{
    int             aborted;
    char            ans[4];
    int             j, browsing = 0;
    char            genbuf[MAXPATHLEN];
    SigInfo	    si;

    memset(&si, 0, sizeof(si));

browse_sigs:

    showsignature(genbuf, &j, &si);
    getdata(0, 0, (browsing || (si.max > si.show_max)) ?
	    "簽名檔 (E)編輯 (D)刪除 (N)翻頁 (Q)取消？[Q] ":
	    "簽名檔 (E)編輯 (D)刪除 (Q)取消？[Q] ",
	    ans, sizeof(ans), LCECHO);

    if(ans[0] == 'n')
    {
	si.show_start = si.show_max + 1;
	if(si.show_start > si.max)
	    si.show_start = 0;
	browsing = 1;
	goto browse_sigs;
    }

    aborted = 0;
    if (ans[0] == 'd')
	aborted = 1;
    else if (ans[0] == 'e')
	aborted = 2;

    if (aborted) {
	if (!getdata(1, 0, "請選擇簽名檔(1-9)？[1] ", ans, sizeof(ans), DOECHO))
	    ans[0] = '1';
	if (ans[0] >= '1' && ans[0] <= '9') {
	    genbuf[j] = ans[0];
	    if (aborted == 1) {
		unlink(genbuf);
		outs(msg_del_ok);
	    } else {
		setutmpmode(EDITSIG);
		aborted = vedit(genbuf, NA, NULL);
		if (aborted != -1)
		    outs("簽名檔更新完畢");
	    }
	}
	pressanykey();
    }
    return 0;
}

int
u_editplan(void)
{
    char            genbuf[200];

    getdata(b_lines - 1, 0, "名片 (D)刪除 (E)編輯 [Q]取消？[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	setuserfile(genbuf, fn_plans);
	aborted = vedit(genbuf, NA, NULL);
	if (aborted != -1)
	    outs("名片更新完畢");
	pressanykey();
	return 0;
    } else if (genbuf[0] == 'd') {
	setuserfile(genbuf, fn_plans);
	unlink(genbuf);
	outmsg("名片刪除完畢");
    }
    return 0;
}

int
u_editcalendar(void)
{
    char            genbuf[200];

    getdata(b_lines - 1, 0, "行事曆 (D)刪除 (E)編輯 (H)說明 [Q]取消？[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	sethomefile(genbuf, cuser.userid, "calendar");
	aborted = vedit(genbuf, NA, NULL);
	if (aborted != -1)
	    vmsg("行事曆更新完畢");
	return 0;
    } else if (genbuf[0] == 'd') {
	sethomefile(genbuf, cuser.userid, "calendar");
	unlink(genbuf);
	vmsg("行事曆刪除完畢");
    } else if (genbuf[0] == 'h') {
	move(1, 0);
	clrtoline(b_lines);
	move(3, 0);
	prints("行事曆格式說明:\n編輯時以一行為單位，如:\n\n# 井號開頭的是註解\n2006/05/04 red 上批踢踢!\n\n其中的 red 是指表示的顏色。");
	pressanykey();
    }
    return 0;
}

/* 使用者填寫註冊表格 */
static void
getfield(int line, const char *info, const char *desc, char *buf, int len)
{
    char            prompt[STRLEN];
    char            genbuf[200];

    move(line, 2);
    prints("原先設定：%-30.30s (%s)", buf, info);
    snprintf(prompt, sizeof(prompt), "%s：", desc);
    if (getdata_str(line + 1, 2, prompt, genbuf, len, DOECHO, buf))
	strcpy(buf, genbuf);
    move(line, 2);
    prints("%s：%s", desc, buf);
    clrtoeol();
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
isvalidemail(const char *email)
{
    FILE           *fp;
    char            buf[128], *c;
    if (!strstr(email, "@"))
	return 0;
    for (c = strstr(email, "@"); *c != 0; ++c)
	if ('A' <= *c && *c <= 'Z')
	    *c += 32;

    if ((fp = fopen("etc/banemail", "r"))) {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (buf[0] == '#')
		continue;
	    chomp(buf);
	    if (buf[0] == 'A' && strcasecmp(&buf[1], email) == 0)
		return 0;
	    if (buf[0] == 'P' && strcasestr(email, &buf[1]))
		return 0;
	    if (buf[0] == 'S' && strcasecmp(strstr(email, "@") + 1, &buf[1]) == 0)
		return 0;
	}
	fclose(fp);
    }
    return 1;
}

static void
toregister(char *email, char *genbuf, char *phone, char *career,
	   char *rname, char *addr, char *mobile)
{
    FILE           *fn;
    char            buf[128];

    justify_wait(cuser.userid, phone, career, rname, addr, mobile);

    clear();
    stand_title("認證設定");
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
	   "但注意這可能會花上數天或更多時間。" ANSI_RESET "\n"
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
	if (strcmp(email, "x") == 0 || strcmp(email, "X") == 0)
	    break;

#ifdef HAVEMOBILE
	else if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0) {
	    if (isvalidmobile(mobile)) {
		char            yn[3];
		getdata(16, 0, "請再次確認您輸入的手機號碼正確嘛? [y/N]",
			yn, sizeof(yn), LCECHO);
		if (yn[0] == 'Y' || yn[0] == 'y')
		    break;
	    } else {
		move(17, 0);
		outs("指定的手機號碼不合法,"
		       "若您無手機門號請選擇其他方式認證");
	    }

	}
#endif
	else if (isvalidemail(email)) {
	    char            yn[3];
	    getdata(16, 0, "請再次確認您輸入的 E-Mail 位置正確嘛? [y/N]",
		    yn, sizeof(yn), LCECHO);
	    if (yn[0] == 'Y' || yn[0] == 'y')
		break;
	} else {
	    move(17, 0);
	    outs("指定的 E-Mail 不合法, 若您無 E-Mail 請輸入 x 由站長手動認證\n");
	    outs("但注意手動認證通常會花上數天的時間。\n");
	}
    }
    strlcpy(cuser.email, email, sizeof(cuser.email));
 REGFORM2:
    if (strcasecmp(email, "x") == 0) {	/* 手動認證 */
	if ((fn = fopen(fn_register, "a"))) {
	    fprintf(fn, "num: %d, %s", usernum, ctime4(&now));
	    fprintf(fn, "uid: %s\n", cuser.userid);
	    fprintf(fn, "name: %s\n", rname);
	    fprintf(fn, "career: %s\n", career);
	    fprintf(fn, "addr: %s\n", addr);
	    fprintf(fn, "phone: %s\n", phone);
	    fprintf(fn, "mobile: %s\n", mobile);
	    fprintf(fn, "email: %s\n", email);
	    fprintf(fn, "----\n");
	    fclose(fn);
	}
    } else {
	if (phone != NULL) {
#ifdef HAVEMOBILE
	    if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0)
		sprintf(genbuf, sizeof(genbuf),
			"%s:%s:<Mobile>", phone, career);
	    else
#endif
		snprintf(genbuf, sizeof(genbuf),
			 "%s:%s:<Email>", phone, career);
	    strlcpy(cuser.justify, genbuf, sizeof(cuser.justify));
	    sethomefile(buf, cuser.userid, "justify");
	}
       email_justify(&cuser);
    }
}

static int HaveRejectStr(const char *s, const char **rej)
{
    int     i;
    char    *ptr, *rejectstr[] =
	{"幹", "阿", "不", "你媽", "某", "笨", "呆", "..", "xx",
	 "你管", "管我", "猜", "天才", "超人", 
	 "ㄅ", "ㄆ", "ㄇ", "ㄈ", "ㄉ", "ㄊ", "ㄋ", "ㄌ", "ㄍ", "ㄎ", "ㄏ",
	 "ㄐ", "ㄑ", "ㄒ", "ㄓ",/*"ㄔ",*/    "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ",
	 "ㄧ", "ㄨ", "ㄩ", "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ", "ㄡ",
	 "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ", NULL};

    if( rej != NULL )
	for( i = 0 ; rej[i] != NULL ; ++i )
	    if( strstr(s, rej[i]) )
		return 1;

    for( i = 0 ; rejectstr[i] != NULL ; ++i )
	if( strstr(s, rejectstr[i]) )
	    return 1;

    if( (ptr = strstr(s, "ㄔ")) != NULL ){
	if( ptr != s && strncmp(ptr - 1, "都市", 4) == 0 )
	    return 0;
	return 1;
    }
    return 0;
}

static char *isvalidname(char *rname)
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
    return "您的輸入不正確";
#endif

}

static char *isvalidcareer(char *career)
{
#ifndef FOREIGN_REG
    const char    *rejectstr[] = {NULL};
    if (!(removespace(career) && career[0] < 0 && strlen(career) >= 6) ||
	strcmp(career, "家裡") == 0 || HaveRejectStr(career, rejectstr) )
	return "您的輸入不正確";
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
    return NULL;
}

static char *isvalidaddr(char *addr)
{
    const char    *rejectstr[] =
	{"地球", "銀河", "火星", NULL};

    if (!removespace(addr) || addr[0] > 0 || strlen(addr) < 15) 
	return "這個地址並不合法";
    if (strstr(addr, "信箱") != NULL || strstr(addr, "郵政") != NULL) 
	return "抱歉我們不接受郵政信箱";
    if ((strstr(addr, "市") == NULL && strstr(addr, "巿") == NULL &&
	 strstr(addr, "縣") == NULL && strstr(addr, "室") == NULL) ||
	HaveRejectStr(addr, rejectstr)             ||
	strcmp(&addr[strlen(addr) - 2], "段") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "路") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "巷") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "弄") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "區") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "市") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "街") == 0    )
	return "這個地址並不合法";
    return NULL;
}

static char *isvalidphone(char *phone)
{
    int     i;
    for( i = 0 ; phone[i] != 0 ; ++i )
	if( !isdigit((int)phone[i]) )
	    return "請不要加分隔符號";
    if (!removespace(phone) || 
	strlen(phone) < 9 || 
	strstr(phone, "00000000") != NULL ||
	strstr(phone, "22222222") != NULL    ) {
	return "這個電話號碼並不合法(請含區碼)" ;
    }
    return NULL;
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
    char            ans[3], *ptr, *errcode;
    char            genbuf[200];
    FILE           *fn;

    if (cuser.userlevel & PERM_LOGINOK) {
	outs("您的身份確認已經完成，不需填寫申請表");
	return XEASY;
    }
    if ((fn = fopen(fn_register, "r"))) {
	int i =0;
	while (fgets(genbuf, STRLEN, fn)) {
	    if ((ptr = strchr(genbuf, '\n')))
		*ptr = '\0';
	    if (strncmp(genbuf, "uid: ", 5) != 0)
		continue;
	    i++;
	    if(strcmp(genbuf + 5, cuser.userid) != 0)
		continue;
	    fclose(fn);
	    /* idiots complain about this, so bug them */
	    clear();
	    move(3, 0);
	    prints("   您的註冊申請單尚在處理中(處理順位: %d)，請耐心等候\n\n", i);
	    outs("   如果您已收到註冊碼卻看到這個畫面，那代表您在使用 Email 註冊後\n");
	    outs("   " ANSI_COLOR(1;31) "又另外申請了站長直接人工審核的註冊申請單。" 
		    ANSI_RESET "\n\n");
	    // outs("該死，都不看說明的...\n");
	    outs("   進入人工審核程序後 Email 註冊自動失效，有註冊碼也沒用，\n");
	    outs("   要等到審核完成 (會多花很多時間，通常起碼一天) ，所以請耐心等候。\n\n");

	    /* 下面是國王的 code 所需要的 message */
#if 0
	    outs("   另外請注意，若站長審註冊單時您正在站上則會無法審核、自動跳過。\n");
	    outs("   所以等候審核時請勿掛站。若超過兩三天仍未被審到，通常就是這個原因。\n");
#endif

	    vmsg("您的註冊申請單尚在處理中");
	    return FULLUPDATE;
	}
	fclose(fn);
    }
    strlcpy(rname, cuser.realname, sizeof(rname));
    strlcpy(addr, cuser.address, sizeof(addr));
    strlcpy(email, cuser.email, sizeof(email));
    snprintf(mobile, sizeof(mobile), "0%09d", cuser.mobile);
    if (cuser.month == 0 && cuser.day && cuser.year == 0)
	birthday[0] = 0;
    else
	snprintf(birthday, sizeof(birthday), "%04i/%02i/%02i",
		 1900 + cuser.year, cuser.month, cuser.day);
    sex_is[0] = (cuser.sex % 8) + '1';
    sex_is[1] = 0;
    career[0] = phone[0] = '\0';
    sethomefile(genbuf, cuser.userid, "justify.wait");
    if ((fn = fopen(genbuf, "r"))) {
	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(phone, genbuf, sizeof(phone));

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(career, genbuf, sizeof(career));

	fgets(genbuf, sizeof(genbuf), fn); // old version compatible

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(rname, genbuf, sizeof(rname));

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(addr, genbuf, sizeof(addr));

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(mobile, genbuf, sizeof(mobile));

	fclose(fn);
    }

    if (cuser.userlevel & PERM_NOREGCODE) {
	vmsg("您不被允許\使用認證碼認證。請填寫註冊申請單");
	goto REGFORM;
    }

    if (cuser.year != 0 &&	/* 已經第一次填過了~ ^^" */
	strcmp(cuser.email, "x") != 0 &&	/* 上次手動認證失敗 */
	strcmp(cuser.email, "X") != 0) {
	clear();
	stand_title("EMail認證");
	move(2, 0);
	prints("%s(%s) 您好，請輸入您的認證碼。\n"
	       "或您可以輸入 x 來重新填寫 E-Mail 或改由站長手動認證\n",
	       cuser.userid, cuser.nickname);
	inregcode[0] = 0;

	do{
	    getdata(10, 0, "您的認證碼：",
		    inregcode, sizeof(inregcode), DOECHO);
	    if( strcmp(inregcode, "x") == 0 || strcmp(inregcode, "X") == 0 )
		break;
	    if( inregcode[0] != 'v' || inregcode[1] != '6' ) {
		/* old regcode */
		vmsg("您輸入的認證碼因系統昇級已失效，"
		     "請輸入 x 重填一次 E-Mail");
	    } else if( strlen(inregcode) != 13 )
		vmsg("認證碼輸入不完全，應該一共有十三碼。");
	    else
		break;
	} while( 1 );

	if (strcmp(inregcode, getregcode(regcode)) == 0) {
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
	    sethomefile(genbuf, cuser.userid, "justify.wait");
	    unlink(genbuf);
	    snprintf(cuser.justify, sizeof(cuser.justify),
		     "%s:%s:email", phone, career);
	    sethomefile(genbuf, cuser.userid, "justify");
	    log_file(genbuf, LOG_CREAT, cuser.justify);
	    pressanykey();
	    u_exit("registed");
	    exit(0);
	    return QUIT;
	} else if (strcmp(inregcode, "x") != 0 &&
		   strcmp(inregcode, "X") != 0) {
	    vmsg("認證碼錯誤！");
	} else {
	    toregister(email, genbuf, phone, career, rname, addr, mobile);
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
	while (1) {
	    getfield(11, "含" ANSI_COLOR(1;33) "縣市" ANSI_RESET "及門寢號碼"
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
	    getfield(13, "不加-(), 包括長途區號", "連絡電話", phone, 11);
	    if( (errcode = isvalidphone(phone)) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	getfield(15, "只輸入數字 如:0912345678 (可不填)",
		 "手機號碼", mobile, 20);
	while (1) {
	    getfield(17, "西元/月月/日日 如:1984/02/29", "生日", birthday, sizeof(birthday));
	    if (birthday[0] == 0) {
		snprintf(birthday, sizeof(birthday), "%04i/%02i/%02i",
			 1900 + cuser.year, cuser.month, cuser.day);
		mon = cuser.month;
		day = cuser.day;
		year = cuser.year;
	    } else {
		int y, m, d;
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
	getfield(19, "1.葛格 2.姐接 ", "性別", sex_is, 2);
	getdata(20, 0, "以上資料是否正確(Y/N)？(Q)取消註冊 [N] ",
		ans, 3, LCECHO);
	if (ans[0] == 'q')
	    return 0;
	if (ans[0] == 'y')
	    break;
    }
    strlcpy(cuser.realname, rname, sizeof(cuser.realname));
    strlcpy(cuser.address, addr, sizeof(cuser.address));
    strlcpy(cuser.email, email, sizeof(cuser.email));
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
    trim(career);
    trim(addr);
    trim(phone);

    toregister(email, genbuf, phone, career, rname, addr, mobile);

    return FULLUPDATE;
}

/* 列出所有註冊使用者 */
static int      usercounter, totalusers;
static unsigned short u_list_special;

static int
u_list_CB(int num, userec_t * uentp)
{
    static int      i;
    char            permstr[8], *ptr;
    register int    level;

    if (uentp == NULL) {
	move(2, 0);
	clrtoeol();
	prints(ANSI_COLOR(7) "  使用者代號   %-25s   上站  文章  %s  "
	       "最近光臨日期     " ANSI_COLOR(0) "\n",
	       "綽號暱稱",
	       HasUserPerm(PERM_SEEULEVELS) ? "等級" : "");
	i = 3;
	return 0;
    }
    if (bad_user_id(uentp->userid))
	return 0;

    if ((uentp->userlevel & ~(u_list_special)) == 0)
	return 0;

    if (i == b_lines) {
	prints(ANSI_COLOR(34;46) "  已顯示 %d/%d 人(%d%%)  " ANSI_COLOR(31;47) "  "
	       "(Space)" ANSI_COLOR(30) " 看下一頁  " ANSI_COLOR(31) "(Q)" ANSI_COLOR(30) " 離開  " ANSI_RESET,
	       usercounter, totalusers, usercounter * 100 / totalusers);
	i = igetch();
	if (i == 'q' || i == 'Q')
	    return QUIT;
	i = 3;
    }
    if (i == 3) {
	move(3, 0);
	clrtobot();
    }
    level = uentp->userlevel;
    strlcpy(permstr, "----", 8);
    if (level & PERM_SYSOP)
	permstr[0] = 'S';
    else if (level & PERM_ACCOUNTS)
	permstr[0] = 'A';
    else if (level & PERM_SYSOPHIDE)
	permstr[0] = 'p';

    if (level & (PERM_BOARD))
	permstr[1] = 'B';
    else if (level & (PERM_BM))
	permstr[1] = 'b';

    if (level & (PERM_XEMPT))
	permstr[2] = 'X';
    else if (level & (PERM_LOGINOK))
	permstr[2] = 'R';

    if (level & (PERM_CLOAK | PERM_SEECLOAK))
	permstr[3] = 'C';

    ptr = (char *)Cdate(&uentp->lastlogin);
    ptr[18] = '\0';
    prints("%-14s %-27.27s%5d %5d  %s  %s\n",
	   uentp->userid,
	   uentp->nickname,
	   uentp->numlogins, uentp->numposts,
	   HasUserPerm(PERM_SEEULEVELS) ? permstr : "", ptr);
    usercounter++;
    i++;
    return 0;
}

int
u_list(void)
{
    char            genbuf[3];

    setutmpmode(LAUSERS);
    u_list_special = usercounter = 0;
    totalusers = SHM->number;
    if (HasUserPerm(PERM_SEEULEVELS)) {
	getdata(b_lines - 1, 0, "觀看 [1]特殊等級 (2)全部？",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2')
	    u_list_special = PERM_BASIC | PERM_CHAT | PERM_PAGE | PERM_POST | PERM_LOGINOK | PERM_BM;
    }
    u_list_CB(0, NULL);
    if (passwd_apply(u_list_CB) == -1) {
	outs(msg_nobody);
	return XEASY;
    }
    move(b_lines, 0);
    clrtoeol();
    prints(ANSI_COLOR(34;46) "  已顯示 %d/%d 的使用者(系統容量無上限)  "
	   ANSI_COLOR(31;47) "  (請按任意鍵繼續)  " ANSI_RESET, usercounter, totalusers);
    igetch();
    return 0;
}

#ifdef DBCSAWARE

/* detect if user is using an evil client that sends double
 * keys for DBCS data.
 * True if client is evil.
 */

int u_detectDBCSAwareEvilClient()
{
    int ret = 0;

    clear();
    move(1, 0);
    outs(ANSI_RESET
	    "* 本站支援自動偵測中文字的移動與編輯，但有些連線程式(如xxMan)\n"
	    "  會自行處理、多送按鍵，於是便會造成" ANSI_COLOR(1;37)
	    "一次移動兩個中文字的現象。" ANSI_RESET "\n\n"
	    "* 讓連線程式處理移動容易造成許\多"
	    "顯示及移動上的問題，所以我們建議您\n"
	    "  關閉該程式上的此項設定（通常叫「偵測(全型或雙位元組)中文」），\n"
	    "  讓 BBS 系統可以正確的控制你的畫面。\n\n"
	    ANSI_COLOR(1;33) 
	    "* 如果您看不懂上面的說明也無所謂，我們會自動偵測適合您的設定。"
	    ANSI_RESET "\n"
	    "  請在設定好連線程式成您偏好的模式後按" ANSI_COLOR(1;33)
	    "一下" ANSI_RESET "您鍵盤上的" ANSI_COLOR(1;33)
	    "←" ANSI_RESET "\n" ANSI_COLOR(1;36)
	    "  (另外左右方向鍵或寫 BS/Backspace 的倒退鍵與 Del 刪除鍵均可)\n"
	    ANSI_RESET);

    /* clear buffer */
    while(num_in_buf() > 0)
	igetch();

    while (1)
    {
	int ch = 0;

	move(12, 0);
	outs("這是偵測區，您的游標會出現在" 
		ANSI_COLOR(7) "這裡" ANSI_RESET);
	move(12, 15*2);
	ch = igetch();
	if(ch != KEY_LEFT && ch != KEY_RIGHT &&
		ch != Ctrl('H') && ch != '\177')
	{
	    move(14, 0);
	    outs("請按一下上面指定的鍵！ 你按到別的鍵了！");
	} else {
	    move(16, 0);
	    /* Actually you may also use num_in_buf here.  those clients
	     * usually sends doubled keys together in one packet.
	     * However when I was writing this, a bug (existed for more than 3
	     * years) of num_in_buf forced me to write new wait_input.
	     * Anyway it is fixed now.
	     */
	    if(wait_input(0.1, 1))
	    // if(igetch() == ch)
	    // if (num_in_buf() > 0)
	    {
		/* evil dbcs aware client */
		outs("偵測到您的連線程式會自行處理游標移動。\n\n"
			// "若日後因此造成瀏覽上的問題本站恕不處理。\n\n"
			"已設定為「讓您的連線程式處理游標移動」\n");
		ret = 1;
	    } else {
		/* good non-dbcs aware client */
		outs("您的連線程式似乎不會多送按鍵，"
			"這樣 BBS 可以更精準的控制畫面。\n\n"
			"已設定為「讓 BBS 伺服器直接處理游標移動」\n");
		ret = 0;
	    }
	    outs(  "\n若想改變設定請至 個人設定區 → 個人化設定 → \n"
		   "    調整「自動偵測雙位元字集(如全型中文)」之設定");
	    while(num_in_buf())
		igetch();
	    break;
	}
    }
    pressanykey();
    return ret;
}
#endif 

/* vim:sw=4
 */
