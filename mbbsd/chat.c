/* $Id$ */
#include "bbs.h"

#define STOP_LINE (t_lines-3)
static int      chatline;
static FILE    *flog;
static void
printchatline(char *str)
{
    move(chatline, 0);
    if (*str == '>' && !PERM_HIDE(currutmp))
	return;
    else if (chatline < STOP_LINE - 1)
	chatline++;
    else {
	region_scroll_up(2, STOP_LINE - 2);
	move(STOP_LINE - 2, 0);
    }
    outs(str);
    outc('\n');
    outs("→");

    if (flog)
	fprintf(flog, "%s\n", str);
}

static void
chat_clear()
{
    for (chatline = 2; chatline < STOP_LINE; chatline++) {
	move(chatline, 0);
	clrtoeol();
    }
    move(b_lines, 0);
    clrtoeol();
    move(chatline = 2, 0);
    outs("→");
}

static void
print_chatid(char *chatid)
{
    move(b_lines - 1, 0);
    clrtoeol();
    outs(chatid);
    outc(':');
}

static int
chat_send(int fd, char *buf)
{
    int             len;
    char            genbuf[200];

    snprintf(genbuf, sizeof(genbuf), "%s\n", buf);
    len = strlen(genbuf);
    return (send(fd, genbuf, len, 0) == len);
}

static int
chat_recv(int fd, char chatroom[IDLEN], char *chatid)
{
    static char     buf[128];
    static int      bufstart = 0;
    int             c, len;
    char           *bptr;

    len = sizeof(buf) - bufstart - 1;
    if ((c = recv(fd, buf + bufstart, len, 0)) <= 0)
	return -1;
    c += bufstart;

    bptr = buf;
    while (c > 0) {
	len = strlen(bptr) + 1;
	if (len > c && (unsigned)len < (sizeof(buf)/ 2) )
	    break;

	if (*bptr == '/') {
	    switch (bptr[1]) {
	    case 'c':
		chat_clear();
		break;
	    case 'n':
		strncpy(chatid, bptr + 2, 8);
		print_chatid(chatid);
		clrtoeol();
		break;
	    case 'r':
		strncpy(chatroom, bptr + 2, IDLEN - 1);
		break;
	    case 't':
		move(0, 0);
		clrtoeol();
		prints("\033[1;37;46m 談天室 [%-12s] \033[45m 話題：%-48s\033[m",
		       chatroom, bptr + 2);
	    }
	} else
	    printchatline(bptr);

	c -= len;
	bptr += len;
    }

    if (c > 0) {
	memmove(buf, bptr, sizeof(buf)-(bptr-buf));
	bufstart = len - 1;
    } else
	bufstart = 0;
    return 0;
}

static int
printuserent(userinfo_t * uentp)
{
    static char     uline[80];
    static int      cnt;
    char            pline[30];

    if (!uentp) {
	if (cnt)
	    printchatline(uline);
	bzero(uline, sizeof(uline));
	cnt = 0;
	return 0;
    }
    if (!HAS_PERM(PERM_SYSOP) && !HAS_PERM(PERM_SEECLOAK) && uentp->invisible)
	return 0;

    snprintf(pline, sizeof(pline), "%-13s%c%-10s ", uentp->userid,
	     uentp->invisible ? '#' : ' ',
	     modestring(uentp, 1));
    if (cnt < 2)
	strcat(pline, "│");
    strcat(uline, pline);
    if (++cnt == 3) {
	printchatline(uline);
	memset(uline, 0, 80);
	cnt = 0;
    }
    return 0;
}

static void
chathelp(char *cmd, char *desc)
{
    char            buf[STRLEN];

    snprintf(buf, sizeof(buf), "  %-20s- %s", cmd, desc);
    printchatline(buf);
}

