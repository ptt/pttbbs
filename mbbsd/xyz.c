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
x_birth(void)
{
    more("etc/birth.today", YEA);
    return 0;
}

int
x_weather(void)
{
    more("etc/weather.tmp", YEA);
    return 0;
}

int
x_mrtmap(void)
{
    more("etc/MRT.map", YEA);
	return 0;
}

int
x_stock(void)
{
    more("etc/stock.tmp", YEA);
    return 0;
}

int
x_note(void)
{
    more(fn_note_ans, YEA);
    return 0;
}

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

#ifdef HAVE_INFO
static int
x_program(void)
{
    more("etc/version", YEA);
    return 0;
}
#endif

#ifdef HAVE_LICENSE
static int
x_gpl(void)
{
    more("etc/GPL", YEA);
    return 0;
}
#endif

int
note(void)
{
    char    *fn_note_tmp = "note.tmp";
    char    *fn_note_dat = "note.dat";
    int             total = 0, i, collect, len;
    struct stat     st;
    char            buf[256], buf2[80];
    int             fd, fx;
    FILE           *fp, *foo;

    typedef struct notedata_t {
	time4_t         date;
	char            userid[IDLEN + 1];
	char            nickname[19];
	char            buf[3][80];
    }               notedata_t;
    notedata_t      myitem;

    if (cuser.money < 5) {
	vmsg(ANSI_COLOR(1;41) " 哎呀! 要投五銀才能留言...沒錢耶.." ANSI_RESET);
	return 0;
    }
    setutmpmode(EDNOTE);
    do {
	myitem.buf[0][0] = myitem.buf[1][0] = myitem.buf[2][0] = '\0';
	move(12, 0);
	clrtobot();
	outs("\n投五銀... 嗶... 請留言 (至多三行)，按[Enter]結束");
	for (i = 0; (i < 3) && getdata(16 + i, 0, "：", myitem.buf[i],
				       sizeof(myitem.buf[i]) - 5, DOECHO)
	     && *myitem.buf[i]; i++);
	getdata(b_lines - 1, 0, "(S)儲存 (E)重新來過 (Q)取消？[S] ",
		buf, 3, LCECHO);

	if (buf[0] == 'q' || (i == 0 && *buf != 'e'))
	    return 0;
    } while (buf[0] == 'e');
    demoney(-5);
    strcpy(myitem.userid, cuser.userid);
    strlcpy(myitem.nickname, cuser.nickname, sizeof(myitem.nickname));
    myitem.date = now;

    /* begin load file */
    if ((foo = fopen(".note", "a")) == NULL)
	return 0;

    unlink(fn_note_ans); // remove first to prevent mmap(pmore) crash
    if ((fp = fopen(fn_note_ans, "w")) == NULL) {
	fclose(fp);
	return 0;
    }

    if ((fx = open(fn_note_tmp, O_WRONLY | O_CREAT, 0644)) <= 0) {
	fclose(foo);
	fclose(fp);
	return 0;
    }

    if ((fd = open(fn_note_dat, O_RDONLY)) == -1)
	total = 1;
    else if (fstat(fd, &st) != -1) {
	total = st.st_size / sizeof(notedata_t) + 1;
	if (total > MAX_NOTE)
	    total = MAX_NOTE;
    }
    fputs(ANSI_COLOR(1;31;44) "⊙┬──────────────┤"
	  ANSI_COLOR(37) "酸甜苦辣板" ANSI_COLOR(31) "├──────────────┬⊙"
	  ANSI_RESET "\n", fp);
    collect = 1;

    while (total) {
	snprintf(buf, sizeof(buf), ANSI_COLOR(1;31) "╔┤" ANSI_COLOR(32) " %s " ANSI_COLOR(37) "(%s)",
		myitem.userid, myitem.nickname);
	len = strlen(buf);

	for (i = len; i < 71; i++)
	    strcat(buf, " ");
	snprintf(buf2, sizeof(buf2), " " ANSI_COLOR(1;36) "%.16s" ANSI_COLOR(31) "   ├╗" ANSI_RESET "\n",
		Cdate(&(myitem.date)));
	strcat(buf, buf2);
	fputs(buf, fp);
	if (collect)
	    fputs(buf, foo);
	for (i = 0; i < 3 && *myitem.buf[i]; i++) {
	    fprintf(fp, ANSI_COLOR(1;31) "│" ANSI_RESET "%-74.74s" ANSI_COLOR(1;31) "│" ANSI_RESET "\n",
		    myitem.buf[i]);
	    if (collect)
		fprintf(foo, ANSI_COLOR(1;31) "│" ANSI_RESET "%-74.74s" ANSI_COLOR(1;31) "│" ANSI_RESET "\n",
			myitem.buf[i]);
	}
	fputs(ANSI_COLOR(1;31) "╚┬───────────────────────"
	      "────────────┬╝" ANSI_RESET "\n", fp);

	if (collect) {
	    fputs(ANSI_COLOR(1;31) "╚┬─────────────────────"
		  "──────────────┬╝" ANSI_RESET "\n", foo);
	    fclose(foo);
	    collect = 0;
	}
	write(fx, &myitem, sizeof(myitem));

	if (--total)
	    read(fd, (char *)&myitem, sizeof(myitem));
    }
    fputs(ANSI_COLOR(1;31;44) "⊙┴───────────────────────"
	  "────────────┴⊙" ANSI_RESET "\n", fp);
    fclose(fp);
    close(fd);
    close(fx);
    Rename(fn_note_tmp, fn_note_dat);
    more(fn_note_ans, YEA);
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
	    do_send(suser, NULL);
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

    movie(999999);
    if (cuser.userlevel) {
	getdata(b_lines - 1, 0,
		"(G)隨風而逝 (M)托夢站長 (N)酸甜苦辣流言板？[G] ",
		genbuf, 3, LCECHO);
	if (genbuf[0] == 'm')
	    mail_sysop();
	else if (genbuf[0] == 'n')
	    note();
    }
    clear();


    more("etc/Logout", NA);

    {
	int diff = (now - login_start_time) / 60;
	snprintf(genbuf, sizeof(genbuf), "此次停留時間: %d 小時 %2d 分",
		diff / 60, diff % 60);
    }
    if(!(cuser.userlevel & PERM_LOGINOK))
	vmsg("尚未完成註冊。如要提昇權限請參考本站公佈欄辦理註冊");
    else
	vmsg(genbuf);

    u_exit("EXIT ");
    return QUIT;
}
