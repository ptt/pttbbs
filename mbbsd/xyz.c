/* $Id$ */
#include "bbs.h"

int
x_boardman(void)
{
    more("etc/topboardman", YEA);
    return 0;
}

int
x_user100(void)
{
    more("etc/topusr100", YEA);
    return 0;
}

int
x_history(void)
{
    more("etc/history", YEA);
    return 0;
}

#ifdef HAVE_X_BOARDS
static int
x_boards(void)
{
    more("etc/topboard.tmp", YEA);
    return 0;
}
#endif

int
x_issue(void)
{
    more("etc/day", YEA);
    return 0;
}

int
x_week(void)
{
    more("etc/week", YEA);
    return 0;
}

int
x_today(void)
{
    more("etc/today", YEA);
    return 0;
}

int
x_yesterday(void)
{
    more("etc/yesterday", YEA);
    return 0;
}

int
x_login(void)
{
    more("etc/Welcome_login.0", YEA);
    return 0;
}

int
Goodbye(void)
{
    char            genbuf[STRLEN];

    getdata(b_lines - 1, 0, "您確定要離開【 " BBSNAME " 】嗎(Y/N)？[N] ",
	    genbuf, 3, LCECHO);

    if (*genbuf != 'y')
	return 0;

    adbanner_goodbye();
    show_80x24_screen("etc/Logout");
    {
	int diff = (now - login_start_time) / 60;
	snprintf(genbuf, sizeof(genbuf), "此次停留時間: %d 小時 %2d 分",
		diff / 60, diff % 60);
    }
    if(!HasUserPerm(PERM_LOGINOK))
	vmsg("尚未完成註冊。如要提昇權限請參考本站公佈欄辦理註冊");
    else
	vmsg(genbuf);

    u_exit("EXIT ");
    return QUIT;
}
