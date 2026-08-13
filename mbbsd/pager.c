#include "bbs.h"

const int PAGER_TABS = WB_OFO_USER_NUM;
static char     t_last_write[80];

int
iswritable_stat(const userinfo_t * uentp, int fri_stat)
{
    if (uentp == currutmp)
        return 0;

    if (HasUserPerm(PERM_SYSOP))
        return 1;

    if (!HasBasicUserPerm(PERM_LOGINOK) || HasUserPerm(PERM_VIOLATELAW))
        return 0;

    return (uentp->pager != PAGER_ANTIWB &&
            (fri_stat & HFM || uentp->pager != PAGER_FRIENDONLY));
}

static void
show_msg(int save, const msgque_t *msg)
{
    char buf[ANSILINELEN];
    int mode = msg->msgmode;

    if (HAS_ANGEL && mode == MSGMODE_TOANGEL) {
        SNPRINTF(buf, ANSI_COLOR(1;37;46) "★%s" ANSI_COLOR(37;45)
                 " %s " ANSI_RESET,
                 msg->userid,
                 msg->last_call_in);
        // I must be an Angel. Let's try to update angel beats info.
        // TODO maybe it's better to move this to "sender".
        angel_notify_activity(msg->userid);
    } else {
        SNPRINTF(buf, ANSI_COLOR(1;33;46) "★%s" ANSI_COLOR(37;45)
                 " %s " ANSI_RESET, msg->userid,
                 msg->last_call_in);
    }
    outmsg(buf);

    if (save && mode != MSGMODE_ALOHA) {
        char genbuf[PATHLEN];
        if (!fp_writelog) {
            sethomefile(genbuf, cuser.userid, fn_writelog);
            fp_writelog = fopen(genbuf, "a");
        }
        if (fp_writelog) {
            fprintf(fp_writelog, "%s [%s]\n", buf, Cdatelite(&now));
        }
    }
}

void check_water_init(void)
{
    if(water)
        return;

    water = (water_t*)malloc(sizeof(water_t) * (WB_OFO_USER_NUM + 1));
    memset(water, 0, sizeof(water_t) * (WB_OFO_USER_NUM + 1));
    water_which = &water[0];

    STRLCPY(water[0].userid, " 全部 ");
}

static int
pager_render_history_list(const water_t *w, int start_row, int max_rows, int selected_idx, int msg_bg)
{
    if (!w)
        return 0;

    int count = w->count;
    if (count > max_rows)
        count = max_rows;

    int i;
    for (i = 0; i < count; i++) {
        int a = (w->top - i - 1 + MAX_REVIEW) % MAX_REVIEW;
        int len = 75 - strlen(w->msg[a].last_call_in) - strlen(w->msg[a].userid);
        if (len < 0)
            len = 0;

        move(i + start_row, 0);
        clrtoeol();
        if (selected_idx != i)
            prints(ANSI_COLOR(1;33;46) " %s " ANSI_COLOR(37;%d) " %s " ANSI_RESET "%*s",
                   w->msg[a].userid, msg_bg, w->msg[a].last_call_in, len, "");
        else
            prints(ANSI_COLOR(1;44) ">" ANSI_COLOR(1;33;47) "%s " ANSI_COLOR(37;%d) " %s " ANSI_RESET "%*s",
                   w->msg[a].userid, msg_bg, w->msg[a].last_call_in, len, "");
    }
    return i;
}

static void
pager_render_history_section(const water_t *w, int start_row, int max_rows, int selected_idx, bool top_sep, int msg_bg)
{
    int y = start_row;
    if (top_sep) {
        move(y, 0);
        clrtoeol();
        outs(MSG_SEPARATOR);
        y++;
    }
    int i = pager_render_history_list(w, y, max_rows, selected_idx, msg_bg);
    y += i;

    const char *last_write = (w == &water[0]) ? t_last_write : w->msg[5].last_call_in;

    if (last_write && last_write[0]) {
        move(y, 0);
        clrtoeol();
        outs(last_write);
        i++, y++;
    }

    move(y, 0);
    outs(MSG_SEPARATOR);
    y++;

    while (i++ <= water[0].count && y < b_lines) {
        move(y++, 0);
        clrtoeol();
    }
}

