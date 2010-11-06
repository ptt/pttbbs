/* $Id$ */
#include "bbs.h"
#include "banip.h"
#include "daemons.h"
#include <sys/wait.h>
#include <netinet/tcp.h>

#ifdef __linux__
#    ifdef CRITICAL_MEMORY
#        include <malloc.h>
#    endif
#    ifdef DEBUG
#        include <mcheck.h>
#    endif
#endif


#define SOCKET_QLEN 4

static void do_aloha(const char *hello);
static void getremotename(const struct in_addr from, char *rhost);

//////////////////////////////////////////////////////////////////
// Site Optimization
// override these macro if you need more optimization, 
// based on OS/lib/package...
#ifndef OPTIMIZE_SOCKET
#define OPTIMIZE_SOCKET(sock) do {} while (0)
#endif

#ifndef XAUTH_HOST
#define XAUTH_HOST(x) x
#endif

#ifndef XAUTH_GETREMOTENAME
#define XAUTH_GETREMOTENAME(x) x
#endif

static int listen_port;

#define MAX_BINDPORT 20
enum TermMode {
    TermMode_TELNET,
    TermMode_TTY,
};

struct ProgramOption {
    bool	daemon_mode;
    bool	tunnel_mode;
    enum TermMode term_mode;
    int		term_width, term_height;
    int		nport;
    int		port[MAX_BINDPORT];
    int		flag_listenfd;
    char*	flag_tunnel_path;

    bool	flag_bypass;
    bool	flag_fork;
    bool	flag_checkload;
    char	flag_user[IDLEN+1];
};

static void 
free_program_option(struct ProgramOption *opt)
{
    if (!opt)
	return;
    free(opt->flag_tunnel_path);
    free(opt);
}

#ifdef DETECT_CLIENT
Fnv32_t client_code=FNV1_32_INIT;

void UpdateClientCode(unsigned char c)
{
    FNV1A_CHAR(c, client_code);
}

void LogClientCode()
{
    int fd = OpenCreate("log/client_code",O_WRONLY | O_APPEND);
    if(fd>=0) {
	write(fd, &client_code, sizeof(client_code));
	close(fd);
    }
}
#endif

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
start_daemon(struct ProgramOption *option)
{
#ifndef VALGRIND
    int             n, fd;
#endif

    /*
     * More idiot speed-hacking --- the first time conversion makes the C
     * library open the files containing the locale definition and time zone.
     * If this hasn't happened in the parent process, it happens in the
     * children, once per connection --- and it does add up.
     */
    time_t          dummy = time(NULL);
    struct tm       dummy_time;
    char            buf[32];

    localtime_r(&dummy, &dummy_time);
    strftime(buf, sizeof(buf), "%d/%b/%Y:%H:%M:%S", &dummy_time);

    if (option->flag_fork) {
	if (fork()) {
	    exit(0);
	}
    }

    /* rocker.011018: it's a good idea to close all unexcept fd!! */
#ifndef VALGRIND
    n = getdtablesize();
    while (n)
	close(--n);

    if( ((fd = OpenCreate("log/stderr", O_WRONLY | O_APPEND)) >= 0) && fd != 2 ){
	dup2(fd, 2);
	close(fd);
    }
#endif

    if(getenv("SSH_CLIENT"))
	unsetenv("SSH_CLIENT");
    if(getenv("SSH_CONNECTION"))
	unsetenv("SSH_CONNECTION");

    /*
     * rocker.011018: we don't need to remember original tty, so request a
     * new session id
     */
    setsid();

    /*
     * rocker.011018: after new session, we should insure the process is
     * clean daemon
     */
    if (option->flag_fork) {
	if (fork()) {
	    exit(0);
	}
    }
}

static void
reapchild(int sig GCC_UNUSED)
{
    int             state, pid;

    while ((pid = waitpid(-1, &state, WNOHANG | WUNTRACED)) > 0);
}

void
log_usies(const char *mode, const char *mesg)
{
    now = time(NULL);
    if (!mesg)
        log_filef(FN_USIES, LOG_CREAT, 
                 "%s %s %-12s Stay:%d (%s)\n",
                 Cdate(&now), mode, cuser.userid ,
                 (int)(now - login_start_time) / 60, cuser.nickname);
    else
        log_filef(FN_USIES, LOG_CREAT,
                 "%s %s %-12s %s\n",
                 Cdate(&now), mode, cuser.userid, mesg);

    /* 追蹤使用者 */
    if (HasUserPerm(PERM_LOGUSER))
        log_user("logout");
}


void
u_exit(const char *mode)
{
    currmode = 0;

    /* close fd 0 & 1 to terminate network */
    close(0);
    close(1);

    // verify if utmp is valid. only flush data if utmp is correct.
    assert(strncmp(currutmp->userid,cuser.userid, IDLEN)==0);
    if(strncmp(currutmp->userid, cuser.userid, IDLEN)!=0)
	return;

    auto_backup();
    save_brdbuf();
    brc_finalize();

    // XXX TOTO for guests, skip the save process?
    pwcuExitSave();
    setutmpbid(0);

    if (!SHM->GV2.e.shutdown) {
	if (!(HasUserPerm(PERM_SYSOP) && HasUserPerm(PERM_SYSOPHIDE)) &&
		!currutmp->invisible)
	    do_aloha("<<下站通知>> -- 我走囉！");
    }

    purge_utmp(currutmp);
    log_usies(mode, NULL);
}

