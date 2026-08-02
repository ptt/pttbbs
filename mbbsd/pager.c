#include "bbs.h"

/* ----------------------------------------------------- */
/* pager processor (Step 1: Extract OFO UI hook)        */
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
process_pager_keys(int ch)
{
    static int water_which_flag = 0;
    assert(currutmp);

    if (PAGER_UI_IS(PAGER_UI_OFO))
        return pager_ofo_key_hook(ch);

    switch (ch)
    {
	case Ctrl('U') :
            if (!is_login_ready || !currutmp ||
                !HasUserPerm(PERM_BASIC) || HasUserPerm(PERM_VIOLATELAW))
                return ch;
	    if ( currutmp->mode == EDITING ||
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

	    // TODO 醜死了的 code ，等好心人 refine
	case Ctrl('R'):

	    // non-UFO
	    check_water_init();

	    if (watermode > 0)
	    {
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
		/* 第二次按 Ctrl-R */
		watermode = 1;
		t_display_new();
		return KEY_INCOMPLETE;
	    }
	    else if (watermode == -1 &&
		    currutmp->msgs[0].pid)
	    {
		/* 第一次按 Ctrl-R (必須先被丟過水球) */
		screen_backup_t old_screen;
		int             my_newfd;
		scr_dump(&old_screen);

		/* 如果正在talk的話先不處理對方送過來的封包 (不去select) */
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

		/* 還原螢幕 */
		scr_restore(&old_screen);
		return KEY_INCOMPLETE;
	    }
	    break;

	case KEY_TAB:
	    if (watermode <= 0 ||
		(!PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)))
		break;

	    check_water_init();
	    watermode = (watermode + water_which->count)
		% water_which->count + 1;
	    t_display_new();
	    return KEY_INCOMPLETE;

	case Ctrl('T'):
	    if (watermode <= 0 ||
		!(PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)))
		   break;

	    check_water_init();
	    if (watermode > 1)
		watermode--;
	    else
		watermode = water_which->count;
	    t_display_new();
	    return KEY_INCOMPLETE;

	case Ctrl('F'):
	    if (watermode <= 0 || !PAGER_UI_IS(PAGER_UI_NEW))
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
	    if (watermode <= 0 || !PAGER_UI_IS(PAGER_UI_NEW))
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

int
pager_key_hook(int ch)
{
    if (!currutmp)
        return ch;
    return process_pager_keys(ch);
}

void
pager_init_hooks(void)
{
    vkey_register_hook(VKEY_HOOK_PRIO_PAGER, pager_key_hook);
}