static void
pager_render_tab_item(const water_t *w, bool is_selected, bool is_vertical)
{
    if (!w || !w->userid[0]) {
        outs("              ");
        return;
    }

    userinfo_t *uin = w->uin;
    if (uin && (w->pid != uin->pid || w->userid[0] != uin->userid[0]))
        uin = (userinfo_t *) search_ulist_pid(w->pid);

    char online_mark = uin ? ' ' : (is_vertical ? 'x' : '#');

    if (is_vertical) {
        const char *color = is_selected ? ANSI_COLOR(1;45) : ANSI_COLOR(1;44);
        prints("%s%c %-12s" ANSI_RESET, color, online_mark, w->userid);
    } else {
        const char *color = is_selected ? (uin ? ANSI_COLOR(1;33;47) : ANSI_COLOR(1;33;45)) : "";
        prints("%s%c%-13.13s" ANSI_RESET, color, online_mark, w->userid);
    }
}

static void
ofo_water_scr(const water_t *tw, int which, char type)
{
    move_ansi(WB_OFO_USER_TOP + 1 + which, WB_OFO_USER_LEFT);
    pager_render_tab_item(tw, type == 1, true);
    if (type != 1)
        return;

    pager_render_history_section(tw, WB_OFO_MSG_TOP, 5, -1, true, 44);

    move(0, 0);
    SOLVE_ANSI_CACHE();
    clrtoeol();
    if (HAS_ANGEL && tw->msg[0].msgmode == MSGMODE_TOANGEL)
        outs("回答小主人: ");
    else
        prints("反擊 %s: ", tw->userid);
}

static void
ofo_init_screen(void)
{
    move(WB_OFO_USER_TOP, 0);
    SOLVE_ANSI_CACHE();
    clrtoln(WB_OFO_MSG_BOTTOM + 1);
    SOLVE_ANSI_CACHE();

#ifndef USE_PFTERM
    refresh();
#endif

    mvouts(WB_OFO_USER_TOP, WB_OFO_USER_LEFT,
           ANSI_COLOR(1;33;46) " ↑反擊對象↓ " ANSI_RESET);

    for (int i = 0; i < WB_OFO_USER_NUM; ++i) {
        if (swater[i] == NULL || swater[i]->pid == 0)
            break;

        if (swater[i]->uin &&
            (swater[i]->pid != swater[i]->uin->pid ||
             swater[i]->userid[0] != swater[i]->uin->userid[0]))
            swater[i]->uin = (userinfo_t *) search_ulist_pid(swater[i]->pid);

        ofo_water_scr(swater[i], i, 0);
    }

    water_which = swater[0];
    ofo_water_scr(swater[0], 0, 1);
    refresh();
}

static void
ofo_switch_user(int delta)
{
    if (water_usies <= 1)
        return;

    assert(0 < water_usies && water_usies <= WB_OFO_USER_NUM);
    int curr = 0;
    for (int i = 0; i < water_usies; i++) {
        if (water_which == swater[i]) {
            curr = i;
            break;
        }
    }

    int next = (curr + delta + water_usies) % water_usies;
    ofo_water_scr(swater[curr], curr, 0);
    ofo_water_scr(swater[next], next, 1);
    water_which = swater[next];
    refresh();
}

static int
ofo_get_confirm_mode(const water_t *tw, char *genbuf, size_t sz)
{
    if (HAS_ANGEL) {
        switch (tw->msg[0].msgmode) {
        case MSGMODE_TOANGEL:
            strlcpy(genbuf, "回答小主人:", sz);
            return WATERBALL_CONFIRM_ANSWER;
        case MSGMODE_FROMANGEL:
            strlcpy(genbuf, "再問他一次：", sz);
            return WATERBALL_CONFIRM_ANGEL;
        default:
            break;
        }
    }
    snprintf(genbuf, sz, "攻擊 %s:", tw->userid);
    return WATERBALL_CONFIRM;
}

static void
ofo_reply_waterball(water_t *tw, int ch)
{
    if (!tw || !tw->uin)
        return;

    char msg[STRLEN];
    if ((ch < 0x100 && !isascii(ch)) || isprint(ch)) {
        msg[0] = (char)ch;
        msg[1] = '\0';
    } else {
        msg[0] = '\0';
    }

    move(0, 0);
    SOLVE_ANSI_CACHE();
    outs(ANSI_RESET);
    clrtoeol();

    char genbuf[256];
    int confirm_mode = ofo_get_confirm_mode(tw, genbuf, sizeof(genbuf));

    if (getdata_buf(0, 0, genbuf, msg, sizeof(msg) - strlen(tw->userid) - 6, DOECHO)) {
        if (my_write(tw->pid, msg, tw->userid, confirm_mode, tw->uin))
            STRLCPY(tw->msg[5].last_call_in, t_last_write);
    }
}