void
abort_bbs(int sig GCC_UNUSED)
{
    /* ignore normal signals */
    Signal(SIGALRM, SIG_IGN);
    Signal(SIGUSR1, SIG_IGN);
    Signal(SIGUSR2, SIG_IGN);
    Signal(SIGHUP, SIG_IGN);
    Signal(SIGTERM, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    if (currmode)
	u_exit("ABORTED");
    exit(0);
}

#ifdef GCC_NORETURN
static void abort_bbs_debug(int sig) GCC_NORETURN;
#endif

/* NOTE: It's better to use signal-safe functions. Avoid to call
 * functions with global/static variable -- data may be corrupted */
static void
abort_bbs_debug(int sig)
{
    int    i;
    sigset_t sigset;

    switch(sig) {
      case SIGINT: STATINC(STAT_SIGINT); break;
      case SIGQUIT: STATINC(STAT_SIGQUIT); break;
      case SIGILL: STATINC(STAT_SIGILL); break;
      case SIGABRT: STATINC(STAT_SIGABRT); break;
      case SIGFPE: STATINC(STAT_SIGFPE); break;
      case SIGBUS: STATINC(STAT_SIGBUS); break;
      case SIGSEGV: STATINC(STAT_SIGSEGV); break;
      case SIGXCPU: STATINC(STAT_SIGXCPU); break;
    }
    /* ignore normal signals */
    Signal(SIGALRM, SIG_IGN);
    Signal(SIGUSR1, SIG_IGN);
    Signal(SIGUSR2, SIG_IGN);
    Signal(SIGHUP, SIG_IGN);
    Signal(SIGTERM, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGSEGV, SIG_DFL);

    /* unblock */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGILL);
    sigaddset(&sigset, SIGABRT);
    sigaddset(&sigset, SIGFPE);
    sigaddset(&sigset, SIGBUS);
    sigaddset(&sigset, SIGSEGV);
    sigaddset(&sigset, SIGXCPU);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    fprintf(stderr, "%d %d %d %.12s\n", (int)time4(NULL), getpid(), sig, cuser.userid);
#define CRASH_MSG ANSI_COLOR(0) \
    "\r\n程式異常, 立刻斷線. \r\n" \
    "請洽 " BN_BUGREPORT " 板詳述問題發生經過。\r\n"

#define XCPU_MSG ANSI_COLOR(0) \
    "\r\n程式耗用過多計算資源, 立刻斷線。\r\n" \
    "可能是 (a)執行太多耗用資源的動作 或 (b)程式掉入無窮迴圈. "\
    "請洽 " BN_BUGREPORT " 板詳述問題發生經過。\r\n"

    if(sig==SIGXCPU)
	write(1, XCPU_MSG, sizeof(XCPU_MSG));
    else
	write(1, CRASH_MSG, sizeof(CRASH_MSG));

    sleep(5);

    /* close all file descriptors (including the network connection) */
    for (i = 0; i < 256; ++i)
	close(i);

    /* log */
    /* assume vsnprintf() in log_file() is signal-safe, is it? */
    log_filef("log/crash.log", LOG_CREAT, 
	    "%d %d %d %.12s\n", (int)time4(NULL), getpid(), sig, cuser.userid);

    /* try logout... not a good idea, maybe crash again. now disabled */
    /*
    if (currmode) {
	currmode = 0;
	u_exit("AXXED");
    }
    */

#ifdef DEBUGSLEEP

#ifndef VALGRIND
    setproctitle("debug me!(%d)(%s,%d)", sig, cuser.userid, currstat);
#endif
    /* do this manually to prevent broken stuff */
    /* will broken currutmp cause problems here? hope not... */
    if(currutmp && strncmp(cuser.userid, currutmp->userid, IDLEN) == EQUSTR)
	currutmp->mode = DEBUGSLEEPING;

    sleep(DEBUGSLEEP_SECONDS);
#endif

    exit(0);
}

#ifdef CPULIMIT_PER_DAY
static void
signal_xcpu_handler(int sig)
{
    static time_t last_time_exceeded = 0;
    struct rlimit   rml;
    int margin_for_handler = 5;
    bool give_more_time = true;

    if (last_time_exceeded == 0)
	last_time_exceeded = login_start_time;
    assert(last_time_exceeded);
    // 不用 (time(0) - login_start_time) 來平均, 避免用好幾天之後突然狂吃 cpu 的狀況.
    if (time(0) - last_time_exceeded < DAY_SECONDS)
	give_more_time = false;
    last_time_exceeded = time(0);

    getrlimit(RLIMIT_CPU, &rml);

    // reach hard limit
    if (rml.rlim_cur + CPULIMIT_PER_DAY + margin_for_handler > rml.rlim_max)
	give_more_time = false;

    if (!give_more_time) {
	abort_bbs_debug(sig);
	assert(0); // shout not reach here
    }

    rml.rlim_cur += CPULIMIT_PER_DAY;
    setrlimit(RLIMIT_CPU, &rml);
}
#endif

