#include "bbs.h"

// Access Control List
// Holder of all access / permission related stuff
//
// Author: Hung-Te Lin (piaip)

///////////////////////////////////////////////////////////////////////////
// Bakuman: New banning system (store in user home)

#define BAKUMAN_OBJECT_TYPE_USER     'u'
#define BAKUMAN_OBJECT_TYPE_BOARD    'b'
#define BAKUMAN_REASON_LEN          (BTLEN)
// Lines below the start of the mute form: the title, the period, the
// note, the period set, and the reason with what will be recorded.
#define MUTE_FORM_LINES             (11)
// Lines below the start of the mute and unmute confirmations: the summary
// or the reason, the modlog preview (up to 6 lines), the notes and the
// prompt.
#define MUTE_CONFIRM_LINES          (14)
#define UNMUTE_CONFIRM_LINES        (13)

static void
bakuman_make_tag_filename(const char *who, const char *object, char object_type,
                          size_t szbuf, char *buf, int create_folder) {
    char prefix[3] = { object_type, '_', 0 };

    if (!who)
        setuserfile(buf, FN_MUTED "/");
    else
        sethomefile(buf, who, FN_MUTED "/");
    if (create_folder && !dashd(buf)) {
        Mkdir(buf);
    }
    strlcat(buf, prefix, szbuf);
    strlcat(buf, object, szbuf);
}

static int
bakuman_get_info(const char *filename,
                 time4_t *pexpire, size_t szreason, char *reason) {
    FILE *fp = fopen(filename, "rt");
    uint32_t ts = 0;
    char buf[STRLEN];

    if(!fp)
        return 0;

    // banned file format:
    //  Line 1: EXPIRE_TIME
    //  Line 2: REASON
    buf[0] = 0;
    if (fgets(buf, sizeof(buf), fp)) {
        sscanf(buf, "%u", &ts);
        if (pexpire) {
            *pexpire = (time4_t) ts;
        }
    }
    buf[0] = 0;
    if (fgets(buf, sizeof(buf), fp)) {
        if (szreason && reason) {
            chomp(buf);
            strlcpy(reason, buf, szreason);
        }
    }
    fclose(fp);
    return 1;
}

static int
bakuman_set_info(const char *filename, time4_t expire, const char *reason) {
    FILE *fp = fopen(filename, "wt");
    if (!fp)
        return 0;
    if (!reason)
        reason = "";

    fprintf(fp, "%u\n%s\n", (unsigned)expire, reason);
    fclose(fp);
    return 1;
}

// return if 'who (NULL=self)' is banned by 'object'
static time4_t
is_muted_by(const char *who, const char *object, char object_type,
             size_t szreason, char *reason) {
    char tag_fn[PATHLEN];
    time4_t expire = 0;

    bakuman_make_tag_filename(who, object, object_type,
                              sizeof(tag_fn), tag_fn, 0);
    if (!dashf(tag_fn))
        return 0;

    // check expire
    if (bakuman_get_info(tag_fn, &expire, szreason, reason) && time4_gt(now, expire)) {
        unlink(tag_fn);
        return 0;
    }
    return expire;
}

// add 'who (NULL=self)' to the ban list of 'object'
static int
mute_user_as(const char *who, const char *object, char object_type,
            time4_t expire, const char *reason) {
    char tag_fn[PATHLEN];

    bakuman_make_tag_filename(who, object, object_type,
                              sizeof(tag_fn), tag_fn, 1);
    return bakuman_set_info(tag_fn, expire, reason);
}

int
get_user_muted_status_by_board(const char *user, const char *board,
                                size_t szreason, char *reason) {
    return is_muted_by(user, board, BAKUMAN_OBJECT_TYPE_BOARD,
                        szreason, reason);
}

time4_t
is_user_muted_by_board(const char *user, const char *board) {
    return is_muted_by(user, board, BAKUMAN_OBJECT_TYPE_BOARD, 0, NULL);
}

time4_t
is_muted_by_board(const char *board) {
    return is_user_muted_by_board(cuser.userid, board);
}

int
mute_user_for_board(const char *user, const char *board,
                   time4_t expire, const char *reason) {
    return mute_user_as(user, board, BAKUMAN_OBJECT_TYPE_BOARD, expire, reason);
}

