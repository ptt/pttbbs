/* $Id: mbbsd.c,v 1.55 2002/09/02 14:57:12 ptt Exp $ */
#include "bbs.h"

#define SOCKET_QLEN 4
#define TH_LOW 100
#define TH_HIGH 120

static void     do_aloha(char *hello);

#if 0
static jmp_buf  byebye;
#endif

static char     remoteusername[40] = "?";

static unsigned char enter_uflag;
static int      use_shell_login_mode = 0;

static struct sockaddr_in xsin;

/* set signal handler, which won't be reset once signal comes */
static void
signal_restart(int signum, void (*handler) (int))
{
    struct sigaction act;
    act.sa_handler = handler;
    memset(&(act.sa_mask), 0, sizeof(sigset_t));
    act.sa_flags = 0;
    sigaction(signum, &act, NULL);
}

static void
start_daemon()
{
    int             n;
    char            buf[80];

    /*
     * More idiot speed-hacking --- the first time conversion makes the C
     * library open the files containing the locale definition and time zone.
     * If this hasn't happened in the parent process, it happens in the
     * children, once per connection --- and it does add up.
     */
    time_t          dummy = time(NULL);
    struct tm      *dummy_time = localtime(&dummy);

    strftime(buf, 80, "%d/%b/%Y:%H:%M:%S", dummy_time);

#ifndef NO_FORK
    if ((n = fork())) {
	exit(0);
    }
#endif

    /* rocker.011018: it's a good idea to close all unexcept fd!! */
    n = getdtablesize();
    while (n)
	close(--n);
    /*
     * rocker.011018: we don't need to remember original tty, so request a
     * new session id
     */
    setsid();

    /*
     * rocker.011018: after new session, we should insure the process is
     * clean daemon
     */
#ifndef NO_FORK
    if ((n = fork())) {
	exit(0);
    }
#endif
}

static void
reapchild(int sig)
{
    int             state, pid;

    while ((pid = waitpid(-1, &state, WNOHANG | WUNTRACED)) > 0);
}

#define BANNER \
"【" BBSNAME "】◎ 台大流行網 ◎(" MYHOSTNAME ") 調幅(" MYIP ") "
/*
 * #define BANNER \ "【" BBSNAME "】◎ 台大流行網 ◎(" MYHOSTNAME ")\r\n"\ "
 * 調幅(" MYIP ") "
 */
/* check load and print approriate banner string in buf */
static int
chkload(char *buf, int length)
{
    char            cpu_load[30];
    int             i;

    i = cpuload(cpu_load);

#ifdef INSCREEN
    if( i > MAX_CPULOAD ){
	strlcpy(buf, BANNER "\r\n系統過載, 請稍後再來\r\n", length);
	return 1;
    }
    buf[0] = 0;
    return 0;
#else
    sprintf(buf, BANNER "%s\r\n",
	    (i > MAX_CPULOAD ? "高負荷量，請稍後再來(請利用port 3000~3010連線)" : ""));
    return i > MAX_CPULOAD ? 1 : 0;
#endif
}

void
log_user(char *msg)
{
    char            filename[200];

    snprintf(filename, sizeof(filename), BBSHOME "/home/%c/%s/USERLOG",
	     cuser.userid[0], cuser.userid);
    log_file(filename, msg);
}


void
log_usies(char *mode, char *mesg)
{
    char            genbuf[200];

    if (!mesg)
	snprintf(genbuf, sizeof(genbuf),
		 cuser.userid[0] ? "%s %s %-12s Stay:%d (%s)" :
		 "%s %s %s Stay:%d (%s)",
		 Cdate(&now), mode, cuser.userid,
		 (int)(now - login_start_time) / 60, cuser.username);
    else
	snprintf(genbuf, sizeof(genbuf),
		 cuser.userid[0] ? "%s %s %-12s %s" : "%s %s %s%s",
		 Cdate(&now), mode, cuser.userid, mesg);
    log_file(FN_USIES, genbuf);

    /* 追蹤使用者 */
    if (HAS_PERM(PERM_LOGUSER))
	log_user(genbuf);
}

static void
setflags(int mask, int value)
{
    if (value)
	cuser.uflag |= mask;
    else
	cuser.uflag &= ~mask;
}

void
u_exit(char *mode)
{
    //userec_t xuser;
    int             diff = (time(0) - login_start_time) / 60;

    /* close fd 0 & a to terminate network */
    close(0);
    close(1);

    reload_money();
    auto_backup();
    setflags(PAGER_FLAG, currutmp->pager != 1);
    setflags(CLOAK_FLAG, currutmp->invisible);

    cuser.invisible = currutmp->invisible;
    cuser.pager = currutmp->pager;
    memcpy(cuser.mind, currutmp->mind, 4);
    setutmpbid(0);
    if (!(HAS_PERM(PERM_SYSOP) && HAS_PERM(PERM_DENYPOST)) &&
	!currutmp->invisible)
	do_aloha("<<下站通知>> -- 我走囉！");

    purge_utmp(currutmp);
    if ((cuser.uflag != enter_uflag) || (currmode & MODE_DIRTY) || diff) {
	if (!diff && cuser.numlogins)
	    cuser.numlogins = --cuser.numlogins;
	/* Leeym 上站停留時間限制式 */
    }
    passwd_update(usernum, &cuser);
    log_usies(mode, NULL);
}