/* 登錄 BBS 程式 */
static void
mysrand(void)
{
    srandom(time(NULL) + getpid());	/* 時間跟 pid 當 rand 的 seed */
}

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
	prints(ANSI_COLOR(33;41) "★%s" ANSI_COLOR(34;47) " [%s] %s " ANSI_COLOR(0) "",
		 SHM->uinfo[currutmp->destuip].userid, Cdatelite(&now),
		 (currutmp->sig == 2) ? "重要消息廣播！(請Ctrl-U,l查看熱訊記錄)"
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
    char            buf[200];
#ifdef PLAY_ANGEL
    if (currutmp->msgs[which].msgmode == MSGMODE_TOANGEL)
	snprintf(buf, sizeof(buf), ANSI_COLOR(1;37;46) "★%s" ANSI_COLOR(37;45) " %s " ANSI_RESET,
		currutmp->msgs[which].userid, currutmp->msgs[which].last_call_in);
    else
#endif
    snprintf(buf, sizeof(buf), ANSI_COLOR(1;33;46) "★%s" ANSI_COLOR(37;45) " %s " ANSI_RESET,
	     currutmp->msgs[which].userid, currutmp->msgs[which].last_call_in);
    outmsg(buf);

    if (save) {
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

static int
add_history(const msgque_t * msg)
{
    int             i = 0, j, waterinit = 0;
    water_t        *tmp;
    check_water_init();
    if (PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW))
	add_history_water(&water[0], msg);
    if (PAGER_UI_IS(PAGER_UI_NEW) || PAGER_UI_IS(PAGER_UI_OFO)) {
	for (i = 0; i < 5 && swater[i]; i++)
	    if (swater[i]->pid == msg->pid
#ifdef PLAY_ANGEL
		    && swater[i]->msg[0].msgmode == msg->msgmode
		    /* When throwing waterball to angel directly */
#endif
	       	)
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
#ifdef NOKILLWATERBALL
    if( reentrant_write_request ) /* kill again by shmctl */
	return;
    reentrant_write_request = 1;
#endif
    syncnow();
    check_water_init();
    if (PAGER_UI_IS(PAGER_UI_OFO)) {
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
#ifdef NOKILLWATERBALL
    reentrant_write_request = 0;
    currutmp->wbtime = 0; /* race */
#endif
}

static userinfo_t*
getotherlogin(int num)
{
    userinfo_t *ui;
    do {
	if (!(ui = (userinfo_t *) search_ulistn(usernum, num)))
	    return NULL;		/* user isn't logged in */

	/* skip sleeping process, this is slow if lots */
	if(ui->mode == DEBUGSLEEPING)
	    num++;
	else if(ui->pid <= 0)
	    num++;
	else if(kill(ui->pid, 0) < 0)
	    num++;
	else
	    break;
    } while (1);

    return ui;
}

static void
multi_user_check(void)
{
    register userinfo_t *ui;
    char            genbuf[3];

    if (HasUserPerm(PERM_SYSOP))
	return;			/* don't check sysops */

    srandom(getpid());
    // race condition here, sleep may help..?
    if (cuser.userlevel) {
	usleep(random()%1000000); // 0~1s
	ui = getotherlogin(1);
	if(ui == NULL)
	    return;

	move(b_lines-6, 0); clrtobot();
	outs("\n" ANSI_COLOR(1) 
		"注意: 您有其它連線已登入此帳號。\n"
		" 同時多次登入同個帳號可能導致文章數或" MONEYNAME "幣異常，\n"
		" 且本站不受理因多重登入造成的損失。" ANSI_RESET);

	getdata(b_lines - 1, 0, "您想刪除其他重複登入的連線嗎？[Y/n] ",
		genbuf, 3, LCECHO);

	usleep(random()%5000000); // 0~5s
	if (genbuf[0] != 'n') {
	    do {
		// scan again, old ui may be invalid
		ui = getotherlogin(1);
		if(ui==NULL)
		    return;
		if (ui->pid > 0) {
		    if(kill(ui->pid, SIGHUP)<0) {
			perror("kill SIGHUP fail");
			break;
		    }
		    log_usies("KICK ", cuser.nickname);
		}  else {
		    fprintf(stderr, "id=%s ui->pid=0\n", cuser.userid);
		}
		usleep(random()%2000000+1000000); // 1~3s
	    } while(getotherlogin(3) != NULL);
	} else {
	    /* deny login if still have 3 */
	    if (getotherlogin(3) != NULL) {
		sleep(1);
		abort_bbs(0);	/* Goodbye(); */
	    }
	}
    } else {
	/* allow multiple guest user */
	if (search_ulistn(usernum, MAX_GUEST) != NULL) {
	    sleep(1);
	    vmsg("抱歉，目前已有太多 guest 在站上, 請用new註冊。");
	    exit(1);
	}
    }
}

void 
mkuserdir(const char *userid)
{
    char genbuf[PATHLEN];
    sethomepath(genbuf, userid);
    // assume it is a dir, so just check if it is exist
    if (access(genbuf, F_OK) != 0)
	Mkdir(genbuf);
}

static int
load_current_user(const char *uid)
{
    // userid should be already normalized.

    // ----------------------------------------------------- NEW ACCOUNT 

#ifdef STR_REGNEW
    if (strcasecmp(uid, STR_REGNEW) == 0) 
    {

# ifndef LOGINASNEW
	assert(false);
	exit(0);
# endif // !LOGINASNEW

	new_register();
	mkuserdir(cuser.userid);
	reginit_fav();
    } else 
#endif

    // --------------------------------------------------- GUEST ACCOUNT 
    
#ifdef STR_GUEST
    if (strcasecmp(uid, STR_GUEST) == 0)
    {
	if (initcuser(STR_GUEST)< 1) exit (0) ;
	pwcuInitGuestPerm();
	// can we prevent mkuserdir() here?
	mkuserdir(cuser.userid);
    } else 
#endif

    // ---------------------------------------------------- USER ACCOUNT 
    {
	if (!cuser.userid[0] && initcuser(uid) < 1) exit(0);

#ifdef LOCAL_LOGIN_MOD
	LOCAL_LOGIN_MOD();
#endif
	if (strcasecmp(str_sysop, cuser.userid) == 0){
#ifdef NO_SYSOP_ACCOUNT
	    exit(0);
#else /* 自動加上各個主要權限 */
	    // TODO only allow in local connection?
	    pwcuInitAdminPerm();
#endif
	}
	/* 早該有 home 了, 不知道為何有的帳號會沒有, 被砍掉了? */
	mkuserdir(cuser.userid);
	logattempt(cuser.userid, ' ', login_start_time, fromhost);
    }

    // check multi user
    multi_user_check();
    return 1;
}

static void
login_query(char *ruid)
{
#ifdef CONVERT
    /* uid 加一位, for gb login */
    char            uid[IDLEN + 2];
    int		    len;
#else
    char            uid[IDLEN + 1];
#endif

    char	    passbuf[PASSLEN];
    int             attempts;

    resolve_garbage();

#ifdef DEBUG
    move(1, 0);
    prints("debugging mode\ncurrent pid: %d\n", getpid());
#else
    show_file("etc/Welcome", 1, -1, SHOWFILE_ALLOW_ALL);
#endif

    attempts = 0;
    while (1) {
	if (attempts++ >= LOGINATTEMPTS) {
	    more("etc/goodbye", NA);
	    pressanykey();
	    sleep(3);
	    exit(1);
	}
	pwcuInitZero();

#ifdef DEBUG
	move(19, 0);
	prints("current pid: %d ", getpid());
#endif

	if (getdata(20, 0, "請輸入代號，或以 guest 參觀，或以 new 註冊: ",
		uid, sizeof(uid), DOECHO) < 1)
	{
	    // got nothing 
	    outs("請重新輸入。\n");
	    continue;
	}
	telnet_turnoff_client_detect();

#ifdef CONVERT
	/* switch to gb mode if uid end with '.' */
	len = strlen(uid);
	if (uid[0] && uid[len - 1] == '.') {
	    set_converting_type(CONV_GB);
	    uid[len - 1] = 0;
	    redrawwin();
	}
	else if (uid[0] && uid[len - 1] == ',') {
	    set_converting_type(CONV_UTF8);
	    uid[len - 1] = 0;
	    redrawwin();
	}
	else if (len >= IDLEN + 1)
	    uid[IDLEN] = 0;
#endif

	if (!is_validuserid(uid)) {

	    outs(err_uid);

#ifdef STR_GUEST
	} else if (strcasecmp(uid, STR_GUEST) == 0) {	/* guest */

	    strlcpy(ruid, STR_GUEST, IDLEN+1);
	    break;
#endif

#ifdef STR_REGNEW
	} else if (strcasecmp(uid, STR_REGNEW) == 0) {
# ifndef LOGINASNEW
	    outs("本系統目前無法以 " STR_REGNEW " 註冊"
#  ifdef STR_GUEST
		 ", 請用 " STR_GUEST " 進入"
#  endif  // STR_GUEST
		 "\n");
	    continue;
# endif // !LOGINASNEW

	    strlcpy(ruid, STR_REGNEW, IDLEN+1);
	    break;

#endif // STR_REGNEW

	} else {

	    /* normal user */
	    getdata(21, 0, MSG_PASSWD,
		    passbuf, sizeof(passbuf), NOECHO);
	    passbuf[8] = '\0';

	    move (22, 0); clrtoeol();
	    outs("正在檢查密碼...");
	    move(22, 0); refresh(); 

	    /* prepare for later */
	    clrtoeol();

	    if( initcuser(uid) < 1 || !cuser.userid[0] ||
		!checkpasswd(cuser.passwd, passbuf) ){

		if(is_validuserid(cuser.userid))
		    logattempt(cuser.userid , '-', login_start_time, fromhost);
		sleep(1);
		outs(ERR_PASSWD);

	    } else {

		strlcpy(ruid, cuser.userid, IDLEN+1);
		outs("密碼正確！ 開始登入系統..."); 
		move(22, 0); refresh();
		clrtoeol();
		break;
	    }
	}
    }

    // auth ok.
#ifdef DETECT_CLIENT
    LogClientCode();
#endif
}

void
add_distinct(const char *fname, const char *line)
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
	rename(tmpname, fname);
    }
}

void
del_distinct(const char *fname, const char *line, int casesensitive)
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
	    if(casesensitive)
	    {
		if (!strcmp(buffer, line))
		    break;
	    } else {
		if (!strcasecmp(buffer, line))
		    break;
	    }
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
	rename(tmpname, fname);
    }
}

