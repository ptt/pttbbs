#include "bbs.h"

const int PAGER_TABS = WB_OFO_USER_NUM;

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


static char     t_last_write[80];

void check_water_init(void)
{
    if(water)
        return;

    water = (water_t*)malloc(sizeof(water_t) * (WB_OFO_USER_NUM + 1));
    memset(water, 0, sizeof(water_t) * (WB_OFO_USER_NUM + 1));
    water_which = &water[0];

    STRLCPY(water[0].userid, " 全部 ");
}

static void
ofo_water_scr(const water_t *tw, int which, char type)
{
    move(8 + which, 28);
    SOLVE_ANSI_CACHE();

    if (type != 1) {
        prints(ANSI_COLOR(0;1;37;44) "  %c %-13s　" ANSI_RESET,
               tw->uin ? ' ' : 'x', tw->userid);
        return;
    }

    prints(ANSI_COLOR(0;1;37;45) "  %c %-14s " ANSI_RESET,
           tw->uin ? ' ' : 'x', tw->userid);

    static const int colors[] = {33, 37, 33, 37, 33};
    for (int i = 0; i < 5; ++i) {
        move(16 + i, 4);
        SOLVE_ANSI_CACHE();
        const char *call_in_msg = tw->msg[(tw->top - i + 4) % 5].last_call_in;
        if (call_in_msg[0] != '\0')
            prints("   " ANSI_COLOR(1;%d;44) "★%-64s" ANSI_RESET "   \n",
                   colors[i], call_in_msg);
        else
            outs("　\n");
    }

    move(21, 4);
    SOLVE_ANSI_CACHE();
    prints("   " ANSI_COLOR(1;37;46) "%-66s" ANSI_RESET "   \n",
           tw->msg[5].last_call_in);

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
    move(WB_OFO_USER_TOP - 1, 0);
    SOLVE_ANSI_CACHE();
    clrtoln(WB_OFO_MSG_BOTTOM + 1);
    SOLVE_ANSI_CACHE();

#ifndef USE_PFTERM
    refresh();
#endif

    mvouts(WB_OFO_USER_TOP, WB_OFO_USER_LEFT,
           ANSI_COLOR(1;33;46) " ↑ 水球反擊對象 ↓" ANSI_RESET);

    for (int i = 0; i < WB_OFO_USER_NUM; ++i) {
        if (swater[i] == NULL || swater[i]->pid == 0)
            break;

        if (swater[i]->uin &&
            (swater[i]->pid != swater[i]->uin->pid ||
             swater[i]->userid[0] != swater[i]->uin->userid[0]))
            swater[i]->uin = (userinfo_t *) search_ulist_pid(swater[i]->pid);

        ofo_water_scr(swater[i], i, 0);
    }

    move(WB_OFO_MSG_TOP, WB_OFO_MSG_LEFT);
    outs(ANSI_RESET " " ANSI_COLOR(1;35) "◇" ANSI_COLOR(1;36)
         "─────────────────────────────────"
         ANSI_COLOR(1;35) "◇" ANSI_RESET " ");

    move(WB_OFO_MSG_BOTTOM, WB_OFO_MSG_LEFT);
    outs(" " ANSI_COLOR(1;35) "◇" ANSI_COLOR(1;36)
         "─────────────────────────────────"
         ANSI_COLOR(1;35) "◇" ANSI_RESET " ");

    ofo_water_scr(swater[0], 0, 1);
    refresh();
}

static void
ofo_switch_user(int *which, int delta)
{
    if (water_usies <= 1)
        return;

    assert(0 < water_usies && water_usies <= WB_OFO_USER_NUM);
    int curr = *which;
    int next = (curr + delta + water_usies) % water_usies;

    ofo_water_scr(swater[curr], curr, 0);
    ofo_water_scr(swater[next], next, 1);
    *which = next;
    refresh();
}

static int
ofo_get_confirm_mode(const water_t *tw, char genbuf[256])
{
    if (HAS_ANGEL) {
        switch (tw->msg[0].msgmode) {
        case MSGMODE_TOANGEL:
            strlcpy(genbuf, "回答小主人:", 256);
            return WATERBALL_CONFIRM_ANSWER;
        case MSGMODE_FROMANGEL:
            strlcpy(genbuf, "再問他一次：", 256);
            return WATERBALL_CONFIRM_ANGEL;
        default:
            break;
        }
    }
    snprintf(genbuf, 256, "攻擊 %s:", tw->userid);
    return WATERBALL_CONFIRM;
}