void
ofo_my_write(void)
{
    check_water_init();
    if (swater[0] == NULL)
        return;

    wmofo = REPLYING;
    int currstat0 = currstat;
    char c0 = currutmp->chatid[0];
    unsigned char mode0 = currutmp->mode;

    currutmp->mode = 0;
    currutmp->chatid[0] = 3;
    currstat = DBACK;

    ofo_init_screen();

    char done = 0;
    while (!done) {
        int ch = vkey();
        switch (ch) {
        case Ctrl('T'):
        case KEY_UP:
            ofo_switch_user(-1);
            break;

        case Ctrl('R'):
        case KEY_DOWN:
            ofo_switch_user(1);
            break;

        case KEY_LEFT:
            done = 1;
            break;

        case KEY_UNKNOWN:
            break;

        default:
            done = 1;
            ofo_reply_waterball(water_which, ch);
            break;
        }
    }

    currstat = currstat0;
    currutmp->chatid[0] = c0;
    currutmp->mode = mode0;

    if (wmofo == RECVINREPLYING) {
        wmofo = NOTREPLYING;
        write_request(0);
    }
    wmofo = NOTREPLYING;
}

/*
 * 被呼叫的時機:
 * 1. 丟群組水球 flag = WATERBALL_PREEDIT, 1 (pre-edit)
 * 2. 回水球     flag = WATERBALL_GENERAL, 0
 * 3. 上站aloha  flag = WATERBALL_ALOHA,   2 (pre-edit)
 * 4. 廣播       flag = WATERBALL_SYSOP,   3 if SYSOP
 *               flag = WATERBALL_PREEDIT, 1 otherwise
 * 5. 丟水球     flag = WATERBALL_GENERAL, 0
 * 6. ofo_my_write  flag = WATERBALL_CONFIRM, 4 (pre-edit but confirm)
 * 7. (when defined PLAY_ANGEL)
 *    呼叫小天使 flag = WATERBALL_ANGEL,   5 (id = "小天使")
 * 8. (when defined PLAY_ANGEL)
 *    回答小主人 flag = WATERBALL_ANSWER,  6 (隱藏 id)
 * 9. (when defined PLAY_ANGEL)
 *    呼叫小天使 flag = WATERBALL_CONFIRM_ANGEL, 7 (pre-edit)
 * 10. (when defined PLAY_ANGEL)
 *    回答小主人 flag = WATERBALL_CONFIRM_ANSWER, 8 (pre-edit)
 */
static void
my_write_restore_state(char c0, unsigned char mode0, int currstat0)
{
    currutmp->chatid[0] = c0;
    currutmp->mode = mode0;
    currstat = currstat0;
}

static bool
my_write_check_pager_status(void)
{
    switch (currutmp->pager) {
    case PAGER_DISABLE:
    case PAGER_ANTIWB:
        if (HasUserPerm(PERM_SYSOP | PERM_ACCOUNTS | PERM_BOARD)) {
            move(1, 0);
            clrtoeol();
            outs(ANSI_COLOR(1;31) "你的呼叫器目前不接受別人丟水球，對方可能無法回話。" ANSI_RESET);
        } else {
            if ('n' == vans("您的呼叫器目前設定為關閉。要打開它嗎?[Y/n] "))
                return false;
            currutmp->pager = PAGER_ON;
        }
        break;

    case PAGER_FRIENDONLY:
        move(1, 0);
        clrtoeol();
        outs(ANSI_COLOR(1;31) "你的呼叫器目前只接受好友丟水球，若對方非好友則可能無法回話。" ANSI_RESET);
        break;
    }
    return true;
}

static bool
my_write_get_input(const char *prompt, char *msg, size_t msg_size,
                   int *flag_out, userinfo_t **uin_out, char *destid)
{
    if (!my_write_check_pager_status())
        return false;

    int len = getdata(0, 0, prompt, msg, msg_size, DOECHO);
    if (!len)
        return false;

    if (watermode > 0) {
        int i = (water_which->top - watermode + MAX_REVIEW) % MAX_REVIEW;
        *uin_out = (userinfo_t *) search_ulist_pid(water_which->msg[i].pid);
        if (HAS_ANGEL) {
            if (water_which->msg[i].msgmode == MSGMODE_FROMANGEL)
                *flag_out = WATERBALL_ANGEL;
            else if (water_which->msg[i].msgmode == MSGMODE_TOANGEL)
                *flag_out = WATERBALL_ANSWER;
            else
                *flag_out = WATERBALL_GENERAL;
        }
        strlcpy(destid, water_which->msg[i].userid, IDLEN + 1);
    }
    return true;
}

