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

static void
mail_sysop(void)
{
    FILE           *fp;
    char            genbuf[STRLEN];

    if ((fp = fopen("etc/sysop", "r"))) {
	int             i, j;
	char           *ptr;

	typedef struct sysoplist_t {
	    char            userid[IDLEN + 1];
	    char            duty[40];
	}               sysoplist_t;
	sysoplist_t     sysoplist[9];

	j = 0;
	while (fgets(genbuf, sizeof(genbuf), fp)) {
	    if ((ptr = strchr(genbuf, '\n'))) {
		*ptr = '\0';
		if ((ptr = strchr(genbuf, ':'))) {
		    *ptr = '\0';
		    do {
			i = *++ptr;
		    } while (i == ' ' || i == '\t');
		    if (i) {
			strlcpy(sysoplist[j].userid, genbuf,
				sizeof(sysoplist[j].userid));
			strlcpy(sysoplist[j++].duty, ptr,
				sizeof(sysoplist[j].duty));
		    }
		}
	    }
	}
	fclose(fp);

	move(12, 0);
	clrtobot();
	outs("            編號   站長 ID           權責劃分\n\n");

	for (i = 0; i < j; i++)
	    prints("%15d.   " ANSI_COLOR(1;%d) "%-16s%s" ANSI_COLOR(0) "\n",
		 i + 1, 31 + i % 7, sysoplist[i].userid, sysoplist[i].duty);
	prints("%-14s0.   " ANSI_COLOR(1;%d) "離開" ANSI_COLOR(0) "", "", 31 + j % 7);
	getdata(b_lines - 1, 0, "                   請輸入代碼[0]：",
		genbuf, 4, DOECHO);
	i = genbuf[0] - '0' - 1;
	if (i >= 0 && i < j) {
	    char *suser = sysoplist[i].userid;
	    clear();
	    showplans(suser);
	    do_send(suser, NULL, "mail_sysop");
	}
    }
}

int
m_sysop(void)
{
    setutmpmode(MSYSOP);
    mail_sysop();
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