#if defined(WHERE) && !defined(FROMD)
static int
where(const char *from)
{
    int i;
    uint32_t ipaddr = ipstr2int(from);

    resolve_fcache();

    for (i = 0; i < SHM->home_num; i++) {
	if ((SHM->home_ip[i] & SHM->home_mask[i]) == (ipaddr & SHM->home_mask[i])) {
	    return i;
	}
    }
    return 0;
}
#endif

static void
check_BM(void)
{
    int i;

    assert(HasUserPerm(PERM_BM));
    for( i = 0 ; i < numboards ; ++i )
	if( is_BM_cache(i + 1) ) /* XXXbid */
	    return;

    // disable BM permission
    pwcuBitDisableLevel(PERM_BM);
}

static void
setup_utmp(int mode)
{
    /* NOTE, 在 getnewutmpent 之前不應該有任何 slow/blocking function */
    userinfo_t      uinfo = {0};
    uinfo.pid	    = currpid = getpid();
    uinfo.uid	    = usernum;
    uinfo.mode	    = currstat = mode;

    uinfo.userlevel = cuser.userlevel;
    uinfo.lastact   = time(NULL);

#ifdef USE_USER_SEX
    uinfo.sex	    = cuser.sex % 8;
#endif

    // only enable this after you've really changed talk.c to NOT use from_alias.
    uinfo.from_ip   = inet_addr(fromhost);

    strlcpy(uinfo.userid,   cuser.userid,   sizeof(uinfo.userid));
    strlcpy(uinfo.nickname, cuser.nickname, sizeof(uinfo.nickname));
    strlcpy(uinfo.from,	    fromhost,	    sizeof(uinfo.from));

    // 當初設計的人把 mind 設計成非 NULL terminated 的...
    memcpy(uinfo.mind,	    cuser.mind,	    sizeof(cuser.mind));

    uinfo.five_win  = cuser.five_win;
    uinfo.five_lose = cuser.five_lose;
    uinfo.five_tie  = cuser.five_tie;
    uinfo.chc_win   = cuser.chc_win;
    uinfo.chc_lose  = cuser.chc_lose;
    uinfo.chc_tie   = cuser.chc_tie;
    uinfo.chess_elo_rating = cuser.chess_elo_rating;
    uinfo.go_win    = cuser.go_win;
    uinfo.go_lose   = cuser.go_lose;
    uinfo.go_tie    = cuser.go_tie;
    uinfo.invisible = cuser.invisible % 2;
    uinfo.pager	    = cuser.pager % PAGER_MODES;
    uinfo.withme    = cuser.withme & ~WITHME_ALLFLAG;

    if(cuser.withme & (cuser.withme<<1) & (WITHME_ALLFLAG<<1))
	uinfo.withme = 0;

    getnewutmpent(&uinfo);

    //////////////////////////////////////////////////////////////////
    // 以下可以進行比較花時間的運算。
    //////////////////////////////////////////////////////////////////

    currmode = MODE_STARTED;
    SHM->UTMPneedsort = 1;

    strip_nonebig5((unsigned char *)currutmp->nickname, sizeof(currutmp->nickname));
    strip_nonebig5((unsigned char *)currutmp->mind, sizeof(currutmp->mind));

    // resolve fromhost
#if defined(WHERE)

# ifdef FROMD
    {
	int fd;
	if ( (fd = toconnect(FROMD_ADDR)) >= 0 ) {
	    write(fd, fromhost, strlen(fromhost));
	    // zero and reuse uinfo.from to check real data
	    memset(uinfo.from, 0, sizeof(uinfo.from));
	    read(fd, uinfo.from,  sizeof(uinfo.from) - 1); // keep trailing zero
	    close(fd);
	    // copy back to currutmp
	    if (uinfo.from[0])
		strlcpy(currutmp->from, uinfo.from, sizeof(currutmp->from));
	}
    }
# else  // !FROMD
    {
	int desc = where(fromhost);
	if (desc > 0)
	    strlcpy(currutmp->from, SHM->home_desc[desc], sizeof(currutmp->from));
    }
# endif // !FROMD

#endif // WHERE

    /* Very, very slow friend_load. */
    if( strcmp(cuser.userid, STR_GUEST) != 0 ) // guest 不處理好友
	friend_load(0, 1);

    nice(3);
}

inline static void welcome_msg(void)
{
    prints(ANSI_RESET "      歡迎您再度拜訪本站，上次您是從 " 
	    ANSI_COLOR(1;33) "%s" ANSI_COLOR(0;37) " 連往本站，" 
	    ANSI_CLRTOEND "\n"
	    "     我記得那天是 " ANSI_COLOR(1;33) "%s" ANSI_COLOR(0;37) "。"
	    ANSI_CLRTOEND "\n"
	    ANSI_CLRTOEND "\n"
	    ,
	    cuser.lasthost, Cdate(&(cuser.lastlogin)));
    pressanykey();
}

inline static void check_bad_login(void)
{
    char            genbuf[200];
    setuserfile(genbuf, FN_BADLOGIN);
    if (more(genbuf, NA) != -1) {
	move(b_lines - 3, 0);
	outs("通常並沒有辦法知道該ip是誰所有, "
		"以及其意圖(是不小心按錯或有意測您密碼)\n"
		"若您有帳號被盜用疑慮, 請經常更改您的密碼或使用加密連線");
	if (vans("您要刪除以上錯誤嘗試的記錄嗎? [y/N] ") == 'y')
	    unlink(genbuf);
    }
}

inline static void append_log_recent_login()
{
    char buf[STRLEN], logfn[PATHLEN];
    int szlogfn = 0, szlogentry = 0;

    // prepare log format
    snprintf(buf, sizeof(buf), "%s %-15s\n", 
	    Cdatelite(&login_start_time), fromhost);
    szlogentry = strlen(buf);	// should be the same for all entries
    
    setuserfile(logfn, FN_RECENTLOGIN);
    szlogfn = dashs(logfn);
    if (szlogfn > SZ_RECENTLOGIN) {
	// rotate to 1/4 of SZ_RECENTLOGIN
	delete_records(logfn, szlogentry, 1, 
		(szlogfn-(SZ_RECENTLOGIN/4)) / szlogentry);
    }
    log_file(logfn, LOG_CREAT, buf);
}

inline static void birthday_make_a_wish(const struct tm *ptime, const struct tm *tmp)
{
    if (tmp->tm_mday != ptime->tm_mday) {
	more("etc/birth.post", YEA);
	if (enter_board("WhoAmI")==0) {
	    do_post();
	}
    }
}

inline static void check_mailbox_quota(void)
{
    if (chkmailbox())
	m_read();
}

static void init_guest_info(void)
{
    pwcuInitGuestInfo();
    currutmp->pager = PAGER_DISABLE;
}