int
unmute_user_for_board(const char *user, const char *board) {
    char tag_fn[PATHLEN];

    bakuman_make_tag_filename(user, board, BAKUMAN_OBJECT_TYPE_BOARD,
                             sizeof(tag_fn), tag_fn, 0);
    return unlink(tag_fn) == 0;
}

static time4_t
ui_print_user_muted_status_for_board(const char *uid, const char *board) {
    char reason[STRLEN];
    time4_t expire = get_user_muted_status_by_board(
        uid, board, sizeof(reason), reason);

    if (time4_gt(expire, now)) {
        prints("暫停使用者 %s 發言，解除時間尚有 %d 天: %s\n理由:%s",
               uid, time4_days_remaining(expire, now),
               Cdatelite(&expire), reason);
    } else {
        prints("使用者 %s 目前不在禁言名單中。\n",
               uid);
        expire = 0;
    }
    return expire;
}

static int
ui_mute_user_for_board(const char *uid, const char *board) {
    time4_t expire = now;
    int y;
    int result;
    char ans[3];
    char reason[STRLEN];
    char datebuf[STRLEN];
    int days = 0;
    AUTO_HOMEFILE(user_history_log, uid, FN_USER_MUTED_HISTORY);

    y = vgety();
    if ((expire = is_user_muted_by_board(uid, board))) {
        vmsgf("使用者之前已被禁言，尚有 %d 天；詳情可用(S)或(L)查看",
              time4_days_remaining(expire, now));
        return -1;
    }
    // Keep the whole form on the screen, or asking the reason would move
    // over the note and the period.
    if (y > b_lines - MUTE_FORM_LINES) {
        y = b_lines - MUTE_FORM_LINES;
        move(y, 0); clrtobot();
    }
    prints("將使用者 %s 加入看板 %s 的禁言名單。", uid, board);
    move(y+3, 0);
    syncnow();
    outs("目前接受的格式是 [數字][單位]。 "
         "單位有: 年(y), 月(m), 天(d)\n"
         "範例: 3m (三個月), 120d (120天), 10y (10年)\n"
         "注意不可混合輸入(例:沒有三個半月這種東西,請換算成天數)\n"
        );
    getdata(y+1, 0, "請以數字跟單位(預設為天)輸入期限: ",
            datebuf, 8, DOECHO);
    trim(datebuf);
    if (!*datebuf) {
        vmsg("未輸入期限，放棄。");
        return -1;
    } else {
        int val = atoi(datebuf);
        switch(tolower(datebuf[strlen(datebuf)-1])) {
            case 'y':
                val *= 365;
                break;
            case 'm':
                val *= 30;
                break;
            case 'd':
            default:
                break;
        }
        if (val < 1) {
            vmsg("日期格式輸入錯誤或是小於一天無法處理。");
            return -1;
        }
        if (time4_add_sec(now, (uint64_t)val * DAY_SECONDS, &expire) < 0) {
            vmsg("日期過大或無法處理，請重新輸入。");
            return -1;
        }
        move(y+3, 0); clrtobot();
        // sprintf(datebuf, "%s", Cdatelite(&expire));
        sprintf(datebuf, "%d 天", val);
        prints("期限將設定為 %s之後: %s\n",
               datebuf, Cdatelite(&expire));
        days = val;
    }

    assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
    mvouts(y+2, 0, ANSI_COLOR(1;31) "請注意此理由會由信件通知使用者也會在看板管理紀錄自動公告。" ANSI_RESET);
    // maybe race condition here, but fine.
    if (modlog_ask_reason(board, y+4, reason, BAKUMAN_REASON_LEN) < 0)
        return -1;

    // The summary, the modlog preview and the confirmation.
    if (y > b_lines - MUTE_CONFIRM_LINES)
        y = b_lines - MUTE_CONFIRM_LINES;
    move(y, 0); clrtobot();
    prints("\n使用者 %s 即將加入禁言名單 (期限: %s)\n"
           "理由: %s\n"
           ANSI_COLOR(1;32) "會寄信通知使用者" ANSI_RESET "\n",
           uid, datebuf,
           reason);
    char action[STRLEN];
    snprintf(action, sizeof(action), "將 %s 加入禁言名單，期限 %s",
             uid, datebuf);
    int yc = modlog_preview(y + 5, board, reason, "%s", action);
    // Shown with the confirmation, as asking the reason clears below it.
    if (days > KEEP_DAYS_REGGED) {
        mvprints(yc, 0, ANSI_COLOR(1;31)
                 "注意: 超過 %d 天的設定有可能因為對方一直"
                 "未上站而導致帳號過期被重新註冊，\n"
                 "      此時同名的新帳號由於不一定是同一人所以"
                 "不會被禁言。" ANSI_RESET, KEEP_DAYS_REGGED);
        yc += 2;
    }

    // last chance
    getdata(yc + 1, 0, "確認以上資料全部正確嗎？ [y/N]: ",
            ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y') {
        vmsg("請重新輸入");
        return -1;
    }

    result = mute_user_for_board(uid, board, expire, reason);
    AUTO_STR(msg_board, ANSI_COLOR(1) ANSI_COLOR(33) "%s" ANSI_RESET "看板",
             board);
    AUTO_STR(msg_tail,
              " 暫停 " ANSI_COLOR(1;31) "%s" ANSI_RESET
              " 發言，期限為 %s\n  理由: %s\n",
              uid, datebuf, reason);
    if (result) {
        mkuserdir(uid);
        log_filef(user_history_log, "%s%s", msg_board, msg_tail);
        modlog(board, reason, "%s", action);
    }
    if (result) {
        char anti_pettifogger[] =
            "請注意，\n"
            ANSI_COLOR(1;31) "您目前使用的網站服務乃本站無償提供,\n"
            "本站可隨時停止對您提供本站的免費服務.\n" ANSI_RESET
            "\n"
            "依照您過去已親自同意之使用者條款,"
            "您已同意本站板主基於管理之考量,\n"
            "得將您之帳號設定停止看板服務(水桶或暫停發言)、\n"
            "刪除您發表之特定文章,\n"
            "以及退回您發表之特定文章。\n"
            "\n"
            "此乃本站與您之民事關係,\n"
            "水桶或暫停發言設定、刪文設定、退文設定等,\n"
            ANSI_COLOR(1;31)
            "乃本站依民事契約終止部分原先無償提供予您之服務,\n"
            ANSI_RESET
            "板主所為此類設定非為無權或無故,\n"
            "應無由成立刑法妨礙電腦使用罪、公然侮辱罪、誹謗罪。\n"
            "\n"
            "請勿濫行提告。\n";
        AUTO_STR(xtitle, "%s 看板暫停發言通知", board);
        AUTO_STR(xmsg, "%s 看板已暫時停止讓您發表意見。\n"
                 "開始時間: %s (期限 %s，此為執行時間，非原始犯規時間)\n"
                 "原因: %s\n"
                 "其它資訊請洽該看板板規與公告。\n\n%s",
                 board, Cdatelite(&now), datebuf,
                 reason, anti_pettifogger);
        mail_send_text(uid,  xtitle,  xmsg,  "[系統通知]");
        sendalert(uid, ALERT_NEW_MAIL);
    }
    invalid_board_permission_cache(board);
    // Last: a dropped connection at this prompt must not skip the above.
    vmsg(result ? "已將使用者加入禁言名單" : "失敗，請向站長報告");
    return 0;
}

