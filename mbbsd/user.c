/* $Id$ */
#include "bbs.h"

#define FN_NOTIN_WHITELIST_NOTICE "etc/whitemail.notice"

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
    int             i, in;
    unsigned int    pbits = cuser.loginview;

    clear();
    move(4, 0);
    for (i = 0; i < NUMVIEWFILE && loginview_file[i][0]; i++)
	prints("    %c. %-20s %-15s \n", 'A' + i,
	       loginview_file[i][1], ((pbits >> i) & 1 ? "ˇ" : "Ｘ"));
    in = i;

    clrtobot();
    while ((i = vmsgf("請按 [A-%c] 切換設定，按 [Return] 結束：", 
		    'A'+in-1))!='\r')
       {
	i = i - 'a';
	if (i >= in || i < 0)
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
   if (currutmp && (currutmp->alerts & ALERT_PWD))
       currutmp->alerts &= ~ALERT_PWD;

   day = 180 - (now - cuser.timeremovebadpost ) / DAY_SECONDS;
   if(day>0 && day<=180)
     {
      vmsgf("每 180 天才能申請一次, 還剩 %d 天.", day);
      vmsg("您也可以注意站方是否有勞動服務方式刪除劣文.");
      return 0;
     }

   if(
      vmsg("我願意尊守站方規定,組規,以及板規[y/N]?")!='y' ||
      vmsg("我願意尊重不歧視族群,不鬧板,尊重各板主權力[y/N]?")!='y' ||
      vmsg("我願意謹慎發表有意義言論,不謾罵攻擊,不跨板廣告[y/N]?")!='y' )

     {vmsg("請您思考清楚後再來申請刪除."); return 0;}

   if(search_ulistn(usernum,2))
     {vmsg("請登出其他視窗, 否則不受理."); return 0;}
   if(cuser.badpost)
   {
       int prev = cuser.badpost--;
       cuser.timeremovebadpost = now;
       passwd_update(usernum, &cuser);
       log_filef("log/cancelbadpost.log", LOG_CREAT,
	        "%s %s 刪除一篇劣文 (%d -> %d 篇)\n", 
		Cdate(&now), cuser.userid, prev, cuser.badpost);
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
    prints("\t\t代號暱稱: %s(%s)\n", u->userid, u->nickname);
    prints("\t\t真實姓名: %s", u->realname);
#if FOREIGN_REG_DAY > 0
    prints(" %s%s",
	   u->uflag2 & FOREIGN ? "(外籍: " : "",
	   u->uflag2 & FOREIGN ?
		(u->uflag2 & LIVERIGHT) ? "永久居留)" : "未取得居留權)"
		: "");
#elif defined(FOREIGN_REG)
    prints(" %s", u->uflag2 & FOREIGN ? "(外籍)" : "");
#endif
    outs("\n"); // end of realname
    prints("\t\t職業學歷: %s\n", u->career);
    prints("\t\t居住地址: %s\n", u->address);
    prints("\t\t電話手機: %s", u->phone);
	if (u->mobile) 
	    prints(" / %010d", u->mobile);
	outs("\n");

    prints("\t\t電子信箱: %s\n", u->email);
    prints("\t\t銀行帳戶: %d 銀兩\n", u->money);
    prints("\t\t性    別: %s\n", sex[u->sex%8]);
    prints("\t\t生    日: %04i/%02i/%02i (%s滿18歲)\n",
	   u->year + 1900, u->month, u->day, 
	   resolve_over18_user(u) ? "已" : "未");

    prints("\t\t註冊日期: %s (已滿%d天)\n", 
	    Cdate(&u->firstlogin), (int)((now - u->firstlogin)/DAY_SECONDS));
    prints("\t\t上次上站: %s (%s)\n", 
	    u->lasthost, Cdate(&u->lastlogin));

    if (adminmode) {
	strcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF");
	for (diff = 0; diff < 32; diff++)
	    if (!(u->userlevel & (1 << diff)))
		genbuf[diff] = '-';
	prints("\t\t帳號權限: %s\n", genbuf);
	prints("\t\t認證資料: %s\n", u->justify);
    }

    prints("\t\t上站文章: %d 次 / %d 篇\n",
	   u->numlogins, u->numposts);

    sethomedir(genbuf, u->userid);
    prints("\t\t私人信箱: %d 封  (購買信箱: %d 封)\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox);

    if (!adminmode) {
	diff = (now - login_start_time) / 60;
	prints("\t\t停留期間: %d 小時 %2d 分\n",
	       diff / 60, diff % 60);
    }

    /* Thor: 想看看這個 user 是那些板的板主 */
    if (u->userlevel >= PERM_BM) {
	int             i;
	boardheader_t  *bhdr;

	outs("\t\t擔任板主: ");

	for (i = 0, bhdr = bcache; i < numboards; i++, bhdr++) {
	    if (is_uBM(bhdr->BM, u->userid)) {
		outs(bhdr->brdname);
		outc(' ');
	    }
	}
	outc('\n');
    }

    // conditional fields
#ifdef ASSESS
    prints("\t\t優 劣 文: 優:%d / 劣:%d\n",
           u->goodpost, u->badpost);
#endif // ASSESS

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

#ifdef PLAY_ANGEL
    if (adminmode)
	prints("\t\t小 天 使: %s\n",
		u->myangel[0] ? u->myangel : "無");
#endif

    outs("        " ANSI_COLOR(30;41) "┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴"
	 "┬┴┬┴┬┴┬" ANSI_RESET);

    outs((u->userlevel & PERM_LOGINOK) ?
	 "\n您的註冊程序已經完成，歡迎加入本站" :
	 "\n如果要提昇權限，請參考本站公佈欄辦理註冊");

#ifdef NEWUSER_LIMIT
    if ((u->lastlogin - u->firstlogin < 3 * DAY_SECONDS) && !HasUserPerm(PERM_POST))
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
	    "時間: %s\n", ctime4(&now));
    fprintf(fp,
	    ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n     " ANSI_COLOR(1;32) "%s" ANSI_RESET
	    "因" ANSI_COLOR(1;35) "%s" ANSI_RESET "行為，\n"
	    "違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET "，特此通知\n\n"
	    "請到 " BN_LAW " 查詢相關法規資訊，並從主選單進入:\n"
	    "(P)lay【娛樂與休閒】=>(P)ay【Ｐtt量販店 】=> (1)ViolateLaw 繳罰單\n"
	    "以繳交罰單。\n",
	    police, crime, reason, result);
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
   while((ui = (userinfo_t *) search_ulistn(num, i)) != NULL)
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
	// 20080417, according to the request of admins, the numpost>100
	// if very easy to achive for such bad users (of course, because
	// usually they violate law by massive posting).
	// Also if he wants to prevent system auto cp check, he will
	// post -> logout -> login -> post. So both numlogin and numpost
	// are not good.
	// We changed the rule to registration date [2 month].
	if (HasUserPerm(PERM_POLICE) && ((now - u->firstlogin) >= 2*30*DAY_SECONDS))
	{
	    vmsg("使用者註冊已超過 60 天，無法砍除。");
	    return;
	}

        kick_all(u->userid);
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
	DBCS_NOINTRESC,
#endif
	DEFBACKUP_FLAG,
	0,
    };

    static const char* desc1[] = {
	"動態看板",
	"隱藏文章修改符號(推文/修文) (~)",
	"改用色彩代替修改符號 (+)",
#ifdef DBCSAWARE
	"自動偵測雙位元字集(如全型中文)",
	"禁止在雙位元中使用色碼(去一字雙色)",
#endif
	"預設備份信件與其它記錄", //"與聊天記錄",
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
	    static const char *wm[] = 
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
	    if (HasUserPerm(PERM_ANGEL))
	    {
		static const char *msgs[ANGELPAUSE_MODES] = {
		    "開放 (接受所有小主人發問)",
		    "停收 (只接受已回應過的小主人的問題)",
		    "關閉 (停止接受所有小主人的問題)",
		};
		prints("%c. %-40s%s\n", 
			'1' + iax++, 
			"小天使神諭呼叫器",
			msgs[currutmp->angelpause % ANGELPAUSE_MODES]);
	    }
#endif // PLAY_ANGEL
	}

	/* input */
	key = vmsgf("請按 [a-%c,1-%c] 切換設定，其它任意鍵結束: ", 
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
	    if (key == iax-1) 
	    { 
		angel_toggle_pause();
		// dirty = 1; // pure utmp change does not need pw dirty
		continue; 
	    }
	}
#endif //PLAY_ANGEL
    }

    grayout(1, b_lines-2, GRAYOUT_DARK);
    move(b_lines-1, 0); clrtoeol();

    if(dirty)
    {
	passwd_update(usernum, &cuser);
	outs("設定已儲存。\n");
    } else {
	outs("結束設定。\n");
    }

    redrawwin(); // in case we changed output pref (like DBCS)
    vmsg("設定完成");
}