static void
chat_help(char *arg)
{
    if (strstr(arg, " op")) {
	printchatline("談天室管理員專用指令");
	chathelp("[/f]lag [+-][ls]", "設定鎖定、秘密狀態");
	chathelp("[/i]nvite <id>", "邀請 <id> 加入談天室");
	chathelp("[/k]ick <id>", "將 <id> 踢出談天室");
	chathelp("[/o]p <id>", "將 Op 的權力轉移給 <id>");
	chathelp("[/t]opic <text>", "換個話題");
	chathelp("[/w]all", "廣播 (站長專用)");
    } else {
	chathelp("[//]help", "MUD-like 社交動詞");
	chathelp("[/.]help", "chicken 鬥雞用指令");
	chathelp("[/h]elp op", "談天室管理員專用指令");
	chathelp("[/a]ct <msg>", "做一個動作");
	chathelp("[/b]ye [msg]", "道別");
	chathelp("[/c]lear", "清除螢幕");
	chathelp("[/j]oin <room>", "建立或加入談天室");
	chathelp("[/l]ist [room]", "列出談天室使用者");
	chathelp("[/m]sg <id> <msg>", "跟 <id> 說悄悄話");
	chathelp("[/n]ick <id>", "將談天代號換成 <id>");
	chathelp("[/p]ager", "切換呼叫器");
	chathelp("[/q]uery", "查詢網友");
	chathelp("[/r]oom", "列出一般談天室");
	chathelp("[/u]sers", "列出站上使用者");
	chathelp("[/w]ho", "列出本談天室使用者");
	chathelp("[/w]hoin <room>", "列出談天室<room> 的使用者");
    }
}

static void
chat_date()
{
    char            genbuf[200];

    snprintf(genbuf, sizeof(genbuf),
	     "◆ " BBSNAME "標準時間: %s", Cdate(&now));
    printchatline(genbuf);
}

static void
chat_pager()
{
    char            genbuf[200];

    char           *msgs[] = {"關閉", "打開", "拔掉", "防水", "好友"};
    snprintf(genbuf, sizeof(genbuf), "◆ 您的呼叫器:[%s]",
	     msgs[currutmp->pager = (currutmp->pager + 1) % 5]);
    printchatline(genbuf);
}

static void
chat_query(char *arg)
{
    char           *uid;
    int             tuid;

    printchatline("");
    strtok(arg, str_space);
    if ((uid = strtok(NULL, str_space)) && (tuid = getuser(uid))) {
	char            buf[128], *ptr;
	FILE           *fp;

	snprintf(buf, sizeof(buf), "%s(%s) 共上站 %d 次，發表過 %d 篇文章",
		 xuser.userid, xuser.username,
		 xuser.numlogins, xuser.numposts);
	printchatline(buf);

	snprintf(buf, sizeof(buf),
		 "最近(%s)從[%s]上站", Cdate(&xuser.lastlogin),
		(xuser.lasthost[0] ? xuser.lasthost : "(不詳)"));
	printchatline(buf);

	sethomefile(buf, xuser.userid, fn_plans);
	if ((fp = fopen(buf, "r"))) {
	    tuid = 0;
	    while (tuid++ < MAX_QUERYLINES && fgets(buf, 128, fp)) {
		if ((ptr = strchr(buf, '\n')))
		    ptr[0] = '\0';
		printchatline(buf);
	    }
	    fclose(fp);
	}
    } else
	printchatline(err_uid);
}

static void
chat_users()
{
    printchatline("");
    printchatline("【 " BBSNAME "的遊客列表 】");
    printchatline(msg_shortulist);

    if (apply_ulist(printuserent) == -1)
	printchatline("空無一人");
    printuserent(NULL);
}

typedef struct chat_command_t {
    char           *cmdname;	/* Chatroom command length */
    void            (*cmdfunc) ();	/* Pointer to function */
}               chat_command_t;

const static chat_command_t chat_cmdtbl[] = {
    {"help", chat_help},
    {"clear", chat_clear},
    {"date", chat_date},
    {"pager", chat_pager},
    {"query", chat_query},
    {"users", chat_users},
    {NULL, NULL}
};

static int
chat_cmd_match(char *buf, char *str)
{
    while (*str && *buf && !isspace(*buf))
	if (tolower(*buf++) != *str++)
	    return 0;
    return 1;
}

static int
chat_cmd(char *buf, int fd)
{
    int             i;

    if (*buf++ != '/')
	return 0;

    for (i = 0; chat_cmdtbl[i].cmdname; i++) {
	if (chat_cmd_match(buf, chat_cmdtbl[i].cmdname)) {
	    chat_cmdtbl[i].cmdfunc(buf);
	    return 1;
	}
    }
    return 0;
}

#define MAXLASTCMD 6
static int      chatid_len = 10;