static bool
my_write_confirm_send(int flag, const char *destid, const char *msg, userinfo_t *uin)
{
    bool is_confirm_needed = (
        flag == WATERBALL_GENERAL || flag == WATERBALL_CONFIRM ||
        (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER ||
                       flag == WATERBALL_CONFIRM_ANGEL || flag == WATERBALL_CONFIRM_ANSWER))
    );

    if (is_confirm_needed && uin && *uin->userid) {
        char buf[ANSILINELEN], genbuf[3];
        SNPRINTF(buf, "丟 %s: %s [Y/n]?", destid, msg);
        getdata(0, 0, buf, genbuf, sizeof(genbuf), LCECHO);
        if (genbuf[0] == 'n')
            return false;
    }
    return true;
}

static bool
my_write_validate_recipient(int flag, const char *destid, const userinfo_t *uin)
{
    if (!uin || !*uin->userid)
        return false;

    if (strcasecmp(destid, uin->userid) != 0) {
        if (!HAS_ANGEL || (flag != WATERBALL_ANGEL && flag != WATERBALL_CONFIRM_ANGEL))
            return false;
    }

    if (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)) {
        if (strcasecmp(cuser.myangel, uin->userid) != 0 || uin->angelpause >= ANGELPAUSE_REJALL)
            return false;
    }

    return true;
}

static bool
my_write_log_to_file(const char *destid, const char *msg)
{
    if (!fp_writelog) {
        char genbuf[PATHLEN];
        sethomefile(genbuf, cuser.userid, fn_writelog);
        fp_writelog = fopen(genbuf, "a");
    }

    if (!fp_writelog) {
        vmsg("抱歉，目前系統異常，暫時無法傳送資料。");
        return false;
    }

    fprintf(fp_writelog, "To %s: %s [%s]\n", destid, msg, Cdatelite(&now));
    snprintf(t_last_write, sizeof(t_last_write), "To %s: %s", destid, msg);
    return true;
}

static bool
my_write_is_rejected(int flag, userinfo_t *uin, int fri_stat)
{
    if (flag == WATERBALL_ALOHA)
        return false;
    if (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)) {
        if (angel_reject_me(uin))
            return true;
    }

    if (HasUserPerm(PERM_SYSOP))
        return false;

    if (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER ||
                      flag == WATERBALL_CONFIRM_ANGEL || flag == WATERBALL_CONFIRM_ANSWER))
        return false;

    if (uin->pager == PAGER_ANTIWB || uin->pager == PAGER_DISABLE)
        return true;

    if (uin->pager == PAGER_FRIENDONLY && !(fri_stat & HFM))
        return true;

    return false;
}

static void
my_write_deliver(int flag, const char *msg, userinfo_t *uin)
{
    if (!uin)
        return;

    int uip = get_utmp_id(uin);
    if (uip < 0)
        return;

    char from_id[IDLEN + 1];
    if (HAS_ANGEL && (flag == WATERBALL_ANSWER || flag == WATERBALL_CONFIRM_ANSWER)) {
        angel_load_my_fullnick(from_id, sizeof(from_id));
    } else {
        STRLCPY(from_id, cuser.userid);
    }

    int msgmode = MSGMODE_WRITE;
    switch (flag) {
    case WATERBALL_ANGEL:
    case WATERBALL_CONFIRM_ANGEL:
    case WATERBALL_ANSWER:
    case WATERBALL_CONFIRM_ANSWER:
        if (HAS_ANGEL) {
            if (flag == WATERBALL_ANGEL)
                angel_log_msg_to_angel();
            if (flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)
                msgmode = MSGMODE_TOANGEL;
            else
                msgmode = MSGMODE_FROMANGEL;
            break;
        }
        /* FALLTHROUGH */
    case WATERBALL_ALOHA:
        msgmode = MSGMODE_ALOHA;
        break;

    default:
        msgmode = MSGMODE_WRITE;
        break;
    }

    int res = write_message(uip, uin->pid, currpid, from_id, msg, msgmode);

    if (flag == WATERBALL_ALOHA)
        return;

    if (res < 0) {
        if (res == -3)
            outmsg(ANSI_COLOR(1;33;41) "糟糕! 對方不行了! (收到太多水球) " ANSI_COLOR(37) "@_@" ANSI_RESET);
        else
            outmsg(ANSI_COLOR(1;33;41) "糟糕! 沒打中! " ANSI_COLOR(37) "~>_<~" ANSI_RESET);
    } else if (uin->msgcount == 1) {
        outmsg(ANSI_COLOR(1;33;44) "水球砸過去了! " ANSI_COLOR(37) "*^o^*" ANSI_RESET);
    } else if (uin->msgcount > 1 && uin->msgcount < MAX_MSGS) {
        outmsg(ANSI_COLOR(1;33;44) "再補上一粒! " ANSI_COLOR(37) "*^o^*" ANSI_RESET);
    }
}

static water_t *swater_get_slot(const msgque_t *msg);