static int
ui_unmute_user_for_board(const char *uid, const char *board) {
    time4_t expire = now;
    int y;
    char ans[3];
    char reason[STRLEN];
    AUTO_HOMEFILE(user_history_log, uid, FN_USER_MUTED_HISTORY);

    y = vgety();
    if (!(expire = is_user_muted_by_board(uid, board))) {
        vmsg("使用者未在禁言名單。");
        return -1;
    }
    // The reason, the modlog preview and the confirmation.
    if (y > b_lines - UNMUTE_CONFIRM_LINES)
        y = b_lines - UNMUTE_CONFIRM_LINES;
    move(y, 0); clrtobot();
    prints("提前解除使用者 %s 於看板 %s 的禁言限制 (尚有 %d 天)。",
           uid, board, time4_days_remaining(expire, now));
    assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
    int yr = modlog_ask_reason(board, y+1, reason, BAKUMAN_REASON_LEN);
    if (yr < 0)
        return -1;
    char action[STRLEN];
    snprintf(action, sizeof(action), "提前解除 %s 的禁言 (距原期限尚有 %d 天)",
             uid, time4_days_remaining(expire, now));
    int yc = modlog_preview(yr, board, reason, "%s", action);
    mvouts(yc + 1, 0, ANSI_COLOR(1;32) "會寄信通知使用者" ANSI_RESET);

    // last chance
    getdata(yc + 2, 0, "確認以上資料全部正確嗎？ [y/N]: ",
            ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y') {
        vmsg("請重新輸入");
        return -1;
    }

    unmute_user_for_board(uid, board);
    AUTO_STR(msg_board, ANSI_COLOR(1) ANSI_COLOR(33) "%s" ANSI_RESET "看板",
             board);
    AUTO_STR(msg_tail,
              " 解除 " ANSI_COLOR(1;32) "%s" ANSI_RESET
              " 的禁言限制 (距原期限尚有 %d 天)\n  理由: %s\n",
              uid, time4_days_remaining(expire, now), reason);
    mkuserdir(uid);
    log_filef(user_history_log, "%s%s", msg_board, msg_tail);
    modlog(board, reason, "%s", action);
    AUTO_STR(xtitle, "%s 看板提前解除禁言通知", board);
    AUTO_STR(xmsg, "%s 看板已提前解除對您的禁言限制。\n"
             "解除時間: %s (距原期限尚有 %d 天)\n"
             "原因: %s\n\n"
             "最晚於您重新上站後生效。\n",
             board, Cdatelite(&now), time4_days_remaining(expire, now),
             reason);
    mail_send_text(uid, xtitle, xmsg, "[系統通知]");
    sendalert(uid, ALERT_NEW_MAIL);
    invalid_board_permission_cache(board);
    // Last: a dropped connection at this prompt must not skip the above.
    vmsg("使用者的禁言限制已解除，最晚至該使用者重新上站後生效");
    return 0;
}

