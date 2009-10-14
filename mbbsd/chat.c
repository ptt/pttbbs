/* $Id$ */
#include "bbs.h"

#define STOP_LINE (t_lines-3)
static int      chatline;
static FILE    *flog;
static void
printchatline(const char *str)
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
chat_clear(char *unused GCC_UNUSED)
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
print_footer()
{
    vs_footer("【談天室】",
	    " (PgUp/PgDn)回顧聊天記錄 (Ctrl-Z)快速切換 (Ctrl-C)離開聊天室");
}

static void
print_chatid(const char *chatid)
{
    move(b_lines - 1, 0);
    clrtobot();
    outs(chatid);
    outc(':');
    print_footer();
}

static int
chat_send(int fd, const char *buf)
{
    int             len;
    char            genbuf[200];

    len = snprintf(genbuf, sizeof(genbuf), "%s\n", buf);
    return (send(fd, genbuf, len, 0) == len);
}

struct ChatBuf {
    char buf[128];
    int bufstart;
};

static int
chat_recv(struct ChatBuf *cb, int fd, char *chatroom, char *chatid, size_t chatid_size)
{
    int             c, len;
    char           *bptr;

    len = sizeof(cb->buf) - cb->bufstart - 1;
    if ((c = recv(fd, cb->buf + cb->bufstart, len, 0)) <= 0)
	return -1;
    c += cb->bufstart;

    bptr = cb->buf;
    while (c > 0) {
	len = strlen(bptr) + 1;
	if (len > c && (unsigned)len < (sizeof(cb->buf)/ 2) )
	    break;

	if (*bptr == '/') {
	    switch (bptr[1]) {
	    case 'c':
		chat_clear(NULL);
		break;
	    case 'n':
		strlcpy(chatid, bptr + 2, chatid_size);
		print_chatid(chatid);
		break;
	    case 'r':
		strlcpy(chatroom, bptr + 2, sizeof(chatroom));
		break;
	    case 't':
		move(0, 0);
		clrtoeol();
		prints(ANSI_COLOR(1;37;46) " 談天室 [%-12s] " ANSI_COLOR(45) " 話題：%-48s" ANSI_RESET,
		       chatroom, bptr + 2);
	    }
	} else
	    printchatline(bptr);

	c -= len;
	bptr += len;
    }

    if (c > 0) {
	memmove(cb->buf, bptr, sizeof(cb->buf)-(bptr-cb->buf));
	cb->bufstart = len - 1;
    } else
	cb->bufstart = 0;
    return 0;
}

static void
chathelp(const char *cmd, const char *desc)
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
	chathelp(" /ban <userid>", "拒絕 <userid> 再次進入此聊天室 (加入黑名單)");
	chathelp(" /unban <userid>", "把 <userid> 移出黑名單");
    } else {
	chathelp(" /help op", "談天室管理員專用指令");
	chathelp("[//]help", "MUD-like 社交動詞");
	chathelp("[/a]ct <msg>", "做一個動作");
	chathelp("[/b]ye [msg]", "道別");
	chathelp("[/c]lear", "清除螢幕");
	chathelp("[/j]oin <room>", "建立或加入談天室");
	chathelp("[/l]ist [room]", "列出談天室使用者");
	chathelp("[/m]sg <id> <msg>", "跟 <id> 說悄悄話");
	chathelp("[/n]ick <id>", "將談天代號換成 <id>");
	chathelp("[/p]ager", "切換呼叫器");
	chathelp("[/q]uery <id>", "查詢網友");
	chathelp("[/r]oom ", "列出一般談天室");
	chathelp("[/w]ho", "列出本談天室使用者");
	chathelp(" /whoin <room>", "列出談天室<room> 的使用者");
	chathelp(" /ignore <userid>", "忽略指定使用者的訊息");
	chathelp(" /unignore <userid>", "停止忽略指定使用者的訊息");
    }
}

static void
chat_date(char *unused GCC_UNUSED)
{
    char            genbuf[200];

    snprintf(genbuf, sizeof(genbuf),
	     "◆ " BBSNAME "標準時間: %s", Cdate(&now));
    printchatline(genbuf);
}

static void
chat_pager(char *unused GCC_UNUSED)
{
    char            genbuf[200];

    char           *msgs[PAGER_MODES] = {
	/* Ref: please match PAGER* in modes.h */
	"關閉", "打開", "拔掉", "防水", "好友"
    };

    snprintf(genbuf, sizeof(genbuf), "◆ 您的呼叫器:[%s]",
	     msgs[currutmp->pager = (currutmp->pager + 1) % PAGER_MODES]);
    printchatline(genbuf);
}

