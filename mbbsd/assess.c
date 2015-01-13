#include "bbs.h"

#ifdef ASSESS

static time4_t
adjust_badpost_time(time4_t timeremovebadpost) {
    time4_t min_timer;

    // reset timer to at least some days to prevent user directly
    // canceling his badpost.
    syncnow();
    min_timer = (now - (BADPOST_CLEAR_DURATION - BADPOST_MIN_CLEAR_DURATION) *
                 DAY_SECONDS);
    if (timeremovebadpost < min_timer)
        timeremovebadpost = min_timer;

    return timeremovebadpost;
}

int
inc_badpost(const char *userid, int num) {
    userec_t xuser;
    int uid = getuser(userid, &xuser);

    if (uid <= 0)
        return 0;

    xuser.timeremovebadpost = adjust_badpost_time(xuser.timeremovebadpost);
    xuser.badpost += num;
    passwd_sync_update(uid, &xuser);
    return xuser.badpost;
}

static char * const badpost_reason[] = {
    "廣告", "不當用辭", "人身攻擊"
};

#define DIM(x)	(sizeof(x)/sizeof(x[0]))

int assign_badpost(const char *userid, fileheader_t *fhdr,
	const char *newpath, const char *comment)
{
    char genbuf[STRLEN];
    char reason[STRLEN];
    char rptpath[PATHLEN];
    int i, tusernum = searchuser(userid, NULL);

    strcpy(rptpath, newpath);
    assert(tusernum > 0 && tusernum < MAX_USERS);
    move(b_lines - 2, 0);
    clrtobot();
    for (i = 0; i < (int)DIM(badpost_reason); i++)
	prints("%d.%s ", i + 1, badpost_reason[i]);
    prints("%d.%s ", i + 1, "其他");
    prints("0.取消退文 ");
    do {
        getdata(b_lines - 1, 0, "請選擇: ", genbuf, 2, NUMECHO);
        i = genbuf[0] - '1';
        if (i == -1) {
            vmsg("取消設定退文。");
            return -1;
        }
        if (i < 0 || i > (int)DIM(badpost_reason))
            bell();
        else
            break;
    } while (1);

    if (i < (int)DIM(badpost_reason)) {
        snprintf(reason, sizeof(reason), "%s", badpost_reason[i]);
    } else if (i == DIM(badpost_reason)) {
        while (!getdata(b_lines, 0, "請輸入原因", reason, 50, DOECHO)) {
            // 對於 comment 目前可以重來，但非comment 文直接刪掉所以沒法 cancel
            if (comment) {
                vmsg("取消設定退文。");
                return -1;
            }
            bell();
            continue;
        }
    }
    assert(i >= 0 && i <= (int)DIM(badpost_reason));

    sprintf(genbuf,"退回%s(%s)", comment ? "推文" : "文章", reason);

    if (fhdr) strncat(genbuf, fhdr->title, 64-strlen(genbuf));

#ifdef USE_COOLDOWN
    add_cooldowntime(tusernum, 60);
    add_posttimes(tusernum, 15); //Ptt: 凍結 post for 1 hour
#endif

    if (!(inc_badpost(userid, 1) % 5)){
	userec_t xuser;
	post_violatelaw(userid, BBSMNAME "系統警察",
		"退文累計 5 篇", "罰單一張");
	mail_violatelaw(userid, BBSMNAME "系統警察",
		"退文累計 5 篇", "罰單一張");
	kick_all(userid);
	passwd_sync_query(tusernum, &xuser);
	xuser.money = moneyof(tusernum);
	xuser.vl_count++;
	xuser.userlevel |= PERM_VIOLATELAW;
	xuser.timeviolatelaw = now;
	passwd_sync_update(tusernum, &xuser);
    }

    if (!comment) {
        log_filef(newpath, LOG_CREAT, "※ BadPost Reason: %s\n", reason);
    }

#ifdef BAD_POST_RECORD
    // we also change rptpath because such record contains more information
    {
	int rpt_bid = getbnum(BAD_POST_RECORD);
	if (rpt_bid > 0) {
	    fileheader_t report_fh;
	    char rptdir[PATHLEN];
	    FILE *fp;

	    setbpath(rptpath, BAD_POST_RECORD);
	    stampfile(rptpath, &report_fh);

	    strcpy(report_fh.owner, "[" BBSMNAME "警察局]");
	    snprintf(report_fh.title, sizeof(report_fh.title),
		    "%s 板 %s 板主退回 %s %s",
		    currboard, cuser.userid, userid, comment ? "推文" : "文章");
	    Copy(newpath, rptpath);
	    fp = fopen(rptpath, "at");

	    if (fp)
	    {
		fprintf(fp, "\n退文原因: %s\n", genbuf);
		if (comment)
		    fprintf(fp, "\n退回推文項目:\n%s", comment);
		fprintf(fp, "\n");
		fclose(fp);
	    }

	    setbdir(rptdir, BAD_POST_RECORD);
	    append_record(rptdir, &report_fh, sizeof(report_fh));

	    touchbtotal(rpt_bid);
	}
    }
#endif /* defined(BAD_POST_RECORD) */

    sendalert(userid,  ALERT_PWD_PERM);
    mail_id(userid, genbuf, rptpath, cuser.userid);

    return 0;
}