// Prints the mute history of the user (on all boards) below the cursor,
// and offers to view it all if it doesn't fit the screen.
static void
ui_print_user_muted_history(const char *uid) {
    AUTO_HOMEFILE(history_log, uid, FN_USER_MUTED_HISTORY);
    char buf[ANSILINELEN];
    int y = vgety() + (vgetx() > 0 ? 1 : 0) + 1;
    bool more_lines = false;
    FILE *fp;

    mvouts(y++, 0, "歷史紀錄：");
    if (!(fp = fopen(history_log, "rt"))) {
        mvouts(y, 0, "(無)");
        pressanykey();
        return;
    }
    while (fgets(buf, sizeof(buf), fp)) {
        if (y >= b_lines) {
            more_lines = true;
            break;
        }
        chomp(buf);
        // One screen line each, or the lines below would be pushed down.
        buf[stream_col_offset(t_columns - 1, buf, NULL)] = '\0';
        mvouts(y++, 0, buf);
        outs(ANSI_RESET);
    }
    fclose(fp);

    if (!more_lines) {
        pressanykey();
        return;
    }
    if (tolower(vmsg("要檢視使用者完整的歷史紀錄嗎？ [y/N]")) == 'y')
        more(history_log, YEA);
}

int
edit_muted_list_for_board(const char *board) {
    // TODO generalize this.
    char uid[IDLEN+1], ans[3];

    if (!board || !*board || getbnum(board) < 1)
        return 0;

    while (1) {
        vs_hdr2("Bakuman 權限設定系統",
                TEMPFORMAT(STRLEN, "看板: %s ，類型: 禁止發言，名單上限: ∞", board));
        move(3, 0);
        outs(ANSI_COLOR(1)
        "                   歡迎使用 Bakuman 權限設定系統!\n\n" ANSI_RESET
        "      本系統提供下列功能:" ANSI_COLOR(1;33)
                                 " - 無人數上限的名單設定\n"
        "                          - 自動生效的時效限制\n"
        "                          - 名單內舊帳號過期重新註冊時自動失效\n\n"
        ANSI_RESET
        "   小提示: 本系統不會(也無法)列出 [現在名單內有哪些人]，\n"
        "           它採取的是設後免理+記錄式的概念: (可由歷史記錄自行推算)\n"
        "         - 平時用(A)新增並設好期限後就不用再去管設了哪些人\n"
        "         - 除非想提前解除或發現設錯，此時可用(D)先刪除然後再用(A)重新設定\n"
        "         - 想確認是否設錯或查某個使用者是不是仍在禁言中，可用(S)來檢查\n"
        "           另外也可用(L)檢視看板管理稽核紀錄\n"
        "         - 沒有[永久禁言]的設定，若有需要請設個 50年之類的大數字\n"
        "         - 新增/解除會寄信通知使用者，另外請注意" ANSI_COLOR(1;33)
                   "帳號被砍後禁言會自動解除\n" ANSI_RESET
        "         - 禁言自動解除不會出現在記錄裡，只有手動提前解除的才會\n"
        ANSI_COLOR(1)
        "         - 想查看某使用者為何被禁言可用(S)。\n"
        ANSI_RESET
        );

        getdata(1, 0, "(A)增加 (D)提前清除 (S)查詢帳號 "
                      "(L)列出設定歷史 "
                      "[Q]結束? ",
                ans, 2, LCECHO);
        move(2, 0); clrtobot();
        if (*ans == 'q' || !*ans)
            break;

        switch (*ans) {
            case 's':
                move(1, 0);
                usercomplete(MSG_UID, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                move(1, 0); clrtobot();
                ui_print_user_muted_status_for_board(uid, board);
                ui_print_user_muted_history(uid);
                break;

            case 'a':
                move(1, 0);
                usercomplete(MSG_UID, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                move(modlog_form(board, "禁言使用者"), 0);
                if (ui_mute_user_for_board(uid, board) < 0)
                    continue;
                break;

            case 'd':
                move(1, 0);
                usercomplete(MSG_UID, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                move(modlog_form(board, "提前解除禁言"), 0);
                if (ui_unmute_user_for_board(uid, board) < 0)
                    continue;
                break;

            case 'l':
                modlog_view(board, 1);
                break;

            default:
                break;
        }
    }
    return 0;
}

int
edit_user_acl_for_board(const char *uid, const char *board) {
    time4_t expire;
    int finished = 0;

#define LNACLEDIT (10)

    int ytitle = b_lines - LNACLEDIT;
    grayout(0, ytitle-2, GRAYOUT_DARK);

    while (!finished) {
        move(ytitle-1, 0); clrtobot();
        outs("\n" ANSI_REVERSE);
        vbar(TEMPFORMAT(STRLEN, " 設定使用者 %s 於看板《%s》之權限", uid, board));

        move(ytitle+2, 0);
        expire = ui_print_user_muted_status_for_board(uid, board);

        move(ytitle+5, 0);
        prints(" " ANSI_COLOR(1;36) "%s" ANSI_RESET " - %s\n",
               expire ? "u" : "w",
               expire ? "提前解除" : "加入禁言名單");

        switch (vans("請選擇欲進行之操作, 其它鍵結束: ")) {
            case 'w':
                if (expire) {
                    finished = 1;
                    break;
                }
                move(modlog_form(board, "禁言使用者"), 0);
                ui_mute_user_for_board(uid, board);
                // The list is redrawn after the full screen form.
                finished = 1;
                break;

            case 'u':
                if (!expire) {
                    finished = 1;
                    break;
                }
                move(modlog_form(board, "提前解除禁言"), 0);
                ui_unmute_user_for_board(uid, board);
                finished = 1;
                break;

            default:
                finished = 1;
                break;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// Modlog (管理紀錄)
//
// Every moderation action on user generated content and every change of
// board settings is appended to two files in the board directory:
//  - FN_MODLOG: public (shown in board config), moderators may edit
//    or delete anything in it.
//  - FN_MODLOG_AUDIT: audit record; the BBS only appends to it
//    and never offers moderators a way to change it.

static void
modlog_strip(char *s)
{
    strip_control_sequence(s, s);
    for (; *s; s++) {
        if (*s == '\n' || *s == '\r' || *s == '\t')
            *s = ' ';
    }
}

// The public modlog never shows accounts, only the role of who did it.
// Police doing police work are shown as MODLOG_POLICE_NAME, the same as
// post_violatelaw2() shows in public, also in the audit record: who it was
// is recorded in BN_POLICELOG.
#define MODLOG_POLICE_NAME "站務警察"

// Whether a police action is done as police, not as a moderator of the
// board or as a SYSOP, so it should be recorded anonymously.
static bool
modlog_as_police(const char *bname)
{
    return HasUserPerm(PERM_POLICE) && !HasUserPerm(PERM_SYSOP) &&
           !is_board_moderator(getbnum(bname));
}

// The role shown in the public modlog.
static const char *
modlog_role(const char *bname, bool anonymous)
{
    if (anonymous)
        return MODLOG_POLICE_NAME;
    if (is_board_moderator(getbnum(bname)))
        return "板主";
    if (HasUserPerm(PERM_SYSOP))
        return "站長";
    if (HasUserPerm(PERM_BOARDS))
        return "看板總管";
    if (currmode & MODE_GROUP_LEAD)
        return "群組長";
    if (currmode & MODE_GROUP_OP)
        return "小組長";
    if (HasUserPerm(PERM_POLICE))
        return MODLOG_POLICE_NAME;
    return "管理人員";
}

#define MODLOG_RECORD_LEN (ANSILINELEN * 2)

static void modlog_formatv(const char *bname, bool anonymous, bool police,
                           const char *reason, char *msg, char *amsg,
                           const char *fmt, va_list ap) GCC_CHECK_FORMAT(7, 0);

// Formats the records for the public modlog (msg) and the audit record
// (amsg), each MODLOG_RECORD_LEN. For police actions (also recorded in
// BN_POLICELOG) the reason is only in the audit record.
static void
modlog_formatv(const char *bname, bool anonymous, bool police,
               const char *reason, char *msg, char *amsg, const char *fmt,
               va_list ap)
{
    char action[ANSILINELEN], why[ANSILINELEN] = "";

    vsnprintf(action, sizeof(action), fmt, ap);
    modlog_strip(action);
    if (reason && *reason) {
        strlcpy(why, reason, sizeof(why));
        modlog_strip(why);
    }

    syncnow();
    const char *role = modlog_role(bname, anonymous);
    bool pub_why = why[0] && !police;
    snprintf(msg, MODLOG_RECORD_LEN, "%s %s %s\n%s%s%s",
             Cdatelite(&now), role, action, pub_why ? "    理由: " : "",
             pub_why ? why : "", pub_why ? "\n" : "");

    // The audit record keeps the account and where it was done from, but
    // not for anonymous police.
    if (anonymous)
        snprintf(amsg, MODLOG_RECORD_LEN, "%s %s %s\n%s%s%s",
                 Cdatelite(&now), role, action,
                 why[0] ? "    理由: " : "", why, why[0] ? "\n" : "");
    else
        snprintf(amsg, MODLOG_RECORD_LEN, "%s %s (%s) %s\n%s%s%s",
                 Cdatelite(&now), cuser.userid, fromhost, action,
                 why[0] ? "    理由: " : "", why, why[0] ? "\n" : "");
}

static void modlogv(const char *bname, bool anonymous, bool police,
                    const char *reason, const char *fmt, va_list ap)
    GCC_CHECK_FORMAT(5, 0);

static void
modlogv(const char *bname, bool anonymous, bool police, const char *reason,
        const char *fmt, va_list ap)
{
    char msg[MODLOG_RECORD_LEN], amsg[MODLOG_RECORD_LEN];

    if (!bname || !*bname)
        return;

    modlog_formatv(bname, anonymous, police, reason, msg, amsg, fmt, ap);
    file_append(TEMP_BFILE(bname, FN_MODLOG), msg);
    file_append(TEMP_BFILE(bname, FN_MODLOG_AUDIT), amsg);
}

// Prints the lines of a record from line y, one screen line each.
static int
modlog_print_record(int y, const char *title, const char *record)
{
    char buf[MODLOG_RECORD_LEN];
    char *line, *save = NULL;

    mvouts(y++, 0, title);
    clrtoeol();
    strlcpy(buf, record, sizeof(buf));
    for (line = strtok_r(buf, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        line[stream_col_offset(t_columns - 1, line, NULL)] = '\0';
        move(y++, 0); clrtoeol();
        outs(line);
    }
    return y;
}

// Shows from line y what modlog() would record, for confirming before
// doing it. Returns the line below it.
int
modlog_preview(int y, const char *bname, const char *reason,
               const char *fmt, ...)
{
    char msg[MODLOG_RECORD_LEN], amsg[MODLOG_RECORD_LEN];
    va_list ap;

    va_start(ap, fmt);
    modlog_formatv(bname, false, false, reason, msg, amsg, fmt, ap);
    va_end(ap);
    y = modlog_print_record(y, ANSI_COLOR(1) "預計記入看板管理紀錄(自動公告)："
                            ANSI_RESET, msg);
    return modlog_print_record(y, ANSI_COLOR(1) "預計記入看板管理稽核紀錄："
                               ANSI_RESET, amsg);
}

// modlog_preview() for what modlog_police() would record.
int
modlog_police_preview(int y, const char *bname, const char *reason,
                      const char *fmt, ...)
{
    char msg[MODLOG_RECORD_LEN], amsg[MODLOG_RECORD_LEN];
    va_list ap;

    va_start(ap, fmt);
    modlog_formatv(bname, modlog_as_police(bname), true, reason, msg, amsg,
                   fmt, ap);
    va_end(ap);
    y = modlog_print_record(y, ANSI_COLOR(1) "預計記入看板管理紀錄(自動公告)："
                            ANSI_RESET, msg);
    return modlog_print_record(y, ANSI_COLOR(1) "預計記入看板管理稽核紀錄："
                               ANSI_RESET, amsg);
}

// Starts a full screen form for a moderation action on the board, so that
// what it is on, the reason, what will be recorded and the confirmation are
// all clear. Returns the line to show what it is on.
int
modlog_form(const char *bname, const char *title)
{
    clear();
    vs_hdr2(title, bname);
    return 2;
}

// Appends a detail line of the record just made (e.g. which posts) to the
// audit record only.
void
modlog_audit_detail(const char *bname, const char *fmt, ...)
{
    char line[ANSILINELEN];
    va_list ap;

    if (!bname || !*bname)
        return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    modlog_strip(line);
    file_appendf(TEMP_BFILE(bname, FN_MODLOG_AUDIT), "    %s\n", line);
}

void
modlog(const char *bname, const char *reason, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    modlogv(bname, false, false, reason, fmt, ap);
    va_end(ap);
}

// For actions also recorded in BN_POLICELOG: the reason is only in the
// audit record, and police are recorded as MODLOG_POLICE_NAME, without their
// account or where it was done from.
void
modlog_police(const char *bname, const char *reason, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    modlogv(bname, modlog_as_police(bname), true, reason, fmt, ap);
    va_end(ap);
}

#define MODLOG_ASK_LINES (7)

// Asks at line y for the reason, with how it will be recorded below it.
// With choices, they are listed above the prompt to pick one by number,
// or the one after them to type it.
// Returns the line where how it will be recorded starts, for the preview
// to replace it, or -1 if no reason was given.
static int
modlog_ask_reason_as(const char *bname, int y, char *reason, size_t sz,
                     bool anonymous, bool police,
                     const char * const *choices, int nchoices)
{
    const char *role = modlog_role(bname, anonymous);
    int lines = MODLOG_ASK_LINES + (choices ? 1 : 0);
    int yp;

    if (sz > MODLOG_REASON_LEN)
        sz = MODLOG_REASON_LEN;
    // If it doesn't fit, ask at the bottom over what is on the screen
    // (e.g. lists): darken that, with a bar between.
    if (y > b_lines - lines) {
        y = b_lines - lines;
        grayout(0, y - 2, GRAYOUT_DARK);
        move(y - 1, 0);
        outs(ANSI_REVERSE);
        vbar(" 輸入理由");
    }
    // A blank line, the choices, the prompt, a blank line and what will be
    // recorded.
    move(y, 0); clrtoln(y + lines);
    yp = y + 1;
    if (choices) {
        move(yp++, 0);
        for (int i = 0; i < nchoices; i++)
            prints("%d.%s ", i + 1, choices[i]);
        prints("%d.其他 (q)取消", nchoices + 1);
    }
    mvouts(yp + 2, 0, "此理由會與執行者的資訊一起被記入下列紀錄：");
    move(yp + 3, 0);
    prints(police ?
           "- 看板管理紀錄(自動公告，可編輯)不寫理由，您的身份會顯示為 [%s]" :
           "- 在看板管理紀錄(自動公告，可編輯)您的身份會顯示為 [%s]", role);
    move(yp + 4, 0);
    prints("- 在看板管理稽核紀錄(管理者才可看，不可編輯)顯示為 [%s]",
           anonymous ? role : cuser.userid);

    reason[0] = '\0';
    if (choices) {
        char ans[3];
        while (1) {
            getdata(yp, 0, "請選擇理由: ", ans, sizeof(ans), LCECHO);
            if (!ans[0] || ans[0] == 'q')
                break;
            int i = ans[0] - '1';
            if (i >= 0 && i < nchoices) {
                strlcpy(reason, choices[i], sz);
                move(yp, 0); clrtoeol();
                prints("理由: %s", reason);
                break;
            }
            if (i == nchoices) {
                getdata(yp, 0, "請輸入理由(空白取消): ", reason, sz, DOECHO);
                break;
            }
            bell();
        }
    } else {
        getdata(yp, 0, "請輸入理由(空白取消): ", reason, sz, DOECHO);
    }
    trim(reason);
    move(yp + 5, 0); clrtobot();
    if (!*reason) {
        vmsg("未輸入理由，取消操作。");
        return -1;
    }
    // What will be recorded is shown there by modlog_preview(), saving
    // the lines telling how.
    return yp + 2;
}

// Asks for the reason of a moderation action on the board, at line y or
// at the bottom if how it will be recorded doesn't fit below it.
// Returns the line for modlog_preview(), or -1 if no reason was given.
int
modlog_ask_reason(const char *bname, int y, char *reason, size_t sz)
{
    return modlog_ask_reason_as(bname, y, reason, sz, false, false, NULL, 0);
}

// modlog_ask_reason() picking one of the choices (or typing one).
int
modlog_ask_reason_choice(const char *bname, int y, char *reason, size_t sz,
                         const char * const *choices, int nchoices)
{
    return modlog_ask_reason_as(bname, y, reason, sz, false, false, choices,
                                nchoices);
}

// modlog_ask_reason() for actions recorded by modlog_police().
int
modlog_police_ask_reason(const char *bname, int y, char *reason, size_t sz)
{
    return modlog_ask_reason_as(bname, y, reason, sz,
                                modlog_as_police(bname), true, NULL, 0);
}

int
modlog_view(const char *bname, int secure)
{
    if (more(TEMP_BFILE(bname, secure ? FN_MODLOG_AUDIT :
                                        FN_MODLOG), YEA) == -1)
        vmsg("目前尚無管理紀錄。");
    return 0;
}

// Moderators may freely change the public modlog, but the audit
// record keeps that they did.
int
modlog_edit(const char *bname)
{
    struct stat st0, st1;
    AUTO_BFILE(fpath, bname, FN_MODLOG);
    int had0 = (stat(fpath, &st0) == 0);

    if (veditfile(fpath) < 0)
        return 0;

    int had1 = (stat(fpath, &st1) == 0);
    if (had0 == had1 && (!had1 ||
        (st0.st_ino == st1.st_ino && st0.st_size == st1.st_size &&
         st0.st_mtime == st1.st_mtime)))
        return 0;

    syncnow();
    file_appendf(TEMP_BFILE(bname, FN_MODLOG_AUDIT),
                 "%s %s (%s) 編輯管理紀錄\n",
                 Cdatelite(&now), cuser.userid, fromhost);
    return 1;
}
