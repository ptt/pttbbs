#include "bbs.h"

int post_msg2(const char* bname, const char* title, const char *msg,
              const char* author, char *output_path)
{
    char fname[PATHLEN];
    FILE           *fp;
    int             bid;
    fileheader_t    fhdr;

    /* 在 bname 板發表新文章 */
    setbpath(fname, bname);
    stampfile(fname, &fhdr);
    fp = fopen(fname, "w");

    if (output_path)
        strlcpy(output_path, fname, PATHLEN);

    if (!fp)
	return -1;

    fprintf(fp, "作者: %s 看板: %s\n標題: %s \n", author, bname, title);
    fprintf(fp, "時間: %s\n", ctime4(&now));

    fputs(msg, fp);
    fclose(fp);

    /* 將檔案加入列表 */
    STRLCPY(fhdr.title, title);
    STRLCPY(fhdr.owner, author);
    if (append_fileheader(TEMP_BDIR(bname), &fhdr) != -1)
	if ((bid = getbnum(bname)) > 0)
	    setbtotal(bid);
    return 0;
}

int post_msg(const char* bname, const char* title, const char *msg,
             const char* author) {
    return post_msg2(bname, title, msg, author, NULL);
}

int
post_file(const char *bname, const char *title, const char *filename, const char *author)
{
    int             size = dashs(filename);
    char           *msg;
    FILE           *fp;

    if (size <= 0)
	return -1;
    if (!(fp = fopen(filename, "r")))
	return -1;
    msg = (char *)malloc(size + 1);
    size = fread(msg, 1, size, fp);
    msg[size] = 0;
    size = post_msg(bname, title, msg, author);
    fclose(fp);
    free(msg);
    return size;
}

void
post_change_perm(int oldperm, int newperm, const char *sysopid, const char *userid)
{
    char genbuf[(NUMPERMS+1) * STRLEN*2] = "", reason[30] = "";
    char *s = genbuf;
    int  i, flag = 0;

    // generate log (warning: each line should <= STRLEN*2)
    for (i = 0; i < NUMPERMS; i++) {
	if (((oldperm >> i) & 1) != ((newperm >> i) & 1)) {
	    sprintf(s, "   站長" ANSI_COLOR(1;32) "%s%s%s%s" ANSI_RESET "的權限\n",
		    sysopid,
	       (((oldperm >> i) & 1) ? ANSI_COLOR(1;33) "關閉" : ANSI_COLOR(1;33) "開啟"),
		    userid, str_permid[i]);
	    s += strlen(s);
	    flag++;
	}
    }

    if (!flag) // nothing changed
	return;

    clrtobot();
    clear();
    while (!getdata(5, 0, "請輸入理由以示負責：",
		reason, sizeof(reason), DOECHO));
    sprintf(s, "\n   " ANSI_COLOR(1;37) "站長%s修改權限理由是：%s\n" ANSI_RESET,
	    cuser.userid, reason);

    post_msg(BN_SECURITY,
	     TEMPFORMAT(TTLEN + 1, "[安全報告] 站長%s修改%s權限報告", cuser.userid, userid),
	     genbuf, "[系統安全局]");
}

void
post_auto_enable_perm(const char *userid, int perm_bit, const char *brdname)
{
    if (!userid || !*userid)
	return;

    const char *perm_name = "管理";
    for (int i = 0; i < NUMPERMS; i++) {
	if ((1 << i) == perm_bit) {
	    perm_name = str_permid[i];
	    break;
	}
    }

    AUTO_STR(title, "[安全報告] 系統自動修改%s權限報告", userid);
    AUTO_STR(msg,
	     "   系統自動為 " ANSI_COLOR(1;32) "%s" ANSI_RESET " 開啟 " ANSI_COLOR(1;33) "%s" ANSI_RESET " 的權限\n\n"
	     "   " ANSI_COLOR(1;37) "修改權限理由是：使用者名列看板 %s 管理名單，由系統自動判定補上靜態權限" ANSI_RESET "\n",
	     userid, perm_name, brdname ? brdname : "");

    post_msg(BN_SECURITY, title, msg, "[系統安全局]");
    // usies names the user doing it; name the one given the permission too
    if (strcmp(userid, cuser.userid) == 0)
	log_usies("AutoPerm", TEMPFORMAT(STRLEN, "%s (%s)", brdname ? brdname : "", perm_name));
    else
	log_usies("AutoPerm", TEMPFORMAT(STRLEN, "%s (%s) for %s", brdname ? brdname : "", perm_name, userid));
    log_user_security(userid, "[AutoPerm] Enabled %s on board %s\n", perm_name, brdname ? brdname : "");
}