void
system_abort()
{
    if (currmode)
	u_exit("ABORT");

    clear();
    refresh();
    fprintf(stdout, "謝謝光臨, 記得常來喔 !\n");
    exit(0);
}

void
abort_bbs(int sig)
{
    if (currmode)
	u_exit("AXXED");
    exit(0);
}

static void
abort_bbs_debug(int sig)
{
    static int      reentrant = 0;

    if (!reentrant) {
	int             i;
	/* close all file descriptors (including the network connection) */
	for (i = 0; i < 256; ++i)
	    close(i);
	reentrant = 1;
	if (currmode)
	    u_exit("AXXED");
	setproctitle("debug me!(%d)(%s,%d)", sig, cuser.userid, currstat);
	sleep(3600);		/* wait 60 mins for debug */
    }
    exit(0);
}

/* 登錄 BBS 程式 */
static void
mysrand()
{
    srand(time(NULL) + currutmp->pid);	/* 時間跟 pid 當 rand 的 seed */
}

int
dosearchuser(char *userid)
{
    if ((usernum = getuser(userid)))
	memcpy(&cuser, &xuser, sizeof(cuser));
    else
	memset(&cuser, 0, sizeof(cuser));
    return usernum;
}

static void
talk_request(int sig)
{
    bell();
    bell();
    if (currutmp->msgcount) {
	char            buf[200];
	time_t          now = time(0);

	snprintf(buf, sizeof(buf),
		 "\033[33;41m★%s\033[34;47m [%s] %s \033[0m",
		 SHM->uinfo[currutmp->destuip].userid, my_ctime(&now),
		 (currutmp->sig == 2) ? "重要消息廣播！(請Ctrl-U,l查看熱訊記錄)"
		 : "呼叫、呼叫，聽到請回答");
	move(0, 0);
	clrtoeol();
	outs(buf);
	refresh();
    } else {
	unsigned char   mode0 = currutmp->mode;
	char            c0 = currutmp->chatid[0];
	screenline_t   *screen0 = calloc(t_lines, sizeof(screenline_t));

	currutmp->mode = 0;
	currutmp->chatid[0] = 1;
	memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));
	talkreply();
	currutmp->mode = mode0;
	currutmp->chatid[0] = c0;
	memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
	free(screen0);
	redoscr();
    }
}

void
show_call_in(int save, int which)
{
    char            buf[200];
    snprintf(buf, sizeof(buf), "\033[1;33;46m★%s\033[37;45m %s \033[m",
	     currutmp->msgs[which].userid, currutmp->msgs[which].last_call_in);
    move(b_lines, 0);
    clrtoeol();
    refresh();
    outmsg(buf);

    if (save) {
	char            genbuf[200];
	time_t          now;
	if (!fp_writelog) {
	    sethomefile(genbuf, cuser.userid, fn_writelog);
	    fp_writelog = fopen(genbuf, "a");
	}
	if (fp_writelog) {
	    time(&now);
	    fprintf(fp_writelog, "%s \033[0m[%s]\n", buf, Cdatelite(&now));
	}
    }
}

static int
add_history_water(water_t * w, msgque_t * msg)
{
    memcpy(&w->msg[w->top], msg, sizeof(msgque_t));
    w->top++;
    w->top %= WATERMODE(WATER_OFO) ? 5 : MAX_REVIEW;

    if (w->count < MAX_REVIEW)
	w->count++;

    return w->count;
}