static void
ofo_reply_waterball(water_t *tw, int ch)
{
    if (!tw || !tw->uin)
        return;

    char msg[80];
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
    int confirm_mode = ofo_get_confirm_mode(tw, genbuf);

    if (getdata_buf(0, 0, genbuf, msg, 80 - strlen(tw->userid) - 6, DOECHO)) {
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

    int which = 0;
    char done = 0;
    while (!done) {
        int ch = vkey();
        switch (ch) {
        case Ctrl('T'):
        case KEY_UP:
            ofo_switch_user(&which, -1);
            break;

        case Ctrl('R'):
        case KEY_DOWN:
            ofo_switch_user(&which, 1);
            break;

        case KEY_LEFT:
            done = 1;
            break;

        case KEY_UNKNOWN:
            break;

        default:
            done = 1;
            ofo_reply_waterball(swater[which], ch);
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
int
my_write(pid_t pid, const char *prompt, const char *id, int flag, userinfo_t * puin)
{
    int             len, currstat0 = currstat, fri_stat = -1;
    char            msg[80], destid[IDLEN + 1];
    char            genbuf[200], buf[200], c0 = currutmp->chatid[0];
    unsigned char   mode0 = currutmp->mode;
    userinfo_t     *uin;
    uin = (puin != NULL) ? puin : (userinfo_t *) search_ulist_pid(pid);
    STRLCPY(destid, id);
    check_water_init();

    /* what if uin is NULL but other conditions are not true?
     * will this situation cause SEGV?
     * should this "!uin &&" replaced by "!uin ||" ?
     */
    if ((!uin || !uin->userid[0]) && !((flag == WATERBALL_GENERAL
                                        || (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER))
                                       )
                                       && water_which->count > 0)) {
        vmsg("糟糕! 對方已落跑了(不在站上)! ");
        watermode = -1;
        return 0;
    }
    currutmp->mode = 0;
    currutmp->chatid[0] = 3;
    currstat = DBACK;

    if (flag == WATERBALL_GENERAL
        || (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER))
       ) {
        /* 一般水球 */
        watermode = 0;

        switch(currutmp->pager)
        {
        case PAGER_DISABLE:
        case PAGER_ANTIWB:
            if (HasUserPerm(PERM_SYSOP | PERM_ACCOUNTS | PERM_BOARD)) {
                // Admins are free to bother people.
                move(1, 0);  clrtoeol();
                outs(ANSI_COLOR(1;31)
                     "你的呼叫器目前不接受別人丟水球，對方可能無法回話。"
                     ANSI_RESET);
            } else {
                // Normal users should not bother people.
                if ('n' == vans("您的呼叫器目前設定為關閉。"
                                "要打開它嗎?[Y/n] "))
                    return 0;
                // enable pager
                currutmp->pager = PAGER_ON;
            }
            break;

        case PAGER_FRIENDONLY:
            move(1, 0);  clrtoeol();
            outs(ANSI_COLOR(1;31) "你的呼叫器目前只接受好友丟水球，若對方非好友則可能無法回話。" ANSI_RESET);
            break;
        }

        if (!(len = getdata(0, 0, prompt, msg, 56, DOECHO))) {
            currutmp->chatid[0] = c0;
            currutmp->mode = mode0;
            currstat = currstat0;
            watermode = -1;
            return 0;
        }

        if (watermode > 0) {
            int             i;

            i = (water_which->top - watermode + MAX_REVIEW) % MAX_REVIEW;
            uin = (userinfo_t *) search_ulist_pid(water_which->msg[i].pid);
            if (HAS_ANGEL) {
                if (water_which->msg[i].msgmode == MSGMODE_FROMANGEL)
                    flag = WATERBALL_ANGEL;
                else if (water_which->msg[i].msgmode == MSGMODE_TOANGEL)
                    flag = WATERBALL_ANSWER;
                else
                    flag = WATERBALL_GENERAL;
            }
            STRLCPY(destid, water_which->msg[i].userid);
        }
    } else {
        /* pre-edit 的水球 */
        STRLCPY(msg, prompt);
        len = strlen(msg);
    }

    strip_ansi(msg, msg, STRIP_ALL);
    if (uin && *uin->userid &&
        (flag == WATERBALL_GENERAL || flag == WATERBALL_CONFIRM
         || (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER
                           || flag == WATERBALL_CONFIRM_ANGEL
                           || flag == WATERBALL_CONFIRM_ANSWER))
        ))
    {
        SNPRINTF(buf, "丟 %s: %s [Y/n]?", destid, msg);

        getdata(0, 0, buf, genbuf, 3, LCECHO);
        if (genbuf[0] == 'n') {
            currutmp->chatid[0] = c0;
            currutmp->mode = mode0;
            currstat = currstat0;
            watermode = -1;
            return 0;
        }
    }
    watermode = -1;
    if (!uin || !*uin->userid ||
        (strcasecmp(destid, uin->userid) && (!HAS_ANGEL || (flag != WATERBALL_ANGEL && flag != WATERBALL_CONFIRM_ANGEL))) ||
        // check if user is changed of angelpause.
        // XXX if flag == WATERBALL_ANGEL, shuold be (uin->angelpause) only.
        (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL) &&
         (strcasecmp(cuser.myangel, uin->userid) || uin->angelpause >= ANGELPAUSE_REJALL))) {
        bell();
        vmsg("糟糕! 對方已落跑了(不在站上)! ");
        currutmp->chatid[0] = c0;
        currutmp->mode = mode0;
        currstat = currstat0;
        return 0;
    }
    if(fri_stat < 0)
        fri_stat = friend_stat(currutmp, uin);
    // else, fri_stat was already calculated. */

    if (flag != WATERBALL_ALOHA) {	/* aloha 的水球不用存下來 */
        /* 存到自己的水球檔 */
        if (!fp_writelog) {
            sethomefile(genbuf, cuser.userid, fn_writelog);
            fp_writelog = fopen(genbuf, "a");
        }
        if (fp_writelog) {
            fprintf(fp_writelog, "To %s: %s [%s]\n",
                    destid, msg, Cdatelite(&now));
            snprintf(t_last_write, 66, "To %s: %s", destid, msg);
        } else {
            vmsg("抱歉，目前系統異常，暫時無法傳送資料。");
            return 0;
        }
    }
    if (flag == WATERBALL_SYSOP && uin->msgcount) {
        /* 不懂 */
        uin->destuip = get_utmp_id(currutmp);
        uin->sig = 2;
        if (uin->pid > 0)
            kill(uin->pid, SIGUSR1);
    } else if ((flag != WATERBALL_ALOHA &&
                (!HAS_ANGEL || (flag != WATERBALL_ANGEL &&
                                flag != WATERBALL_ANSWER &&
                                flag != WATERBALL_CONFIRM_ANGEL &&
                                flag != WATERBALL_CONFIRM_ANSWER)) &&
                /* Angel accept or not is checked outside.
                 * Avoiding new users don't know what pager is. */
                !HasUserPerm(PERM_SYSOP) &&
                (uin->pager == PAGER_ANTIWB ||
                 uin->pager == PAGER_DISABLE ||
                 (uin->pager == PAGER_FRIENDONLY &&
                  !(fri_stat & HFM))))
               || (HAS_ANGEL && (flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)
                   && angel_reject_me(uin))
              ) {
        outmsg(ANSI_COLOR(1;33;41) "糟糕! 對方防水了! " ANSI_COLOR(37) "~>_<~" ANSI_RESET);
    } else {
        int     write_pos = uin->msgcount; /* try to avoid race */
        if ( write_pos < (MAX_MSGS - 1) ) { /* race here */
            unsigned char   pager0 = uin->pager;

            uin->msgcount = write_pos + 1;
            uin->pager = PAGER_DISABLE;
            uin->msgs[write_pos].pid = currpid;
            if (HAS_ANGEL && (flag == WATERBALL_ANSWER || flag == WATERBALL_CONFIRM_ANSWER))
                angel_load_my_fullnick(uin->msgs[write_pos].userid,
                                       sizeof(uin->msgs[write_pos].userid));
            else
                STRLCPY(uin->msgs[write_pos].userid, cuser.userid);
            STRLCPY(uin->msgs[write_pos].last_call_in, msg);
            switch (flag) {
            case WATERBALL_ANGEL:
            case WATERBALL_CONFIRM_ANGEL:
            case WATERBALL_ANSWER:
            case WATERBALL_CONFIRM_ANSWER:
                if (HAS_ANGEL) {
                    if (flag == WATERBALL_ANGEL)
                        angel_log_msg_to_angel();
                    if (flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)
                        uin->msgs[write_pos].msgmode = MSGMODE_TOANGEL;
                    else
                        uin->msgs[write_pos].msgmode = MSGMODE_FROMANGEL;
                    break;
                }
                /* FALLTHROUGH */
            case WATERBALL_ALOHA:
                uin->msgs[write_pos].msgmode = MSGMODE_ALOHA;
                break;

            default:
                uin->msgs[write_pos].msgmode = MSGMODE_WRITE;
                break;
            }
            uin->pager = pager0;
        } else if (flag != WATERBALL_ALOHA)
            outmsg(ANSI_COLOR(1;33;41) "糟糕! 對方不行了! (收到太多水球) " ANSI_COLOR(37) "@_@" ANSI_RESET);

        if (uin->msgcount >= 1 && (uin->pid <= 0 || kill(uin->pid, SIGUSR2) == -1) && flag != WATERBALL_ALOHA)
            outmsg(ANSI_COLOR(1;33;41) "糟糕! 沒打中! " ANSI_COLOR(37) "~>_<~" ANSI_RESET);
        else if (uin->msgcount == 1 && flag != WATERBALL_ALOHA)
            outmsg(ANSI_COLOR(1;33;44) "水球砸過去了! " ANSI_COLOR(37) "*^o^*" ANSI_RESET);
        else if (uin->msgcount > 1 && uin->msgcount < MAX_MSGS &&
                 flag != WATERBALL_ALOHA)
            outmsg(ANSI_COLOR(1;33;44) "再補上一粒! " ANSI_COLOR(37) "*^o^*" ANSI_RESET);

    }

    clrtoeol();

    currutmp->chatid[0] = c0;
    currutmp->mode = mode0;
    currstat = currstat0;
    return 1;
}