#if FOREIGN_REG_DAY > 0
inline static void foreign_warning(void){
    if ((HasUserFlag(UF_FOREIGN)) && !(HasUserFlag(UF_LIVERIGHT))){
	if (login_start_time - cuser.firstlogin > (FOREIGN_REG_DAY - 5) * 24 * 3600){
	    mail_muser(cuser, "[出入境管理局]", "etc/foreign_expired_warn");
	}
	else if (login_start_time - cuser.firstlogin > FOREIGN_REG_DAY * 24 * 3600){
	    pwcuBitDisableLevel(PERM_LOGINOK | PERM_POST);
	    vmsg("警告：請至出入境管理局申請永久居留");
	}
    }
}
#endif

// XXX temporary...
int query_adbanner_usong_pref_changed(const userec_t *u, char force_yn);

static void
user_login(void)
{
    struct tm       ptime, lasttime;
    int             nowusers, ifbirth = 0, i;

    /* NOTE! 在 setup_utmp 之前, 不應該有任何 blocking/slow function,
     * 否則可藉機 race condition 達到 multi-login */

    /* resolve_boards(); */
    numboards = SHM->Bnumber;

    /* 初始化 uinfo、flag、mode */
    setup_utmp(LOGIN);

    /* log usies */
    log_usies("ENTER", fromhost);
#ifndef VALGRIND
    setproctitle("%s: %s", margs, cuser.userid);
#endif

    /* get local time */
    localtime4_r(&now, &ptime);
    localtime4_r(&cuser.lastlogin, &lasttime);

    redrawwin();

    /* mask fromhost a.b.c.d to a.b.c.* */
    strlcpy(fromhost_masked, fromhost, sizeof(fromhost_masked));
    obfuscate_ipstr(fromhost_masked);

    /* show welcome_login */
    if( (ifbirth = (ptime.tm_mday == cuser.day &&
		    ptime.tm_mon + 1 == cuser.month)) ){
	char buf[PATHLEN];
	snprintf(buf, sizeof(buf), "etc/Welcome_birth.%d", getHoroscope(cuser.month, cuser.day));
	more(buf, NA);
    }
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
    refresh();
    currutmp->alerts |= load_mailalert(cuser.userid);

    if ((nowusers = SHM->UTMPnumber) > SHM->max_user) {
	SHM->max_user = nowusers;
	SHM->max_time = now;
    }

    if (!(HasUserPerm(PERM_SYSOP) && HasUserPerm(PERM_SYSOPHIDE)) &&
	!currutmp->invisible)
    {
	/* do_aloha is costly. do it later? */
	do_aloha("<<上站通知>> -- 我來啦！");
    }

    if (SHM->loginmsg.pid){
        if(search_ulist_pid(SHM->loginmsg.pid))
	    getmessage(SHM->loginmsg);
        else
	    SHM->loginmsg.pid=0;
    }

    if (cuser.userlevel) {	/* not guest */
	move(t_lines - 4, 0);
	clrtobot();
	welcome_msg();
	resolve_over18();

	if( ifbirth ){
	    birthday_make_a_wish(&ptime, &lasttime);
	    if( vans("是否要顯示「壽星」於使用者名單上？(y/N)") == 'y' )
		currutmp->birth = 1;
	}

	append_log_recent_login();
	check_bad_login();
	check_mailbox_quota();
	check_register();
	pwcuLoginSave();	// is_first_login_of_today is only valid after pwcuLoginSave.
	// cuser.lastlogin 由 pwcuLoginSave 後值就變了，要看 last_login_time
	restore_backup();

	// XXX 這個 check 花不少時間，有點間隔比較好
	if (HasUserPerm(PERM_BM) && 
	    (cuser.numlogindays % 10 == 0) &&	// when using numlogindays, check with is_first_login_of_today
	    is_first_login_of_today )
	    check_BM();		/* 自動取下離職板主權力 */

	// XXX only for temporary...
#ifdef ADBANNER_USONG_TIMEBOMB
	if (last_login_time < ADBANNER_USONG_TIMEBOMB)
	{
	    if (query_adbanner_usong_pref_changed(cuser_ref, 1))
		pwcuToggleUserFlag(UF_ADBANNER_USONG);
	}
#endif

    } else if (strcmp(cuser.userid, STR_GUEST) == 0) { /* guest */

	init_guest_info();
	pressanykey();

    } else {
	// XXX no userlevel, no guest - what is this?
	clear();
	outs("抱歉，您的帳號資料異常或已被停權。\n");
	pressanykey();
	exit(1);
	// check_mailbox_quota();
    }

    if(ptime.tm_yday!=lasttime.tm_yday)
	STATINC(STAT_TODAYLOGIN_MAX);

    if (!PERM_HIDE(currutmp)) {
	/* If you wanna do incremental upgrade
	 * (like, added a function/flag that wants user to confirm againe)
	 * put it here.
	 * But you must use 'lasttime' because cuser.lastlogin
	 * is already changed.
	 */

	/* login time update */
	if(ptime.tm_yday!=lasttime.tm_yday)
	    STATINC(STAT_TODAYLOGIN_MIN);
    }

#if FOREIGN_REG_DAY > 0
    foreign_warning();
#endif

    if(HasUserFlag(UF_FAV_ADDNEW)) {
	fav_load();
	if (get_fav_root() != NULL) {
	    int num;
	    num = updatenewfav(1);
	    if (num > NEW_FAV_THRESHOLD &&
		vansf("找到 %d 個新看板，確定要加入我的最愛嗎？[Y/n]", num) == 'n') {
		fav_free();
		fav_load();
	    }
	}
    }

    for (i = 0; i < NUMVIEWFILE; i++)
    {
	if ((cuser.loginview >> i) & 1)
	{
	    const char *fn = loginview_file[(int)i][0];
            // fn == '\0': ignore; NULL: break;
	    if (!fn)
		break;

            switch (*fn) {
                case '\0':
                    // simply ignore it.
                    break;

                case '@':
                    // since only one special now, let's write directly...
                    if (strcmp(fn, "@calendar") == 0)
                        calendar();
                    break;

                default:
                    // use NA+pause or YEA?
                    more(fn, YEA);
                    break;
	    }
	}
    }
}

static void
do_aloha(const char *hello)
{
    FILE           *fp;
    char            userid[80];
    char            genbuf[200];

    setuserfile(genbuf, "aloha");
    if ((fp = fopen(genbuf, "r"))) {
	while (fgets(userid, 80, fp)) {
	    userinfo_t     *uentp;
	    if ((uentp = (userinfo_t *) search_ulist_userid(userid)) && 
		    isvisible(uentp, currutmp)) {
		my_write(uentp->pid, hello, uentp->userid, WATERBALL_ALOHA, uentp);
	    }
	}
	fclose(fp);
    }
}

static void
do_term_init(enum TermMode term_mode, int w, int h)
{
    term_init();
    initscr();

    // if the terminal was already determined, resize for it.
    if ((w && (w != t_columns)) || 
	(h && (h != t_lines  )) )
    {
	term_resize(w, h);
    }

    if (term_mode == TermMode_TTY)
	raise(SIGWINCH);
}

