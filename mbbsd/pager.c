#include "bbs.h"

/* ----------------------------------------------------- */
/* pager processor (Step 3: Extract ORIG UI hook & Table Dispatch) */
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
    } else
        SNPRINTF(buf, ANSI_COLOR(1;33;46) "★%s" ANSI_COLOR(37;45)
                 " %s " ANSI_RESET, currutmp->msgs[which].userid,
                 currutmp->msgs[which].last_call_in);
    outmsg(buf);

    if (save && mode != MSGMODE_ALOHA) {
        char            genbuf[200];
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
add_history_water(water_t * w, const msgque_t * msg)
{
    memcpy(&w->msg[w->top], msg, sizeof(msgque_t));
    w->top++;
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

static int
add_history(const msgque_t * msg)
{
    int             i = 0, j, waterinit = 0;
    water_t        *tmp;
    check_water_init();
    if (PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW))
        add_history_water(&water[0], msg);
    if (PAGER_UI_IS(PAGER_UI_NEW) || PAGER_UI_IS(PAGER_UI_OFO)) {
        for (i = 0; i < WB_OFO_USER_NUM; i++) {
            if (swater[i] == NULL)
                break;
            if (swater[i]->pid == msg->pid
                && (!HAS_ANGEL ||
                    (is_angel_msgmode(swater[i]->msg[0].msgmode) || is_angel_msgmode(msg->msgmode)
                     ? swater[i]->msg[0].msgmode == msg->msgmode
                     : 1))
                /* When throwing waterball to angel directly */
               )
                break;
        }
        if (i == WB_OFO_USER_NUM) {
            waterinit = 1;
            i = WB_OFO_USER_NUM - 1;
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

        for (j = i; j > 0; j--)
            swater[j] = swater[j - 1];
        swater[0] = tmp;
        add_history_water(swater[0], msg);
    }
    if (PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)) {
        if (watermode > 0 &&
            (water_which == swater[0] || water_which == &water[0])) {
            if (watermode < water_which->count)
                watermode++;
            t_display_new();
        }
    }
    return i;
}

void
write_request(int sig)
{
    int             i, msgcount;

    STATINC(STAT_WRITEREQUEST);
    syncnow();
    check_water_init();
    if (PAGER_UI_IS(PAGER_UI_OFO)) {
        /* 如果目前在回水球模式, 則不可 add_history() ,
           因為會寫 water[], 而使回水球的指標亂掉, 所以分兩種情況考慮.
           sig != 0有水球進, 嗶嗶.
           sig == 0表示沒水球進, 只是把先前還沒寫入 water[].
           */
        static  int     alreadyshow = 0;

        if( sig ){ /* 有水球進 */

            /* 若原本在 REPLYING , 則改成 RECVINREPLYING,
               這樣在回水球完, 會再呼叫一次 write_request(0) */
            if( wmofo == REPLYING )
                wmofo = RECVINREPLYING;

            /* 嗶 */
            for( ; alreadyshow < currutmp->msgcount && alreadyshow < MAX_MSGS
                 ; ++alreadyshow ){
                bell();
                show_call_in(1, alreadyshow);
                refresh();
            }
        }

        /* 看看是不是要把 currutmp->msg 寫回 water[] (by add_history())
           要不在回水球中 (NOTREPLYING) */
        if( wmofo == NOTREPLYING &&
            (msgcount = currutmp->msgcount) > 0 ){
            for( i = 0 ; i < msgcount ; ++i )
                add_history(&currutmp->msgs[i]);
            if( (currutmp->msgcount -= msgcount) < 0 )
                currutmp->msgcount = 0;
            alreadyshow = 0;
        }
    } else {
        if (currutmp->mode != 0 &&
            currutmp->pager != PAGER_OFF &&
            cuser.userlevel != 0 &&
            currutmp->msgcount != 0 &&
            currutmp->mode != TALK &&
            currutmp->mode != EDITING &&
            currutmp->mode != CHATING &&
            currutmp->mode != PAGE &&
            currutmp->mode != IDLE &&
            currutmp->mode != MAILALL && currutmp->mode != MONITOR) {
            char            c0 = currutmp->chatid[0];
            int             currstat0 = currstat;
            unsigned char   mode0 = currutmp->mode;

            currutmp->mode = 0;
            currutmp->chatid[0] = 2;
            currstat = HIT;

            if( (msgcount = currutmp->msgcount) > 0 ){
                for( i = 0 ; i < msgcount ; ++i ){
                    bell();
                    show_call_in(1, 0);
                    add_history(&currutmp->msgs[0]);

                    if( (--currutmp->msgcount) < 0 )
                        i = msgcount; /* force to exit for() */
                    else if( currutmp->msgcount > 0 )
                        memmove(&currutmp->msgs[0],
                                &currutmp->msgs[1],
                                sizeof(msgque_t) * currutmp->msgcount);
                    vkey();
                }
            }

            currutmp->chatid[0] = c0;
            currutmp->mode = mode0;
            currstat = currstat0;
        } else {
            bell();
            show_call_in(1, 0);
            add_history(&currutmp->msgs[0]);

            refresh();
            currutmp->msgcount = 0;
        }
    }
}