static void
chat_query(char *arg)
{
    char           *uid;
    int             tuid;
    userec_t        xuser;
    char *strtok_pos;

    printchatline("");
    strtok_r(arg, str_space, &strtok_pos);
    if ((uid = strtok_r(NULL, str_space, &strtok_pos)) && (tuid = getuser(uid, &xuser))) {
	char            buf[ANSILINELEN], *ptr;
	FILE           *fp;

	snprintf(buf, sizeof(buf), 
		"%s(%s) " STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY "，發表過 %d 篇文章",
		 xuser.userid, xuser.nickname,
		 xuser.numlogindays, xuser.numposts);
	printchatline(buf);

	snprintf(buf, sizeof(buf),
		 "最近(%s)從[%s]上站", 
		 Cdate(xuser.lastseen ? &xuser.lastseen : &xuser.lastlogin),
		(xuser.lasthost[0] ? xuser.lasthost : "(不詳)"));
	printchatline(buf);

	sethomefile(buf, xuser.userid, fn_plans);
	if ((fp = fopen(buf, "r"))) {
	    tuid = 0;
	    while (tuid++ < MAX_QUERYLINES && fgets(buf, sizeof(buf), fp)) {
		if ((ptr = strchr(buf, '\n')))
		    ptr[0] = '\0';
		printchatline(buf);
	    }
	    fclose(fp);
	}
    } else
	printchatline(err_uid);
}

typedef struct chat_command_t {
    char           *cmdname;	/* Chatroom command length */
    void            (*cmdfunc) (char *);	/* Pointer to function */
}               chat_command_t;

static const chat_command_t chat_cmdtbl[] = {
    {"help", chat_help},
    {"clear", chat_clear},
    {"date", chat_date},
    {"pager", chat_pager},
    {"query", chat_query},
    {NULL, NULL}
};

static int
chat_cmd_match(const char *buf, const char *str)
{
    while (*str && *buf && !isspace((int)*buf))
	if (tolower(*buf++) != *str++)
	    return 0;
    return 1;
}

static int
chat_cmd(char *buf, int fd GCC_UNUSED)
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

typedef struct {
    struct ChatBuf *chatbuf;
    int    cfd;
    char  *chatroom;
    char  *chatid;
    int	  *chatting;
    char  *logfpath;
} ChatCbParam;

static int
_vgetcb_peek(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance)
{
    ChatCbParam *p = (ChatCbParam*) instance;
    assert(p);

    switch (key) {
	case I_OTHERDATA: // incoming
	    // XXX why 9? I don't know... just copied from old code.
	    if (chat_recv(p->chatbuf, p->cfd, p->chatroom, p->chatid, 9) == -1) {
		chat_send(p->cfd, "/b");
		*(p->chatting) = 0;
		return VGETCB_ABORT;
	    }
	    return VGETCB_NEXT;

	case Ctrl('C'):
	    chat_send(p->cfd, "/b");
	    *(p->chatting) = 0;
	    return VGETCB_ABORT;

	case Ctrl('I'):
	    {
		VREFSCR scr = vscr_save();
		add_io(0, 0);
		t_idle();
		vscr_restore(scr);
		add_io(p->cfd, 0);
	    }
	    return VGETCB_NEXT;

	case KEY_PGUP:
	case KEY_PGDN:
	    if (flog)
	    {
		VREFSCR scr = vscr_save();
		add_io(0, 0);

		fflush(flog);
		more(p->logfpath, YEA);

		vscr_restore(scr);
		add_io(p->cfd, 0);
	    }
	    return VGETCB_NEXT;

	    // Support ZA because chat is mostly independent and secure.
	case Ctrl('Z'):
	    {
		int za = 0;
		VREFCUR cur = vcur_save();
		add_io(0, 0);
		za = ZA_Select();
		print_footer();
		vcur_restore(cur);
		add_io(p->cfd, 0);
		if (za)
		    return VGETCB_ABORT;
		return VGETCB_NEXT;
	    }
    }
    return VGETCB_NONE;
}

static int      chatid_len = 10;