static int
start_client(struct ProgramOption *option)
{
#ifdef CPULIMIT_PER_DAY
    struct rlimit   rml;
    getrlimit(RLIMIT_CPU, &rml);
    rml.rlim_cur = CPULIMIT_PER_DAY;
    setrlimit(RLIMIT_CPU, &rml);
#endif

    STATINC(STAT_LOGIN);
    /* system init */
    nice(2);			/* Ptt: lower priority */
    login_start_time = time(0);
    currmode = 0;

    Signal(SIGHUP, abort_bbs);
    Signal(SIGTERM, abort_bbs);
    Signal(SIGPIPE, abort_bbs);

    Signal(SIGINT, abort_bbs_debug);
    Signal(SIGQUIT, abort_bbs_debug);
    Signal(SIGILL, abort_bbs_debug);
    Signal(SIGABRT, abort_bbs_debug);
    Signal(SIGFPE, abort_bbs_debug);
    Signal(SIGBUS, abort_bbs_debug);
    Signal(SIGSEGV, abort_bbs_debug);
#ifdef CPULIMIT_PER_DAY
    Signal(SIGXCPU, signal_xcpu_handler);
#endif

    signal_restart(SIGUSR1, talk_request);
    signal_restart(SIGUSR2, write_request);

    Signal(SIGALRM, abort_bbs);
    alarm(600);

    mysrand(); /* 初始化: random number 增加user跟時間的差異 */
    now = time(0);

    // if flag_user contains an uid, it is already authorized.
    if (!option->flag_user[0])
    {
	// query user
	login_query(option->flag_user);
    }
    // process new, register, and load user data
    load_current_user(option->flag_user);
    last_login_time = cuser.lastlogin;	// keep a backup

    m_init();			/* init the user mail path */
    user_login();
    auto_close_polls();		/* 自動開票 */

    Signal(SIGALRM, SIG_IGN);
    return 0;
}

static void
getremotename(const struct in_addr fromaddr, char *rhost)
{
    /* get remote host name */
    XAUTH_HOST(strcpy(rhost, (char *)inet_ntoa(fromaddr)));
}

static int 
set_connection_opt(int sock)
{
    const int szrecv = 1024, szsend=4096;
    const struct linger lin = {0};

    // keep alive: server will check target connection (default 2 hours)
    const int on = 1; 
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void*)&on, sizeof(on));

#if defined(SOL_TCP) && defined(TCP_KEEPIDLE)
    {
	const int idle = 300*2; // experimental, minimal keep alive check
	setsockopt(sock, SOL_TCP,    TCP_KEEPIDLE, (void*)&idle, sizeof(idle));
    }
#endif
   
    // fast close
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));

    // adjust transmission window
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&szrecv, sizeof(szrecv));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void*)&szsend, sizeof(szsend));
    OPTIMIZE_SOCKET(sock);

    return 0;
}

static int
set_bind_opt(int sock)
{
    const int on = 1;

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on));
    set_connection_opt(sock);

    return 0;
}

static int
bind_port(int port)
{
    int    sock;
    struct sockaddr_in xsin;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    set_bind_opt(sock);

    xsin.sin_family = AF_INET;
    xsin.sin_addr.s_addr = htonl(INADDR_ANY);
    xsin.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *) & xsin, sizeof xsin) < 0) {
	syslog(LOG_ERR, "bbsd bind_port can't bind to %d", port);
	exit(1);
    }
    if (listen(sock, SOCKET_QLEN) < 0) {
	syslog(LOG_ERR, "bbsd bind_port can't listen to %d", port);
	exit(1);
    }
    return sock;
}


/*******************************************************/


static int      shell_login  (char *argv0, struct ProgramOption *option);
static int      daemon_login (char *argv0, struct ProgramOption *option);
static int      tunnel_login (char *argv0, struct ProgramOption *option);
static int      check_ban_and_load(int fd, struct ProgramOption *option);
static int	start_client (             struct ProgramOption *option);
static int      check_banip  (char *host);

static void init(void)
{
    start_time = time(NULL);

    /* avoid SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);

    /* avoid erroneous signal from other mbbsd */
    Signal(SIGUSR1, SIG_IGN);
    Signal(SIGUSR2, SIG_IGN);

#if defined(__GLIBC__) && defined(CRITICAL_MEMORY)
    #define MY__MMAP_THRESHOLD (1024 * 8)
    #define MY__MMAP_MAX (0)
    #define MY__TRIM_THRESHOLD (1024 * 8)
    #define MY__TOP_PAD (0)

    mallopt (M_MMAP_THRESHOLD, MY__MMAP_THRESHOLD);
    mallopt (M_MMAP_MAX, MY__MMAP_MAX);
    mallopt (M_TRIM_THRESHOLD, MY__TRIM_THRESHOLD);
    mallopt (M_TOP_PAD, MY__TOP_PAD);
#endif

#ifdef CONVERT
    init_convert();
#endif
    
}

static void usage(char *argv0)
{
    fprintf(stdout,
	    "Usage: %s { -d | -D } [options]\n"
	    "\n"
	    "daemon mode\n"
	    "\t-d                 use daemon mode, imply -t telnet\n"
	    "\t-n tunnel          enable tunnel mode, imply -d\n"
	    "\t-p port            listen port\n"
	    "\t-l fd              pre-listen fd\n"
	    "\t-f bindport_conf   read from configuration file\n"
	    "\n"
	    "non-daemon mode\n"
	    "\t-D                 use non-daemon mode, imply -t tty\n"
	    "\t-h hostip          hostip (default 127.0.0.1)\n"
	    "\t-b                 bypass opening and ask password directly\n"
	    "\t-u user            for -b: user (default guest)\n"
	    "\n"
	    "flags\n"
	    "\t-t type            terminal mode, telnet | tty\n"
	    "\t-e encoding        encoding (default big5), big5 | gb | utf8\n"
	    "\n"
	    "testing flags\n"
	    "\t-F                 don't fork\n"
	    "\t-C                 don't check load\n"
	    "\n",
	    argv0
	  );
}

bool parse_bindport_conf(const char *path, struct ProgramOption *option)
{
    FILE *fp = fopen(path, "rt");
    char  buf[PATHLEN], vprogram[STRLEN], vport[STRLEN], vopt[STRLEN];
    int   port = 0;
    if (!fp)
	return false;

    while (fgets(buf, sizeof(buf), fp))
    {
	if (sscanf(buf, "%s%s", vprogram, vport) != 2 ||
	    strcmp(vprogram, "mbbsd") != 0)
	    continue;

	if (strcmp(vport, "tunnel") == 0)
	{
	    if (sscanf(buf, "%*s%*s%s", vopt) != 1 || !*vopt)
	    {
		fprintf(stderr, "error: invalid tunnel setting in %s.\r\n",
			path);
		exit(1);
	    }
	    option->tunnel_mode = true;
	    if (option->flag_tunnel_path)
	    {
		fprintf(stderr, "warning: ignore tunnel path specified in %s.\r\n",
			path);
		continue;
	    }
	    option->flag_tunnel_path = strdup(vopt);
	    continue;
	}

	// normal port?
	port = atoi(vport);
	if (!port)
	{
	    fprintf(stderr,"warning: unknown setting: %s\r\n", buf);
	    continue;
	}
	if (option->nport >= MAX_BINDPORT)
	{
	    fprintf(stderr, "too many port (>%d)\n", MAX_BINDPORT);
	    exit(1);
	}
	// add port
	option->port[option->nport++] = port;
    }

    fclose(fp);
    return true;
}

