/* $Id$ */
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
        mkdir(buf, 0755);
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
is_banned_by(const char *who, const char *object, char object_type) {
    char tag_fn[PATHLEN];
    time4_t expire = 0;

    bakuman_make_tag_filename(who, object, object_type,
                              sizeof(tag_fn), tag_fn, 0);
    if (!dashf(tag_fn))
        return 0;

    // check expire
    if (bakuman_get_info(tag_fn, &expire, 0, NULL) && now > expire) {
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

time4_t
is_user_banned_by_board(const char *user, const char *board) {
    return is_banned_by(user, board, BAKUMAN_OBJECT_TYPE_BOARD);
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

int
edit_banned_list_for_board(const char *board) {
    // TODO generalize this.
    int result;
    char uid[IDLEN+1], ans[3];
    char history_log[PATHLEN];
    char reason[STRLEN];
    char datebuf[STRLEN];
    time4_t expire = now;

    if (!board || !*board || getbnum(board) < 1)
        return 0;

    setbfile(history_log, board, FN_BANNED_HISTORY);

    while (1) {
        clear();
        vs_hdr2f(" Bakuman 權限設定系統 \t"
                 " 看板: %s ，類型: 禁止發言(水桶)，名單上限: ∞", board);
        move(4, 0);
        outs(ANSI_COLOR(1)
        "                   歡迎使用 Bakuman 權限設定系統!\n\n" ANSI_RESET
        "                        本系統提供下列功\能:\n"
        ANSI_COLOR(1;33)
        "                          - 無上限的名單設定\n"
        "                          - 自動生效的時效限制\n"
        "                          - 名單內舊帳號過期重新註冊時自動取消\n\n"
        ANSI_RESET
        "      小提示: 跟舊系統稍有不同，新系統不會(也無法)列出 [現在名單內有哪些人]\n"
        "              它採取的是設後不理+記錄式的概念：\n"
        "            - 平時用(A)新增並設好期限後就不用再去管設了哪些人。\n"
        "            - 除非想提前解除或發現設錯，此時可用(D)先刪除然後再用(A)重新設定\n"
        "            - 想確認是否設錯或查某個使用者是不是仍在水桶中，可用(S)來檢查。\n"
        "              另外也可用(L)看設定記錄。\n\n"
#ifdef WATERBAN_UPGRADE_TIME_STR
        // enable and change this if you've just made an upgrade
        ANSI_COLOR(1;32)
        "      註: 系統更新時已把所有您放在舊水桶名單的帳號全部設上了" 
                   WATERBAN_UPGRADE_TIME_STR "的水桶，\n"
        "          但沒有記錄在(L)的列表裡面。 您可以利用(O)的舊名單參考有沒有想\n"
        "          修改的部份，然後利用(D)跟(A)來調整。\n" ANSI_RESET
#endif
        "");

        getdata(1, 0, "(A)增加 (D)提前清除 (S)取得目前狀態 "
                      "(L)列出設定歷史 (O)檢視舊水桶 [Q]結束? ",
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
                expire = is_user_banned_by_board(uid, board);
                if (expire > now) {
                    prints("使用者 %s 禁言中，解除時間: %s\n",
                           uid, Cdatelite(&expire));
                } else {
                    prints("使用者 %s 目前不在禁言名單中。\n",
                           uid);
                }
                pressanykey();
                break;

            case 'a':
                move(1, 0);
                usercomplete(msg_uid, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                if ((expire = is_user_banned_by_board(uid, board))) {
                    vmsgf("使用者之前已被禁言，尚有 %d 天。",
                          (expire - now) / DAY_SECONDS+1);
                    continue;
                }
                move(1, 0); clrtobot();
                prints("將使用者 %s 加入看板 %s 的禁言名單。", uid, board);
                syncnow();
                move(4, 0);
                outs("目前接受的格式是 [數字][單位]。 "
                     "單位有: 年(y), 月(m), 天(d)\n"
                     "範例: 3m (三個月), 180d (180天), 10y (10年)\n"
                     "注意不可混合輸入(例:沒有三個半月這種東西,請換算成天數)\n"
                     );
                getdata(2, 0, "請以數字跟單位(預設為天)輸入期限: ",
                        datebuf, 8, DOECHO);
                trim(datebuf);
                if (!*datebuf) {
                    vmsg("未輸入期限，放棄。");
                    continue;
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
                        continue;
                    }
                    if (now + val * DAY_SECONDS < now) {
                        vmsg("日期過大或無法處理，請重新輸入。");
                        continue;
                    }
                    expire = now + val * DAY_SECONDS;
                    move(4, 0); clrtobot();
                    // sprintf(datebuf, "%s", Cdatelite(&expire));
                    sprintf(datebuf, "%d 天", val);
                    prints("期限將設定為 %s之後: %s",
                            datebuf, Cdatelite(&expire));
                }

                assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
                // maybe race condition here, but fine.
                getdata(5, 0, "請輸入理由(空白可取消新增): ",
                        reason, BAKUMAN_REASON_LEN, DOECHO);
                if (!*reason) {
                    vmsg("未輸入理由，取消此次設定");
                    continue;
                }

                move(1, 0); clrtobot();
                prints("\n使用者 %s 即將加入禁言名單 (期限: %s)\n"
                       "理由: %s\n", uid, datebuf, reason);

                // last chance
                getdata(5, 0, "確認以上資料全部正確嗎？ [y/N]: ",
                        ans, sizeof(ans), LCECHO);
                if (ans[0] != 'y') {
                    vmsg("請重新輸入");
                    continue;
                }

                result = ban_user_for_board(uid, board, expire, reason);
                log_filef(history_log, LOG_CREAT,
                          ANSI_COLOR(1) "%s %s" ANSI_COLOR(33) "%s" ANSI_RESET 
                          " 限制 " ANSI_COLOR(1;31) "%s" ANSI_RESET 
                          " 發言，期限為 %s\n  理由: %s\n",
                          Cdatelite(&now),
                          result ? "" : 
                            ANSI_COLOR(0;31)"[系統錯誤] "ANSI_COLOR(1),
                          cuser.userid, uid,  datebuf, reason);
                vmsg(result ? "已將使用者加入禁言名單" : "失敗，請向站長報告");
                invalid_board_permission_cache(board);
                break;

            case 'd':
                move(1, 0);
                usercomplete(msg_uid, uid);
                if (!*uid || !searchuser(uid, uid))
                    continue;
                if (!(expire = is_user_banned_by_board(uid, board))) {
                    vmsg("使用者未在禁言名單。");
                    continue;
                }
                move(1, 0); clrtobot();
                prints("提前解除使用者 %s 於看板 %s 的禁言限制 (尚有 %d 天)。",
                       uid, board, (expire-now)/DAY_SECONDS+1);
                assert(sizeof(reason) >= BAKUMAN_REASON_LEN);
                getdata(2, 0, "請輸入理由(空白可取消解除): ",
                        reason, BAKUMAN_REASON_LEN, DOECHO);
                if (!*reason) {
                    vmsg("未輸入理由，取消此次設定");
                    continue;
                }

                // last chance
                getdata(5, 0, "確認以上資料全部正確嗎？ [y/N]: ",
                        ans, sizeof(ans), LCECHO);
                if (ans[0] != 'y') {
                    vmsg("請重新輸入");
                    continue;
                }

                unban_user_for_board(uid, board);
                log_filef(history_log, LOG_CREAT,
                          ANSI_COLOR(1) "%s " ANSI_COLOR(33) "%s" ANSI_RESET
                          " 解除 " ANSI_COLOR(1;32) "%s" ANSI_RESET 
                          " 的禁言限制 (距原期限尚有 %d 天)\n  理由: %s\n",
                          Cdatelite(&now), cuser.userid, uid, 
                          (expire - now) / DAY_SECONDS+1, reason);
                vmsg("使用者的禁言限制已解除。");
                invalid_board_permission_cache(board);
                break;

            case 'l':
                if (more(history_log, YEA) == -1)
                    vmsg("目前尚無設定記錄。");
                break;

            case 'o':
                {
                    char old_log[PATHLEN];
                    setbfile(old_log, board, fn_water);
                    if (more(old_log, YEA) == -1)
                        vmsg("無舊水桶資料。");
                }
                break;

            default:
                break;
        }
    }
    return 0;
}
