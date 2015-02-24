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

static void
bakuman_make_tag_filename(const char *who, const char *object, char object_type,
                          size_t szbuf, char *buf, int create_folder) {
    char prefix[3] = { object_type, '_', 0 };

    if (!who)
        setuserfile(buf, FN_BANNED "/");
    else
        sethomefile(buf, who, FN_BANNED "/");
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
    int ts = 0;
    char buf[STRLEN];

    if(!fp)
        return 0;

    // banned file format:
    //  EXPIRE_TIME
    //  REASON
    buf[0] = 0;
    fgets(buf, sizeof(buf), fp);
    if (pexpire) {
        sscanf(buf, "%u", &ts);
        *pexpire = (time4_t) ts;
    }
    buf[0] = 0;
    fgets(buf, sizeof(buf), fp);
    if (szreason && reason) {
        chomp(buf);
        strlcpy(reason, buf, szreason);
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
is_banned_by(const char *who, const char *object, char object_type,
             size_t szreason, char *reason) {
    char tag_fn[PATHLEN];
    time4_t expire = 0;

    bakuman_make_tag_filename(who, object, object_type,
                              sizeof(tag_fn), tag_fn, 0);
    if (!dashf(tag_fn))
        return 0;

    // check expire
    if (bakuman_get_info(tag_fn, &expire, szreason, reason) && now > expire) {
        unlink(tag_fn);
        return 0;
    }
    return expire;
}

// add 'who (NULL=self)' to the ban list of 'object'
static int
ban_user_as(const char *who, const char *object, char object_type,
            time_t expire, const char *reason) {
    char tag_fn[PATHLEN];

    bakuman_make_tag_filename(who, object, object_type,
                              sizeof(tag_fn), tag_fn, 1);
    return bakuman_set_info(tag_fn, expire, reason);
}

int
get_user_banned_status_by_board(const char *user, const char *board,
                                size_t szreason, char *reason) {
    return is_banned_by(user, board, BAKUMAN_OBJECT_TYPE_BOARD,
                        szreason, reason);
}

time4_t
is_user_banned_by_board(const char *user, const char *board) {
    return is_banned_by(user, board, BAKUMAN_OBJECT_TYPE_BOARD, 0, NULL);
}

time4_t
is_banned_by_board(const char *board) {
    return is_user_banned_by_board(cuser.userid, board);
}

int
ban_user_for_board(const char *user, const char *board,
                   time4_t expire, const char *reason) {
    return ban_user_as(user, board, BAKUMAN_OBJECT_TYPE_BOARD, expire, reason);
}

int
unban_user_for_board(const char *user, const char *board) {
    char tag_fn[PATHLEN];

    bakuman_make_tag_filename(user, board, BAKUMAN_OBJECT_TYPE_BOARD,
                             sizeof(tag_fn), tag_fn, 0);
    return unlink(tag_fn) == 0;
}

static time4_t
ui_print_user_banned_status_for_board(const char *uid, const char *board) {
    char reason[STRLEN];
    time4_t expire = expire = get_user_banned_status_by_board(
        uid, board, sizeof(reason), reason);

    if (expire > now) {
        prints("暫停使用者 %s 發言，解除時間尚有 %d 天: %s\n理由:%s",
               uid, (expire-now)/DAY_SECONDS+1,
               Cdatelite(&expire), reason);
    } else {
        prints("使用者 %s 目前不在禁言名單中。\n",
               uid);
        expire = 0;
    }
    return expire;
}

static int
ui_ban_user_for_board(const char *uid, const char *board) {
    time4_t expire = now;
    int y, x;
    int result;
    char ans[3];
    char history_log[PATHLEN];
    char reason[STRLEN];
    char datebuf[STRLEN];

    setbfile(history_log, board, FN_BANNED_HISTORY);

    getyx(&y, &x);
    if ((expire = is_user_banned_by_board(uid, board))) {
        vmsgf("使用者之前已被禁言，尚有 %d 天；詳情可用(S)或(L)查看",
              (expire - now) / DAY_SECONDS+1);
        return -1;
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
        uint64_t long_now;
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
        long_now = (uint64_t)now + val * (uint64_t)DAY_SECONDS;
        expire = long_now;
        if ((uint64_t)expire != long_now) {
            vmsg("日期過大或無法處理，請重新輸入。");
            return -1;
        }
        move(y+3, 0); clrtobot();
        // sprintf(datebuf, "%s", Cdatelite(&expire));
        sprintf(datebuf, "%d 天", val);
        prints("期限將設定為 %s之後: %s\n",
               datebuf, Cdatelite(&expire));
        if (val > KEEP_DAYS_REGGED) {
            mvprints(y+6, 0, ANSI_COLOR(1;31)
                     "注意: 超過 %d 天的設定有可能因為對方一直"
                     "未上站而導致帳號過期被重新註冊，\n"
                     "      此時同名的新帳號由於不一定是同一人所以"
                     "不會被禁言。\n" ANSI_RESET,
                     KEEP_DAYS_REGGED);
        }
    }

    assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
    mvouts(y+5, 0, ANSI_COLOR(1;31) "請注意此理由會經由信件通知使用者，未來也可能會公開於看板上。");
    // maybe race condition here, but fine.
    getdata(y+4, 0, "請輸入理由(空白可取消新增): ",
            reason, BAKUMAN_REASON_LEN, DOECHO);
    if (!*reason) {
        vmsg("未輸入理由，取消此次設定");
        return -1;
    }

    move(y, 0); clrtobot();
    prints("\n使用者 %s 即將加入禁言名單 (期限: %s)\n"
           "理由: %s\n"
           ANSI_COLOR(1;32) "會寄信通知使用者" ANSI_RESET "\n",
           uid, datebuf, reason);

    // last chance
    getdata(y+5, 0, "確認以上資料全部正確嗎？ [y/N]: ",
            ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y') {
        vmsg("請重新輸入");
        return -1;
    }

    result = ban_user_for_board(uid, board, expire, reason);
    log_filef(history_log, LOG_CREAT,
              ANSI_COLOR(1) "%s %s" ANSI_COLOR(33) "%s" ANSI_RESET
              " 暫停 " ANSI_COLOR(1;31) "%s" ANSI_RESET
              " 發言，期限為 %s\n  理由: %s\n",
              Cdatelite(&now),
              result ? "" :
              ANSI_COLOR(0;31)"[系統錯誤] "ANSI_COLOR(1),
              cuser.userid, uid,  datebuf, reason);
    vmsg(result ? "已將使用者加入禁言名單" : "失敗，請向站長報告");
    if (result) {
        char xtitle[STRLEN];
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
        char xmsg[STRLEN*5 + sizeof(anti_pettifogger)];

        snprintf(xtitle, sizeof(xtitle), "%s 看板暫停發言通知(水桶)", board);
        snprintf(xmsg, sizeof(xmsg),
                 "%s 看板已暫時停止讓您發表意見。\n"
                 "開始時間: %s (期限 %s，此為執行時間，非原始犯規時間)\n"
                 "原因: %s\n"
                 "其它資訊請洽該看板板規與公告。\n\n%s",
                 board, Cdatelite(&now), datebuf, reason, anti_pettifogger);
        mail_log2id_text(uid, xtitle, xmsg, "[系統通知]", 1);
        sendalert(uid, ALERT_NEW_MAIL);
    }
    invalid_board_permission_cache(board);
    return 0;
}

static int
ui_unban_user_for_board(const char *uid, const char *board) {
    time4_t expire = now;
    int y, x;
    char ans[3];
    char history_log[PATHLEN];
    char reason[STRLEN];

    setbfile(history_log, board, FN_BANNED_HISTORY);

    getyx(&y, &x);
    if (!(expire = is_user_banned_by_board(uid, board))) {
        vmsg("使用者未在禁言名單。");
        return -1;
    }
    move(y, 0); clrtobot();
    prints("提前解除使用者 %s 於看板 %s 的禁言限制 (尚有 %d 天)。",
           uid, board, (expire-now)/DAY_SECONDS+1);
    assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
    getdata(y+1, 0, "請輸入理由(空白可取消解除): ",
            reason, BAKUMAN_REASON_LEN, DOECHO);
    if (!*reason) {
        vmsg("未輸入理由，取消此次設定");
        return -1;
    }

    // last chance
    getdata(y+4, 0, "確認以上資料全部正確嗎？ [y/N]: ",
            ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y') {
        vmsg("請重新輸入");
        return -1;
    }

    unban_user_for_board(uid, board);
    log_filef(history_log, LOG_CREAT,
              ANSI_COLOR(1) "%s " ANSI_COLOR(33) "%s" ANSI_RESET
              " 解除 " ANSI_COLOR(1;32) "%s" ANSI_RESET
              " 的禁言限制 (距原期限尚有 %d 天)\n  理由: %s\n",
              Cdatelite(&now), cuser.userid, uid,
              (expire - now) / DAY_SECONDS+1, reason);
    vmsg("使用者的禁言限制已解除，最晚至該使用者重新上站後生效");
    invalid_board_permission_cache(board);
    return 0;
}

int
edit_banned_list_for_board(const char *board) {
    // TODO generalize this.
    char uid[IDLEN+1], ans[3];

    if (!board || !*board || getbnum(board) < 1)
        return 0;

    while (1) {
        clear();
        vs_hdr2f(" Bakuman 權限設定系統 \t"
                 " 看板: %s ，類型: 停止發言(水桶)，名單上限: ∞", board);
        move(3, 0);
        outs(ANSI_COLOR(1)
        "                   歡迎使用 Bakuman 權限設定系統!\n\n" ANSI_RESET
        "      本系統提供下列功\能:" ANSI_COLOR(1;33)
                                 " - 無人數上限的名單設定\n"
        "                          - 自動生效的時效限制\n"
        "                          - 名單內舊帳號過期重新註冊時自動失效\n\n"
        ANSI_RESET
#if 1
        ANSI_COLOR(1;32)
        "   提醒您: 此系統的設計並無意干涉板主執行板務的方式，但若板主有特別的\n"
        "           使用方法 (如: 設定比公告長的時限，或是規定要另行信件才得解除)\n"
        "           請自行與板友及相關組別溝通好並明定板規。若因此造成使用者抗議\n"
        "           或爭議時後果請自負。\n"
        ANSI_RESET
#endif
        "   小提示: 跟舊系統稍有不同，新系統不會(也無法)列出 [現在名單內有哪些人]，\n"
        "           它採取的是設後免理+記錄式的概念: (可由歷史記錄自行推算)\n"
        "         - 平時用(A)新增並設好期限後就不用再去管設了哪些人\n"
        "         - 除非想提前解除或發現設錯，此時可用(D)先刪除然後再用(A)重新設定\n"
        "         - 想確認是否設錯或查某個使用者是不是仍在禁言中，可用(S)來檢查\n"
        "           另外也可用(L)看設定歷史記錄 (此記錄原則上系統不會清除)\n"
        "         - 目前沒有[永久禁言]的設定，若有需要請設個 10年或 20年\n"
        "         - 目前新增/解除不會寄信通知，另外請注意" ANSI_COLOR(1;33)
                   "帳號被砍後禁言會自動解除\n" ANSI_RESET
        "         - 禁言自動解除不會出現在記錄裡，只有手動提前解除的才會\n"
ANSI_COLOR(1) "         - 想查看某使用者為何被禁言可用(S)或是(L)再用 / 搜尋\n"
        ANSI_RESET
#ifdef WATERBAN_UPGRADE_TIME_STR
        // enable and change this if you've just made an upgrade
        ANSI_COLOR(0;32)
        " 系統更新資訊: 本系統啟用時已把所有您放在舊水桶名單的帳號全部設上了\n"
        "               " WATERBAN_UPGRADE_TIME_STR "的水桶，但沒有記錄在(L)的列表裡面。您可以參考(O)的舊名單\n"
        "               看看有沒有想修改的部份，然後利用(D)跟(A)來調整。\n"
        ANSI_COLOR(1;31) "               注意舊水桶內 ID 有改變過大小寫的無法轉換，請手動重設\n"
        ANSI_RESET
#endif
        "");

        getdata(1, 0, "(A)增加 (D)提前清除 (S)取得目前狀態 "
                      "(L)列出設定歷史 "
#ifdef SHOW_OLD_BAN
                      "(O)檢視舊水桶 "
#endif
                      "[Q]結束? ",
                ans, 2, LCECHO);
        move(2, 0); clrtobot();
        if (*ans == 'q' || !*ans)
            break;

        switch (*ans) {
            case 's':
                move(1, 0);
                usercomplete(msg_uid, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                move(1, 0); clrtobot();
                ui_print_user_banned_status_for_board(uid, board);
                pressanykey();
                break;

            case 'a':
                move(1, 0);
                usercomplete(msg_uid, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                move(1, 0); clrtobot();
                if (ui_ban_user_for_board(uid, board) < 0)
                    continue;
                break;

            case 'd':
                move(1, 0);
                usercomplete(msg_uid, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                move(1, 0);
                if (ui_unban_user_for_board(uid, board) < 0)
                    continue;
                break;

            case 'l':
                do {
                    char history_log[PATHLEN];
                    setbfile(history_log, board, FN_BANNED_HISTORY);
                    if (more(history_log, YEA) == -1)
                        vmsg("目前尚無設定記錄。");
                } while (0);
                break;

#ifdef SHOW_OLD_BAN
            case 'o':
                {
                    char old_log[PATHLEN];
                    setbfile(old_log, board, fn_water);
                    if (dashf(old_log)) {
                        vmsg("請注意: 此份資料僅供參考，與現在實際禁言名單完全沒有關係。");
                        more(old_log, YEA);
                    } else {
                        vmsg("無舊水桶資料。");
                    }
                }
                break;
#endif

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
        vbarf(" 設定使用者 %s 於看板《%s》之權限", uid, board);

        move(ytitle+2, 0);
        expire = ui_print_user_banned_status_for_board(uid, board);

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
                move(ytitle-1, 0); clrtobot();
                outs("\n" ANSI_REVERSE);
                vbarf(" 禁言使用者");
                move(ytitle+2, 0);
                if (ui_ban_user_for_board(uid, board) < 0)
                    continue;
                break;

            case 'u':
                if (!expire) {
                    finished = 1;
                    break;
                }
                move(ytitle-1, 0); clrtobot();
                outs("\n" ANSI_REVERSE);
                vbarf(" 提前解除禁言");
                move(ytitle+2, 0);
                if (ui_unban_user_for_board(uid, board) < 0)
                    continue;
                break;

            default:
                finished = 1;
                break;
        }
    }
    return 0;
}