bool parse_argv(int argc, char *argv[], struct ProgramOption *option)
{
    int ch;
    bool given_mode = false;

    // init options
    memset(option, 0, sizeof(*option));
    option->flag_listenfd = -1;
    option->flag_checkload = true;

    while ((ch = getopt(argc, argv, "dn:p:f:l:Dt:h:e:bu:FC")) != -1) {
	switch (ch) {
	    case 'n':
		option->tunnel_mode = true;
		if (option->flag_tunnel_path)
		{
		    fprintf(stderr, "warning: changed tunnel path to %s.\r\n", optarg);
		}
		free(option->flag_tunnel_path);
		option->flag_tunnel_path = strdup(optarg);
		// reuse 'd' mode, no break here.
	    case 'd':
		given_mode = true;
		option->daemon_mode = true;
		option->term_mode = TermMode_TELNET;
		option->flag_fork = true;
		break;
	    case 'f':
		if (!parse_bindport_conf(optarg, option))
		{
		    perror(optarg);
		    exit(1);
		}
		break;
	    case 'p':
		if (option->nport < MAX_BINDPORT) {
		    int port = atoi(optarg);
		    option->port[option->nport++] = port;
		} else {
		    fprintf(stderr, "too many port (>%d)\n", MAX_BINDPORT);
		    exit(1);
		}
		break;
	    case 'l':
		option->flag_listenfd = atoi(optarg);
		break;
	    case 'D':
		given_mode = true;
		option->daemon_mode = false;
		option->term_mode = TermMode_TTY;
		break;
	    case 't':
		if (strcmp(optarg, "telnet") == 0) {
		    option->term_mode = TermMode_TELNET;
		} else if (strcmp(optarg, "tty") == 0) {
		    option->term_mode = TermMode_TTY;
		} else {
		    fprintf(stderr, "unknown type: %s\n", optarg);
		    exit(1);
		}
		break;
	    case 'h':
		strlcpy(fromhost, optarg, sizeof(fromhost));
		break;
	    case 'e':
#ifdef CONVERT
		if (strcmp(optarg, "big5") == 0) {
		    set_converting_type(CONV_NORMAL);
		} else if (strcmp(optarg, "gb") == 0) {
		    set_converting_type(CONV_GB);
		} else if (strcmp(optarg, "utf8") == 0) {
		    set_converting_type(CONV_UTF8);
		} else {
		    fprintf(stderr, "unknown encoding: %s\n", optarg);
		    exit(1);
		}
#endif
		break;
	    case 'b':
		option->flag_bypass = true;
		// TODO
		fprintf(stderr, "not yet implemented\n");
		exit(1);
		break;
	    case 'u':
		strlcpy(option->flag_user, optarg, sizeof(option->flag_user));
		break;
	    case 'F':
		option->flag_fork = false;
		break;
	    case 'C':
		option->flag_checkload = false;
		break;
	    default:
		fprintf(stderr, "unknown option -%c\n", ch);
		return false;
	}
    }

    if (!given_mode) {
	fprintf(stderr, "please specify -d or -D mode\n");
	return false;
    }

    if (option->daemon_mode) {

	if (option->flag_listenfd >= 0) {
	    if (option->nport != 1) {
		fprintf(stderr, "for pre-binded fd, you should give 1 port number (for information)\n");
		return false;
	    }
	}

	if ( option->tunnel_mode && option->nport)
	{
	    // should we do so?
	    signal_restart(SIGCHLD, reapchild);

	    // dual mode: we need to fork.
	    if (fork() == 0)
	    {
		// ports daemon
		option->tunnel_mode = false;
		free(option->flag_tunnel_path);
		option->flag_tunnel_path = NULL;
	    } else {
		// tunnel mode daemon
		option->nport = 0;
	    }
	}

	if ( option->tunnel_mode && option->nport != 0) {
	    fprintf(stderr, "you cannot bind ports port in tunnel mode.\n");
	    return false;
	}

	if (!option->tunnel_mode && option->nport == 0) {
	    fprintf(stderr, "don't forget specify port number (-p)\n");
	    return false;
	}

	if (option->nport > 1 && !option->flag_fork) {
	    fprintf(stderr, "you can bind only 1 port with non-fork flag\n");
	    return false;
	}
    }


    return true;
}

int
main(int argc, char *argv[], char *envp[])
{
    bool oklogin = false;
    struct ProgramOption *option;

    init();

    option = (struct ProgramOption*) malloc(sizeof(struct ProgramOption));
    if (!parse_argv(argc, argv, option)) {
	usage(argv[0]);
	return 1;
    }
    initsetproctitle(argc, argv, envp);

    attach_SHM();

    if (!option->daemon_mode)
	oklogin = shell_login (argv[0], option);
    else if (option->tunnel_mode)
	oklogin = tunnel_login(argv[0], option);
    else
	oklogin = daemon_login(argv[0], option);

    if (!oklogin) {
	free_program_option(option);
	return 0;
    }

    do_term_init(option->term_mode, 
	    option->term_width, option->term_height);

    if (option->tunnel_mode)
    {
	// force reset attribute because tunnel may not have enough time
	// to flush output...
	output(ANSI_RESET, sizeof(ANSI_RESET)-1);
	move(b_lines-1, 0);
	outs("登入中，請稍候...");
	doupdate();
    }

    start_client(option);
    free_program_option(option);

    // tail recursion!
    return main_menu();
}

static int
shell_login(char *argv0, struct ProgramOption *option)
{
    int fd;

    STATINC(STAT_SHELLLOGIN);
    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);

#if defined(linux) && defined(DEBUG)
//    mtrace();
#endif

    snprintf(margs, sizeof(margs), "%s ssh ", argv0);
    close(2);
    /* don't close fd 1, at least init_tty need it */
    if(((fd = OpenCreate("log/stderr", O_WRONLY | O_APPEND)) >= 0) && 
       fd != 2 ){
	dup2(fd, 2);
	close(fd);
    }

    init_tty();

    // XXX overwrite fromhost here is better?
    if(getenv("SSH_CONNECTION") != NULL){
	char frombuf[50];
	sscanf(getenv("SSH_CONNECTION"), "%s", frombuf);
	strlcpy(fromhost, frombuf, sizeof(fromhost));
    }

    if (check_ban_and_load(0, option)) {
	sleep(10);
	return 0;
    }
#ifdef DETECT_CLIENT
    FNV1A_CHAR(123, client_code);
#endif
    return 1;
}