int
t_chat()
{
    char     chatroom[IDLEN];/* Chat-Room Name */
    char            inbuf[80], chatid[20], lastcmd[MAXLASTCMD][80], *ptr = "";
    struct sockaddr_in sin;
    struct hostent *h;
    int             cfd, cmdpos, ch;
    int             currchar;
    int             newmail;
    int             chatting = YEA;
    char            fpath[80];

    outs("                     驅車前往 請梢候........         ");
    if (!(h = gethostbyname("localhost"))) {
	perror("gethostbyname");
	return -1;
    }
    memset(&sin, 0, sizeof sin);
#ifdef __FreeBSD__
    sin.sin_len = sizeof(sin);
#endif
    sin.sin_family = PF_INET;
    memcpy(&sin.sin_addr, h->h_addr, h->h_length);
    sin.sin_port = htons(NEW_CHATPORT);
    cfd = socket(sin.sin_family, SOCK_STREAM, 0);
    if (connect(cfd, (struct sockaddr *) & sin, sizeof sin) != 0) {
	outs("\n              "
		"哇! 沒人在那邊耶...要有那地方的人先去開門啦!...");
	system("bin/xchatd");
	pressanykey();
	close(cfd);
	return -1;
    }

    while (1) {
	getdata(b_lines - 1, 0, "請輸入聊天代號：", chatid, 9, DOECHO);
	if(!chatid[0])
	    strlcpy(chatid, cuser.userid, sizeof(chatid));
	chatid[8] = '\0';
	/*
	 * 新格式:    /! UserID ChatID Password
	 */
	snprintf(inbuf, sizeof(inbuf), "/! %s %s %s",
		cuser.userid, chatid, cuser.passwd);
	chat_send(cfd, inbuf);
	if (recv(cfd, inbuf, 3, 0) != 3) {
	    close(cfd);
	    return 0;
	}
	if (!strcmp(inbuf, CHAT_LOGIN_OK))
	    break;
	else if (!strcmp(inbuf, CHAT_LOGIN_EXISTS))
	    ptr = "這個代號已經有人用了";
	else if (!strcmp(inbuf, CHAT_LOGIN_INVALID))
	    ptr = "這個代號是錯誤的";
	else if (!strcmp(inbuf, CHAT_LOGIN_BOGUS))
	    ptr = "請勿派遣分身進入聊天室 !!";

	move(b_lines - 2, 0);
	outs(ptr);
	clrtoeol();
	bell();
    }

    add_io(cfd, 0);

    newmail = currchar = 0;
    cmdpos = -1;
    memset(lastcmd, 0, sizeof(lastcmd));

    setutmpmode(CHATING);
    currutmp->in_chat = YEA;
    strlcpy(currutmp->chatid, chatid, sizeof(currutmp->chatid));

    clear();
    chatline = 2;

    move(STOP_LINE, 0);
    outs(msg_seperator);
    move(STOP_LINE, 60);
    outs(" /help 查詢指令 ");
    move(1, 0);
    outs(msg_seperator);
    print_chatid(chatid);
    memset(inbuf, 0, sizeof(inbuf));

    setuserfile(fpath, "chat_XXXXXX");
    flog = fdopen(mkstemp(fpath), "w");

    while (chatting) {
	move(b_lines - 1, currchar + chatid_len);
	ch = igetch();

	switch (ch) {
	case KEY_DOWN:
	    cmdpos += MAXLASTCMD - 2;
	case KEY_UP:
	    cmdpos++;
	    cmdpos %= MAXLASTCMD;
	    strlcpy(inbuf, lastcmd[cmdpos], sizeof(inbuf));
	    move(b_lines - 1, chatid_len);
	    clrtoeol();
	    outs(inbuf);
	    currchar = strlen(inbuf);
	    continue;
	case KEY_LEFT:
	    if (currchar)
		--currchar;
	    continue;
	case KEY_RIGHT:
	    if (inbuf[currchar])
		++currchar;
	    continue;
	}

	if (!newmail && currutmp->mailalert) {
	    newmail = 1;
	    printchatline("◆ 噹！郵差又來了...");
	}
	if (ch == I_OTHERDATA) {/* incoming */
	    if (chat_recv(cfd, chatroom, chatid) == -1) {
		chatting = chat_send(cfd, "/b");
		break;
	    }
	} else if (isprint2(ch)) {
	    if (currchar < 68) {
		if (inbuf[currchar]) {	/* insert */
		    int             i;

		    for (i = currchar; inbuf[i] && i < 68; i++);
		    inbuf[i + 1] = '\0';
		    for (; i > currchar; i--)
			inbuf[i] = inbuf[i - 1];
		} else		/* append */
		    inbuf[currchar + 1] = '\0';
		inbuf[currchar] = ch;
		move(b_lines - 1, currchar + chatid_len);
		outs(&inbuf[currchar++]);
	    }
	} else if (ch == '\n' || ch == '\r') {
	    if (*inbuf) {
		chatting = chat_cmd(inbuf, cfd);
		if (chatting == 0)
		    chatting = chat_send(cfd, inbuf);
		if (!strncmp(inbuf, "/b", 2))
		    break;

		for (cmdpos = MAXLASTCMD - 1; cmdpos; cmdpos--)
		    strlcpy(lastcmd[cmdpos],
			    lastcmd[cmdpos - 1], sizeof(lastcmd[cmdpos]));
		strlcpy(lastcmd[0], inbuf, sizeof(lastcmd[0]));

		inbuf[0] = '\0';
		currchar = 0;
		cmdpos = -1;
	    }
	    print_chatid(chatid);
	    move(b_lines - 1, chatid_len);
	} else if (ch == Ctrl('H') || ch == '\177') {
	    if (currchar) {
		currchar--;
		inbuf[69] = '\0';
		memcpy(&inbuf[currchar], &inbuf[currchar + 1], 69 - currchar);
		move(b_lines - 1, currchar + chatid_len);
		clrtoeol();
		outs(&inbuf[currchar]);
	    }
	} else if (ch == Ctrl('Z') || ch == Ctrl('Y')) {
	    inbuf[0] = '\0';
	    currchar = 0;
	    print_chatid(chatid);
	    move(b_lines - 1, chatid_len);
	} else if (ch == Ctrl('C')) {
	    chat_send(cfd, "/b");
	    break;
	} else if (ch == Ctrl('D')) {
	    if ((size_t)currchar < strlen(inbuf)) {
		inbuf[69] = '\0';
		memcpy(&inbuf[currchar], &inbuf[currchar + 1], 69 - currchar);
		move(b_lines - 1, currchar + chatid_len);
		clrtoeol();
		outs(&inbuf[currchar]);
	    }
	} else if (ch == Ctrl('K')) {
	    inbuf[currchar] = 0;
	    move(b_lines - 1, currchar + chatid_len);
	    clrtoeol();
	} else if (ch == Ctrl('A')) {
	    currchar = 0;
	} else if (ch == Ctrl('E')) {
	    currchar = strlen(inbuf);
	} else if (ch == Ctrl('I')) {
	    screenline_t   *screen0 = calloc(t_lines, sizeof(screenline_t));

	    memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));
	    add_io(0, 0);
	    t_idle();
	    memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
	    free(screen0);
	    redoscr();
	    add_io(cfd, 0);
	} else if (ch == Ctrl('Q')) {
	    print_chatid(chatid);
	    move(b_lines - 1, chatid_len);
	    outs(inbuf);
	    continue;
	}
    }

    close(cfd);
    add_io(0, 0);
    currutmp->in_chat = currutmp->chatid[0] = 0;

    if (flog) {
	char            ans[4];

	fclose(flog);
	more(fpath, NA);
	getdata(b_lines - 1, 0, "清除(C) 移至備忘錄(M) (C/M)?[C]",
		ans, sizeof(ans), LCECHO);
	if (*ans == 'm') {
	    fileheader_t    mymail;
	    char            title[128];
	    char            genbuf[200];

	    sethomepath(genbuf, cuser.userid);
	    stampfile(genbuf, &mymail);
	    mymail.filemode = FILE_READ ;
	    strlcpy(mymail.owner, "[備.忘.錄]", sizeof(mymail.owner));
	    strlcpy(mymail.title, "會議\033[1;33m記錄\033[m", sizeof(mymail.title));
	    sethomedir(title, cuser.userid);
	    append_record(title, &mymail, sizeof(mymail));
	    Rename(fpath, genbuf);
	} else
	    unlink(fpath);
    }
    return 0;
}