int
my_write(pid_t pid, const char *prompt, const char *id, int flag, userinfo_t *puin)
{
    userinfo_t *uin = (puin != NULL) ? puin : (userinfo_t *) search_ulist_pid(pid);
    char destid[IDLEN + 1];
    STRLCPY(destid, id);
    check_water_init();

    bool is_interactive = (flag == WATERBALL_GENERAL ||
                           (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER)));

    if ((!uin || !*uin->userid) && !(is_interactive && water_which->count > 0)) {
        vmsg("糟糕! 對方已落跑了(不在站上)! ");
        watermode = -1;
        return 0;
    }

    int currstat0 = currstat;
    char c0 = currutmp->chatid[0];
    unsigned char mode0 = currutmp->mode;

    currutmp->mode = 0;
    currutmp->chatid[0] = 3;
    currstat = DBACK;

    char msg[80];
    if (is_interactive) {
        watermode = 0;
        if (!my_write_get_input(prompt, msg, 56, &flag, &uin, destid)) {
            my_write_restore_state(c0, mode0, currstat0);
            watermode = -1;
            return 0;
        }
    } else {
        STRLCPY(msg, prompt);
    }

    strip_ansi(msg, msg, STRIP_ALL);

    if (!my_write_confirm_send(flag, destid, msg, uin)) {
        my_write_restore_state(c0, mode0, currstat0);
        watermode = -1;
        return 0;
    }

    watermode = -1;

    if (!my_write_validate_recipient(flag, destid, uin)) {
        bell();
        vmsg("糟糕! 對方已落跑了(不在站上)! ");
        my_write_restore_state(c0, mode0, currstat0);
        return 0;
    }

    int fri_stat = friend_stat(currutmp, uin);

    if (flag != WATERBALL_ALOHA) {
        if (!my_write_log_to_file(destid, msg)) {
            my_write_restore_state(c0, mode0, currstat0);
            return 0;
        }
    }

    if (flag == WATERBALL_SYSOP && uin->msgcount) {
        uin->destuip = get_utmp_id(currutmp);
        uin->sig = 2;
        if (uin->pid > 0)
            kill(uin->pid, SIGUSR1);
    } else if (my_write_is_rejected(flag, uin, fri_stat)) {
        outmsg(ANSI_COLOR(1;33;41) "糟糕! 對方防水了! " ANSI_COLOR(37) "~>_<~" ANSI_RESET);
    } else {
        my_write_deliver(flag, msg, uin);
        msgque_t dummy_msg;
        memset(&dummy_msg, 0, sizeof(dummy_msg));
        dummy_msg.pid = uin->pid;
        STRLCPY(dummy_msg.userid, destid);
        water_t *target_slot = swater_get_slot(&dummy_msg);
        if (target_slot)
            STRLCPY(target_slot->msg[5].last_call_in, t_last_write);
    }

    clrtoeol();
    my_write_restore_state(c0, mode0, currstat0);
    return 1;
}

static void
pager_show_panel_new(void)
{
    if (!water[0].count || watermode <= 0)
        return;
    int oy, ox;
    getyx_ansi(&oy, &ox);

    mvouts(1, 0,
           "───────水─球─回─顧───用[Ctrl-R Ctrl-T Ctrl-F Ctrl-G ]鍵切換───\n");
    for (int idx = 0; idx < 6; idx++) {
        if (idx == 0) {
            prints("%s 全部  " ANSI_RESET,
                   water_which == &water[0] ? ANSI_COLOR(1;33;47) " " : " ");
            continue;
        }
        const water_t *itm = swater[idx - 1];
        pager_render_tab_item(itm, itm == water_which, false);
    }
    outs("\n");
    pager_render_history_section(water_which, 3, MAX_REVIEW, watermode - 1, false, 45);
    move_ansi(oy, ox);
}

static void
pager_show_panel(void)
{
    static int in_panel = 0;
    if (in_panel)
        return;
    in_panel = 1;

    check_water_init();
    pager_show_panel_new();

    in_panel = 0;
}

