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
// The actions of muting and unmuting in the modlog, which the board mute
// history (FN_MUTED_HISTORY) is made of.
#define MODLOG_MUTE                 "暫停發言"
#define MODLOG_UNMUTE               "開放發言"

static void muted_history_view(const char *bname);

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
        move(y+2, 0); clrtobot();
        snprintf(datebuf, sizeof(datebuf), "%d天", val);
        prints("期限將設定為 %s之後: %s\n", datebuf, Cdatelite(&expire));
        days = val;
    }

    assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
    mvouts(y+3, 0, ANSI_COLOR(1;31)
           "請注意輸入的理由會由信件通知使用者也會在看板管理紀錄自動公告。" ANSI_RESET);
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
    bof_modlog_t log;
    bof_modlog_init(&log, MODLOG_MUTE ": %s %d天", uid, days);
    int yc = modlog_preview(y + 5, board, reason, &log);
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
        modlog(board, reason, &log);
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
    bof_modlog_t log;
    bof_modlog_init(&log, MODLOG_UNMUTE ": %s 原剩%d天", uid,
                time4_days_remaining(expire, now));
    int yc = modlog_preview(yr, board, reason, &log);
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
    modlog(board, reason, &log);
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
        "           另外也可用(L)列出設定歷史\n"
        "         - 沒有[永久禁言]的設定，若有需要請設個 50年之類的大數字\n"
        "         - 新增/解除會寄信通知使用者，另外請注意" ANSI_COLOR(1;33)
                   "帳號被砍後禁言會自動解除\n" ANSI_RESET
        "         - 禁言自動解除不會出現在記錄裡，只有手動提前解除的才會\n"
        ANSI_COLOR(1)
        "         - 想查看某使用者為何被禁言可用(S)。\n"
        ANSI_RESET
        );

        getdata(1, 0, "(A)增加 (D)提前清除 (S)查詢帳號 "
                      "(L)設定歷史 "
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
                muted_history_view(board);
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

// The public modlog never shows accounts, only the role of who did it.
// Police doing police work are shown as MODLOG_POLICE_NAME, the same as
// violatelaw_post() shows in public, also in the audit record: who it was
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

// The role shown in the modlog: the one closest to the board, as who
// would be asked about it first.
static const char *
modlog_role(const char *bname, bool anonymous)
{
    int bid = getbnum(bname);

    if (anonymous)
        return MODLOG_POLICE_NAME;
    if (is_board_moderator(bid))
        return "板主";
    // Sysops and board admins get the group modes in any group, so they are
    // shown as themselves. Group leads have both modes.
    if (!HasUserPerm(PERM_SYSOP | PERM_BOARDS)) {
        if (currmode & MODE_GROUP_LEAD)
            return "群組長";
        if (currmode & MODE_GROUP_OP)
            return "小組長";
    }
    if (HasUserPerm(PERM_BOARDS))
        return "看板總管";
    if (HasUserPerm(PERM_SYSOP))
        return "站長";
    if (HasUserPerm(PERM_POLICE))
        return MODLOG_POLICE_NAME;
    return "管理人員";
}

// The actor for bof: the current user, shown in the modlog as its role, and
// hidden in the audit record too when anonymous (police).
static bof_actor_t
modlog_actor(const char *bname, bool anonymous)
{
    bof_actor_t actor = {
        .name = cuser.userid, .from = fromhost,
        .bbs_role = modlog_role(bname, anonymous),
        .bbs_hidden_in_audit = anonymous,
    };
    return actor;
}

// The records for the public modlog (*msg) and the audit record (*amsg), to
// free(); for police actions (also recorded in BN_POLICELOG) the reason is
// only in the audit record. Returns -1 if out of memory.
static int
modlog_format(const char *bname, bool anonymous, bool police,
              const char *reason, const bof_modlog_t *m, char **msg,
              char **amsg)
{
    bof_actor_t actor = modlog_actor(bname, anonymous);
    return bof_modlog_format(&actor, reason, police, m, msg, amsg);
}

static void
modlog_write(const char *bname, bool anonymous, bool police,
             const char *reason, const bof_modlog_t *m)
{
    bof_actor_t actor = modlog_actor(bname, anonymous);
    bof_modlog_write(bname, &actor, reason, police, m);
}

// Whether the n bytes at line are one of the lines in lines.
static bool
modlog_has_line(const char *lines, const char *line, size_t n)
{
    while (lines && *lines) {
        const char *e = strchrnul(lines, '\n');
        if ((size_t)(e - lines) == n && memcmp(lines, line, n) == 0)
            return true;
        lines = *e ? e + 1 : e;
    }
    return false;
}

// Prints the lines of a record from line y, one screen line each. Lines
// also in skip (if not NULL) are left out. Returns the line below them.
static int
modlog_print_record(int y, const char *title, const char *record,
                    const char *skip)
{
    char line[ANSILINELEN];

    mvouts(y++, 0, title);
    clrtoeol();
    for (const char *p = record; *p;) {
        const char *e = strchrnul(p, '\n');
        size_t n = e - p;
        if (!modlog_has_line(skip, p, n)) {
            snprintf(line, sizeof(line), "%.*s", (int)n, p);
            line[stream_col_offset(t_columns - 1, line, NULL)] = '\0';
            move(y++, 0); clrtoeol();
            outs(line);
        }
        p = *e ? e + 1 : e;
    }
    return y;
}

static int
modlog_preview_as(int y, const char *bname, bool anonymous, bool police,
                  const char *reason, const bof_modlog_t *m)
{
    char *msg, *amsg;

    if (modlog_format(bname, anonymous, police, reason, m, &msg, &amsg) < 0)
        return y;
    y = modlog_print_record(y, ANSI_COLOR(1) "預計記入看板管理紀錄(自動公告)："
                            ANSI_RESET, msg, NULL);
    // Only what is different, to save lines.
    y = modlog_print_record(y, ANSI_COLOR(1) "預計記入看板管理稽核紀錄"
                            "(相同內容省略)：" ANSI_RESET, amsg, msg);
    free(msg);
    free(amsg);
    return y;
}

// Shows from line y what modlog() would record, for confirming before
// doing it. Returns the line below it.
int
modlog_preview(int y, const char *bname, const char *reason,
               const bof_modlog_t *m)
{
    return modlog_preview_as(y, bname, false, false, reason, m);
}

// modlog_preview() for what modlog_police() would record.
int
modlog_police_preview(int y, const char *bname, const char *reason,
                      const bof_modlog_t *m)
{
    return modlog_preview_as(y, bname, modlog_as_police(bname), true, reason,
                             m);
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

void
modlog(const char *bname, const char *reason, const bof_modlog_t *m)
{
    modlog_write(bname, false, false, reason, m);
}

// For actions also recorded in BN_POLICELOG: the reason is only in the
// audit record, and police are recorded as MODLOG_POLICE_NAME, without their
// account or where it was done from.
void
modlog_police(const char *bname, const char *reason, const bof_modlog_t *m)
{
    modlog_write(bname, modlog_as_police(bname), true, reason, m);
}

#define MODLOG_ASK_LINES (7)

// If the lines from line y don't fit above line bottom, moves them up to
// end there, over what is on the screen (e.g. lists): darkens that, with a
// bar between. Returns the line to start them.
static int
modlog_ask_fit(int y, int lines, int bottom)
{
    if (y > bottom - lines) {
        y = bottom - lines;
        grayout(0, y - 2, GRAYOUT_DARK);
        move(y - 1, 0);
        outs(ANSI_REVERSE);
        vbar(" 輸入理由");
    }
    return y;
}

// Tells, in the 3 lines from line y, where the reason will be recorded.
static void
modlog_print_how(int y, const char *bname, bool anonymous, bool police)
{
    const char *role = modlog_role(bname, anonymous);

    mvouts(y, 0, "此理由會與執行者的資訊一起被記入下列紀錄：");
    move(y + 1, 0);
    prints(police ?
           "- 看板管理紀錄(自動公告，可編輯)不寫理由，您的身份會顯示為[%s]" :
           "- 在看板管理紀錄(自動公告，可編輯)您的身份會顯示為[%s]", role);
    move(y + 2, 0);
    if (anonymous)
        prints("- 看板管理稽核紀錄(管理者才可看，不可編輯)一樣顯示為[%s]",
               role);
    else
        prints("- 看板管理稽核紀錄(管理者才可看，不可編輯)另記錄執行者[%s]"
               "與來源IP", cuser.userid);
}

// Asks at line y for the reason, with how it will be recorded below it.
// Returns the line where how it will be recorded starts, for the preview
// to replace it, or -1 if no reason was given.
static int
modlog_ask_reason_as(const char *bname, int y, char *reason, size_t sz,
                     bool anonymous, bool police)
{
    int yp;

    if (sz > MODLOG_REASON_LEN)
        sz = MODLOG_REASON_LEN;
    y = modlog_ask_fit(y, MODLOG_ASK_LINES, b_lines);
    // A blank line, the prompt, a blank line and what will be recorded.
    move(y, 0); clrtoln(y + MODLOG_ASK_LINES);
    yp = y + 1;
    modlog_print_how(yp + 2, bname, anonymous, police);

    reason[0] = '\0';
    getdata(yp, 0, "請輸入理由(空白取消): ", reason, sz, DOECHO);
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
    return modlog_ask_reason_as(bname, y, reason, sz, false, false);
}

// Asks at line y for a reason that is not put in the modlog, e.g. for posts
// in the digest, where it is still used elsewhere. Returns the line below
// it, or -1 if no reason was given.
int
modlog_ask_reason_unlogged(int y, char *reason, size_t sz)
{
    move(y, 0); clrtobot();
    getdata(y + 1, 0, "請輸入理由(空白取消): ", reason, sz, DOECHO);
    trim(reason);
    if (!*reason) {
        vmsg("未輸入理由，取消操作。");
        return -1;
    }
    return y + 3;
}

// The usual reasons a board keeps in a file, one per line, for
// modlog_ask_reason_list() to pick by number. Lines past these are kept
// but not shown.
#define MODLOG_LIST_MAX (9)
// A blank line, the title, the reasons, the line to type another, a blank
// line and what will be recorded.
#define MODLOG_LIST_LINES (2 + MODLOG_LIST_MAX + 2 + 3)

// Makes s one line without colors, as bof_modlog_*() do with values.
static void
modlog_strip(char *s)
{
    strip_control_sequence(s, s);
    for (; *s; s++) {
        if (*s == '\n' || *s == '\r' || *s == '\t')
            *s = ' ';
    }
}

// Reads the first MODLOG_LIST_MAX lines of path into reasons. Returns
// false if there is no such file.
static bool
modlog_read_reasons(const char *path,
                    char reasons[MODLOG_LIST_MAX][MODLOG_REASON_LEN])
{
    char line[ANSILINELEN];
    FILE *fp = fopen(path, "r");
    int i = 0;

    if (!fp)
        return false;
    while (i < MODLOG_LIST_MAX && fgets(line, sizeof(line), fp)) {
        // The rest of a line too long for the buffer.
        if (!strchr(line, '\n')) {
            int c;
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;
        }
        storage_to_mb(line, line, sizeof(line));
        modlog_strip(line);
        trim(line);
        strlcpy(reasons[i], line, MODLOG_REASON_LEN);
        mbs_safe_trim(reasons[i]);
        i++;
    }
    fclose(fp);
    return true;
}

// Writes reasons to path as its first MODLOG_LIST_MAX lines, keeping the
// lines after them. Returns 0 on success.
static int
modlog_write_reasons(const char *path,
                     char reasons[MODLOG_LIST_MAX][MODLOG_REASON_LEN])
{
    char tmp[PATHLEN], line[ANSILINELEN];
    FILE *in, *out;
    int n, skipped = 0, ret = 0;

    snprintf(tmp, sizeof(tmp), "%s.%d", path, (int)getpid());
    if (!(out = fopen(tmp, "w")))
        return -1;
    // Skip the lines replaced, to copy those after them.
    if ((in = fopen(path, "r")) != NULL) {
        while (skipped < MODLOG_LIST_MAX && fgets(line, sizeof(line), in)) {
            if (strchr(line, '\n'))
                skipped++;
        }
    }
    // All of them if lines follow, to keep their places; otherwise up to
    // the last one given.
    int c = in ? fgetc(in) : EOF;
    if (c != EOF) {
        ungetc(c, in);
        n = MODLOG_LIST_MAX;
    } else {
        for (n = MODLOG_LIST_MAX; n > 0 && !*reasons[n - 1]; n--)
            ;
    }
    for (int i = 0; i < n; i++) {
        mb_to_storage(reasons[i], line, sizeof(line));
        fprintf(out, "%s\n", line);
    }
    while (in && fgets(line, sizeof(line), in))
        fputs(line, out);
    if (in)
        fclose(in);
    if (fclose(out) != 0 || Rename(tmp, path) != 0)
        ret = -1;
    if (ret)
        unlink(tmp);
    return ret;
}

// What the keys of modlog_ask_reason_list() work on.
typedef struct {
    char (*reasons)[MODLOG_REASON_LEN];
    const char *path;   // where the reasons are saved
    char *reason;       // the reason picked, empty until then
    size_t sz;
    int cur;            // the reason highlighted
    int yp;             // the line of the first reason
    int yo;             // the line to type another
} reason_list_t;

static int
reason_list_cmd_num(cmd_ctx_t *ctx)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    rl->cur = ctx->key - '1';
    return 0;
}

static int
reason_list_cmd_up(cmd_ctx_t *ctx)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    rl->cur = (rl->cur + MODLOG_LIST_MAX - 1) % MODLOG_LIST_MAX;
    return 0;
}

