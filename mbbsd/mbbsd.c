/* $Id$ */
#include "bbs.h"

#define SOCKET_QLEN 4
#define TH_LOW 100
#define TH_HIGH 120

static void do_aloha(char *hello);
static void getremotename(struct sockaddr_in * from, char *rhost, char *rname);

#if 0
static jmp_buf  byebye;
#endif

static char     remoteusername[40] = "?";

static unsigned char enter_uflag;
static int      use_shell_login_mode = 0;

static struct sockaddr_in xsin;

#ifdef USE_RFORK
#define fork() rfork(RFFDG | RFPROC | RFNOWAIT)
#endif

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
    int             n, fd;

    /*
     * More idiot speed-hacking --- the first time conversion makes the C
     * library open the files containing the locale definition and time zone.
     * If this hasn't happened in the parent process, it happens in the
     * children, once per connection --- and it does add up.
     */
    time_t          dummy = time(NULL);
    struct tm      *dummy_time = localtime(&dummy);
    char            buf[32];

    strftime(buf, sizeof(buf), "%d/%b/%Y:%H:%M:%S", dummy_time);

#ifndef NO_FORK
    if ((n = fork())) {
	exit(0);
    }
#endif

    /* rocker.011018: it's a good idea to close all unexcept fd!! */
    n = getdtablesize();
    while (n)
	close(--n);

    /* in2: open /dev/null to fd:2 */
    if( ((fd = open("/dev/null", O_WRONLY)) >= 0) && fd != 2 ){
	dup2(fd, 2);
	close(fd);
    }

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

    if(getenv("SSH_CLIENT"))
      unsetenv("SSH_CLIENT");
}

static void
reapchild(int sig)
{
    int             state, pid;

    while ((pid = waitpid(-1, &state, WNOHANG | WUNTRACED)) > 0);
}

void
log_user(char *msg)
{
    char            filename[200], buf[200];

    snprintf(filename, sizeof(filename), BBSHOME "/home/%c/%s/USERLOG",
	     cuser.userid[0], cuser.userid);
    snprintf(buf, sizeof(buf), "%s\n", msg);
    log_file(filename, msg, 1);
}


void
log_usies(char *mode, char *mesg)
{
    char            genbuf[200];

    if (!mesg)
	snprintf(genbuf, sizeof(genbuf),
		 cuser.userid[0] ? "%s %s %-12s Stay:%d (%s)\n" :
		 "%s %s %s Stay:%d (%s)\n",
		 Cdate(&now), mode, cuser.userid,
		 (int)(now - login_start_time) / 60, cuser.username);
    else
	snprintf(genbuf, sizeof(genbuf),
		 cuser.userid[0] ? "%s %s %-12s %s\n" : "%s %s %s%s\n",
		 Cdate(&now), mode, cuser.userid, mesg);
    log_file(FN_USIES, genbuf, 1);

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

    /* close fd 0 & 1 to terminate network */
    close(0);
    close(1);

    reload_money();
    auto_backup();
    setflags(PAGER_FLAG, currutmp->pager != 1);
    setflags(CLOAK_FLAG, currutmp->invisible);
    save_brdbuf();
    brc_finalize();

#ifdef ASSESS
    cuser.goodpost = currutmp->goodpost;
    cuser.badpost = currutmp->badpost;
    cuser.goodsale = currutmp->goodsale;
    cuser.badsale = currutmp->badsale;
#endif

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
#ifdef DEBUGSLEEP
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
#endif
    exit(0);
}