int
t_chat(void)
{
    static time4_t lastEnter = 0;

    char     chatroom[IDLEN+1] = "";/* Chat-Room Name */
    char            inbuf[80], chatid[20] = "", *ptr = "";
    char	    hasnewmail = 0;
    char            fpath[PATHLEN];
    int             cfd;
    int             chatting = YEA;
    struct ChatBuf  chatbuf;
    ChatCbParam	    vgetparam = {0};

    if(HasUserPerm(PERM_VIOLATELAW))
    {
       vmsg("請先繳罰單才能使用聊天室!");
       return -1;
    }

    if (!HasUserPerm(PERM_CHAT))
	return -1;

    syncnow();

#ifdef CHAT_GAPMINS
    if ((now - lastEnter)/60 < CHAT_GAPMINS)
    {
       vmsg("您才剛離開聊天室，裡面正在整理中。請稍後再試。");
       return 0;
    }
#endif

#ifdef CHAT_REGDAYS
    if ((now - cuser.firstlogin)/DAY_SECONDS < CHAT_REGDAYS)
    {
	int i = CHAT_REGDAYS - (now-cuser.firstlogin)/DAY_SECONDS +1;
	vmsgf("您還不夠資深喔 (再等 %d 天吧)", i);
	return 0;
    }
#endif

    memset(&chatbuf, 0, sizeof(chatbuf));
    outs("                     驅車前往 請梢候........         ");

    cfd = toconnect(XCHATD_ADDR);
    if (cfd < 0) {
	outs("\n              "
	     "哇! 沒人在那邊耶...要有那地方的人先去開門啦!...");
	system("bin/xchatd");
	pressanykey();
	return -1;
    }

    while (1) {
	getdata(b_lines - 1, 0, "請輸入想使用的聊天暱稱：", chatid, 9, DOECHO);
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
           vmsg("系統錯誤。");
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
    syncnow();
    lastEnter = now;

    add_io(cfd, 0);

    setutmpmode(CHATING);
    currutmp->in_chat = YEA;
    strlcpy(currutmp->chatid, chatid, sizeof(currutmp->chatid));

    clear();
    chatline = 2;

    move(STOP_LINE, 0);
    outs(msg_seperator);
    move(STOP_LINE, 56);
    outs(" /h 查詢指令  /b 離開 ");
    move(1, 0);
    outs(msg_seperator);
    print_chatid(chatid);
    memset(inbuf, 0, sizeof(inbuf));

    setuserfile(fpath, "chat_XXXXXX");
    flog = fdopen(mkstemp(fpath), "w");

    // set up vgetstring callback parameter
    VGET_CALLBACKS vge = { _vgetcb_peek };

    vgetparam.chatbuf = &chatbuf;
    vgetparam.cfd = cfd;
    vgetparam.chatid = chatid;
    vgetparam.chatroom = chatroom;
    vgetparam.chatting = &chatting;
    vgetparam.logfpath = fpath;

    while (chatting) {
	if (ZA_Waiting())
	{
	    // process ZA
	    VREFSCR scr = vscr_save();
	    add_io(0, 0);
	    ZA_Enter();
	    vscr_restore(scr);
	    add_io(cfd, 0);
	}
	print_chatid(chatid);
	move(b_lines-1, chatid_len);

	// chatid_len = 10, quote(:) occupies 1, so 79-11=68
	vgetstring(inbuf, 68, VGET_TRANSPARENT, "", &vge, &vgetparam);

	// quick check for end flag or exit command.
	if (!chatting)
	    break;

	if (strncasecmp(inbuf, "/b", 2) == 0)
	{
	    // cases: /b, /bye, "/b "
	    // !cases: /ban
	    if (tolower(inbuf[2]) != 'a')
		break;
	}

	// quick continue for empty input
	if (!*inbuf)
	    continue;

#ifdef EXP_ANTIFLOOD
	{
	    // prevent flooding */
	    static time4_t lasttime = 0;
	    static int flood = 0;

	    syncnow();
	    if (now - lasttime < 3 )
	    {
		// 3 秒內洗半面是不行的 ((25-5)/2)
		if( ++flood > 10 ){
		    // flush all input!
		    drop_input();
		    while (wait_input(1, 0))
		    {
			if (num_in_buf())
			    drop_input();
			else
			    tty_read((unsigned char*)inbuf, sizeof(inbuf));
		    }
		    drop_input();
		    vmsg("請勿大量剪貼或造成洗板面的效果。");
		    // log?
		    sleep(2);
		    continue;
		}
	    } else {
		lasttime = now;
		flood = 0;
	    }
	}
#endif // anti-flood

	// send message to server if possible.
	if (!chat_cmd(inbuf, cfd))
	    chatting = chat_send(cfd, inbuf);

	// print mail message if possible.
	if (ISNEWMAIL(currutmp))
	{
	    if (!hasnewmail)
	    {
		printchatline("◆ 您有未讀的新信件。");
		hasnewmail = 1;
	    }
	} else {
	    if (hasnewmail)
		hasnewmail = 0;
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
	    if (mail_log2id(cuser.userid, "會議記錄",
		    fpath, "[備.忘.錄]", 0, 1) < 0)
		vmsg("備忘錄儲存失敗。");
	}
	unlink(fpath);
    }
    return 0;
}