// Penalties (違規處分) are announced in ViolateLaw and mailed. police is who
// gives them: NULL for the current user (police or SYSOP), shown as
// VIOLATELAW_POLICE and with the account in BN_POLICELOG, or the name of a
// system check (e.g. BBSMNAME " 系統警察"), shown as it is.
#define VIOLATELAW_POLICE "站務警察"

// Posts that userid gets the penalty result for reason, with memo (may be
// NULL) below.
void
violatelaw_post(const char *userid, const char *police, const char *reason,
                const char *result, const char *memo)
{
    AUTO_STR(title, "[報告] %s:%-*s 判決", userid,
	    (int)(30 - stream_width(userid)), reason);

    if (!police) {
	AUTO_STR(msg, ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n"
		"     " ANSI_COLOR(1;32) "%s" ANSI_RESET "因"
                ANSI_COLOR(1;35) "%s" ANSI_RESET "行為，\n"
		"違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET
                "，特此公告\n\n\n%s\n",
		cuser.userid, userid, reason, result, memo ? memo : "");
	post_msg(BN_POLICELOG, title, msg, "[" BBSMNAME "法院]");
	police = VIOLATELAW_POLICE;
    }

    AUTO_STR(msg, ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n"
	    "     " ANSI_COLOR(1;32) "%s" ANSI_RESET "因"
            ANSI_COLOR(1;35) "%s" ANSI_RESET "行為，\n"
	    "違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET
            "，特此公告\n\n\n%s\n",
	    police, userid, reason, result, memo ? memo : "");
    post_msg("ViolateLaw", title, msg, "[" BBSMNAME "法院]");
}

#define VIOLATELAW_MAIL_FROM  "[" BBSMNAME "警察局]"
#define VIOLATELAW_MAIL_TITLE "[報告] 違法判決報告"

// Mails userid the penalty result, with how to pay the ticket.
static void
violatelaw_mail(const char *userid, const char *police, const char *reason,
                const char *result, const char *memo)
{
    if (!police)
	police = VIOLATELAW_POLICE;
    // The permissions changed.
    sendalert(userid,  ALERT_PWD_PERM);

    // The usual header (ctime4() ends with a new line), then the verdict.
    AUTO_STR(msg, "作者: " VIOLATELAW_MAIL_FROM "\n"
	    "標題: " VIOLATELAW_MAIL_TITLE "\n"
	    "時間: %s\n"
	    ANSI_COLOR(1;32) "%s" ANSI_RESET "判決：\n     " ANSI_COLOR(1;32) "%s" ANSI_RESET
	    "因" ANSI_COLOR(1;35) "%s%s%s%s" ANSI_RESET "行為，\n"
	    "違反本站站規，處以" ANSI_COLOR(1;35) "%s" ANSI_RESET "，特此通知\n\n"
	    "請到 " BN_LAW " 查詢相關法規資訊，並從主選單進入:\n"
	    "(P)lay【娛樂與休閒】=>(P)ay【" BBSMNAME2 "量販店 】=> (1)ViolateLaw 繳罰單\n"
	    "以繳交罰單。\n",
	    ctime4(&now), police, userid, reason, memo ? "\n " : "",
	    memo ? memo : "", memo ? "\n" : "", result);
    save_mailbox(VIOLATELAW_MAIL_FROM, userid, VIOLATELAW_MAIL_TITLE, msg,
                 NULL, 0, MAILSEND_FLAG_NONE, NULL);
}

// Gives userid a ticket (罰單) for reason: PERM_VIOLATELAW until it is paid
// (see save_violatelaw()), posted, mailed, and all sessions kicked.
// Returns 0, or -1 if there is no such user.
int
violatelaw_ticket(const char *userid, const char *police, const char *reason,
                  const char *memo)
{
    const char *result = "罰單處分";
    int uid = searchuser(userid, NULL);

    if (uid <= 0)
	return -1;
    if (uid == usernum) {
	pwcuViolateLaw();
	violatelaw_post(userid, police, reason, result, memo);
	violatelaw_mail(userid, police, reason, result, memo);
	kick_all(userid);
	return 0;
    }

    // Kicked first, as their sessions could save the record over it.
    userec_t u;
    kick_all(userid);
    if (passwd_sync_query(uid, &u) != 0)
	return -1;
    u.userlevel |= PERM_VIOLATELAW;
    u.timeviolatelaw = now;
    u.vl_count++;
    passwd_sync_update(uid, &u);
    violatelaw_post(userid, police, reason, result, memo);
    violatelaw_mail(userid, police, reason, result, memo);
    return 0;
}