static int
reason_list_cmd_down(cmd_ctx_t *ctx)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    rl->cur = (rl->cur + 1) % MODLOG_LIST_MAX;
    return 0;
}

static int
reason_list_cmd_edit(cmd_ctx_t *ctx)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    char buf[MODLOG_REASON_LEN], prompt[8];
    char *r = rl->reasons[rl->cur];

    snprintf(prompt, sizeof(prompt), "> %d. ", rl->cur + 1);
    move(rl->yp + rl->cur, 0); clrtoeol();
    getdata_str(rl->yp + rl->cur, 0, prompt, buf, rl->sz, DOECHO, r);
    trim(buf);
    if (strcmp(buf, r) != 0) {
        strlcpy(r, buf, MODLOG_REASON_LEN);
        if (modlog_write_reasons(rl->path, rl->reasons) != 0)
            vmsg("無法儲存理由，請稍候再試。");
    }
    return 0;
}

// Picks the reason highlighted, or edits it if there is none, as e.
static int
reason_list_cmd_pick(cmd_ctx_t *ctx)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    if (!*rl->reasons[rl->cur])
        return reason_list_cmd_edit(ctx);
    strlcpy(rl->reason, rl->reasons[rl->cur], rl->sz);
    return 0;
}

static int
reason_list_cmd_other(cmd_ctx_t *ctx)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    getdata(rl->yo, 0, "請輸入理由(空白返回): ", rl->reason, rl->sz, DOECHO);
    trim(rl->reason);
    move(rl->yo, 0); clrtoeol();
    return 0;
}