static int
add_history(msgque_t * msg)
{
    int             i = 0, j, waterinit = 0;
    water_t        *tmp;
    if (WATERMODE(WATER_ORIG) || WATERMODE(WATER_NEW))
	add_history_water(&water[0], msg);
    if (WATERMODE(WATER_NEW) || WATERMODE(WATER_OFO)) {
	for (i = 0; i < 5 && swater[i]; i++)
	    if (swater[i]->pid == msg->pid)
		break;
	if (i == 5) {
	    waterinit = 1;
	    i = 4;
	    memset(swater[4], 0, sizeof(water_t));
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
    if (WATERMODE(WATER_ORIG) || WATERMODE(WATER_NEW)) {
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
    int             i;

    if (WATERMODE(WATER_OFO)) {
	/*
	 * sig = SIGUSR2 waterball come in 0       flush to water[]  (by
	 * my_write2())
	 */
	if (sig != 0) {
	    if (wmofo == 0)	/* 正在回水球 */
		wmofo = 1;
	    bell();
	    show_call_in(1, currutmp->msgcount - 1);
	    refresh();
	}
	if (sig == 0 ||		/* 回水球的時候又有水球進來, 回完後一次寫回去  */
	    wmofo == -1) {	/* 不在回水球模式                              */
	    do {
		add_history(&currutmp->msgs[0]);
		if (currutmp->msgcount--)
		    for (i = 0; i < currutmp->msgcount; i++)
			currutmp->msgs[i] = currutmp->msgs[i + 1];
	    }
	    while (currutmp->msgcount);
	    currutmp->msgcount = 0;
	}
    } else {
	if (currutmp->mode != 0 &&
	    currutmp->pager != 0 &&
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

	    do {
		bell();
		show_call_in(1, 0);
		igetch();
		currutmp->msgcount--;
		if (currutmp->msgcount >= MAX_MSGS) {
		    /* this causes chaos... jochang */
		    raise(SIGFPE);
		}
		add_history(&currutmp->msgs[0]);
		for (i = 0; i < currutmp->msgcount; i++)
		    currutmp->msgs[i] = currutmp->msgs[i + 1];
	    }
	    while (currutmp->msgcount);
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

static void
multi_user_check()
{
    register userinfo_t *ui;
    register pid_t  pid;
    char            genbuf[3];

    if (HAS_PERM(PERM_SYSOP))
	return;			/* don't check sysops */

    if (cuser.userlevel) {
	sort_utmp();
	if (!(ui = (userinfo_t *) search_ulist(usernum)))
	    return;		/* user isn't logged in */

	pid = ui->pid;
	if (!pid /* || (kill(pid, 0) == -1) */ )
	    return;		/* stale entry in utmp file */

	getdata(b_lines - 1, 0, "您想刪除其他重複的 login (Y/N)嗎？[Y] ",
		genbuf, 3, LCECHO);

	if (genbuf[0] != 'n') {
	    if (pid > 0)
		kill(pid, SIGHUP);
	    log_usies("KICK ", cuser.username);
	} else {
	    if (search_ulistn(usernum, 3) != NULL)
		system_abort();	/* Goodbye(); */
	}
    } else {
	/* allow multiple guest user */
	if (search_ulistn(usernum, 100) != NULL) {
	    outs("\n抱歉，目前已有太多 guest 在站上, 請用new註冊。\n");
	    pressanykey();
	    oflush();
	    exit(1);
	}
    }
}

/* bad login */
static char     str_badlogin[] = "logins.bad";

static void
logattempt(char *uid, char type)
{
    char            fname[40];
    int             fd, len;
    char            genbuf[200];

    snprintf(genbuf, sizeof(genbuf), "%c%-12s[%s] %s@%s\n", type, uid,
	    Cdate(&login_start_time), remoteusername, fromhost);
    len = strlen(genbuf);
    if ((fd = open(str_badlogin, O_WRONLY | O_CREAT | O_APPEND, 0644)) > 0) {
	write(fd, genbuf, len);
	close(fd);
    }
    if (type == '-') {
	snprintf(genbuf, sizeof(genbuf),
		 "[%s] %s\n", Cdate(&login_start_time), fromhost);
	len = strlen(genbuf);
	sethomefile(fname, uid, str_badlogin);
	if ((fd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0644)) > 0) {
	    write(fd, genbuf, len);
	    close(fd);
	}
    }
}

static void
login_query()
{
    char            uid[IDLEN + 1], passbuf[PASSLEN];
    int             attempts;
    char            genbuf[200];
    //attach_SHM();
    resolve_garbage();
    now = time(0);
    attempts = SHM->UTMPnumber;
    if (attempts >= MAX_ACTIVE
#ifdef DYMAX_ACTIVE
	|| (GLOBALVAR[9] > 1000 && attempts >= GLOBALVAR[9] )
#endif
	) {
	++GLOBALVAR[8];
	outs("由於人數太多，請您稍後再來。分散流量請到 ptt.cc 或 ptt2.cc\n");
	refresh();
	exit(1);
    }

#ifdef DEBUG
    move(1, 0);
    prints("debugging mode\ncurrent pid: %d\n", getpid());
#else
    show_file("etc/Welcome", 1, -1, NO_RELOAD);
#endif
    output("1", 1);

    /* hint */

    attempts = 0;
    while (1) {
	if (attempts++ >= LOGINATTEMPTS) {
	    more("etc/goodbye", NA);
	    pressanykey();
	    exit(1);
	}
#ifdef DEBUG
	move(19, 0);
	prints("current pid: %d ", getpid());
#endif
	getdata(20, 0, "請輸入代號，或以[guest]參觀，以[new]註冊：",
		uid, sizeof(uid), DOECHO);
	if (strcasecmp(uid, str_new) == 0) {
#ifdef LOGINASNEW
	    new_register();
	    break;
#else
	    outs("本系統目前無法以 new 註冊, 請用 guest 進入\n");
	    continue;
#endif
	} else if (uid[0] == '\0' || !dosearchuser(uid)) {
	    outs(err_uid);
	} else if (strcmp(uid, STR_GUEST)) {
	    getdata(21, 0, MSG_PASSWD, passbuf, sizeof(passbuf), NOECHO);
	    passbuf[8] = '\0';

	    if (!checkpasswd(cuser.passwd, passbuf)
		 /* || (HAS_PERM(PERM_SYSOP) && !use_shell_login_mode) */ ) {
		logattempt(cuser.userid, '-');
		outs(ERR_PASSWD);
	    } else {
		logattempt(cuser.userid, ' ');
		if (strcasecmp("SYSOP", cuser.userid) == 0)
		    cuser.userlevel = PERM_BASIC | PERM_CHAT | PERM_PAGE |
			PERM_POST | PERM_LOGINOK | PERM_MAILLIMIT |
			PERM_CLOAK | PERM_SEECLOAK | PERM_XEMPT |
			PERM_DENYPOST | PERM_BM | PERM_ACCOUNTS |
			PERM_CHATROOM | PERM_BOARD | PERM_SYSOP | PERM_BBSADM;
		break;
	    }
	} else {		/* guest */
	    cuser.userlevel = 0;
	    cuser.uflag = COLOR_FLAG | PAGER_FLAG | BRDSORT_FLAG | MOVIE_FLAG;
	    break;
	}
    }
    multi_user_check();
    sethomepath(genbuf, cuser.userid);
    mkdir(genbuf, 0755);
}

void
add_distinct(char *fname, char *line)
{
    FILE           *fp;
    int             n = 0;

    if ((fp = fopen(fname, "a+"))) {
	char            buffer[80];
	char            tmpname[100];
	FILE           *fptmp;

	strlcpy(tmpname, fname, sizeof(tmpname));
	strcat(tmpname, "_tmp");
	if (!(fptmp = fopen(tmpname, "w"))) {
	    fclose(fp);
	    return;
	}
	rewind(fp);
	while (fgets(buffer, 80, fp)) {
	    char           *p = buffer + strlen(buffer) - 1;

	    if (p[-1] == '\n' || p[-1] == '\r')
		p[-1] = 0;
	    if (!strcmp(buffer, line))
		break;
	    sscanf(buffer + strlen(buffer) + 2, "%d", &n);
	    fprintf(fptmp, "%s%c#%d\n", buffer, 0, n);
	}

	if (feof(fp))
	    fprintf(fptmp, "%s%c#1\n", line, 0);
	else {
	    sscanf(buffer + strlen(buffer) + 2, "%d", &n);
	    fprintf(fptmp, "%s%c#%d\n", buffer, 0, n + 1);
	    while (fgets(buffer, 80, fp)) {
		sscanf(buffer + strlen(buffer) + 2, "%d", &n);
		fprintf(fptmp, "%s%c#%d\n", buffer, 0, n);
	    }
	}
	fclose(fp);
	fclose(fptmp);
	unlink(fname);
	rename(tmpname, fname);
    }
}

void
del_distinct(char *fname, char *line)
{
    FILE           *fp;
    int             n = 0;

    if ((fp = fopen(fname, "r"))) {
	char            buffer[80];
	char            tmpname[100];
	FILE           *fptmp;

	strlcpy(tmpname, fname, sizeof(tmpname));
	strcat(tmpname, "_tmp");
	if (!(fptmp = fopen(tmpname, "w"))) {
	    fclose(fp);
	    return;
	}
	rewind(fp);
	while (fgets(buffer, 80, fp)) {
	    char           *p = buffer + strlen(buffer) - 1;

	    if (p[-1] == '\n' || p[-1] == '\r')
		p[-1] = 0;
	    if (!strcmp(buffer, line))
		break;
	    sscanf(buffer + strlen(buffer) + 2, "%d", &n);
	    fprintf(fptmp, "%s%c#%d\n", buffer, 0, n);
	}

	if (!feof(fp))
	    while (fgets(buffer, 80, fp)) {
		sscanf(buffer + strlen(buffer) + 2, "%d", &n);
		fprintf(fptmp, "%s%c#%d\n", buffer, 0, n);
	    }
	fclose(fp);
	fclose(fptmp);
	unlink(fname);
	rename(tmpname, fname);
    }
}

#ifdef WHERE
static int
where(char *from)
{
    register int    i = 0, count = 0, j;

    for (j = 0; j < SHM->top; j++) {
	char           *token = strtok(SHM->domain[j], "&");

	i = 0;
	count = 0;
	while (token) {
	    if (strstr(from, token))
		count++;
	    token = strtok(NULL, "&");
	    i++;
	}
	if (i == count)
	    break;
    }
    if (i != count)
	return 0;
    return j;
}
#endif

static void
check_BM()
{
    int             i;
    boardheader_t  *bhdr;

    cuser.userlevel &= ~PERM_BM;
    for (i = 0, bhdr = bcache; i < numboards && !is_BM(bhdr->BM); i++, bhdr++);
}

static void
setup_utmp(int mode)
{
    userinfo_t      uinfo;
    memset(&uinfo, 0, sizeof(uinfo));
    uinfo.pid = currpid = getpid();
    uinfo.uid = usernum;
    uinfo.mode = currstat = mode;
    uinfo.mailalert = load_mailalert(cuser.userid);
    if (!(cuser.numlogins % 20) && cuser.userlevel & PERM_BM)
	check_BM();		/* Ptt 自動取下離職板主權力 */

    uinfo.userlevel = cuser.userlevel;
    uinfo.sex = cuser.sex % 8;
    uinfo.lastact = time(NULL);
    strlcpy(uinfo.userid, cuser.userid, sizeof(uinfo.userid));
    strlcpy(uinfo.realname, cuser.realname, sizeof(uinfo.realname));
    strlcpy(uinfo.username, cuser.username, sizeof(uinfo.username));
    strlcpy(uinfo.from, fromhost, sizeof(uinfo.from));
    uinfo.five_win = cuser.five_win;
    uinfo.five_lose = cuser.five_lose;
    uinfo.five_tie = cuser.five_tie;
    uinfo.invisible = cuser.invisible % 2;
    uinfo.pager = cuser.pager % 5;
    memcpy(uinfo.mind, cuser.mind, 4);
#ifdef WHERE
    uinfo.from_alias = where(fromhost);
#endif
#ifndef FAST_LOGIN
    setuserfile(buf, "remoteuser");

    strlcpy(remotebuf, fromhost, sizeof(fromhost));
    strcat(remotebuf, ctime(&now));
    remotebuf[strlen(remotebuf) - 1] = 0;
    add_distinct(buf, remotebuf);
#endif
    if (enter_uflag & CLOAK_FLAG)
	uinfo.invisible = YEA;
    getnewutmpent(&uinfo);
#ifndef _BBS_UTIL_C_
    friend_load();
#endif
}

static void
user_login()
{
    char            ans[4], i;
    char            genbuf[200];
    struct tm      *ptime, *tmp;
    time_t          now;
    int             a;
    /*** Heat:廣告詞
	 char *ADV[] = {
	 "7/17 @LIVE 亂彈, 何欣穗 的 入場卷要送給 ptt 的愛用者!",
	 "欲知詳情請看 PttAct 板!!",
	 }; ***/

    log_usies("ENTER", fromhost);
    setproctitle("%s: %s", margs, cuser.userid);
    resolve_fcache();
    resolve_boards();
    memset(&water[0], 0, sizeof(water_t) * 6);
    strlcpy(water[0].userid, " 全部 ", sizeof(water[0].userid));
    /* 初始化 uinfo、flag、mode */
    setup_utmp(LOGIN);
    mysrand();			/* 初始化: random number 增加user跟時間的差異 */
    currmode = MODE_STARTED;
    enter_uflag = cuser.uflag;

    /* get local time */
    time(&now);
    ptime = localtime(&now);
    tmp = localtime(&cuser.lastlogin);
    if ((a = SHM->UTMPnumber) > SHM->max_user) {
	SHM->max_user = a;
	SHM->max_time = now;
    }
    init_brdbuf();
    brc_initial(DEFAULT_BOARD);
    set_board();
    /* 畫面處理開始 */
    if (!(HAS_PERM(PERM_SYSOP) && HAS_PERM(PERM_DENYPOST)) && !currutmp->invisible)
	do_aloha("<<上站通知>> -- 我來啦！");
    if (ptime->tm_mday == cuser.day && ptime->tm_mon + 1 == cuser.month) {
	more("etc/Welcome_birth", NA);
	currutmp->birth = 1;
    } else {
#ifdef MULTI_WELCOME_LOGIN
	char            buf[80];
	int             nScreens;
	for (nScreens = 0; nScreens < 10; ++nScreens) {
	    snprintf(buf, sizeof(buf), "etc/Welcome_login.%d", nScreens);
	    if (access(buf, 0) < 0)
		break;
	}
	printf("%d\n", nScreens);
	if (nScreens == 0) {
	    //multi screen error ?
		more("etc/Welcome_login", NA);
	} else {
	    snprintf(buf, sizeof(buf), "etc/Welcome_login.%d", (int)login_start_time % nScreens);
	    more(buf, NA);
	}
#else
	more("etc/Welcome_login", NA);
#endif
	//pressanykey();
	//more("etc/CSIE_Week", NA);
	currutmp->birth = 0;
    }

    if (cuser.userlevel) {	/* not guest */
	move(t_lines - 4, 0);
	prints("\033[m      歡迎您第 \033[1;33m%d\033[0;37m 度拜訪本站，"
	       "上次您是從 \033[1;33m%s\033[0;37m 連往本站，\n"
	       "     我記得那天是 \033[1;33m%s\033[0;37m。\n",
	       ++cuser.numlogins, cuser.lasthost, Cdate(&cuser.lastlogin));
	pressanykey();

	if (currutmp->birth && tmp->tm_mday != ptime->tm_mday) {
	    more("etc/birth.post", YEA);
	    brc_initial("WhoAmI");
	    set_board();
	    do_post();
	}
	setuserfile(genbuf, str_badlogin);
	if (more(genbuf, NA) != -1) {
	    getdata(b_lines - 1, 0, "您要刪除以上錯誤嘗試的記錄嗎(Y/N)?[Y]",
		    ans, 3, LCECHO);
	    if (*ans != 'n')
		unlink(genbuf);
	}
	check_register();
	strncpy(cuser.lasthost, fromhost, 16);
	cuser.lasthost[15] = '\0';
	restore_backup();
    } else if (!strcmp(cuser.userid, STR_GUEST)) {
	char           *nick[13] = {
	    "椰子", "貝殼", "內衣", "寶特瓶", "翻車魚",
	    "樹葉", "浮萍", "鞋子", "潛水艇", "魔王",
	    "鐵罐", "考卷", "大美女"
	};
	char           *name[13] = {
	    "大王椰子", "鸚鵡螺", "比基尼", "可口可樂", "仰泳的魚",
	    "憶", "高岡屋", "AIR Jordon", "紅色十月號", "批踢踢",
	    "SASAYA椰奶", "鴨蛋", "布魯克鱈魚香絲"
	};
	char           *addr[13] = {
	    "天堂樂園", "大海", "綠島小夜曲", "美國", "綠色珊瑚礁",
	    "遠方", "原本海", "NIKE", "蘇聯", "男八618室",
	    "愛之味", "天上", "藍色珊瑚礁"
	};
	i = login_start_time % 13;
	snprintf(cuser.username, sizeof(cuser.username),
		 "海邊漂來的%s", nick[(int)i]);
	strlcpy(currutmp->username, cuser.username,
		sizeof(currutmp->username));
	strlcpy(cuser.realname, name[(int)i], sizeof(cuser.realname));
	strlcpy(currutmp->realname, cuser.realname,
		sizeof(currutmp->realname));
	strlcpy(cuser.address, addr[(int)i], sizeof(cuser.address));
	cuser.sex = i % 8;
	currutmp->pager = 2;
	pressanykey();
    } else
	pressanykey();

    if (!PERM_HIDE(currutmp))
	cuser.lastlogin = login_start_time;

    passwd_update(usernum, &cuser);

    for (i = 0; i < NUMVIEWFILE; i++)
	if ((cuser.loginview >> i) & 1)
	    more(loginview_file[(int)i][0], YEA);
}

static void
do_aloha(char *hello)
{
    FILE           *fp;
    char            userid[80];
    char            genbuf[200];

    setuserfile(genbuf, "aloha");
    if ((fp = fopen(genbuf, "r"))) {
	snprintf(genbuf, sizeof(genbuf), hello);
	while (fgets(userid, 80, fp)) {
	    userinfo_t     *uentp;
	    int             tuid;

	    if ((tuid = searchuser(userid)) && tuid != usernum &&
		(uentp = (userinfo_t *) search_ulist(tuid)) &&
		isvisible(uentp, currutmp)) {
		my_write(uentp->pid, genbuf, uentp->userid, 2, NULL);
	    }
	}
	fclose(fp);
    }
}

static void
do_term_init()
{
    term_init();
    initscr();
    if(use_shell_login_mode)
	raise(SIGWINCH);
}

static void
start_client()
{
#ifdef CPULIMIT
    struct rlimit   rml;
    rml.rlim_cur = CPULIMIT * 60;
    rml.rlim_max = CPULIMIT * 60;
    setrlimit(RLIMIT_CPU, &rml);
#endif

    /* system init */
    nice(2);			/* Ptt: lower priority */
    login_start_time = time(0);
    currmode = 0;

    signal(SIGHUP, abort_bbs);
    signal(SIGTERM, abort_bbs);
    signal(SIGPIPE, abort_bbs);

    signal(SIGINT, abort_bbs_debug);
    signal(SIGQUIT, abort_bbs_debug);
    signal(SIGILL, abort_bbs_debug);
    signal(SIGABRT, abort_bbs_debug);
    signal(SIGFPE, abort_bbs_debug);
    signal(SIGBUS, abort_bbs_debug);
    signal(SIGSEGV, abort_bbs_debug);

    signal_restart(SIGUSR1, talk_request);
    signal_restart(SIGUSR2, write_request);

    dup2(0, 1);

    /* initialize passwd semaphores */
    if (passwd_init())
	exit(1);

    do_term_init();
    signal(SIGALRM, abort_bbs);
    alarm(600);
    login_query();		/* Ptt 加上login time out */
    user_login();
    m_init();

    if (now - SHM->close_vote_time > 86400)
	//改為一天一次
    {
	b_closepolls();
	SHM->close_vote_time = now;
    }
    if (!(cuser.uflag & COLOR_FLAG))
	showansi = 0;
    signal(SIGALRM, SIG_IGN);
    if (chkmailbox())
	m_read();

    domenu(MMENU, "主功\能表", (currutmp->mailalert ? 'M' : 'C'), cmdlist);
}

/* FSA (finite state automata) for telnet protocol */
static void
telnet_init()
{
    static char     svr[] = {
	IAC, DO, TELOPT_TTYPE,
	IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE,
	IAC, WILL, TELOPT_ECHO,
	IAC, WILL, TELOPT_SGA
    };
    char           *cmd;
    int             n, len, rset;
    struct timeval  to;
    char            buf[64];
    for (n = 0, cmd = svr; n < 4; n++) {
	len = (n == 1 ? 6 : 3);
	write(0, cmd, len);
	cmd += len;
	to.tv_sec = 3;
	to.tv_usec = 0;
	rset = 1;
	if (select(1, (fd_set *) & rset, NULL, NULL, &to) > 0)
	    recv(0, buf, sizeof(buf), 0);
    }
}

/* 取得 remote user name 以判定身份                */
/*
 * rfc931() speaks a common subset of the RFC 931, AUTH, TAP, IDENT and RFC
 * 1413 protocols. It queries an RFC 931 etc. compatible daemon on a remote
 * host to look up the owner of a connection. The information should not be
 * used for authentication purposes. This routine intercepts alarm signals.
 * 
 * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
 */

#define STRN_CPY(d,s,l) { strncpy((d),(s),(l)); (d)[(l)-1] = 0; }
#define RFC931_TIMEOUT   10
#define RFC931_PORT     113	/* Semi-well-known port */
#define ANY_PORT        0	/* Any old port will do */

#if 0
/* timeout - handle timeouts */
static void
timeout(int sig)
{
    longjmp(byebye, sig);
}
#endif

static void
getremotename(struct sockaddr_in * from, char *rhost, char *rname)
{

    /* get remote host name */

#ifdef FAST_LOGIN
    strcpy(rhost, (char *)inet_ntoa(from->sin_addr));
#else
    struct sockaddr_in our_sin;
    struct sockaddr_in rmt_sin;
    unsigned        rmt_port, rmt_pt;
    unsigned        our_port, our_pt;
    FILE           *fp;
    char            buffer[512], user[80], *cp;
    int             s;
    static struct hostent *hp;


    hp = NULL;
    if (setjmp(byebye) == 0) {
	signal(SIGALRM, timeout);
	alarm(3);
	hp = gethostbyaddr((char *)&from->sin_addr, sizeof(struct in_addr),
			   from->sin_family);
	alarm(0);
    }
    strcpy(rhost, hp ? hp->h_name : (char *)inet_ntoa(from->sin_addr));

    /*
     * Use one unbuffered stdio stream for writing to and for reading from
     * the RFC931 etc. server. This is done because of a bug in the SunOS
     * 4.1.x stdio library. The bug may live in other stdio implementations,
     * too. When we use a single, buffered, bidirectional stdio stream ("r+"
     * or "w+" mode) we read our own output. Such behaviour would make sense
     * with resources that support random-access operations, but not with
     * sockets.
     */

    s = sizeof(our_sin);
    if (getsockname(0, (struct sockaddr *) & our_sin, &s) < 0)
	return;

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return;

    if (!(fp = fdopen(s, "r+"))) {
	close(s);
	return;
    }
    /* Set up a timer so we won't get stuck while waiting for the server. */
    if (setjmp(byebye) == 0) {
	signal(SIGALRM, timeout);
	alarm(RFC931_TIMEOUT);

	/*
	 * Bind the local and remote ends of the query socket to the same IP
	 * addresses as the connection under investigation. We go through all
	 * this trouble because the local or remote system might have more
	 * than one network address. The RFC931 etc. client sends only port
	 * numbers; the server takes the IP addresses from the query socket.
	 */
	our_pt = ntohs(our_sin.sin_port);
	our_sin.sin_port = htons(ANY_PORT);

	rmt_sin = *from;
	rmt_pt = ntohs(rmt_sin.sin_port);
	rmt_sin.sin_port = htons(RFC931_PORT);

	setbuf(fp, (char *)0);
	s = fileno(fp);

	if (bind(s, (struct sockaddr *) & our_sin, sizeof(our_sin)) >= 0 &&
	  connect(s, (struct sockaddr *) & rmt_sin, sizeof(rmt_sin)) >= 0) {
	    /*
	     * Send query to server. Neglect the risk that a 13-byte write
	     * would have to be fragmented by the local system and cause
	     * trouble with buggy System V stdio libraries.
	     */
	    fprintf(fp, "%u,%u\r\n", rmt_pt, our_pt);
	    fflush(fp);
	    /*
	     * Read response from server. Use fgets()/sscanf() so we can work
	     * around System V stdio libraries that incorrectly assume EOF
	     * when a read from a socket returns less than requested.
	     */
	    if (fgets(buffer, sizeof(buffer), fp) && !ferror(fp)
		&& !feof(fp)
		&& sscanf(buffer, "%u , %u : USERID :%*[^:]:%79s", &rmt_port,
			  &our_port, user) == 3 && rmt_pt == rmt_port
		&& our_pt == our_port) {

		/*
		 * Strip trailing carriage return. It is part of the
		 * protocol, not part of the data.
		 */
		if ((cp = (char *)strchr(user, '\r')))
		    *cp = 0;
		strlcpy(rname, user, sizeof(user));
	    }
	}
	alarm(0);
    }
    fclose(fp);
#endif
}

static int
bind_port(int port)
{
    int             sock, on;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on));

    on = 0;
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&on, sizeof(on));

    xsin.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *) & xsin, sizeof xsin) < 0) {
	syslog(LOG_INFO, "bbsd bind_port can't bind to %d", port);
	exit(1);
    }
    if (listen(sock, SOCKET_QLEN) < 0) {
	syslog(LOG_INFO, "bbsd bind_port can't listen to %d", port);
	exit(1);
    }
    return sock;
}