void
getmessage(msgque_t msg)
{
    int     write_pos = currutmp->msgcount;
    if ( write_pos < (MAX_MSGS - 1) ) {
        unsigned char pager0 = currutmp->pager;
        currutmp->msgcount = write_pos+1;
        memcpy(&currutmp->msgs[write_pos], &msg, sizeof(msgque_t));
        currutmp->pager = pager0;
        write_request(SIGUSR1);
    }
}

void
t_display_new(void)
{
    static int      t_display_new_flag = 0;
    int             i, off = 2;
    if (t_display_new_flag)
        return;
    else
        t_display_new_flag = 1;

    check_water_init();
    if (PAGER_UI_IS(PAGER_UI_ORIG))
        water_which = &water[0];
    else
        off = 3;

    if (water[0].count && watermode > 0) {
        move(1, 0);
        outs("───────水─球─回─顧───");
        outs(PAGER_UI_IS(PAGER_UI_ORIG) ?
             "──────用[Ctrl-R Ctrl-T]鍵切換─────" :
             "用[Ctrl-R Ctrl-T Ctrl-F Ctrl-G ]鍵切換────");
        if (PAGER_UI_IS(PAGER_UI_NEW)) {
            move(2, 0);
            clrtoeol();
            for (i = 0; i < 6; i++) {
                if (i > 0)
                    if (swater[i - 1]) {

                        if (swater[i - 1]->uin &&
                            (swater[i - 1]->pid != swater[i - 1]->uin->pid ||
                             swater[i - 1]->userid[0] != swater[i - 1]->uin->userid[0]))
                            swater[i - 1]->uin = (userinfo_t *) search_ulist_pid(swater[i - 1]->pid);
                        prints("%s%c%-13.13s" ANSI_RESET,
                               swater[i - 1] != water_which ? "" :
                               swater[i - 1]->uin ? ANSI_COLOR(1;33;47) :
                               ANSI_COLOR(1;33;45),
                               !swater[i - 1]->uin ? '#' : ' ',
                               swater[i - 1]->userid);
                    } else
                        outs("              ");
                    else
                        prints("%s 全部  " ANSI_RESET,
                               water_which == &water[0] ? ANSI_COLOR(1;33;47) " " :
                               " "
                              );
            }
        }
        for (i = 0; i < water_which->count; i++) {
            int a = (water_which->top - i - 1 + MAX_REVIEW) % MAX_REVIEW;
            int len = 75 - strlen(water_which->msg[a].last_call_in)
                    - strlen(water_which->msg[a].userid);
            if (len < 0)
                len = 0;

            move(i + (PAGER_UI_IS(PAGER_UI_ORIG) ? 2 : 3), 0);
            clrtoeol();
            if (watermode - 1 != i)
                prints(ANSI_COLOR(1;33;46) " %s " ANSI_COLOR(37;45) " %s " ANSI_RESET "%*s",
                       water_which->msg[a].userid,
                       water_which->msg[a].last_call_in, len,
                       "");
            else
                prints(ANSI_COLOR(1;44) ">" ANSI_COLOR(1;33;47) "%s "
                       ANSI_COLOR(37;45) " %s " ANSI_RESET "%*s",
                       water_which->msg[a].userid,
                       water_which->msg[a].last_call_in,
                       len, "");
        }

        if (t_last_write[0]) {
            move(i + off, 0);
            clrtoeol();
            outs(t_last_write);
            i++;
        }
        move(i + off, 0);
        outs("──────────────────────"
             "─────────────────");
        if (PAGER_UI_IS(PAGER_UI_NEW))
            while (i++ <= water[0].count) {
                move(i + off, 0);
                clrtoeol();
            }
    }
    t_display_new_flag = 0;
}