void
uinfo_query(userec_t *u, int adminmode, int unum)
{
    userec_t        x;
    int    i = 0, fail;
    int             ans;
    char            buf[STRLEN];
    char            genbuf[200];
    char	    pre_confirmed = 0;
    int y = 0;
    int perm_changed;
    int mail_changed;
    int money_changed;
    int tokill = 0;
    int changefrom = 0;

    fail = 0;
    mail_changed = money_changed = perm_changed = 0;

    {
	// verify unum
	int xuid =  getuser(u->userid, &x);
	if (xuid != unum)
	{
	    move(b_lines-1, 0); clrtobol();
	    prints(ANSI_COLOR(1;31) "錯誤資訊: unum=%d (lookup xuid=%d)"
		    ANSI_RESET "\n", unum, xuid);
	    vmsg("系統錯誤: 使用者資料號碼 (unum) 不合。請至 " BN_BUGREPORT "報告。");
	    return;
	}
    }

    memcpy(&x, u, sizeof(userec_t));
    ans = vans(adminmode ?
    "(1)改資料(2)密碼(3)權限(4)砍帳號(5)改ID(6)寵物(7)審判(M)信箱  [0]結束 " :
    "請選擇 (1)修改資料 (2)設定密碼 (M)修改信箱 (C) 個人化設定 ==> [0]結束 ");

    if (ans > '2' && ans != 'm' && ans != 'c' && !adminmode)
	ans = '0';

    if (ans == '1' || ans == '3' || ans == 'm') {
	clear();
	y = 1;
	move(y++, 0);
	outs(msg_uid);
	outs(x.userid);
    }

    if (adminmode && ((ans >= '1' && ans <= '7') || ans == 'm') &&
	search_ulist(unum))
    {
	if (vans("使用者目前正在線上，修改資料會先踢下線。確定要繼續嗎？ (y/N): ") 
		!= 'y')
		return;
    }
    switch (ans) {
    case 'c':
	// Customize can only apply to self.
	if (!adminmode)
	    Customize();
	return;

    case 'm':
	do {
	    getdata_str(y, 0, "電子信箱 [變動要重新認證]：", buf, 
		    sizeof(x.email), DOECHO, x.email);

	    strip_blank(buf, buf);

	    // fast break
	    if (!buf[0] || strcasecmp(buf, "x") == 0)
		break;

	    // TODO 這裡也要 emaildb_check
#ifdef USE_EMAILDB
	    if (isvalidemail(buf))
	    {
		int email_count = emaildb_check_email(buf, strlen(buf));
		if (email_count < 0)
		    vmsg("暫時不允許\ email 認證, 請稍後再試");
		else if (email_count >= EMAILDB_LIMIT) 
		    vmsg("指定的 E-Mail 已註冊過多帳號, 請使用其他 E-Mail");
		else // valid
		    break;
	    }
	    continue;
#endif
	} while (!isvalidemail(buf) && vmsg("認證信箱不能用使用免費信箱"));
	y++;

	// admins may want to use special names
	if (buf[0] &&
		strcmp(buf, x.email) && 
		(strchr(buf, '@') || adminmode)) {

	    // TODO 這裡也要 emaildb_check
#ifdef USE_EMAILDB
	    if (emaildb_update_email(cuser.userid, strlen(cuser.userid),
			buf, strlen(buf)) < 0) {
		vmsg("暫時不允許\ email 認證, 請稍後再試");
		break;
	    }
#endif
	    strlcpy(x.email, buf, sizeof(x.email));
	    mail_changed = 1;
	    delregcodefile();
	}
	break;

    case '7':
	violate_law(&x, unum);
	return;
    case '1':
	move(0, 0);
	outs("請逐項修改。");

	getdata_buf(y++, 0, " 暱 稱  ：", x.nickname,
		    sizeof(x.nickname), DOECHO);
	if (adminmode) {
	    getdata_buf(y++, 0, "真實姓名：",
			x.realname, sizeof(x.realname), DOECHO);
	    getdata_buf(y++, 0, "居住地址：",
			x.address, sizeof(x.address), DOECHO);
	    getdata_buf(y++, 0, "學歷職業：", x.career,
		    sizeof(x.career), DOECHO);
	    getdata_buf(y++, 0, "電話號碼：", x.phone,
		    sizeof(x.phone), DOECHO);
	}
	buf[0] = 0;
	if (x.mobile)
	    snprintf(buf, sizeof(buf), "%010d", x.mobile);
	getdata_buf(y++, 0, "手機號碼：", buf, 11, NUMECHO);
	x.mobile = atoi(buf);
	snprintf(genbuf, sizeof(genbuf), "%d", (u->sex + 1) % 8);
	getdata_str(y++, 0, "性別 (1)葛格 (2)姐接 (3)底迪 (4)美眉 (5)薯叔 "
		    "(6)阿姨 (7)植物 (8)礦物：",
		    buf, 3, NUMECHO, genbuf);
	if (buf[0] >= '1' && buf[0] <= '8')
	    x.sex = (buf[0] - '1') % 8;
	else
	    x.sex = u->sex % 8;

	while (1) {
	    snprintf(genbuf, sizeof(genbuf), "%04i/%02i/%02i",
		     u->year + 1900, u->month, u->day);
	    if (getdata_str(y, 0, "生日 西元/月月/日日：", buf, 11, DOECHO, genbuf) == 0) {
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
	    y++;
	    break;
	}

#ifdef PLAY_ANGEL
	if (adminmode) {
	    const char* prompt;
	    userec_t the_angel;
	    if (x.myangel[0] == 0 || x.myangel[0] == '-' ||
		    (getuser(x.myangel, &the_angel) &&
		     the_angel.userlevel & PERM_ANGEL))
		prompt = "小 天 使：";
	    else
		prompt = "小天使（此帳號已無小天使資格）：";
	    while (1) {
	        userec_t xuser;
		getdata_str(y, 0, prompt, buf, IDLEN + 1, DOECHO,
			x.myangel);
		if(buf[0] == 0 || strcmp(buf, "-") == 0 ||
			(getuser(buf, &xuser) &&
			    (xuser.userlevel & PERM_ANGEL)) ||
			strcmp(x.myangel, buf) == 0){
		    strlcpy(x.myangel, xuser.userid, IDLEN + 1);
		    ++y;
		    break;
		}

		prompt = "小 天 使：";
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
		    getdata_buf(y, 0, mybuf, genbuf + 11, 80 - 11, DOECHO);
		    ++y;

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
	    int tmp;
	    if (HasUserPerm(PERM_BBSADM)) {
		snprintf(genbuf, sizeof(genbuf), "%d", x.money);
		if (getdata_str(y++, 0, "銀行帳戶：", buf, 10, DOECHO, genbuf))
		    if ((tmp = atol(buf)) != 0) {
			if (tmp != x.money) {
			    money_changed = 1;
			    changefrom = x.money;
			    x.money = tmp;
			}
		    }
	    }
	    snprintf(genbuf, sizeof(genbuf), "%d", x.exmailbox);
	    if (getdata_str(y++, 0, "購買信箱：", buf, 6,
			    DOECHO, genbuf))
		if ((tmp = atoi(buf)) != 0)
		    x.exmailbox = (int)tmp;

	    getdata_buf(y++, 0, "認證資料：", x.justify,
			sizeof(x.justify), DOECHO);
	    getdata_buf(y++, 0, "最近光臨機器：",
			x.lasthost, sizeof(x.lasthost), DOECHO);

	    snprintf(genbuf, sizeof(genbuf), "%d", x.numlogins);
	    if (getdata_str(y++, 0, "上線次數：", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.numlogins = tmp;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->numposts);
	    if (getdata_str(y++, 0, "文章數目：", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.numposts = tmp;
#ifdef ASSESS
	    snprintf(genbuf, sizeof(genbuf), "%d", u->goodpost);
	    if (getdata_str(y++, 0, "優良文章數:", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.goodpost = tmp;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->badpost);
	    if (getdata_str(y++, 0, "惡劣文章數:", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.badpost = tmp;
#endif // ASSESS

	    snprintf(genbuf, sizeof(genbuf), "%d", u->vl_count);
	    if (getdata_str(y++, 0, "違法記錄：", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.vl_count = tmp;

	    snprintf(genbuf, sizeof(genbuf),
		     "%d/%d/%d", u->five_win, u->five_lose, u->five_tie);
	    if (getdata_str(y++, 0, "五子棋戰績 勝/敗/和：", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    char *p;
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
	    if (getdata_str(y++, 0, "象棋戰績 勝/敗/和：", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    char *p;
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
	    if (getdata_str(y++, 0, "住在 1)台灣 2)其他：", buf, 2, DOECHO, x.uflag2 & FOREIGN ? "2" : "1"))
		if ((tmp = atoi(buf)) > 0){
		    if (tmp == 2){
			x.uflag2 |= FOREIGN;
		    }
		    else
			x.uflag2 &= ~FOREIGN;
		}
	    if (x.uflag2 & FOREIGN)
		if (getdata_str(y++, 0, "永久居留權 1)是 2)否：", buf, 2, DOECHO, x.uflag2 & LIVERIGHT ? "1" : "2")){
		    if ((tmp = atoi(buf)) > 0){
			if (tmp == 1){
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
	}
	break;

    case '2':
	y = 19;
	if (!adminmode) {
	    if (!getdata(y++, 0, "請輸入原密碼：", buf, PASSLEN, NOECHO) ||
		!checkpasswd(u->passwd, buf)) {
		outs("\n\n您輸入的密碼不正確\n");
		fail++;
		break;
	    }
	} 
	if (!getdata(y++, 0, "請設定新密碼：", buf, PASSLEN, NOECHO)) {
	    outs("\n\n密碼設定取消, 繼續使用舊密碼\n");
	    fail++;
	    break;
	}
	strlcpy(genbuf, buf, PASSLEN);

	move(y+1, 0);
	outs("請注意設定密碼只有前八個字元有效，超過的將自動忽略。");

	getdata(y++, 0, "請檢查新密碼：", buf, PASSLEN, NOECHO);
	if (strncmp(buf, genbuf, PASSLEN)) {
	    outs("\n\n新密碼確認失敗, 無法設定新密碼\n");
	    fail++;
	    break;
	}
	buf[8] = '\0';
	strlcpy(x.passwd, genpasswd(buf), sizeof(x.passwd));

	// for admin mode, do verify after.
	if (adminmode)
	{
            FILE *fp;
	    char  witness[3][IDLEN+1], title[100];
	    int uid;
	    for (i = 0; i < 3; i++) {
		if (!getdata(y + i, 0, "請輸入協助證明之使用者：",
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
		    // Adjust upper or lower case
                    strlcpy(witness[i], atuser.userid, sizeof(witness[i]));
		}
	    }
	    y += 3;

	    if (i < 3 || fail > 0 || vans(msg_sure_ny) != 'y')
	    {
		fail++;
		break;
	    }
	    pre_confirmed = 1;

	    sprintf(title, "%s 的密碼重設通知 (by %s)",u->userid, cuser.userid);
            unlink("etc/updatepwd.log");
	    if(! (fp = fopen("etc/updatepwd.log", "w")))
	    {
		move(b_lines-1, 0); clrtobot();
		outs("系統錯誤: 無法建立通知檔，請至 " BN_BUGREPORT " 報告。");
		fail++; pre_confirmed = 0;
		break;
	    }

	    fprintf(fp, "%s 要求密碼重設:\n"
		    "見證人為 %s, %s, %s",
		    u->userid, witness[0], witness[1], witness[2] );
	    fclose(fp);

	    post_file(BN_SECURITY, title, "etc/updatepwd.log", "[系統安全局]");
	    mail_id(u->userid, title, "etc/updatepwd.log", cuser.userid);
	    for(i=0; i<3; i++)
	    {
		mail_id(witness[i], title, "etc/updatepwd.log", cuser.userid);
	    }
	}
	break;

    case '3':
	{
	    int tmp = setperms(x.userlevel, str_permid);
	    if (tmp == x.userlevel)
		fail++;
	    else {
		perm_changed = 1;
		changefrom = x.userlevel;
		x.userlevel = tmp;
	    }
	}
	break;

    case '4':
	tokill = 1;
	{
	    char reason[STRLEN];
	    char title[STRLEN], msg[1024];
	    while (!getdata(b_lines-3, 0, "請輸入理由以示負責：", reason, 50, DOECHO));
	    if (vans(msg_sure_ny) != 'y')
	    {
		fail++;
		break;
	    }
	    pre_confirmed = 1;
	    snprintf(title, sizeof(title), 
		    "刪除ID: %s (站長: %s)", x.userid, cuser.userid);
	    snprintf(msg, sizeof(msg), 
		    "帳號 %s 由站長 %s 執行刪除，理由:\n %s\n\n"
		    "真實姓名:%s\n住址:%s\n認證資料:%s\nEmail:%s\n",
		    x.userid, cuser.userid, reason,
		    x.realname, x.address, x.justify, x.email);
	    post_msg(BN_SECURITY, title, msg, "[系統安全局]");
	}
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
	chicken_toggle_death(x.userid);
	break;
    default:
	return;
    }

    if (fail) {
	pressanykey();
	return;
    }

    if (!pre_confirmed)
    {
	if (vans(msg_sure_ny) != 'y')
	    return;
    }

    // now confirmed. do everything directly.

    // perm_changed is by sysop only.
    if (perm_changed) {
	sendalert(x.userid,  ALERT_PWD_PERM); // force to reload perm
	post_change_perm(changefrom, x.userlevel, cuser.userid, x.userid);
#ifdef PLAY_ANGEL
	if (x.userlevel & ~changefrom & PERM_ANGEL)
	    mail_id(x.userid, "翅膀長出來了！", "etc/angel_notify", "[上帝]");
#endif
    }
    if (strcmp(u->userid, x.userid)) {
	char            src[STRLEN], dst[STRLEN];
	kick_all(u->userid);
	sethomepath(src, u->userid);
	sethomepath(dst, x.userid);
	Rename(src, dst);
	setuserid(unum, x.userid);
    }
    if (mail_changed && !adminmode) {
	// wait registration.
	x.userlevel &= ~(PERM_LOGINOK | PERM_POST);
    }
    memcpy(u, &x, sizeof(x));
    if (tokill) {
	kick_all(x.userid);
	kill_user(unum, x.userid);
	return;
    } else
	log_usies("SetUser", x.userid);

    if (money_changed) {
	char title[TTLEN+1];
	char msg[512];
	char reason[50];
	clrtobot();
	clear();
	while (!getdata(5, 0, "請輸入理由以示負責：",
		    reason, sizeof(reason), DOECHO));

	snprintf(msg, sizeof(msg),
		"   站長" ANSI_COLOR(1;32) "%s" ANSI_RESET "把" ANSI_COLOR(1;32) "%s" ANSI_RESET "的錢"
		"從" ANSI_COLOR(1;35) "%d" ANSI_RESET "改成" ANSI_COLOR(1;35) "%d" ANSI_RESET "\n"
		"   " ANSI_COLOR(1;37) "站長%s修改錢理由是：%s" ANSI_RESET,
		cuser.userid, x.userid, changefrom, x.money,
		cuser.userid, reason);
	snprintf(title, sizeof(title),
		"[公安報告] 站長%s修改%s錢報告", cuser.userid,
		x.userid);
	post_msg(BN_SECURITY, title, msg, "[系統安全局]");
	setumoney(unum, x.money);
    }

    passwd_update(unum, &x);

    if (adminmode)
    {
	sendalert(x.userid, ALERT_PWD_RELOAD);
	kick_all(x.userid);
    }

    // resolve_over18 only works for cuser
    if (!adminmode)
	resolve_over18();
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
	    show_file(genbuf, 13, 10, SHOWFILE_ALLOW_COLOR);
	    return;
	}
    }
#endif /* defined(CHESSCOUNTRY) */

    sethomefile(genbuf, user->userid, fn_plans);
    if (!show_file(genbuf, 7, MAX_QUERYLINES, SHOWFILE_ALLOW_COLOR))
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
    char            genbuf[PATHLEN];
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
isvalidemail(const char *email)
{
    FILE           *fp;
    char            buf[128], *c;
    if (!strstr(email, "@"))
	return 0;
    for (c = strstr(email, "@"); *c != 0; ++c)
	if ('A' <= *c && *c <= 'Z')
	    *c += 32;

    // allow list
    if ((fp = fopen("etc/whitemail", "rt")))
    {
	int allow = 0;
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
	    }
	    return 0;
	}
    }

    // reject list
    if ((fp = fopen("etc/banemail", "r"))) {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (buf[0] == '#')
		continue;
	    chomp(buf);
	    c = buf+1;
	    switch(buf[0])
	    {
		case 'A': if (strcasecmp(c, email) == 0)	return 0;
		case 'P': if (strcasestr(email, c))	return 0;
		case 'S': if (strcasecmp(strstr(email, "@") + 1, c) == 0) return 0;
		case 'D': if (strlen(email) > strlen(c))
			  {
			      // c2 points to starting of possible c.
			      const char *c2 = email + strlen(email) - strlen(c);
			      if (strcasecmp(c2, c) != 0)
				  break;
			      c2--;
			      if (*c2 == '.' || *c2 == '@')
				  return 0;
			  }
			  break;
	    }
	}
	fclose(fp);
    }
    return 1;
}

/* 列出所有註冊使用者 */
struct ListAllUsetCtx {
    int usercounter;
    int totalusers;
    unsigned short u_list_special;
    int y;
};

static int
u_list_CB(void *data, int num, userec_t * uentp)
{
    char            permstr[8], *ptr;
    register int    level;
    struct ListAllUsetCtx *ctx = (struct ListAllUsetCtx*) data;
    (void)num;

    if (uentp == NULL) {
	move(2, 0);
	clrtoeol();
	prints(ANSI_REVERSE "  使用者代號   %-25s   上站  文章  %s  "
	       "最近光臨日期     " ANSI_COLOR(0) "\n",
	       "綽號暱稱",
	       HasUserPerm(PERM_SEEULEVELS) ? "等級" : "");
	ctx->y = 3;
	return 0;
    }
    if (bad_user_id(uentp->userid))
	return 0;

    if ((uentp->userlevel & ~(ctx->u_list_special)) == 0)
	return 0;

    if (ctx->y == b_lines) {
	int ch;
	prints(ANSI_COLOR(34;46) "  已顯示 %d/%d 人(%d%%)  " ANSI_COLOR(31;47) "  "
	       "(Space)" ANSI_COLOR(30) " 看下一頁  " ANSI_COLOR(31) "(Q)" ANSI_COLOR(30) " 離開  " ANSI_RESET,
	       ctx->usercounter, ctx->totalusers, ctx->usercounter * 100 / ctx->totalusers);
	ch = igetch();
	if (ch == 'q' || ch == 'Q')
	    return -1;
	ctx->y = 3;
    }
    if (ctx->y == 3) {
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
    ctx->usercounter++;
    ctx->y++;
    return 0;
}

int
u_list(void)
{
    char            genbuf[3];
    struct ListAllUsetCtx data, *ctx = &data;

    setutmpmode(LAUSERS);
    ctx->u_list_special = ctx->usercounter = 0;
    ctx->totalusers = SHM->number;
    if (HasUserPerm(PERM_SEEULEVELS)) {
	getdata(b_lines - 1, 0, "觀看 [1]特殊等級 (2)全部？",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2')
	    ctx->u_list_special = PERM_BASIC | PERM_CHAT | PERM_PAGE | PERM_POST | PERM_LOGINOK | PERM_BM;
    }
    u_list_CB(ctx, 0, NULL);
    passwd_apply(ctx, u_list_CB);
    move(b_lines, 0);
    clrtoeol();
    prints(ANSI_COLOR(34;46) "  已顯示 %d/%d 的使用者(系統容量無上限)  "
	   ANSI_COLOR(31;47) "  (請按任意鍵繼續)  " ANSI_RESET, ctx->usercounter, ctx->totalusers);
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
    vs_hdr("設定自動偵測雙位元字集 (全型中文)");
    move(2, 0);
    outs(ANSI_RESET
    "* 本站支援自動偵測中文字的移動與編輯，但有些連線程式 (如xxMan)\n"
    "  也會自行試圖偵測、多送按鍵，於是便會造成" ANSI_COLOR(1;37)
      "一次移動兩個中文字的現象。" ANSI_RESET "\n\n"
    "* 讓連線程式處理移動容易造成顯示及移動上誤判的問題，所以我們建議您\n"
    "  關閉該程式上的設定（通常叫「偵測(全型或雙位元組)中文」），\n"
    "  讓 BBS 系統可以正確的控制你的畫面。\n\n"
    ANSI_COLOR(1;33) 
    "* 如果您看不懂上面的說明也無所謂，我們會自動偵測適合您的設定。"
    ANSI_RESET "\n"
    "  請在設定好連線程式成您偏好的模式後按" ANSI_COLOR(1;33)
    "一下" ANSI_RESET "您鍵盤上的" ANSI_COLOR(1;33)
    "←" ANSI_RESET "\n" ANSI_COLOR(1;36)
    "  (左右方向鍵或寫 BS/Backspace 的倒退鍵與 Del 刪除鍵均可)\n"
	    ANSI_RESET);

    /* clear buffer */
    peek_input(0.1, Ctrl('C'));
    drop_input();

    while (1)
    {
	int ch = 0;

	move(14, 0);
	outs("這是偵測區，您的游標會出現在" 
		ANSI_REVERSE "這裡" ANSI_RESET);
	move(14, 15*2);
	ch = igetch();
	if(ch != KEY_LEFT && ch != KEY_RIGHT &&
	   ch != KEY_BS && ch != KEY_BS2)
	{
	    move(16, 0);
	    bell();
	    outs("請按一下上面指定的鍵！ 你按到別的鍵了！");
	} else {
	    move(16, 0);
	    /* Actually you may also use num_in_buf here.  those clients
	     * usually sends doubled keys together in one packet.
	     * However when I was writing this, a bug (existed for more than 3
	     * years) of num_in_buf forced me to write new wait_input.
	     * Anyway it is fixed now.
	     */
	    refresh();
	    if(wait_input(0.1, 0))
	    // if(igetch() == ch)
	    // if (num_in_buf() > 0)
	    {
		/* evil dbcs aware client */
		outs("偵測到您的連線程式會自行處理游標移動。\n"
			// "若日後因此造成瀏覽上的問題本站恕不處理。\n\n"
			"已設定為「讓您的連線程式處理游標移動」\n");
		ret = 1;
	    } else {
		/* good non-dbcs aware client */
		outs("您的連線程式似乎不會多送按鍵，"
			"這樣 BBS 可以更精準的控制畫面。\n"
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
    drop_input();
    pressanykey();
    return ret;
}
#endif 

/* vim:sw=4
 */