int
pager_show_log(void) {
    char genbuf[PATHLEN], ans[4];
    if (fp_writelog) {
        // Why not simply fflush here? Because later when user enter (M) or (C),
        // fp_writelog must be re-opened -- and there will be a race condition.
        fclose(fp_writelog);
        fp_writelog = NULL;
    }
    setuserfile(genbuf, fn_writelog);
    if (more(genbuf, YEA) == -1) {
        vmsg("暫無訊息記錄");
        return FULLUPDATE;
    } else {
        grayout(0, b_lines-5, GRAYOUT_DARK);
        move(b_lines - 4, 0);
        clrtobot();
        outs(ANSI_COLOR(1;33;45) "★水球整理程式 " ANSI_RESET "\n"
             "提醒您: 可將水球存入信箱(M)後, 到【郵件選單】該信件前按 u,\n"
             "系統會將水球紀錄重新整理後寄送給您唷! " ANSI_RESET "\n");

        getdata(b_lines - 1, 0, "清除(C) 存入信箱(M) 保留(R) (C/M/R)?[R]",
                ans, sizeof(ans), LCECHO);
        if (*ans == 'm') {
            // only delete if success because the file can be re-used.
            if (mail_log2id(cuser.userid, "熱線記錄", genbuf, "[備.忘.錄]", 0, 1) == 0)
                unlink(genbuf);
            else
                vmsg("信箱儲存失敗。");
        } else if (*ans == 'c') {
            getdata(b_lines - 1, 0, "確定清除？(y/N) [N] ",
                    ans, sizeof(ans), LCECHO);
            if(*ans == 'Y' || *ans == 'y')
                unlink(genbuf);
            else
                vmsg("取消清除。");
        }
        return FULLUPDATE;
    }
    return DONOTHING;
}


int
call_in(const userinfo_t * uentp, int fri_stat)
{
    if (iswritable_stat(uentp, fri_stat)) {
        char            genbuf[60];
        SNPRINTF(genbuf, "丟 %s 水球: ", uentp->userid);
        my_write(uentp->pid, genbuf, uentp->userid, WATERBALL_GENERAL, NULL);
        return 1;
    }
    return 0;
}


int
pager_toggle_mode(void)
{
    currutmp->pager = (currutmp->pager + 1) % PAGER_MODES;
    return 0;
}

/* ----------------------------------------------------- */
/* Pager key handling (Ctrl-R)                           */
/* ----------------------------------------------------- */

static int
pager_handle_ctrl_r_ofo(int ch)
{
    int my_newfd;
    screen_backup_t old_screen;

    check_water_init();

    water_t *w = (swater[0] != NULL && swater[0]->pid != 0) ? swater[0] : &water[0];
    if (!w || w->count == 0 || wmofo != NOTREPLYING)
        return ch;

    scr_dump(&old_screen);

    my_newfd = vkey_detach();
    ofo_my_write();
    scr_restore(&old_screen);
    vkey_attach(my_newfd);
    return KEY_INCOMPLETE;
}

static int
pager_handle_ctrl_r_default(int ch)
{
    check_water_init();

    if (watermode > 0)
    {
        // Press Ctrl-R for N+ times.
        watermode = (watermode + water_which->count)
                % water_which->count + 1;
        pager_show_panel();
        return KEY_INCOMPLETE;
    }
    else if (watermode == 0 &&
             currutmp->mode == 0 &&
             (currutmp->chatid[0] == 2 || currutmp->chatid[0] == 3) &&
             water_which->count != 0)
    {
        // Press Ctrl-R for the "2nd" time.
        watermode = 1;
        pager_show_panel();
        return KEY_INCOMPLETE;
    }
    else if (watermode == -1)
    {
        water_t *w = (swater[0] != NULL && swater[0]->pid != 0) ? swater[0] : &water[0];
        if (!w || w->count == 0)
            return ch;

        int last_idx = (w->top - 1 + MAX_REVIEW) % MAX_REVIEW;
        msgque_t *last_msg = &w->msg[last_idx];
        if (last_msg->pid == 0)
            return ch;

        // Press Ctrl-R for the "1st" time (and already received a message for reply)
        screen_backup_t old_screen;
        int             my_newfd;
        scr_dump(&old_screen);

        my_newfd = vkey_detach();
        char buf[ANSILINELEN];
        SNPRINTF(buf, ANSI_COLOR(1;33;46) "★%s" ANSI_COLOR(37;45)
                 " %s " ANSI_RESET, last_msg->userid, last_msg->last_call_in);
        outmsg(buf);

        watermode = 0;
        if (!HAS_ANGEL) {
            my_write(last_msg->pid, "水球丟過去： ",
                     last_msg->userid, WATERBALL_GENERAL, NULL);
        } else {
            switch (last_msg->msgmode) {
            case MSGMODE_TALK:
            case MSGMODE_WRITE:
            case MSGMODE_ALOHA:
                my_write(last_msg->pid, "水球丟過去： ",
                         last_msg->userid, WATERBALL_GENERAL, NULL);
                break;
            case MSGMODE_FROMANGEL:
                my_write(last_msg->pid, "再問一次： ",
                         last_msg->userid, WATERBALL_ANGEL, NULL);
                break;
            case MSGMODE_TOANGEL:
                my_write(last_msg->pid, "回答小主人： ",
                         last_msg->userid, WATERBALL_ANSWER, NULL);
                break;
            }
        }
        vkey_attach(my_newfd);

        scr_restore(&old_screen);
        return KEY_INCOMPLETE;
    }
    return ch;
}

