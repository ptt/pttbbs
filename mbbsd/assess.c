#include "bbs.h"

static time4_t
adjust_badpost_time(time4_t timeremovebadpost) {
    time4_t min_timer;

    // reset timer to at least some days to prevent user directly
    // canceling his badpost.
    syncnow();
    min_timer = time4_diff(
            now, (BADPOST_CLEAR_DURATION - BADPOST_MIN_CLEAR_DURATION) * DAY_SECONDS);
    if (time4_lt(timeremovebadpost, min_timer))
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
    on_inc_badpost(&xuser);
    passwd_sync_update(uid, &xuser);
    return xuser.badpost;
}

static char * const badpost_reason[] = {
    "廣告", "不當用辭", "人身攻擊"
};


// The usual reasons of bad posts, for modlog_ask_reason_choice().
int
badpost_reasons(const char * const **reasons)
{
    *reasons = (const char * const *)badpost_reason;
    return (int)ARRAY_SIZE(badpost_reason);
}

// Records a bad post with the given reason, without asking anything.
void
apply_badpost(const char *userid, const fileheader_t *fhdr,
	      const char *newpath, const char *reason)
{
    char genbuf[STRLEN];
    char rptpath[PATHLEN];
    int tusernum = searchuser(userid, NULL);

    // The account may be gone since the caller looked it up.
    if (tusernum <= 0 || tusernum >= MAX_USERS)
        return;
    STRLCPY(rptpath, newpath);

    snprintf(genbuf, sizeof(genbuf), "退回文章(%s)", reason);
    if (fhdr && strlen(genbuf) < 64)
        strncat(genbuf, fhdr->title, 64 - strlen(genbuf));

    // Only police or the system give tickets, not a count of bad posts.
    inc_badpost(userid, 1);

    file_appendf(newpath, STR_POST_MARK "BadPost Reason: %s\n", reason);

#ifdef BAD_POST_RECORD
    // we also change rptpath because such record contains more information
    {
	int rpt_bid = getbnum(BAD_POST_RECORD);
	if (rpt_bid > 0) {
	    fileheader_t report_fh;
	    char rptdir[PATHLEN];

	    setbpath(rptpath, BAD_POST_RECORD);
	    stampfile(rptpath, &report_fh);

	    strcpy(report_fh.owner, "[" BBSMNAME "警察局]");
	    SNPRINTF(report_fh.title, "%s 板 %s 板主退回 %s 文章",
		    currboard, cuser.userid, userid);
	    Copy(newpath, rptpath);
	    file_appendf(rptpath, "\n退文原因: %s\n", genbuf);
	    file_append(rptpath, "\n");

	    setbdir(rptdir, BAD_POST_RECORD);
	    append_fileheader(rptdir, &report_fh);

	    touchbtotal(rpt_bid);
	}
    }
#endif /* defined(BAD_POST_RECORD) */

    sendalert(userid,  ALERT_PWD_PERM);
    mail_send_file(userid, genbuf, rptpath, cuser.userid);
}

int
reassign_badpost(const char *userid) {
    // 劣退復鞭屍，補劣何其多。補劣無通知，用戶搞不懂。
    // 用戶若被補劣累，東牽西扯罵系統。
    //
    // 所以，如果真的要補劣，一定要告知使用者。
    userec_t u;
    char buf[10];
    char reason[50];
    int orig_badpost = 0;
    int uid;

    vs_hdr2("退文修正", userid);
    if ((uid = getuser(userid, &u)) == 0) {
        vmsgf("找不到使用者 %s。", userid);
        return -1;
    }
    orig_badpost = u.badpost;
    prints("\n使用者 %s 的退文數目前為: %d\n", userid, u.badpost);
    SNPRINTF(buf, "%d", u.badpost);
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
    AUTO_STR(msg, "   站長" ANSI_COLOR(1;32) "%s" ANSI_RESET "把" ANSI_COLOR(1;32)
             "%s" ANSI_RESET "的退文從" ANSI_COLOR(1;35) "%d" ANSI_RESET
             "改成" ANSI_COLOR(1;35) "%d" ANSI_RESET "\n"
             "   " ANSI_COLOR(1;37) "修改理由是：%s" ANSI_RESET,
             cuser.userid, u.userid, orig_badpost, u.badpost, reason);
    AUTO_STR(title, "[安全報告] 站長%s修改%s退文報告", cuser.userid, u.userid);
    post_msg(BN_SECURITY, title, msg, "[系統安全局]");
    mail_send_text(u.userid, "[系統通知] 退文變更", msg,
                   "[系統安全局]");
    passwd_sync_update(uid, &u);
    kick_all(u.userid);

    return 0;
}