int
reassign_badpost(const char *userid) {
    // 劣退復鞭屍，補劣何其多。補劣無通知，用戶搞不懂。
    // 用戶若被補劣累，東牽西扯罵系統。
    //
    // 所以，如果真的要補劣，一定要告知使用者。
    userec_t u;
    char title[TTLEN+1];
    char msg[512];
    char buf[10];
    char reason[50];
    int orig_badpost = 0;
    int uid;

    vs_hdr2(" 退文修正 ", userid);
    if ((uid = getuser(userid, &u)) == 0) {
        vmsgf("找不到使用者 %s。", userid);
        return -1;
    }
    orig_badpost = u.badpost;
    prints("\n使用者 %s 的退文數目前為: %d\n", userid, u.badpost);
    snprintf(buf, sizeof(buf), "%d", u.badpost);
    if (!getdata_str(5, 0, "調整退文數目為: ", buf, sizeof(buf), DOECHO, buf) ||
        atoi(buf) == u.badpost) {
        vmsg("退文數目不變，未變動。");
        return 0;
    }

    // something changed.
    u.badpost = atoi(buf);
    if (u.badpost > orig_badpost) {
        u.timeremovebadpost = adjust_badpost_time(u.timeremovebadpost);
    }
    prints("\n使用者 %s 的退文即將由 %d 改為 %d。請輸入理由(會寄給使用者)\n",
           userid, orig_badpost, u.badpost);
    if (!getdata(7, 0, "理由: ", reason, sizeof(reason), DOECHO)) {
        vmsg("錯誤: 不能無理由。");
        return -1;
    }

    move(6, 0); clrtobot();
    prints("使用者 %s 的退文由 %d 改為 %d。\n理由: %s\n",
           userid, orig_badpost, u.badpost, reason);
    if (!getdata(9, 0, "確定？ [y/N]", buf, 3, LCECHO) || buf[0] != 'y') {
        vmsg("錯誤: 未確定，放棄。");
        return -1;
    }

    // GOGOGO
    snprintf(msg, sizeof(msg),
             "   站長" ANSI_COLOR(1;32) "%s" ANSI_RESET "把" ANSI_COLOR(1;32)
             "%s" ANSI_RESET "的退文從" ANSI_COLOR(1;35) "%d" ANSI_RESET
             "改成" ANSI_COLOR(1;35) "%d" ANSI_RESET "\n"
             "   " ANSI_COLOR(1;37) "修改理由是：%s" ANSI_RESET,
             cuser.userid, u.userid, orig_badpost, u.badpost, reason);
    snprintf(title, sizeof(title),
             "[安全報告] 站長%s修改%s退文報告", cuser.userid, u.userid);
    post_msg(BN_SECURITY, title, msg, "[系統安全局]");
    mail_log2id_text(u.userid, "[系統通知] 退文變更", msg,
                     "[系統安全局]", NA);
    passwd_sync_update(uid, &u);
    kick_all(u.userid);

    return 0;
}
#endif