static int
pager_modal_key_hook(int ch)
{
    static int water_which_flag = 0;

    if (!currutmp || watermode <= 0)
        return ch;

    switch (ch)
    {
    case KEY_TAB:
        check_water_init();
        watermode = (watermode + water_which->count)
                % water_which->count + 1;
        pager_show_panel();
        return KEY_INCOMPLETE;

    case Ctrl('T'):
        check_water_init();
        if (watermode > 1)
            watermode--;
        else
            watermode = water_which->count;
        pager_show_panel();
        return KEY_INCOMPLETE;

    case Ctrl('F'):
        check_water_init();
        water_which_flag =
                (water_which_flag + 1) % (int)(water_usies + 1);
        if (water_which_flag == 0)
            water_which = &water[0];
        else
            water_which = swater[water_which_flag - 1];
        watermode = 1;
        pager_show_panel();
        return KEY_INCOMPLETE;

    case Ctrl('G'):
        check_water_init();
        water_which_flag = (water_which_flag + water_usies) %
                (water_usies + 1);
        if (water_which_flag == 0)
            water_which = &water[0];
        else
            water_which = swater[water_which_flag - 1];

        watermode = 1;
        pager_show_panel();
        return KEY_INCOMPLETE;
    }
    return ch;
}

static int
pager_global_key_hook(int ch)
{
    if (!currutmp)
        return ch;

    switch (ch)
    {
    case Ctrl('R'):
        if (PAGER_UI_IS(PAGER_UI_OFO))
            return pager_handle_ctrl_r_ofo(ch);
        return pager_handle_ctrl_r_default(ch);
    }
    return ch;
}

void
pager_init_hooks(void)
{
    vkey_register_hook(VKEY_HOOK_PRIO_MODAL, pager_modal_key_hook);
    vkey_register_hook(VKEY_HOOK_PRIO_PAGER, pager_global_key_hook);
}

/* ----------------------------------------------------- */
/* Waterball & Pager Request Handlers & History          */
/* ----------------------------------------------------- */

void
talk_request(int sig GCC_UNUSED)
{
    STATINC(STAT_TALKREQUEST);
    bell();
    bell();
    if (currutmp->msgcount) {
        syncnow();
        move(0, 0);
        clrtoeol();
        prints(ANSI_COLOR(33;41) "★%s" ANSI_COLOR(34;47) " [%s] %s " ANSI_RESET,
               SHM->uinfo[currutmp->destuip].userid, Cdatelite(&now),
               (currutmp->sig == 2) ? "有急事！(按Ctrl-U,l可看訊息)"
               : "呼叫、呼叫，聽到請回答");
        refresh();
    } else {
        unsigned char   mode0 = currutmp->mode;
        char            c0 = currutmp->chatid[0];
        screen_backup_t old_screen;

        currutmp->mode = 0;
        currutmp->chatid[0] = 1;
        scr_dump(&old_screen);
        talkreply();
        currutmp->mode = mode0;
        currutmp->chatid[0] = c0;
        scr_restore(&old_screen);
    }
}

void
show_call_in(int save, int which)
{
    show_msg(save, &currutmp->msgs[which]);
}

static int
add_history_entry(water_t * w, const msgque_t * msg)
{
    memcpy(&w->msg[w->top], msg, sizeof(msgque_t));
    w->top++;
    w->top %= MAX_REVIEW;

    if (w->count < MAX_REVIEW)
        w->count++;

    return w->count;
}

static inline int
is_angel_msgmode(int mode)
{
    return (mode == MSGMODE_FROMANGEL || mode == MSGMODE_TOANGEL);
}

static inline int
is_swater_compatible(const water_t *w, const msgque_t *msg)
{
    if (w->pid != msg->pid)
        return 0;

    if (!HAS_ANGEL)
        return 1;

    /* Angel modes (FROMANGEL/TOANGEL) and Non-Angel modes (WRITE/ALOHA/TALK)
     * must never mix, but all Angel modes match each other and all Non-Angel modes match each other. */
    return is_angel_msgmode(w->msg[0].msgmode) == is_angel_msgmode(msg->msgmode);
}