/* 登錄 BBS 程式 */
static void
mysrand()
{
    srand(time(NULL) + getpid());	/* 時間跟 pid 當 rand 的 seed */
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

void
talk_request(int sig)
{
    bell();
    bell();
    if (currutmp->msgcount) {
	char            timebuf[100];
	time_t          now = time(0);

	move(0, 0);
	clrtoeol();
	prints("\033[33;41m★%s\033[34;47m [%s] %s \033[0m",
		 SHM->uinfo[currutmp->destuip].userid, my_ctime(&now,timebuf,sizeof(timebuf)),
		 (currutmp->sig == 2) ? "重要消息廣播！(請Ctrl-U,l查看熱訊記錄)"
		 : "呼叫、呼叫，聽到請回答");
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
	    fprintf(fp_writelog, "%s [%s]\n", buf, Cdatelite(&now));
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
    int             i, msgcount;

#ifdef NOKILLWATERBALL
    if( reentrant_write_request ) /* kill again by shmctl */
	return;
    reentrant_write_request = 1;
#endif
    if (WATERMODE(WATER_OFO)) {
	/* 如果目前正在回水球模式的話, 就不能進行 add_history() ,
	   因為會改寫 water[], 而使回水球目的爛掉, 所以分成幾種情況考慮.
	   sig != 0表真的有水球進來, 故顯示.
	   sig == 0表示沒有水球進來, 不過之前尚有水球還沒寫到 water[].
	*/
	static  int     alreadyshow = 0;

	if( sig ){ /* 真的有水球進來 */

	    /* 若原來正在 REPLYING , 則改成 RECVINREPLYING,
	       這樣在回水球結束後, 會再呼叫一次 write_request(0) */
	    if( wmofo == REPLYING )
		wmofo = RECVINREPLYING;

	    /* 顯示 */
	    for( ; alreadyshow < currutmp->msgcount && alreadyshow < MAX_MSGS
		     ; ++alreadyshow ){
		bell();
		show_call_in(1, alreadyshow);
		refresh();
	    }
	}

	/* 看看是不是要把 currutmp->msg 拿回 water[] (by add_history())
	   須要是不在回水球中 (NOTREPLYING) */
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

#ifdef NOKILLWATERBALL
	    currutmp->wbtime = 0;
#endif
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
		    igetkey();
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
#ifdef NOKILLWATERBALL
    reentrant_write_request = 0;
    currutmp->wbtime = 0; /* race */
#endif
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

inline static void mkuserdir(char *userid)
{
    char genbuf[200];
    sethomepath(genbuf, userid);
    // assume it is a dir, so just check if it is exist
    if (access(genbuf, F_OK) != 0)
	mkdir(genbuf, 0755);
}

static void
login_query()
{
#ifdef CONVERT
    /* uid 加一位, for gb login */
    char            uid[IDLEN + 2], passbuf[PASSLEN];
    int             attempts, len;
#else
    char            uid[IDLEN + 1], passbuf[PASSLEN];
    int             attempts;
#endif
    resolve_garbage();
    now = time(0);

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

#ifdef CONVERT
	/* switch to gb mode if uid end with '.' */
	len = strlen(uid);
	if (uid[0] && uid[len - 1] == '.') {
	    set_converting_type(CONV_GB);
	    uid[len - 1] = 0;
	}
	else if (uid[0] && uid[len - 1] == ',') {
	    set_converting_type(CONV_UTF8);
	    uid[len - 1] = 0;
	}
	else if (len >= IDLEN + 1)
	    uid[IDLEN] = 0;
#endif

	if (strcasecmp(uid, str_new) == 0) {
#ifdef LOGINASNEW
	    new_register();
	    mkuserdir(cuser.userid);
	    break;
#else
	    outs("本系統目前無法以 new 註冊, 請用 guest 進入\n");
	    continue;
#endif
	} else if (uid[0] == '\0' || !dosearchuser(uid)) {
	    outs(err_uid);
	} else if (strcmp(uid, STR_GUEST)) {
	    getdata(21, 0, MSG_PASSWD,
		    passbuf, sizeof(passbuf), NOECHO);
	    passbuf[8] = '\0';

	    if (!checkpasswd(cuser.passwd, passbuf)
		 /* || (HAS_PERM(PERM_SYSOP) && !use_shell_login_mode) */ ) {
		logattempt(cuser.userid, '-');
		outs(ERR_PASSWD);
	    } else {
		logattempt(cuser.userid, ' ');
		if (strcasecmp("SYSOP", cuser.userid) == 0){
		    cuser.userlevel = PERM_BASIC | PERM_CHAT | PERM_PAGE |
			PERM_POST | PERM_LOGINOK | PERM_MAILLIMIT |
			PERM_CLOAK | PERM_SEECLOAK | PERM_XEMPT |
			PERM_DENYPOST | PERM_BM | PERM_ACCOUNTS |
			PERM_CHATROOM | PERM_BOARD | PERM_SYSOP | PERM_BBSADM;
		    mkuserdir(cuser.userid);
		}
		break;
	    }
	} else {		/* guest */
	    cuser.userlevel = 0;
	    cuser.uflag = COLOR_FLAG | PAGER_FLAG | BRDSORT_FLAG | MOVIE_FLAG;
	    mkuserdir(cuser.userid);
	    break;
	}
    }
    multi_user_check();
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
check_BM(void)
{
    /* XXX: -_- */
    int             i;

    cuser.userlevel &= ~PERM_BM;
    for( i = 0 ; i < numboards ; ++i )
	if( is_BM_cache(i + 1) ) /* XXXbid */
	    return;
    //for (i = 0, bhdr = bcache; i < numboards && !is_BM(bhdr->BM); i++, bhdr++);
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

#ifdef ASSESS
    uinfo.goodpost = cuser.goodpost;
    uinfo.badpost = cuser.badpost;
    uinfo.goodsale = cuser.goodsale;
    uinfo.badsale = cuser.badsale;
#endif

    uinfo.userlevel = cuser.userlevel;
    uinfo.sex = cuser.sex % 8;
    uinfo.lastact = time(NULL);
    strlcpy(uinfo.userid, cuser.userid, sizeof(uinfo.userid));
    //strlcpy(uinfo.realname, cuser.realname, sizeof(uinfo.realname));
    strlcpy(uinfo.username, cuser.username, sizeof(uinfo.username));
    strlcpy(uinfo.from, fromhost, sizeof(uinfo.from));
    uinfo.five_win = cuser.five_win;
    uinfo.five_lose = cuser.five_lose;
    uinfo.five_tie = cuser.five_tie;
    uinfo.chc_win = cuser.chc_win;
    uinfo.chc_lose = cuser.chc_lose;
    uinfo.chc_tie = cuser.chc_tie;
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
    SHM->UTMPneedsort = 1;
    if (!(cuser.numlogins % 20) && cuser.userlevel & PERM_BM)
	check_BM();		/* Ptt 自動取下離職板主權力 */
#ifndef _BBS_UTIL_C_
    friend_load(0);
    nice(3);
#endif
}

inline static void welcome_msg(void) {
    prints("\033[m      歡迎您第 \033[1;33m%d\033[0;37m 度拜訪本站，"
	    "上次您是從 \033[1;33m%s\033[0;37m 連往本站，\n"
	    "     我記得那天是 \033[1;33m%s\033[0;37m。\n",
	    ++cuser.numlogins, cuser.lasthost, Cdate(&cuser.lastlogin));
    pressanykey();
}

inline static void check_bad_login(void) {
    char            genbuf[200];
    setuserfile(genbuf, str_badlogin);
    if (more(genbuf, NA) != -1) {
	move(b_lines - 3, 0);
	prints("通常並沒有辦法知道該ip是誰所有, "
		"以及其意圖(是不小心按錯或有意測您密碼)\n"
		"若您有帳號被盜用疑慮, 請經常更改您的密碼或使用加密連線");
	if (getans("您要刪除以上錯誤嘗試的記錄嗎(Y/N)?[Y]") != 'n')
	    unlink(genbuf);
    }
}

inline static void birthday_make_a_wish(struct tm *ptime, struct tm *tmp){
    if (currutmp->birth && tmp->tm_mday != ptime->tm_mday) {
	more("etc/birth.post", YEA);
	brc_initial("WhoAmI");
	set_board();
	do_post();
    }
}

inline static void record_lasthost(char *fromhost, int len){
    strncpy(cuser.lasthost, fromhost, len);
    cuser.lasthost[len - 1] = '\0';
}

inline static void check_mailbox_and_read(void){
    if (chkmailbox())
	m_read();
}

static void init_guest_info(void)
{
    int i;
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
    //strlcpy(currutmp->realname, cuser.realname, sizeof(currutmp->realname));
    strlcpy(cuser.address, addr[(int)i], sizeof(cuser.address));
    cuser.sex = i % 8;
    currutmp->pager = 2;
}

#ifdef FOREIGN_REG
inline static void foreign_warning(void){
    if ((cuser.uflag2 & FOREIGN) && !(cuser.uflag2 & LIVERIGHT)){
	if (login_start_time - cuser.firstlogin > (FOREIGN_REG_DAY - 5) * 24 * 3600){
	    mail_muser(cuser, "[出入境管理局]", "etc/foreign_expired_warn");
	}
	else if (login_start_time - cuser.firstlogin > FOREIGN_REG_DAY * 24 * 3600){
	    cuser.userlevel &= ~(PERM_LOGINOK | PERM_POST);
	    vmsg("警告：請至出入境管理局申請永久居留");
	}
    }
}
#endif

static void
user_login()
{
    char            i;
    struct tm      *ptime, *tmp;
    time_t          now;
    int             a, ifbirth;

    /* get local time */
    time(&now);
    ptime = localtime(&now);
    
    /* 初始化: random number 增加user跟時間的差異 */
    mysrand();

    /* show welcome_login */
    ifbirth = (ptime->tm_mday == cuser.day &&
	       ptime->tm_mon + 1 == cuser.month);
    if (ifbirth)
	more("etc/Welcome_birth", NA);
    else {
#ifndef MULTI_WELCOME_LOGIN
	more("etc/Welcome_login", NA);
#else
	if( SHM->GV2.e.nWelcomes ){
	    char            buf[80];
	    snprintf(buf, sizeof(buf), "etc/Welcome_login.%d",
		     (int)login_start_time % SHM->GV2.e.nWelcomes);
	    more(buf, NA);
	}
#endif
    }

    log_usies("ENTER", fromhost);
    setproctitle("%s: %s", margs, cuser.userid);
    resolve_fcache();
    resolve_boards();
    memset(&water[0], 0, sizeof(water_t) * 6);
    strlcpy(water[0].userid, " 全部 ", sizeof(water[0].userid));

    if(getenv("SSH_CLIENT") != NULL){
	char frombuf[50];
	sscanf(getenv("SSH_CLIENT"), "%s", frombuf);
	xsin.sin_family = AF_INET;
	xsin.sin_port = htons(23);
	inet_pton(AF_INET, frombuf, &xsin.sin_addr);
	getremotename(&xsin, fromhost, remoteusername);   /* FC931 */
    }

    /* 初始化 uinfo、flag、mode */
    setup_utmp(LOGIN);
    currmode = MODE_STARTED;
    enter_uflag = cuser.uflag;
    currutmp->birth = ifbirth;

    tmp = localtime(&cuser.lastlogin);
    if ((a = SHM->UTMPnumber) > SHM->max_user) {
	SHM->max_user = a;
	SHM->max_time = now;
    }
    init_brdbuf();
    brc_initial(DEFAULT_BOARD);
    set_board();

    if (!(HAS_PERM(PERM_SYSOP) && HAS_PERM(PERM_DENYPOST)) &&
	!currutmp->invisible)
	do_aloha("<<上站通知>> -- 我來啦！");

    if (cuser.userlevel) {	/* not guest */
	move(t_lines - 4, 0);
	welcome_msg();

	birthday_make_a_wish(ptime, tmp);
	check_bad_login();
	check_mailbox_and_read();
	check_register();
	record_lasthost(fromhost, 16);
	restore_backup();
    } else if (!strcmp(cuser.userid, STR_GUEST)) {
	init_guest_info();
	pressanykey();
    } else {
	pressanykey();
	check_mailbox_and_read();
    }

    if (!PERM_HIDE(currutmp))
	cuser.lastlogin = login_start_time;

#ifdef FOREIGN_REG
    foreign_warning();
#endif
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
	    if ((uentp = (userinfo_t *) search_ulist_userid(userid)) && 
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

inline static void
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
    m_init();			/* init the user mail path */
    user_login();

    if (now - SHM->close_vote_time > 86400)
	//改為一天一次
    {
	b_closepolls();
	SHM->close_vote_time = now;
    }
    if (!(cuser.uflag & COLOR_FLAG))
	showansi = 0;
    signal(SIGALRM, SIG_IGN);

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
    int             n, len;
    struct timeval  to;
    char            buf[64];
    fd_set          ReadSet, r;
    
    FD_ZERO(&ReadSet);
    FD_SET(0, &ReadSet);
    for (n = 0, cmd = svr; n < 4; n++) {
	len = (n == 1 ? 6 : 3);
	write(0, cmd, len);
	cmd += len;
	to.tv_sec = 3;
	to.tv_usec = 0;
	r = ReadSet;
	if (select(1, &r, NULL, NULL, &to) > 0)
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


static int      shell_login(int argc, char *argv[], char *envp[]);
static int      daemon_login(int argc, char *argv[], char *envp[]);
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

    attach_SHM();
    if( (argc == 3 && shell_login(argc, argv, envp)) ||
	(argc != 3 && daemon_login(argc, argv, envp)) )
	start_client();

    return 0;
}

static int
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
	return 0;
    }
    return 1;
}

static int
daemon_login(int argc, char *argv[], char *envp[])
{
    int             msock, csock;	/* socket for Master and Child */
    FILE           *fp;
    int             listen_port = 23;
    int             len_of_sock_addr, overloading = 0;
    char            buf[256];
#if OVERLOADBLOCKFDS
    int             blockfd[OVERLOADBLOCKFDS];
    int             i, nblocked = 0;
#endif

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

    /* It's better to do something before fork */
#ifdef CONVERT
    void big2gb_init(void*);
    void gb2big_init(void*);
    void big2uni_init(void*);
    void uni2big_init(void*);
    big2gb_init(NULL);
    gb2big_init(NULL);
    big2uni_init(NULL);
    uni2big_init(NULL);
#endif
    
#ifndef NO_FORK
#ifdef PRE_FORK
    if( listen_port == 23 ){ // only pre-fork in port 23
	int     i;
	for( i = 0 ; i < PRE_FORK ; ++i )
	    if( fork() <= 0 )
		break;
    }
#endif
#endif

    snprintf(buf, sizeof(buf), "run/mbbsd.%d.%d.pid", listen_port, getpid());
    if ((fp = fopen(buf, "w"))) {
	fprintf(fp, "%d\n", getpid());
	fclose(fp);
    }

    /* main loop */
    while( 1 ){
	len_of_sock_addr = sizeof(xsin);
	if( (csock = accept(msock, (struct sockaddr *)&xsin,
			    (socklen_t *)&len_of_sock_addr)) < 0 ){
	    if (errno != EINTR)
		sleep(1);
	    continue;
	}

	overloading = check_ban_and_load(csock);
#if OVERLOADBLOCKFDS
	if( (!overloading && nblocked) ||
	    (overloading && nblocked == OVERLOADBLOCKFDS) ){
	    for( i = 0 ; i < OVERLOADBLOCKFDS ; ++i )
		if( blockfd[i] != csock && blockfd[i] != msock )
		    /* blockfd[i] should not be msock, but it happened */
		    close(blockfd[i]);
	    nblocked = 0;
	}
#endif

	if( overloading ){
#if OVERLOADBLOCKFDS
	    blockfd[nblocked++] = csock;
#else
	    close(csock);
#endif
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
    return 1;
}

/*
 * check if we're banning login and if the load is too high. if login is
 * permitted, return 0; else return -1; approriate message is output to fd.
 */
static int
check_ban_and_load(int fd)
{
    FILE           *fp;
    static time_t   chkload_time = 0;
    static int      overload = 0;	/* overload or banned, update every 1
					 * sec  */
    static int      banned = 0;

#ifdef INSCREEN
    write(fd, INSCREEN, sizeof(INSCREEN));
#else
#define BANNER \
"【" BBSNAME "】◎ 台大流行網 ◎(" MYHOSTNAME ") 調幅(" MYIP ") \r\n"
    write(fd, BANNER, sizeof(BANNER));
#endif

    if ((time(0) - chkload_time) > 1) {
	overload = 0;
	banned = 0;

	if(cpuload(NULL) > MAX_CPULOAD)
	    overload = 1;
	else if (SHM->UTMPnumber >= MAX_ACTIVE
#ifdef DYMAX_ACTIVE
		|| (SHM->GV2.e.dymaxactive > 2000 &&
		    SHM->UTMPnumber >= SHM->GV2.e.dymaxactive)
#endif
		) {
	    ++SHM->GV2.e.toomanyusers;
	    overload = 2;
	} else if(!access(BBSHOME "/" BAN_FILE, R_OK))
	    banned = 1;

	chkload_time = time(0);
    }

    if(overload == 1)
	write(fd, "系統過載, 請稍後再來\r\n", 22);
    else if(overload == 2)
	write(fd, "由於人數過多，請您稍後再來。", 28);
    else if (banned && (fp = fopen(BBSHOME "/" BAN_FILE, "r"))) {
	char     buf[256];
	while (fgets(buf, sizeof(buf), fp))
	    write(fd, buf, strlen(buf));
	fclose(fp);
    }

    if (banned || overload)
	return -1;

    return 0;
}