static int
reason_list_cmd_quit(cmd_ctx_t *ctx)
{
    ctx->quit = true;
    return 0;
}

static int
reason_list_cmd_help(cmd_ctx_t *ctx)
{
    screen_backup_t old_screen;

    scr_dump(&old_screen);
    int sel_key = cmd_show_help_layers(ctx->caption, ctx->active_layers);
    scr_restore(&old_screen);
    if (sel_key > 0 && sel_key != ctx->key) {
        ctx->key = sel_key;
        ctx->redispatch = true;
    }
    return 0;
}

// Clicking a reason highlights it (and picks it, as Enter).
static void
reason_list_on_select(cmd_ctx_t *ctx, int new_curr)
{
    reason_list_t *rl = (reason_list_t *)ctx->priv;
    if (new_curr >= 0 && new_curr < MODLOG_LIST_MAX)
        rl->cur = new_curr;
}

static const cmd_t reason_list_cmds[] = {
    { KEY_ENTER, "確定", "使用反白的理由(空的則編輯)", reason_list_cmd_pick, 0,
      CMD_PRIO_MAX },
    { KEY_LF, NULL, NULL, reason_list_cmd_pick, 0, CMD_PRIO_NONE },
    { 'e', "編輯並儲存", "編輯反白的理由，存為看板的常用理由",
      reason_list_cmd_edit, 0, CMD_PRIO_HIGH },
    { 'E', NULL, NULL, reason_list_cmd_edit, 0, CMD_PRIO_NONE },
    { 'o', "其他(不儲存)", "輸入只用這一次的理由", reason_list_cmd_other, 0,
      CMD_PRIO_HIGH },
    { 'O', NULL, NULL, reason_list_cmd_other, 0, CMD_PRIO_NONE },
    { '1', "選擇", "反白第 1-9 個理由", reason_list_cmd_num, 0,
      CMD_PRIO_NAV },
    { '2', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '3', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '4', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '5', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '6', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '7', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '8', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { '9', NULL, NULL, reason_list_cmd_num, 0, CMD_PRIO_NONE },
    { KEY_UP, NULL, "反白上一個理由", reason_list_cmd_up, 0, CMD_PRIO_NONE },
    { 'k', NULL, NULL, reason_list_cmd_up, 0, CMD_PRIO_NONE },
    { KEY_DOWN, NULL, "反白下一個理由", reason_list_cmd_down, 0,
      CMD_PRIO_NONE },
    { 'j', NULL, NULL, reason_list_cmd_down, 0, CMD_PRIO_NONE },
    { 'q', "取消", "不輸入理由，取消操作", reason_list_cmd_quit, 0,
      CMD_PRIO_QUIT },
    { 'Q', NULL, NULL, reason_list_cmd_quit, 0, CMD_PRIO_NONE },
    { KEY_ESC, NULL, NULL, reason_list_cmd_quit, 0, CMD_PRIO_NONE },
    { 'h', "說明", "顯示按鍵說明", reason_list_cmd_help, 0, CMD_PRIO_USAGE },
    { 'H', NULL, NULL, reason_list_cmd_help, 0, CMD_PRIO_NONE },
    { '?', NULL, NULL, reason_list_cmd_help, 0, CMD_PRIO_NONE },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

// Asks for the reason of a moderation action on the board like
// modlog_ask_reason(), picking one of the usual reasons the board keeps
// in its file fn (or defaults if there is none), listed below title.
// Moderators may edit them, which is saved to fn, or type another for
// this time only. The keys are in the action bar, with h for help.
// Returns the line for modlog_preview(), or -1 if no reason was given.
int
modlog_ask_reason_list(const char *bname, int y, char *reason, size_t sz,
                       const char *title, const char *fn,
                       const char * const *defaults, int ndefaults)
{
    static const cmd_layer_t layers[] = {
        { reason_list_cmds, NULL },
        { NULL, NULL }
    };
    static const char caption[] = "選擇理由";
    char reasons[MODLOG_LIST_MAX][MODLOG_REASON_LEN] = {{0}};
    char path[PATHLEN];
    reason_list_t rl = { reasons, path, reason, sz, 0, 0, 0 };

    if (sz > MODLOG_REASON_LEN)
        rl.sz = sz = MODLOG_REASON_LEN;
    setbfile(path, bname, fn);
    if (!modlog_read_reasons(path, reasons)) {
        for (int i = 0; i < ndefaults && i < MODLOG_LIST_MAX; i++)
            strlcpy(reasons[i], defaults[i], sizeof(reasons[i]));
    }

    // Above the action bar.
    y = modlog_ask_fit(y, MODLOG_LIST_LINES, b_lines - 1);
    move(y, 0); clrtobot();
    mvouts(y + 1, 0, title);
    rl.yp = y + 2;
    rl.yo = rl.yp + MODLOG_LIST_MAX;
    modlog_print_how(rl.yo + 2, bname, false, false);

    reason[0] = '\0';
    while (!*reason) {
        for (int i = 0; i < MODLOG_LIST_MAX; i++) {
            // Also marked with '>', for monochrome terminals.
            move(rl.yp + i, 0); clrtoeol();
            prints(i == rl.cur ? ANSI_REVERSE "> %d. %s " ANSI_RESET :
                   "  %d. %s ", i + 1, *reasons[i] ? reasons[i] : "(空)");
        }
        vs_cmd_bar(VS_FOOTER, caption, layers);
        move(rl.yp + rl.cur, 0);

        cmd_ctx_t cctx = {
            .key = vkey(),
            .curr = rl.cur,
            .total = MODLOG_LIST_MAX,
            .header_lines = rl.yp,
            .visible_rows = MODLOG_LIST_MAX,
            .on_select = reason_list_on_select,
            .priv = &rl,
        };
        cmd_dispatch_layers(layers, &cctx, caption);
        if (cctx.quit)
            break;
    }

    // Only the reason picked is left, with how it will be recorded.
    move(y, 0); clrtobot();
    if (!*reason) {
        vmsg("未輸入理由，取消操作。");
        return -1;
    }
    move(y + 1, 0);
    prints("理由: %s", reason);
    modlog_print_how(y + 3, bname, false, false);
    return y + 3;
}

// modlog_ask_reason() for actions recorded by modlog_police().
int
modlog_police_ask_reason(const char *bname, int y, char *reason, size_t sz)
{
    return modlog_ask_reason_as(bname, y, reason, sz,
                                modlog_as_police(bname), true);
}

int
modlog_view(const char *bname, int secure)
{
    char path[PATHLEN];

    bof_get_modlog_path(bname, secure, path, sizeof(path));
    if (more(path, YEA | MORE_FROM_END) == -1)
        vmsg("目前尚無管理紀錄。");
    return 0;
}

// The board mute history (FN_MUTED_HISTORY) is made from the audit record
// (FN_MODLOG_AUDIT): only the records of muting and unmuting, colored like
// the mute history used to be. It is made again when the audit record is
// newer. Records from before the modlog (if not converted by
// upgrade/convert_modlog_audit_mutes.py) are copied as they are.

// The action of the modlog record started by line ("MM/DD/YYYY HH:MM role
// action..."), or NULL if line does not start one.
static const char *
modlog_record_action(const char *line)
{
    static const char shape[] = "99/99/9999 99:99 ";
    const char *p;

    for (size_t i = 0; i < sizeof(shape) - 1; i++) {
        if (shape[i] == '9' ? !isdigit((unsigned char)line[i]) :
            line[i] != shape[i])
            return NULL;
    }
    p = strchr(line + sizeof(shape) - 1, ' ');
    return p ? p + 1 : NULL;
}

// Whether line starts a record of muting or unmuting from the mute history
// before the modlog, e.g. "date ID 暫停 UID 發言，期限為 7 天" (colored).
static bool
is_legacy_mute_record(const char *line)
{
    char s[ANSILINELEN];

    strlcpy(s, line, sizeof(s));
    strip_control_sequence(s, s);
    return strstr(s, " 發言，期限為 ") || strstr(s, " 的禁言限制 (距原期限");
}

// Writes the line of a record to out, with the account after the action
// (or after "執行: ") colored by color.
static void
muted_history_put(FILE *out, const char *line, const char *after,
                  const char *color)
{
    const char *p = strstr(line, after);
    size_t n;

    if (!p) {
        fputs(line, out);
        return;
    }
    p += strlen(after);
    n = strcspn(p, " \n");
    fprintf(out, "%.*s%s%.*s" ANSI_RESET "%s", (int)(p - line), line, color,
            (int)n, p, p + n);
}

// Makes path (of PATHLEN), the board mute history, from the audit record
// unless it is newer already. Returns 0 if it has any record.
static int
muted_history_update(const char *bname, char *path)
{
    char audit[PATHLEN], tmp[PATHLEN], line[ANSILINELEN];
    struct stat sa, sh;
    FILE *in, *out;
    bool keep = false, cont = false;
    int n = 0;

    bof_get_modlog_path(bname, 1, audit, sizeof(audit));
    setbfile(path, bname, FN_MUTED_HISTORY);
    if (stat(audit, &sa) != 0)
        return -1;
    if (stat(path, &sh) == 0 &&
        (sh.st_mtim.tv_sec > sa.st_mtim.tv_sec ||
         (sh.st_mtim.tv_sec == sa.st_mtim.tv_sec &&
          sh.st_mtim.tv_nsec > sa.st_mtim.tv_nsec)))
        return sh.st_size > 0 ? 0 : -1;

    if (!(in = fopen(audit, "r")))
        return -1;
    snprintf(tmp, sizeof(tmp), "%s.%d", path, (int)getpid());
    if (!(out = fopen(tmp, "w"))) {
        fclose(in);
        return -1;
    }
    while (fgets(line, sizeof(line), in)) {
        const char *action;
        // The rest of a line too long for the buffer goes with it.
        bool rest = cont;
        cont = !strchr(line, '\n');
        if (rest || line[0] == ' ') {
            if (!keep)
                continue;
            if (!rest && str_starts_with(line, MODLOG_INDENT STR_MODLOG_BY ": "))
                muted_history_put(out, line, STR_MODLOG_BY ": ", ANSI_COLOR(1;33));
            else
                fputs(line, out);
        } else if ((action = modlog_record_action(line))) {
            bool mute = str_starts_with(action, MODLOG_MUTE ": ");
            keep = mute || str_starts_with(action, MODLOG_UNMUTE ": ");
            if (!keep)
                continue;
            n++;
            muted_history_put(out, line,
                              mute ? MODLOG_MUTE ": " : MODLOG_UNMUTE ": ",
                              mute ? ANSI_COLOR(1;31) : ANSI_COLOR(1;32));
        } else if ((keep = is_legacy_mute_record(line))) {
            n++;
            fputs(line, out);
        }
    }
    fclose(in);
    if (fclose(out) != 0 || Rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return n > 0 ? 0 : -1;
}

// Shows the board mute history, the newest at the bottom.
static void
muted_history_view(const char *bname)
{
    char path[PATHLEN];

    if (muted_history_update(bname, path) != 0 ||
        more(path, YEA | MORE_FROM_END) == -1)
        vmsg("目前尚無禁言紀錄。");
}

// The largest public modlog that may be edited, as the editor keeps it all
// in memory (the EDIT_SIZE_LIMIT of the editor).
#define MODLOG_EDIT_SIZE_LIMIT (65536 * 1024)

// Whether the file a has the same content as the first n bytes of b.
static bool
file_is_prefix(const char *a, const char *b, off_t n)
{
    char ba[8192], bb[8192];
    FILE *fa = fopen(a, "r"), *fb = fopen(b, "r");
    bool same = fa && (fb || n == 0);
    size_t ra;

    while (same && (ra = fread(ba, 1, sizeof(ba), fa)) > 0) {
        if ((off_t)ra > n || fread(bb, 1, ra, fb) != ra ||
            memcmp(ba, bb, ra) != 0)
            same = false;
        n -= ra;
    }
    if (n != 0)
        same = false;
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);
    return same;
}

// Moderators may freely change the public modlog, but the audit
// record keeps that they did. It may have any number of lines. A copy is
// edited, and the records appended to the modlog meanwhile (which only
// appends) are put after it, so they are not lost.
int
modlog_edit(const char *bname)
{
    struct stat st;
    char tmp[PATHLEN], fpath[PATHLEN];
    off_t size0 = 0;
    bof_get_modlog_path(bname, 0, fpath, sizeof(fpath));

    if (stat(fpath, &st) == 0) {
        if (st.st_size > MODLOG_EDIT_SIZE_LIMIT) {
            vmsg("管理紀錄太大，無法編輯。");
            return 0;
        }
        size0 = st.st_size;
    }
    snprintf(tmp, sizeof(tmp), "%s.edit.%d", fpath, (int)getpid());
    if (size0 > 0 ? Copy(fpath, tmp) < 0 : unlink(tmp) < 0 && errno != ENOENT) {
        vmsg("無法編輯管理紀錄，請稍候再試。");
        return 0;
    }
    if (vedit2(tmp, NA, NULL, EDITFLAG_UNLIMITED) < 0 ||
        file_is_prefix(tmp, fpath, size0)) {
        unlink(tmp);
        return 0;
    }

    // What was recorded while editing, on lines of its own.
    if (stat(fpath, &st) == 0 && st.st_size > size0) {
        if (dashs(tmp) > 0) {
            int fd = open(tmp, O_RDWR);
            char c = '\n';
            if (fd >= 0) {
                if (lseek(fd, -1, SEEK_END) >= 0 && read(fd, &c, 1) == 1 &&
                    c != '\n')
                    write(fd, "\n", 1);
                close(fd);
            }
        }
        if (AppendTail(fpath, tmp, (int)size0) < 0) {
            unlink(tmp);
            vmsg("無法儲存管理紀錄，請稍候再試。");
            return 0;
        }
    }
    if (Rename(tmp, fpath) < 0) {
        unlink(tmp);
        vmsg("無法儲存管理紀錄，請稍候再試。");
        return 0;
    }

    bof_actor_t actor = modlog_actor(bname, false);
    bof_modlog_note_edit(bname, &actor);
    return 1;
}
