/* $Id: user.c,v 1.31 2002/07/21 09:26:02 in2 Exp $ */
#include "bbs.h"

static char    *sex[8] = {
    MSG_BIG_BOY, MSG_BIG_GIRL, MSG_LITTLE_BOY, MSG_LITTLE_GIRL,
    MSG_MAN, MSG_WOMAN, MSG_PLANT, MSG_MIME
};

int
u_loginview()
{
    int             i;
    unsigned int    pbits = cuser.loginview;
    char            choice[5];

    clear();
    move(4, 0);
    for (i = 0; i < NUMVIEWFILE; i++)
	prints("    %c. %-20s %-15s \n", 'A' + i,
	       loginview_file[i][1], ((pbits >> i) & 1 ? "ˇ" : "Ｘ"));

    clrtobot();
    while (getdata(b_lines - 1, 0, "請按 [A-N] 切換設定，按 [Return] 結束：",
		   choice, sizeof(choice), LCECHO)) {
	i = choice[0] - 'a';
	if (i >= NUMVIEWFILE || i < 0)
	    bell();
	else {
	    pbits ^= (1 << i);
	    move(i + 4, 28);
	    prints((pbits >> i) & 1 ? "ˇ" : "Ｘ");
	}
    }

    if (pbits != cuser.loginview) {
	cuser.loginview = pbits;
	passwd_update(usernum, &cuser);
    }
    return 0;
}

void
user_display(userec_t * u, int real)
{
    int             diff = 0;
    char            genbuf[200];

    clrtobot();
    prints(
	   "        \033[30;41m┴┬┴┬┴┬\033[m  \033[1;30;45m    使 用 者"
	   " 資 料        "
	   "     \033[m  \033[30;41m┴┬┴┬┴┬\033[m\n");
    prints("                代號暱稱: %s(%s)\n"
	   "                真實姓名: %s\n"
	   "                居住住址: %s\n"
	   "                電子信箱: %s\n"
	   "                性    別: %s\n"
	   "                銀行帳戶: %ld 銀兩\n",
	   u->userid, u->username, u->realname, u->address, u->email,
	   sex[u->sex % 8], u->money);

    sethomedir(genbuf, u->userid);
    prints("                私人信箱: %d 封  (購買信箱: %d 封)\n"
	   "                身分證號: %s\n"
	   "                手機號碼: %010d\n"
	   "                生    日: %02i/%02i/%02i\n"
	   "                小雞名字: %s\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox, u->ident, u->mobile,
	   u->month, u->day, u->year % 100, u->mychicken.name);
    prints("                註冊日期: %s", ctime(&u->firstlogin));
    prints("                前次光臨: %s", ctime(&u->lastlogin));
    prints("                前次點歌: %s", ctime(&u->lastsong));
    prints("                上站文章: %d 次 / %d 篇\n",
	   u->numlogins, u->numposts);

    if (real) {
	strlcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF", sizeof(genbuf));
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
    outs("        \033[30;41m┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴"
	 "┬┴┬┴┬┴┬\033[m");

    outs((u->userlevel & PERM_LOGINOK) ?
	 "\n您的註冊程序已經完成，歡迎加入本站" :
	 "\n如果要提昇權限，請參考本站公佈欄辦理註冊");

#ifdef NEWUSER_LIMIT
    if ((u->lastlogin - u->firstlogin < 3 * 86400) && !HAS_PERM(PERM_POST))
	outs("\n新手上路，三天後開放權限");
#endif
}

void
mail_violatelaw(char *crime, char *police, char *reason, char *result)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;
    sprintf(genbuf, "home/%c/%s", crime[0], crime);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "作者: [Ptt法院]\n"
	    "標題: [報告] 違法判決報告\n"
	    "時間: %s\n"
	    "\033[1;32m%s\033[m判決：\n     \033[1;32m%s\033[m"
	    "因\033[1;35m%s\033[m行為，\n違反本站站規，處以\033[1;35m%s\033[m，特此通知"
	"\n請到 PttLaw 查詢相關法規資訊，並到 Play-Pay-ViolateLaw 繳交罰單",
	    ctime(&now), police, crime, reason, result);
    fclose(fp);
    sprintf(fhdr.title, "[報告] 違法判決報告");
    strlcpy(fhdr.owner, "[Ptt法院]", sizeof(fhdr.owner));
    sprintf(genbuf, "home/%c/%s/.DIR", crime[0], crime);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