static int
tunnel_login(char *argv0, struct ProgramOption *option)
{
    int tunnel = 0, csock = 0;
    unsigned int pid;
    struct login_data dat = {0};
    char buf[PATHLEN];
    FILE *fp;

    /* setup standalone */
    start_daemon(option);
    pid = (unsigned int)getpid(); // the pid will be changed after start_daemon.
    signal_restart(SIGCHLD, reapchild);

    assert( option->flag_tunnel_path &&
	   *option->flag_tunnel_path);

    tunnel = toconnect(option->flag_tunnel_path);
    if (tunnel < 0)
    {
	syslog(LOG_ERR, "mbbsd tunnel connection failed: %s\n", 
		option->flag_tunnel_path);
	exit(1);
    }

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);

    // log pid
    snprintf(buf, sizeof(buf),
	     "run/mbbsd.%s.%u.pid", "tunnel",pid);
    if ((fp = fopen(buf, "w"))) {
	fprintf(fp, "%u\n", pid);
	fclose(fp);
    }

    // XXX on Linux, argv0 will change if you setproctitle.
    strlcpy(buf, argv0, sizeof(buf));

    /* proctitle */
#ifndef VALGRIND
    snprintf(margs, sizeof(margs), "%s tunnel(%u) ", buf, pid);
    setproctitle("%s: listening ", margs);
#endif

    /* main loop */
    while( 1 )
    {
	csock = recv_remote_fd(tunnel, option->flag_tunnel_path);

	// XXX use continue or return herer?
	if (csock < 0)
	{
	    return 0;
	}
	if (toread (tunnel, &dat, sizeof(dat)) < sizeof(dat) ||
	    towrite(tunnel, &dat.ack, sizeof(dat.ack)) < sizeof(dat.ack))
	    return 0;

	assert(dat.cb == sizeof(dat));

	// optimize connection
	set_connection_opt(csock);

	// mbbsd is using blocking mode (tunnel daemon may use non-blocking mode)
	fcntl(csock, F_SETFL, fcntl(csock, F_GETFL) & ~O_NONBLOCK);

	if (option->flag_fork) {
	    if (fork() == 0)
		break;
	    else
		close(csock);
	}
    }
    /* here is only child running */

#ifndef VALGRIND
    snprintf(margs, sizeof(margs), "%s tunnel(%u)-%s ", buf, pid, dat.port);
    setproctitle("%s: ...login wait... ", margs);
#endif
    close(tunnel);
    dup2(csock, 0);
    close(csock);
    dup2(0, 1);

    strlcpy(option->flag_user, dat.userid, sizeof(option->flag_user));
    strlcpy(fromhost, dat.hostip, sizeof(fromhost));
    listen_port = atoi(dat.port);
    option->term_width  = dat.t_cols;
    option->term_height = dat.t_lines;

#ifdef CONVERT
    if (dat.encoding)
	set_converting_type(dat.encoding);
#endif

    telnet_init(0);

#ifdef DETECT_CLIENT
    telnet_turnoff_client_detect();
    client_code = dat.client_code;  // use the client code detected by remote daemon
    LogClientCode();
#endif

    return 1;
}

static int
daemon_login(char *argv0, struct ProgramOption *option)
{
    int             msock = 0, csock;	/* socket for Master and Child */
    FILE           *fp;
    int             len_of_sock_addr, overloading = 0;
    char            buf[256];
#if OVERLOADBLOCKFDS
    int             blockfd[OVERLOADBLOCKFDS];
    int             nblocked = 0;
#endif
    struct sockaddr_in xsin;
    xsin.sin_family = AF_INET;

    /* setup standalone */
    start_daemon(option);
    signal_restart(SIGCHLD, reapchild);

    /* port binding */
    if (option->flag_listenfd < 0) {
	int i;
	assert(option->nport > 0);
	for (i = 0; i < option->nport; i++) {
	    listen_port = option->port[i];
	    if (i == option->nport - 1 || fork() == 0) {
		if( (msock = bind_port(listen_port)) < 0 ){
		    syslog(LOG_ERR, "mbbsd bind_port failed.\n");
		    exit(1);
		}
		break;
	    }
	}
    } else {
	msock = option->flag_listenfd;
	assert(option->nport == 1);
	listen_port = option->port[0];
    }

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);
    chdir(BBSHOME);

    /* proctitle */
#ifndef VALGRIND
    snprintf(margs, sizeof(margs), "%s %d ", argv0, listen_port);
    setproctitle("%s: listening ", margs);
#endif

#ifdef PRE_FORK
    if (option->flag_fork) {
	if( listen_port == 23 ){ // only pre-fork in port 23
	    int i;
	    for( i = 0 ; i < PRE_FORK ; ++i )
		if( fork() <= 0 )
		    break;
	}
    }
#endif

    snprintf(buf, sizeof(buf),
	     "run/mbbsd.%d.%d.pid", listen_port, (int)getpid());
    if ((fp = fopen(buf, "w"))) {
	fprintf(fp, "%d\n", (int)getpid());
	fclose(fp);
    }

    /* main loop */
    while( 1 ){
	len_of_sock_addr = sizeof(xsin);
	if ( (csock = accept(msock, (struct sockaddr *)&xsin,
			(socklen_t *)&len_of_sock_addr)) < 0 ) {
	    if (errno != EINTR)
		sleep(1);
	    continue;
	}

	overloading = check_ban_and_load(csock, option);
#if OVERLOADBLOCKFDS
	if( (!overloading && nblocked) ||
	    (overloading && nblocked == OVERLOADBLOCKFDS) ){
	    int i;
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

	if (option->flag_fork) {
	    if (fork() == 0)
		break;
	    else
		close(csock);
	} else {
	    break;
	}
    }
    /* here is only child running */

#ifndef VALGRIND
    setproctitle("%s: ...login wait... ", margs);
#endif
    close(msock);
    dup2(csock, 0);
    close(csock);
    dup2(0, 1);

    XAUTH_GETREMOTENAME(getremotename(xsin.sin_addr, fromhost));

    if( check_banip(fromhost) ){
	sleep(10);
	exit(0);
    }
    telnet_init(1);
    return 1;
}

/*
 * check if we're banning login and if the load is too high. if login is
 * permitted, return 0; else return -1; approriate message is output to fd.
 */
static int
check_ban_and_load(int fd, struct ProgramOption *option)
{
    FILE           *fp;
    static time4_t   chkload_time = 0;
    static int      overload = 0;	/* overload or banned, update every 1
					 * sec  */
    static int      banned = 0;

    // if you have your own banner, define as INSCREEN in pttbbs.conf
    // if you don't want anny benner, define NO_INSCREEN
#ifndef NO_INSCREEN
# ifndef   INSCREEN
#  define  INSCREEN "【" BBSNAME "】◎(" MYHOSTNAME ", " MYIP ") \r\n"
# endif
    write(fd, INSCREEN, sizeof(INSCREEN));
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

    if (!option->flag_checkload)
	overload = 0;

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

static int check_banip(char *host)
{
    uint32_t thisip = ipstr2int(host);

    return uintbsearch(thisip, &banip[1], banip[0]) ? 1 : 0;
}

/* vim: sw=4 
 */