/* Locate or allocate a swater session slot, promoting it to swater[0] */
static water_t *
swater_get_slot(const msgque_t *msg)
{
    int i, j;
    int waterinit = 0;
    water_t *tmp;

    assert(PAGER_TABS <= ARRAY_SIZE(swater));

    for (i = 0; i < PAGER_TABS; i++) {
        if (swater[i] == NULL)
            break;
        if (is_swater_compatible(swater[i], msg))
            break;
    }

    if (i == PAGER_TABS) {
        waterinit = 1;
        i = PAGER_TABS - 1;
        memset(swater[i], 0, sizeof(water_t));
    } else if (!swater[i]) {
        water_usies = i + 1;
        swater[i] = &water[i + 1];
        waterinit = 1;
    }

    tmp = swater[i];

    if (waterinit) {
        memcpy(swater[i]->userid, msg->userid, sizeof(swater[i]->userid));
        swater[i]->pid = msg->pid;
    }
    if (!swater[i]->uin)
        swater[i]->uin = currutmp;

    /* Shift elements so the active slot becomes swater[0] */
    for (j = i; j > 0; j--)
        swater[j] = swater[j - 1];
    swater[0] = tmp;

    return swater[0];
}

static int
add_history(const msgque_t * msg)
{
    check_water_init();

    /* 1. Always record to global linear history buffer (water[0]) */
    add_history_entry(&water[0], msg);

    /* 2. Record to per-user session history buffer (swater[0]) */
    water_t *target_slot = swater_get_slot(msg);
    add_history_entry(target_slot, msg);

    /* 3. Refresh display if user is currently reviewing messages */
    if (watermode > 0 &&
        (water_which == swater[0] || water_which == &water[0])) {
        if (watermode < water_which->count)
            watermode++;
        pager_show_panel();
    }

    return 0;
}

static inline int
can_pop_pager_ui(void)
{
    return currutmp->mode != 0 &&
           currutmp->pager != PAGER_OFF &&
           cuser.userlevel != 0 &&
           currutmp->msgcount != 0 &&
           currutmp->mode != TALK &&
           currutmp->mode != EDITING &&
           currutmp->mode != CHATING &&
           currutmp->mode != PAGE &&
           currutmp->mode != IDLE &&
           currutmp->mode != MAILALL &&
           currutmp->mode != MONITOR;
}

static void
write_request_ofo(int sig)
{
    static int alreadyshow = 0;
    int i, msgcount;

    if (sig) { /* Incoming waterball signal */
        /* If currently in REPLYING mode, switch to RECVINREPLYING
         * so write_request(0) is triggered after reply finishes. */
        if (wmofo == REPLYING)
            wmofo = RECVINREPLYING;

        for (; alreadyshow < currutmp->msgcount && alreadyshow < MAX_MSGS; ++alreadyshow) {
            bell();
            show_call_in(1, alreadyshow);
            refresh();
        }
    }

    /* Flush pending messages from currutmp->msg to water[] when NOTREPLYING */
    if (wmofo == NOTREPLYING && (msgcount = currutmp->msgcount) > 0) {
        for (i = 0; i < msgcount; ++i) {
            add_history(&currutmp->msgs[i]);
            currutmp->msgs[i].pid = 0;
        }
        if ((currutmp->msgcount -= msgcount) < 0)
            currutmp->msgcount = 0;
        alreadyshow = 0;
    }
}

static void
write_request_default(void)
{
    int i, msgcount;

    if (!can_pop_pager_ui()) {
        msgcount = currutmp->msgcount;
        for (i = 0; i < msgcount; ++i) {
            bell();
            show_call_in(1, i);
            add_history(&currutmp->msgs[i]);
            currutmp->msgs[i].pid = 0;
        }
        currutmp->msgcount = 0;
        refresh();
        return;
    }

    char c0 = currutmp->chatid[0];
    int currstat0 = currstat;
    unsigned char mode0 = currutmp->mode;

    currutmp->mode = 0;
    currutmp->chatid[0] = 2;
    currstat = HIT;
    msgcount = currutmp->msgcount;

    for (i = 0; i < msgcount; ++i) {
        bell();
        show_call_in(1, 0);
        add_history(&currutmp->msgs[0]);

        if ((--currutmp->msgcount) < 0)
            break;

        if (currutmp->msgcount > 0) {
            memmove(&currutmp->msgs[0],
                    &currutmp->msgs[1],
                    sizeof(msgque_t) * currutmp->msgcount);
        }
        currutmp->msgs[(int)currutmp->msgcount].pid = 0;
        vkey();
    }

    currutmp->chatid[0] = c0;
    currutmp->mode = mode0;
    currstat = currstat0;
}

void
write_request(int sig)
{
    STATINC(STAT_WRITEREQUEST);
    syncnow();
    check_water_init();

    if (PAGER_UI_IS(PAGER_UI_OFO)) {
        write_request_ofo(sig);
    } else {
        write_request_default();
    }
}