static void
violate_law(userec_t * u, int unum)
{
    char            ans[4], ans2[4];
    char            reason[128];
    move(1, 0);
    clrtobot();
    move(2, 0);
    prints("(1)Cross-post (2)亂發廣告信 (3)亂發連鎖信\n");
    prints("(4)騷擾站上使用者 (8)其他以罰單處置行為\n(9)砍 id 行為\n");
    getdata(5, 0, "(0)結束", ans, sizeof(ans), DOECHO);
    switch (ans[0]) {
    case '1':
	sprintf(reason, "%s", "Cross-post");
	break;
    case '2':
	sprintf(reason, "%s", "亂發廣告信");
	break;
    case '3':
	sprintf(reason, "%s", "亂發連鎖信");
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
    getdata(7, 0, msg_sure_ny, ans2, sizeof(ans2), LCECHO);
    if (*ans2 != 'y')
	return;
    if (ans[0] == '9') {
	char            src[STRLEN], dst[STRLEN];
	sprintf(src, "home/%c/%s", u->userid[0], u->userid);
	sprintf(dst, "tmp/%s", u->userid);
	Rename(src, dst);
	log_usies("KILL", u->userid);
	post_violatelaw(u->userid, cuser.userid, reason, "砍除 ID");
	u->userid[0] = '\0';
	setuserid(unum, u->userid);
	passwd_update(unum, u);
    } else {
	u->userlevel |= PERM_VIOLATELAW;
	u->vl_count++;
	passwd_update(unum, u);
	post_violatelaw(u->userid, cuser.userid, reason, "罰單處份");
	mail_violatelaw(u->userid, cuser.userid, reason, "罰單處份");
    }
    pressanykey();
}


void
uinfo_query(userec_t * u, int real, int unum)
{
    userec_t        x;
    register int    i = 0, fail, mail_changed;
    int             uid;
    char            ans[4], buf[STRLEN], *p;
    char            genbuf[200], reason[50];
    unsigned long int money = 0;
    fileheader_t    fhdr;
    int             flag = 0, temp = 0, money_change = 0;

    FILE           *fp;

    fail = mail_changed = 0;

    memcpy(&x, u, sizeof(userec_t));
    getdata(b_lines - 1, 0, real ?
	    "(1)改資料(2)設密碼(3)設權限(4)砍帳號(5)改ID"
	    "(6)殺/復活寵物(7)審判 [0]結束 " :
	    "請選擇 (1)修改資料 (2)設定密碼 ==> [0]結束 ",
	    ans, sizeof(ans), DOECHO);

    if (ans[0] > '2' && !real)
	ans[0] = '0';

    if (ans[0] == '1' || ans[0] == '3') {
	clear();
	i = 2;
	move(i++, 0);
	outs(msg_uid);
	outs(x.userid);
    }
    switch (ans[0]) {
    case '7':
	violate_law(&x, unum);
	return;
    case '1':
	move(0, 0);
	outs("請逐項修改。");

	getdata_buf(i++, 0, " 暱 稱  ：", x.username,
		    sizeof(x.username), DOECHO);
	if (real) {
	    getdata_buf(i++, 0, "真實姓名：",
			x.realname, sizeof(x.realname), DOECHO);
	    getdata_buf(i++, 0, "身分證號：",
			x.ident, sizeof(x.ident), DOECHO);
	    getdata_buf(i++, 0, "居住地址：",
			x.address, sizeof(x.address), DOECHO);
	}
	sprintf(buf, "%010d", x.mobile);
	getdata_buf(i++, 0, "手機號碼：", buf, 11, LCECHO);
	x.mobile = atoi(buf);
	getdata_str(i++, 0, "電子信箱[變動要重新認證]：", buf, 50, DOECHO,
		    x.email);
	if (strcmp(buf, x.email) && strchr(buf, '@')) {
	    strlcpy(x.email, buf, sizeof(x.email));
	    mail_changed = 1 - real;
	}
	sprintf(genbuf, "%i", (u->sex + 1) % 8);
	getdata_str(i++, 0, "性別 (1)葛格 (2)姐接 (3)底迪 (4)美眉 (5)薯叔 "
		    "(6)阿姨 (7)植物 (8)礦物：",
		    buf, 3, DOECHO, genbuf);
	if (buf[0] >= '1' && buf[0] <= '8')
	    x.sex = (buf[0] - '1') % 8;
	else
	    x.sex = u->sex % 8;

	while (1) {
	    int             len;

	    sprintf(genbuf, "%02i/%02i/%02i",
		    u->month, u->day, u->year % 100);
	    len = getdata_str(i, 0, "生日 月月/日日/西元：", buf, 9,
			      DOECHO, genbuf);
	    if (len && len != 8)
		continue;
	    if (!len) {
		x.month = u->month;
		x.day = u->day;
		x.year = u->year;
	    } else if (len == 8) {
		x.month = (buf[0] - '0') * 10 + (buf[1] - '0');
		x.day = (buf[3] - '0') * 10 + (buf[4] - '0');
		x.year = (buf[6] - '0') * 10 + (buf[7] - '0');
	    } else
		continue;
	    if (!real && (x.month > 12 || x.month < 1 || x.day > 31 ||
			  x.day < 1 || x.year > 90 || x.year < 40))
		continue;
	    i++;
	    break;
	}
	if (real) {
	    unsigned long int l;
	    if (HAS_PERM(PERM_BBSADM)) {
		sprintf(genbuf, "%d", x.money);
		if (getdata_str(i++, 0, "銀行帳戶：", buf, 10, DOECHO, genbuf))
		    if ((l = atol(buf)) != 0) {
			if (l != x.money) {
			    money_change = 1;
			    money = x.money;
			    x.money = l;
			}
		    }
	    }
	    sprintf(genbuf, "%d", x.exmailbox);
	    if (getdata_str(i++, 0, "購買信箱數：", buf, 4, DOECHO, genbuf))
		if ((l = atol(buf)) != 0)
		    x.exmailbox = (int)l;

	    getdata_buf(i++, 0, "認證資料：", x.justify,
			sizeof(x.justify), DOECHO);
	    getdata_buf(i++, 0, "最近光臨機器：",
			x.lasthost, sizeof(x.lasthost), DOECHO);

	    sprintf(genbuf, "%d", x.numlogins);
	    if (getdata_str(i++, 0, "上線次數：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.numlogins = fail;

	    sprintf(genbuf, "%d", u->numposts);
	    if (getdata_str(i++, 0, "文章數目：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.numposts = fail;
	    sprintf(genbuf, "%d", u->vl_count);
	    if (getdata_str(i++, 0, "違法記錄：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.vl_count = fail;

	    sprintf(genbuf, "%d/%d/%d", u->five_win, u->five_lose,
		    u->five_tie);
	    if (getdata_str(i++, 0, "五子棋戰績 勝/敗/和：", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    p = strtok(buf, "/\r\n");
		    if (!p)
			break;
		    x.five_win = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.five_lose = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.five_tie = atoi(p);
		    break;
		}
	    sprintf(genbuf, "%d/%d/%d", u->chc_win, u->chc_lose, u->chc_tie);
	    if (getdata_str(i++, 0, "象棋戰績 勝/敗/和：", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    p = strtok(buf, "/\r\n");
		    if (!p)
			break;
		    x.chc_win = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.chc_lose = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.chc_tie = atoi(p);
		    break;
		}
	    fail = 0;
	}
	break;

    case '2':
	i = 19;
	if (!real) {
	    if (!getdata(i++, 0, "請輸入原密碼：", buf, PASSLEN, NOECHO) ||
		!checkpasswd(u->passwd, buf)) {
		outs("\n\n您輸入的密碼不正確\n");
		fail++;
		break;
	    }
	} else {
	    char            witness[3][32];
	    for (i = 0; i < 3; i++) {
		if (!getdata(19 + i, 0, "請輸入協助證明之使用者：",
			     witness[i], sizeof(witness[i]), DOECHO)) {
		    outs("\n不輸入則無法更改\n");
		    fail++;
		    break;
		} else if (!(uid = getuser(witness[i]))) {
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
		}
	    }
	    if (i < 3)
		break;
	    else
		i = 20;
	}

	if (!getdata(i++, 0, "請設定新密碼：", buf, PASSLEN, NOECHO)) {
	    outs("\n\n密碼設定取消, 繼續使用舊密碼\n");
	    fail++;
	    break;
	}
	strncpy(genbuf, buf, PASSLEN);

	getdata(i++, 0, "請檢查新密碼：", buf, PASSLEN, NOECHO);
	if (strncmp(buf, genbuf, PASSLEN)) {
	    outs("\n\n新密碼確認失敗, 無法設定新密碼\n");
	    fail++;
	    break;
	}
	buf[8] = '\0';
	strncpy(x.passwd, genpasswd(buf), PASSLEN);
	if (real)
	    x.userlevel &= (!PERM_LOGINOK);
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
	    if (searchuser(genbuf)) {
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
    getdata(b_lines - 1, 0, msg_sure_ny, ans, 3, LCECHO);
    if (*ans == 'y') {
	if (flag)
	    post_change_perm(temp, i, cuser.userid, x.userid);
	if (strcmp(u->userid, x.userid)) {
	    char            src[STRLEN], dst[STRLEN];

	    sethomepath(src, u->userid);
	    sethomepath(dst, x.userid);
	    Rename(src, dst);
	    setuserid(unum, x.userid);
	}
	memcpy(u, &x, sizeof(x));
	if (mail_changed) {
#ifdef EMAIL_JUSTIFY
	    x.userlevel &= ~PERM_LOGINOK;
	    mail_justify();
#endif
	}
	if (i == QUIT) {
	    char            src[STRLEN], dst[STRLEN];

	    sprintf(src, "home/%c/%s", x.userid[0], x.userid);
	    sprintf(dst, "tmp/%s", x.userid);
	    if (Rename(src, dst)) {
		sprintf(genbuf, "/bin/rm -fr %s >/dev/null 2>&1", src);
		/*
		 * do not remove system(genbuf);
		 */
	    }
	    log_usies("KILL", x.userid);
	    x.userid[0] = '\0';
	    setuserid(unum, x.userid);
	} else
	    log_usies("SetUser", x.userid);
	if (money_change)
	    setumoney(unum, x.money);
	passwd_update(unum, &x);
	if (money_change) {
	    strlcpy(genbuf, "boards/S/Security", sizeof(genbuf));
	    stampfile(genbuf, &fhdr);
	    if (!(fp = fopen(genbuf, "w")))
		return;

	    fprintf(fp, "作者: [系統安全局] 看板: Security\n"
		    "標題: [公安報告] 站長修改金錢報告\n"
		    "時間: %s\n"
		    "   站長\033[1;32m%s\033[m把\033[1;32m%s\033[m"
		    "的錢從\033[1;35m%ld\033[m改成\033[1;35m%d\033[m",
		    ctime(&now), cuser.userid, x.userid, money, x.money);

	    clrtobot();
	    clear();
	    while (!getdata(5, 0, "請輸入理由以示負責：",
			    reason, sizeof(reason), DOECHO));

	    fprintf(fp, "\n   \033[1;37m站長%s修改錢理由是：%s\033[m",
		    cuser.userid, reason);
	    fclose(fp);
	    sprintf(fhdr.title, "[公安報告] 站長%s修改%s錢報告", cuser.userid,
		    x.userid);
	    strlcpy(fhdr.owner, "[系統安全局]", sizeof(fhdr.owner));
	    append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
	}
    }
}

int
u_info()
{
    move(2, 0);
    user_display(&cuser, 0);
    uinfo_query(&cuser, 0, usernum);
    strlcpy(currutmp->realname, cuser.realname, sizeof(currutmp->realname));
    strlcpy(currutmp->username, cuser.username, sizeof(currutmp->username));
    return 0;
}

int
u_ansi()
{
    showansi ^= 1;
    cuser.uflag ^= COLOR_FLAG;
    outs(reset_color);
    return 0;
}

int
u_cloak()
{
    outs((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
    return XEASY;
}

int
u_switchproverb()
{
    /* char *state[4]={"用功\型","安逸型","自定型","SHUTUP"}; */
    char            buf[100];

    cuser.proverb = (cuser.proverb + 1) % 4;
    setuserfile(buf, fn_proverb);
    if (cuser.proverb == 2 && dashd(buf)) {
	FILE           *fp = fopen(buf, "a");

	fprintf(fp, "座右銘狀態為[自定型]要記得設座右銘的內容唷!!");
	fclose(fp);
    }
    passwd_update(usernum, &cuser);
    return 0;
}

int
u_editproverb()
{
    char            buf[100];

    setutmpmode(PROVERB);
    setuserfile(buf, fn_proverb);
    move(1, 0);
    clrtobot();
    outs("\n\n 請一行一行依序鍵入想系統提醒你的內容,\n"
	 " 儲存後記得把狀態設為 [自定型] 才有作用\n"
	 " 座右銘最多100條");
    pressanykey();
    vedit(buf, NA, NULL);
    return 0;
}

void
showplans(char *uid)
{
    char            genbuf[200];

    sethomefile(genbuf, uid, fn_plans);
    if (!show_file(genbuf, 7, MAX_QUERYLINES, ONLY_COLOR))
	prints("《個人名片》%s 目前沒有名片", uid);
}

int
showsignature(char *fname)
{
    FILE           *fp;
    char            buf[256];
    int             i, j;
    char            ch;

    clear();
    move(2, 0);
    setuserfile(fname, "sig.0");
    j = strlen(fname) - 1;

    for (ch = '1'; ch <= '9'; ch++) {
	fname[j] = ch;
	if ((fp = fopen(fname, "r"))) {
	    prints("\033[36m【 簽名檔.%c 】\033[m\n", ch);
	    for (i = 0; i++ < MAX_SIGLINES && fgets(buf, 256, fp); outs(buf));
	    fclose(fp);
	}
    }
    return j;
}

int
u_editsig()
{
    int             aborted;
    char            ans[4];
    int             j;
    char            genbuf[200];

    j = showsignature(genbuf);

    getdata(0, 0, "簽名檔 (E)編輯 (D)刪除 (Q)取消？[Q] ",
	    ans, sizeof(ans), LCECHO);

    aborted = 0;
    if (ans[0] == 'd')
	aborted = 1;
    if (ans[0] == 'e')
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
u_editplan()
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
u_editcalendar()
{
    char            genbuf[200];

    getdata(b_lines - 1, 0, "行事曆 (D)刪除 (E)編輯 [Q]取消？[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	setcalfile(genbuf, cuser.userid);
	aborted = vedit(genbuf, NA, NULL);
	if (aborted != -1)
	    outs("行事曆更新完畢");
	pressanykey();
	return 0;
    } else if (genbuf[0] == 'd') {
	setcalfile(genbuf, cuser.userid);
	unlink(genbuf);
	outmsg("行事曆刪除完畢");
    }
    return 0;
}

/* 使用者填寫註冊表格 */
static void
getfield(int line, char *info, char *desc, char *buf, int len)
{
    char            prompt[STRLEN];
    char            genbuf[200];

    sprintf(genbuf, "原先設定：%-30.30s (%s)", buf, info);
    move(line, 2);
    outs(genbuf);
    sprintf(prompt, "%s：", desc);
    if (getdata_str(line + 1, 2, prompt, genbuf, len, DOECHO, buf))
	strlcpy(buf, genbuf, sizeof(buf));
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

static int
ispersonalid(char *inid)
{
    char           *lst = "ABCDEFGHJKLMNPQRSTUVWXYZIO", id[20];
    int             i, j, cksum;

    strlcpy(id, inid, sizeof(id));
    i = cksum = 0;
    if (!isalpha(id[0]) && (strlen(id) != 10))
	return 0;
    id[0] = toupper(id[0]);
    /* A->10, B->11, ..H->17,I->34, J->18... */
    while (lst[i] != id[0])
	i++;
    i += 10;
    id[0] = i % 10 + '0';
    if (!isdigit(id[9]))
	return 0;
    cksum += (id[9] - '0') + (i / 10);

    for (j = 0; j < 9; ++j) {
	if (!isdigit(id[j]))
	    return 0;
	cksum += (id[j] - '0') * (9 - j);
    }
    return (cksum % 10) == 0;
}

static char    *
getregcode(char *buf)
{
    sprintf(buf, "%s", crypt(cuser.userid, "02"));
    return buf;
}

static int
isvaildemail(char *email)
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
	    buf[strlen(buf) - 1] = 0;
	    if (buf[0] == 'A' && strcmp(&buf[1], email) == 0)
		return 0;
	    if (buf[0] == 'P' && strstr(email, &buf[1]))
		return 0;
	    if (buf[0] == 'S' && strcmp(strstr(email, "@") + 1, &buf[1]) == 0)
		return 0;
	}
	fclose(fp);
    }
    return 1;
}

static void
toregister(char *email, char *genbuf, char *phone, char *career,
	   char *ident, char *rname, char *addr, char *mobile)
{
    FILE           *fn;
    char            buf[128];

    sethomefile(buf, cuser.userid, "justify.wait");
    if (phone[0] != 0) {
	fn = fopen(buf, "w");
	fprintf(fn, "%s\n%s\n%s\n%s\n%s\n%s\n",
		phone, career, ident, rname, addr, mobile);
	fclose(fn);
    }
    clear();
    stand_title("認證設定");
    move(2, 0);
    outs("您好, 本站認證認證的方式有:\n"
	 "  1.若您有 E-Mail  (本站不接受 yahoo, kimo等免費的 E-Mail)\n"
	 "    請輸入您的 E-Mail , 我們會寄發含有認證碼的信件給您\n"
	 "    收到後請到 (U)ser => (R)egister 輸入認證碼, 即可通過認證\n"
	 "\n"
	 "  2.若您沒有 E-Mail , 請輸入 x ,\n"
	 "    我們會由站長親自審核您的註冊資料\n"
	 "************************************************************\n"
	 "* 注意!                                                    *\n"
	 "* 您應該會在輸入完成後十分鐘內收到認證信, 若過久未收到,    *\n"
	 "* 或輸入後發生認證碼錯誤, 麻煩重填一次 E-Mail 或改手動認證 *\n"
	 "************************************************************\n");

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
	    if (isvaildmobile(mobile)) {
		char            yn[3];
		getdata(16, 0, "請再次確認您輸入的手機號碼正確嘛? [y/N]",
			yn, sizeof(yn), LCECHO);
		if (yn[0] == 'Y' || yn[0] == 'y')
		    break;
	    } else {
		move(17, 0);
		prints("指定的手機號碼不合法,"
		       "若您無手機門號請選擇其他方式認證");
	    }

	}
#endif
	else if (isvaildemail(email)) {
	    char            yn[3];
	    getdata(16, 0, "請再次確認您輸入的 E-Mail 位置正確嘛? [y/N]",
		    yn, sizeof(yn), LCECHO);
	    if (yn[0] == 'Y' || yn[0] == 'y')
		break;
	} else {
	    move(17, 0);
	    prints("指定的 E-Mail 不合法,"
		   "若您無 E-Mail 請輸入 x由站長手動認證");
	}
    }
    strncpy(cuser.email, email, sizeof(cuser.email));
    if (strcasecmp(email, "x") == 0) {	/* 手動認證 */
	if ((fn = fopen(fn_register, "a"))) {
	    fprintf(fn, "num: %d, %s", usernum, ctime(&now));
	    fprintf(fn, "uid: %s\n", cuser.userid);
	    fprintf(fn, "ident: %s\n", ident);
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
	char            tmp[IDLEN + 1];
	if (phone != NULL) {
#ifdef HAVEMOBILE
	    if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0)
		sprintf(genbuf, "%s:%s:<Mobile>", phone, career);
	    else
#endif
		sprintf(genbuf, "%s:%s:<Email>", phone, career);
	    strncpy(cuser.justify, genbuf, REGLEN);
	    sethomefile(buf, cuser.userid, "justify");
	}
	sprintf(buf, "您在 " BBSNAME " 的認證碼: %s", getregcode(genbuf));
	strlcpy(tmp, cuser.userid, sizeof(tmp));
	strlcpy(cuser.userid, "SYSOP", sizeof(cuser.userid));
#ifdef HAVEMOBILE
	if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0)
	    mobile_message(mobile, buf);
	else
#endif
	    bsmtp("etc/registermail", buf, email, 0);
	strlcpy(cuser.userid, tmp, sizeof(cuser.userid));
	outs("\n\n\n我們即將寄出認證信 (您應該會在 10 分鐘內收到)\n"
	     "收到後您可以跟據認證信標題的認證碼\n"
	     "輸入到 (U)ser -> (R)egister 後就可以完成註冊");
	pressanykey();
	return;
    }
}

int
u_register(void)
{
    char            rname[21], addr[51], ident[12], mobile[21];
    char            phone[21], career[41], email[51], birthday[9], sex_is[2],
                    year, mon, day;
    char            inregcode[14], regcode[50];
    char            ans[3], *ptr;
    char            genbuf[200];
    FILE           *fn;

    if (cuser.userlevel & PERM_LOGINOK) {
	outs("您的身份確認已經完成，不需填寫申請表");
	return XEASY;
    }
    if ((fn = fopen(fn_register, "r"))) {
	while (fgets(genbuf, STRLEN, fn)) {
	    if ((ptr = strchr(genbuf, '\n')))
		*ptr = '\0';
	    if (strncmp(genbuf, "uid: ", 5) == 0 &&
		strcmp(genbuf + 5, cuser.userid) == 0) {
		fclose(fn);
		outs("您的註冊申請單尚在處理中，請耐心等候");
		return XEASY;
	    }
	}
	fclose(fn);
    }
    strlcpy(ident, cuser.ident, sizeof(ident));
    strlcpy(rname, cuser.realname, sizeof(rname));
    strlcpy(addr, cuser.address, sizeof(addr));
    strlcpy(email, cuser.email, sizeof(email));
    sprintf(mobile, "0%09d", cuser.mobile);
    if (cuser.month == 0 && cuser.day && cuser.year == 0)
	birthday[0] = 0;
    else
	sprintf(birthday, "%02i/%02i/%02i",
		cuser.month, cuser.day, cuser.year % 100);
    sex_is[0] = (cuser.sex % 8) + '1';
    sex_is[1] = 0;
    career[0] = phone[0] = '\0';
    sethomefile(genbuf, cuser.userid, "justify.wait");
    if ((fn = fopen(genbuf, "r"))) {
	fgets(phone, 21, fn);
	phone[strlen(phone) - 1] = 0;
	fgets(career, 41, fn);
	career[strlen(career) - 1] = 0;
	fgets(ident, 12, fn);
	ident[strlen(ident) - 1] = 0;
	fgets(rname, 21, fn);
	rname[strlen(rname) - 1] = 0;
	fgets(addr, 51, fn);
	addr[strlen(addr) - 1] = 0;
	fgets(mobile, 21, fn);
	mobile[strlen(mobile) - 1] = 0;
	fclose(fn);
    }
    if (cuser.year != 0 &&	/* 已經第一次填過了~ ^^" */
	strcmp(cuser.email, "x") != 0 &&	/* 上次手動認證失敗 */
	strcmp(cuser.email, "X") != 0) {
	clear();
	stand_title("EMail認證");
	move(2, 0);
	prints("%s(%s) 您好，請輸入您的認證碼。\n"
	       "或您可以輸入 x來重新填寫 E-Mail 或改由站長手動認證",
	       cuser.userid, cuser.username);
	inregcode[0] = 0;
	getdata(10, 0, "您的輸入: ", inregcode, sizeof(inregcode), DOECHO);
	if (strcmp(inregcode, getregcode(regcode)) == 0) {
	    int             unum;
	    if ((unum = getuser(cuser.userid)) == 0) {
		outs("系統錯誤，查無此人\n\n");
		pressanykey();
		u_exit("getuser error");
		exit(0);
	    }
	    mail_muser(cuser, "[註冊成功\囉]", "etc/registeredmail");
	    cuser.userlevel |= (PERM_LOGINOK | PERM_POST);
	    prints("\n註冊成功\, 重新上站後將取得完整權限\n"
		   "請按下任一鍵跳離後重新上站~ :)");
	    sethomefile(genbuf, cuser.userid, "justify.wait");
	    unlink(genbuf);
	    pressanykey();
	    u_exit("registed");
	    exit(0);
	    return QUIT;
	} else if (strcmp(inregcode, "x") != 0 && strcmp(inregcode, "X") != 0) {
	    outs("認證碼錯誤\n");
	    pressanykey();
	}
	toregister(email, genbuf, phone, career, ident, rname, addr, mobile);
	return FULLUPDATE;
    }
    getdata(b_lines - 1, 0, "您確定要填寫註冊單嗎(Y/N)？[N] ",
	    ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y')
	return FULLUPDATE;

    move(2, 0);
    clrtobot();
    while (1) {
	clear();
	move(1, 0);
	prints("%s(%s) 您好，請據實填寫以下的資料:",
	       cuser.userid, cuser.username);
	do {
	    getfield(3, "D120908396", "身分證號", ident, 11);
	    if ('a' <= ident[0] && ident[0] <= 'z')
		ident[0] -= 32;
	} while (!ispersonalid(ident));
	while (1) {
	    getfield(5, "請用中文", "真實姓名", rname, 20);
	    if (removespace(rname) && rname[0] < 0 &&
		!strstr(rname, "阿") && !strstr(rname, "小"))
		break;
	    vmsg("您的輸入不正確");
	}

	while (1) {
	    getfield(7, "學校(含\033[1;33m系所年級\033[m)或單位職稱",
		     "服務單位", career, 40);
	    if (!(removespace(career) && career[0] < 0 && strlen(career) >= 4)) {
		vmsg("您的輸入不正確");
		continue;
	    }
	    if (strcmp(&career[strlen(career) - 2], "大") == 0 ||
		strcmp(&career[strlen(career) - 4], "大學") == 0) {
		vmsg("麻煩請加系所");
		continue;
	    }
	    break;
	}
	while (1) {
	    getfield(9, "含\033[1;33m縣市\033[m及門寢號碼"
		     "(台北請加\033[1;33m行政區\033[m)",
		     "目前住址", addr, 50);
	    if (!removespace(addr) || addr[0] > 0 || strlen(addr) < 15) {
		vmsg("這個地址並不合法");
		continue;
	    }
	    if (strstr(addr, "信箱") != NULL || strstr(addr, "郵政") != NULL) {
		vmsg("抱歉我們不接受郵政信箱");
		continue;
	    }
	    if (strstr(addr, "市") == NULL && strstr(addr, "縣") == NULL) {
		vmsg("這個地址並不合法");
		continue;
	    }
	    break;
	}
	while (1) {
	    getfield(11, "不加-(), 包括長途區號", "連絡電話", phone, 11);
	    if (!removespace(phone) || phone[0] != '0' ||
		strlen(phone) < 9 || phone[1] == '0') {
		vmsg("這個電話號碼並不合法");
		continue;
	    }
	    break;
	}
	getfield(13, "只輸入數字 如:0912345678 (可不填)",
		 "手機號碼", mobile, 20);
	while (1) {
	    int             len;

	    getfield(15, "月月/日日/西元 如:09/27/76", "生日", birthday, 9);
	    len = strlen(birthday);
	    if (!len) {
		sprintf(birthday, "%02i/%02i/%02i",
			cuser.month, cuser.day, cuser.year % 100);
		mon = cuser.month;
		day = cuser.day;
		year = cuser.year;
	    } else if (len == 8) {
		mon = (birthday[0] - '0') * 10 + (birthday[1] - '0');
		day = (birthday[3] - '0') * 10 + (birthday[4] - '0');
		year = (birthday[6] - '0') * 10 + (birthday[7] - '0');
	    } else
		continue;
	    if (mon > 12 || mon < 1 || day > 31 || day < 1 || year > 90 ||
		year < 40)
		continue;
	    break;
	}
	getfield(17, "1.葛格 2.姐接 ", "性別", sex_is, 2);
	getdata(18, 0, "以上資料是否正確(Y/N)？(Q)取消註冊 [N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] == 'q')
	    return 0;
	if (ans[0] == 'y')
	    break;
    }
    strlcpy(cuser.ident, ident, sizeof(cuser.ident));
    strlcpy(cuser.realname, rname, sizeof(cuser.realname));
    strlcpy(cuser.address, addr, sizeof(cuser.address));
    strlcpy(cuser.email, email, sizeof(cuser.email));
    cuser.mobile = atoi(mobile);
    cuser.sex = (sex_is[0] - '1') % 8;
    cuser.month = mon;
    cuser.day = day;
    cuser.year = year;
    trim(career);
    trim(addr);
    trim(phone);

    toregister(email, genbuf, phone, career, ident, rname, addr, mobile);

    clear();
    move(9, 3);
    prints("最後Post一篇\033[32m自我介紹文章\033[m給大家吧，"
	   "告訴所有老骨頭\033[31m我來啦^$。\\n\n\n\n");
    pressanykey();
    cuser.userlevel |= PERM_POST;
    brc_initial("WhoAmI");
    set_board();
    do_post();
    cuser.userlevel &= ~PERM_POST;
    return 0;
}

/* 列出所有註冊使用者 */
static int      usercounter, totalusers, showrealname;
static ushort   u_list_special;

static int
u_list_CB(userec_t * uentp)
{
    static int      i;
    char            permstr[8], *ptr;
    register int    level;

    if (uentp == NULL) {
	move(2, 0);
	clrtoeol();
	prints("\033[7m  使用者代號   %-25s   上站  文章  %s  "
	       "最近光臨日期     \033[0m\n",
	       showrealname ? "真實姓名" : "綽號暱稱",
	       HAS_PERM(PERM_SEEULEVELS) ? "等級" : "");
	i = 3;
	return 0;
    }
    if (bad_user_id(uentp->userid))
	return 0;

    if ((uentp->userlevel & ~(u_list_special)) == 0)
	return 0;

    if (i == b_lines) {
	prints("\033[34;46m  已顯示 %d/%d 人(%d%%)  \033[31;47m  "
	       "(Space)\033[30m 看下一頁  \033[31m(Q)\033[30m 離開  \033[m",
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
    strlcpy(permstr, "----", sizeof(permstr));
    if (level & PERM_SYSOP)
	permstr[0] = 'S';
    else if (level & PERM_ACCOUNTS)
	permstr[0] = 'A';
    else if (level & PERM_DENYPOST)
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
	   showrealname ? uentp->realname : uentp->username,
	   uentp->numlogins, uentp->numposts,
	   HAS_PERM(PERM_SEEULEVELS) ? permstr : "", ptr);
    usercounter++;
    i++;
    return 0;
}

int
u_list()
{
    char            genbuf[3];

    setutmpmode(LAUSERS);
    showrealname = u_list_special = usercounter = 0;
    totalusers = SHM->number;
    if (HAS_PERM(PERM_SEEULEVELS)) {
	getdata(b_lines - 1, 0, "觀看 [1]特殊等級 (2)全部？",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2')
	    u_list_special = PERM_BASIC | PERM_CHAT | PERM_PAGE | PERM_POST | PERM_LOGINOK | PERM_BM;
    }
    if (HAS_PERM(PERM_CHATROOM) || HAS_PERM(PERM_SYSOP)) {
	getdata(b_lines - 1, 0, "顯示 [1]真實姓名 (2)暱稱？",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2')
	    showrealname = 1;
    }
    u_list_CB(NULL);
    if (passwd_apply(u_list_CB) == -1) {
	outs(msg_nobody);
	return XEASY;
    }
    move(b_lines, 0);
    clrtoeol();
    prints("\033[34;46m  已顯示 %d/%d 的使用者(系統容量無上限)  "
	   "\033[31;47m  (請按任意鍵繼續)  \033[m", usercounter, totalusers);
    egetch();
    return 0;
}