// Pays the ticket (罰單) given by violatelaw_ticket(), after waiting 3 days
// for each ticket ever given, with 1000 for each. (Heat CharlieL)
int
save_violatelaw(void)
{
    char            ok[3];
    int             day;

    setutmpmode(VIOLATELAW);
    clear();
    vs_hdr("繳罰單中心");

    // XXX reload lots of stuff here?
    pwcuReload();
    if (!(cuser.userlevel & PERM_VIOLATELAW)) {
	vmsg("你沒有被開罰單。");
	return 0;
    }

    day =  cuser.vl_count*3 - time4_days_elapsed(now, cuser.timeviolatelaw);
    if (day > 0) {
        vmsgf("依照違規次數(%d), 你還需要 %d 天才能繳罰單",
              cuser.vl_count, day);
	return 0;
    }
    reload_money();
    if (cuser.money < (int)cuser.vl_count * 1000) {
	mvouts(22, 0, TEMPFORMAT(128, ANSI_COLOR(1;31) "這是你第 %d 次違反本站法規"
                 "必須繳出 %d " MONEYNAME "；但你目前只有 %d ，數量不足!!"
                 ANSI_RESET, (int)cuser.vl_count, (int)cuser.vl_count * 1000,
                 cuser.money));
	pressanykey();
	return 0;
    }
    move(5, 0);
    prints("這是你第 %d 次違法 必須繳出 %d " MONEYNAME "\n\n",
	    cuser.vl_count, cuser.vl_count * 1000);
    outs(ANSI_COLOR(1;37) "你是否確定以後不會再犯了？" ANSI_RESET "\n");

    if (!getdata(10, 0, "確定嗎？[y/N]:", ok, sizeof(ok), LCECHO) ||
	ok[0] != 'y')
    {
	move(15, 0);
	outs(ANSI_COLOR(1;31) "不想付嗎？ 還是不知道要按 y ？\n"
	     "請養成看清楚系統訊息的好習慣。\n" ANSI_RESET);
	pressanykey();
	return 0;
    }

    //Ptt:check one more time
    reload_money();
    if (cuser.money < (int)cuser.vl_count * 1000)
    {
	log_filef("log/violation",
		"%s pay-violation error: race-conditionn hack?\n",
		cuser.userid);
	vmsg(MONEYNAME "怎麼忽然不夠了？ 試圖欺騙系統被查到將砍帳號！");
	return 0;
    }

    pay(1000 * (int)cuser.vl_count, "繳付罰單 (#%d)", cuser.vl_count);
    pwcuSaveViolateLaw();
    log_filef("log/violation",
	    "%s pay-violation: $%d complete.\n",
	    cuser.userid, (int)cuser.vl_count*1000);

    vmsg("罰單已付，請重新登入。");
    u_exit("save_violate");
    exit(0);
    return 0;
}

void
post_newboard(const char *bgroup, const char *bname, const char *bms)
{
    post_msg("Record",
	     TEMPFORMAT(TTLEN + 1, "[新板成立] %s", bname),
	     TEMPFORMAT(ANSILINELEN, "%s 開了一個新板 %s : %s\n\n新任板主為 %s\n\n恭喜*^_^*\n",
			cuser.userid, bname, bgroup, bms),
	     "[系統]");
}

// reporter: who detected it, NULL for the system.
void post_policelog_spam(const char *bname, const char *attach_file,
			 const char *reporter)
{
    char fpath[PATHLEN] = "";
    if (post_msg2(BN_POLICELOG,
		  TEMPFORMAT(STRLEN, "[報告] %s:偵測到不當發言", cuser.userid),
		  TEMPFORMAT(ANSILINELEN, "%s 偵測到 %s 試圖於 %s %s 發表不當言論，\n"
			     "原文如下 (不予發出，但有備份至使用者信箱):\n%s\n\n",
			     reporter ? reporter : "系統",
			     cuser.userid, bname ? "看板" : "郵件", bname ? bname : "",
			     MSG_SEPARATOR),
		  "[系統]", fpath))
        return;
    assert(attach_file && *attach_file);
    if (*fpath)
        AppendTail(attach_file, fpath, 0);
}

void
post_policelog2(const char *bname, const char *atitle, const char *action,
                const char *reason, const int toggle, const char *attach_file)
{
    char msg_file[PATHLEN];

    if (post_msg2(BN_POLICELOG,
		  TEMPFORMAT(TTLEN + 1, "[%s][%s] %s by %s", action,
			     toggle ? "開啟" : "關閉", bname, cuser.userid),
		  TEMPFORMAT(ANSILINELEN, "%s (%s) %s %s 看板 %s 功能\n原因 : %s\n%s%s\n\n",
			     cuser.userid, fromhost, toggle ? "開啟" : "關閉", bname, action,
			     reason, atitle ? "文章標題 : " : "", atitle ? atitle : ""),
		  "[系統]", msg_file) == 0) {
        if (attach_file)
            AppendTail(attach_file, msg_file, 0);
    }
}

void
post_policelog(const char *bname, const char *atitle, const char *action,
               const char *reason, const int toggle) {
    return post_policelog2(bname, atitle, action, reason, toggle, NULL);
}