/*******************************************************/


static void     shell_login(int argc, char *argv[], char *envp[]);
static void     daemon_login(int argc, char *argv[], char *envp[]);
static int      check_ban_and_load(int fd);

int
main(int argc, char *argv[], char *envp[])
{
    start_time = time(NULL);

    /* avoid SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    /* avoid erroneous signal from other mbbsd */
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    /* check if invoked as "bbs" */
    attach_SHM();
    if (argc == 3)
	shell_login(argc, argv, envp);
    else
	daemon_login(argc, argv, envp);

    return 0;
}

static void
shell_login(int argc, char *argv[], char *envp[])
{

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);


    use_shell_login_mode = 1;
    initsetproctitle(argc, argv, envp);

    /*
     * copy fromindent: Standard input:1138: Error:Unexpected end of file the
     * original "bbs"
     */
    if (argc > 1) {
	strcpy(fromhost, argv[1]);
	if (argc > 3)
	    strlcpy(remoteusername, argv[3], sizeof(remoteusername));
    }
    close(2);
    /* don't close fd 1, at least init_tty need it */

    init_tty();
    if (check_ban_and_load(0)) {
	exit(0);
    }
    start_client();
}

static void
daemon_login(int argc, char *argv[], char *envp[])
{
    int             msock, csock;	/* socket for Master and Child */
    FILE           *fp;
    int             listen_port = 23;
    int             len_of_sock_addr;
    char            buf[256];

    /* setup standalone */

    start_daemon();

    signal_restart(SIGCHLD, reapchild);

    /* choose port */
    if (argc == 1)
	listen_port = 3006;
    else if (argc >= 2)
	listen_port = atoi(argv[1]);

    snprintf(margs, sizeof(margs), "%s %d ", argv[0], listen_port);

    /* port binding */
    xsin.sin_family = AF_INET;
    msock = bind_port(listen_port);
    if (msock < 0) {
	syslog(LOG_INFO, "mbbsd bind_port failed.\n");
	exit(1);
    }
    initsetproctitle(argc, argv, envp);
    setproctitle("%s: listening ", margs);

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);

    snprintf(buf, sizeof(buf), "run/mbbsd.%d.pid", listen_port);
    if ((fp = fopen(buf, "w"))) {
	fprintf(fp, "%d\n", getpid());
	fclose(fp);
    }

    /* main loop */
    for (;;) {
	len_of_sock_addr = sizeof(xsin);
	csock = accept(msock, (struct sockaddr *) & xsin, (socklen_t *) & len_of_sock_addr);

	if (csock < 0) {
	    if (errno != EINTR)
		sleep(1);
	    continue;
	}
	if (check_ban_and_load(csock)) {
	    close(csock);
	    continue;
	}
#ifdef NO_FORK
	break;
#else
	if (fork() == 0)
	    break;
	else
	    close(csock);
#endif

    }
    /* here is only child running */

    setproctitle("%s: ...login wait... ", margs);
    close(msock);
    dup2(csock, 0);
    close(csock);

    getremotename(&xsin, fromhost, remoteusername);
    telnet_init();
    start_client();
    close(0);
    close(1);
}