int
t_display(void) {
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
t_pager(void)
{
    currutmp->pager = (currutmp->pager + 1) % PAGER_MODES;
    return 0;
}

/* ----------------------------------------------------- */
/* Pager key handling (Ctrl-R)                           */
/* ----------------------------------------------------- */

static int
pager_handle_ctrl_u(int ch)
{
    if (!is_login_ready || !currutmp ||
        !HasUserPerm(PERM_BASIC) || HasUserPerm(PERM_VIOLATELAW))
        return ch;
    if (currutmp->mode == EDITING ||
        currutmp->mode == LUSERS  ||
        !currutmp->mode) {
        return ch;
    } else {
        screen_backup_t old_screen;
        int             my_newfd;

        scr_dump(&old_screen);
        my_newfd = vkey_detach();

        t_users();

        vkey_attach(my_newfd);
        scr_restore(&old_screen);
    }
    return KEY_INCOMPLETE;
}

static int
pager_handle_ctrl_r_ofo(int ch)
{
    int my_newfd;
    screen_backup_t old_screen;

    if (!currutmp->msgs[0].pid ||
        wmofo != NOTREPLYING)
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
        t_display_new();
        return KEY_INCOMPLETE;
    }
    else if (watermode == 0 &&
             currutmp->mode == 0 &&
             (currutmp->chatid[0] == 2 || currutmp->chatid[0] == 3) &&
             water_which->count != 0)
    {
        // Press Ctrl-R for the "2nd" time.
        watermode = 1;
        t_display_new();
        return KEY_INCOMPLETE;
    }
    else if (watermode == -1 &&
             currutmp->msgs[0].pid)
    {
        // Press Ctrl-R for the "1st" time (and already received a message for reply)
        screen_backup_t old_screen;
        int             my_newfd;
        scr_dump(&old_screen);

        my_newfd = vkey_detach();
        show_call_in(0, 0);
        watermode = 0;
        if (!HAS_ANGEL) {
            my_write(currutmp->msgs[0].pid, "水球丟過去： ",
                     currutmp->msgs[0].userid, WATERBALL_GENERAL, NULL);
        } else {
            switch (currutmp->msgs[0].msgmode) {
            case MSGMODE_TALK:
            case MSGMODE_WRITE:
            case MSGMODE_ALOHA:
                my_write(currutmp->msgs[0].pid, "水球丟過去： ",
                         currutmp->msgs[0].userid, WATERBALL_GENERAL, NULL);
                break;
            case MSGMODE_FROMANGEL:
                my_write(currutmp->msgs[0].pid, "再問一次： ",
                         currutmp->msgs[0].userid, WATERBALL_ANGEL, NULL);
                break;
            case MSGMODE_TOANGEL:
                my_write(currutmp->msgs[0].pid, "回答小主人： ",
                         currutmp->msgs[0].userid, WATERBALL_ANSWER, NULL);
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
pager_ofo_key_hook(int ch)
{
    switch (ch)
    {
    case Ctrl('U'):
        return pager_handle_ctrl_u(ch);

    case Ctrl('R'):
        return pager_handle_ctrl_r_ofo(ch);
    }
    return ch;
}

static int
pager_orig_key_hook(int ch)
{
    switch (ch)
    {
    case Ctrl('U'):
        return pager_handle_ctrl_u(ch);

    case Ctrl('R'):
        return pager_handle_ctrl_r_default(ch);

    case KEY_TAB:
        if (watermode <= 0)
            break;

        check_water_init();
        watermode = (watermode + water_which->count)
                % water_which->count + 1;
        t_display_new();
        return KEY_INCOMPLETE;

    case Ctrl('T'):
        if (watermode <= 0)
            break;

        check_water_init();
        if (watermode > 1)
            watermode--;
        else
            watermode = water_which->count;
        t_display_new();
        return KEY_INCOMPLETE;
    }
    return ch;
}

static int
pager_new_key_hook(int ch)
{
    static int water_which_flag = 0;

    switch (ch)
    {
    case Ctrl('U'):
        return pager_handle_ctrl_u(ch);

    case Ctrl('R'):
        return pager_handle_ctrl_r_default(ch);

    case KEY_TAB:
        if (watermode <= 0)
            break;

        check_water_init();
        watermode = (watermode + water_which->count)
                % water_which->count + 1;
        t_display_new();
        return KEY_INCOMPLETE;

    case Ctrl('T'):
        if (watermode <= 0)
            break;

        check_water_init();
        if (watermode > 1)
            watermode--;
        else
            watermode = water_which->count;
        t_display_new();
        return KEY_INCOMPLETE;

    case Ctrl('F'):
        if (watermode <= 0)
            break;

        check_water_init();
        water_which_flag =
                (water_which_flag + 1) % (int)(water_usies + 1);
        if (water_which_flag == 0)
            water_which = &water[0];
        else
            water_which = swater[water_which_flag - 1];
        watermode = 1;
        t_display_new();
        return KEY_INCOMPLETE;

    case Ctrl('G'):
        if (watermode <= 0)
            break;

        check_water_init();
        water_which_flag = (water_which_flag + water_usies) %
                (water_usies + 1);
        if (water_which_flag == 0)
            water_which = &water[0];
        else
            water_which = swater[water_which_flag - 1];

        watermode = 1;
        t_display_new();
        return KEY_INCOMPLETE;
    }
    return ch;
}

static vkey_hook_fn pager_ui_hooks[PAGER_UI_TYPES] = {
    [PAGER_UI_ORIG] = pager_orig_key_hook,
    [PAGER_UI_NEW]  = pager_new_key_hook,
    [PAGER_UI_OFO]  = pager_ofo_key_hook,
};

static int
pager_key_hook(int ch)
{
    if (!currutmp)
        return ch;

    int uitype = cuser.pager_ui_type % PAGER_UI_TYPES;
    if (pager_ui_hooks[uitype])
        return pager_ui_hooks[uitype](ch);

    return ch;
}

void
pager_init_hooks(void)
{
    vkey_register_hook(VKEY_HOOK_PRIO_PAGER, pager_key_hook);
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
    char buf[200];
    int mode = currutmp->msgs[which].msgmode;

    if (HAS_ANGEL && mode == MSGMODE_TOANGEL) {
        SNPRINTF(buf, ANSI_COLOR(1;37;46) "★%s" ANSI_COLOR(37;45)
                 " %s " ANSI_RESET,
                 currutmp->msgs[which].userid,
                 currutmp->msgs[which].last_call_in);
        // I must be an Angel. Let's try to update angel beats info.
        // TODO maybe it's better to move this to "sender".
        angel_notify_activity(currutmp->msgs[which].userid);
    } else {
        SNPRINTF(buf, ANSI_COLOR(1;33;46) "★%s" ANSI_COLOR(37;45)
                 " %s " ANSI_RESET, currutmp->msgs[which].userid,
                 currutmp->msgs[which].last_call_in);
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

static int
add_history_entry(water_t * w, const msgque_t * msg)
{
    memcpy(&w->msg[w->top], msg, sizeof(msgque_t));
    w->top++;
    // TODO: In Pager_UI_OFO, the ofo_water_scr is hard-coded to 5 rows.
    // We should fix that in the future, but meanwhile msg[5] is for the buffer
    // of user input. We need to refactor all these together.
    w->top %= PAGER_UI_IS(PAGER_UI_OFO) ? 5 : MAX_REVIEW;

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
    if (!PAGER_UI_IS(PAGER_UI_ORIG)) {
        water_t *target_slot = swater_get_slot(msg);
        add_history_entry(target_slot, msg);
    }

    /* 3. Refresh display if user is currently reviewing messages */
    if (watermode > 0 &&
        (water_which == swater[0] || water_which == &water[0])) {
        if (watermode < water_which->count)
            watermode++;
        t_display_new();
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
        for (i = 0; i < msgcount; ++i)
            add_history(&currutmp->msgs[i]);
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
        bell();
        show_call_in(1, 0);
        add_history(&currutmp->msgs[0]);

        refresh();
        currutmp->msgcount = 0;
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