/*
 * check if we're banning login and if the load is too high. if login is
 * permitted, return 0; else return -1; approriate message is output to fd.
 */
static int
check_ban_and_load(int fd)
{
    FILE           *fp;
    static char     buf[256];
    static time_t   chkload_time = 0;
    static int      overload = 0;	/* overload or banned, update every 1
					 * sec  */
    static int      banned = 0;

#ifdef INSCREEN
    write(fd, INSCREEN, strlen(INSCREEN));
#endif

    if ((time(0) - chkload_time) > 1) {
	overload = chkload(buf, sizeof(buf));
	banned = !access(BBSHOME "/BAN", R_OK) &&
	    (strcmp(fromhost, "localhost") != 0);
	chkload_time = time(0);
    }
    write(fd, buf, strlen(buf));

    if (banned && (fp = fopen(BBSHOME "/BAN", "r"))) {
	while (fgets(buf, 256, fp))
	    write(fd, buf, strlen(buf));
	fclose(fp);
    }
    if (SHM->UTMPnumber >= MAX_ACTIVE
#ifdef DYMAX_ACTIVE
	|| (GLOBALVAR[9] > 500 && SHM->UTMPnumber >= GLOBALVAR[9] )
#endif
	) {
	++GLOBALVAR[8];
	snprintf(buf, sizeof(buf), "由於人數過多，請您稍後再來。");
	write(fd, buf, strlen(buf));
	overload = 1;
    }

    if (banned || overload)
	return -1;

    return 0;
}
