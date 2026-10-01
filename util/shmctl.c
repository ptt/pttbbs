#include "bbs.h"
#include <sys/wait.h>
#include <string.h>

extern SHM_t   *SHM;

/* utmp -------------------------------------------------------------------- */
int utmpfix(int argc, char **argv)
{
    if (dashf("bin/utmp.ctl")) {
        char cmd[PATHLEN * 2];
        strlcpy(cmd, "bin/utmp.ctl fix", sizeof(cmd));
        for (int i = 1; i < argc; i++) {
            strlcat(cmd, " ", sizeof(cmd));
            strlcat(cmd, argv[i], sizeof(cmd));
        }
        return system(cmd);
    }
    fprintf(stderr, "Error: bin/utmp.ctl not found. Please build service/utmp first.\n");
    return 1;
}

int utmpsortd(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (dashf("bin/utmp.svc")) {
        puts("Starting utmp.svc...");
        return system("bin/utmp.svc");
    }
    utmp_update();
    puts("utmp_update done (utmp.svc not found)");
    return 0;
}
/* end of utmp ------------------------------------------------------------- */

char *CTIMEx(char *buf, time4_t t)
{
    strlcpy(buf, ctime4(&t), 32);
    buf[strlen(buf) - 1] = 0;
    return buf;
}
int utmpstatus(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (dashf("bin/utmp.ctl"))
        return system("bin/utmp.ctl status");
    time_t  now;
    char    upbuf[64], nowbuf[64];
    now = time(NULL);
    CTIMEx(upbuf,  SHM->UTMPuptime);
    CTIMEx(nowbuf, now);
    printf("now:        %s\n", nowbuf);
    printf("uptime:     %s\n", upbuf);
    printf("number:     %d\n", SHM->UTMPnumber);
    printf("busystate:  %d\n", SHM->UTMPbusystate);
    return 0;
}

int utmpreset(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (dashf("bin/utmp.ctl"))
        return system("bin/utmp.ctl reset");
    SHM->UTMPbusystate=0;
    utmpstatus(0, NULL);
    return 0;
}

#define TIMES	10
int utmpwatch(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (dashf("bin/utmp.ctl"))
        return system("bin/utmp.ctl watch");
    int     i;
    while( 1 ){
	for( i = 0 ; i < TIMES ; ++i ){
	    usleep(300);
	    if( !SHM->UTMPbusystate )
		break;
	    puts("busying...");
	}
	if( i == TIMES ){
	    puts("reset!");
	    SHM->UTMPbusystate = 0;
	}
    }
    return 0;
}

int utmpnum(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (dashf("bin/utmp.ctl"))
        return system("bin/utmp.ctl num");
    printf("%d.0\n", SHM->UTMPnumber);
    return 0;
}

const char    *GV2str[] = {"dymaxactive", "toomanyusers",
                           "noonlineuser","now", "nWelcomes", "shutdown",
                           NULL};
int showglobal(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int     i;
    for( i = 0 ; GV2str[i] != NULL ; ++i )
	printf("GV2.%s = %d\n", GV2str[i], SHM->GV2.v[i]);
    return 0;
}

int setglobal(int argc, char **argv)
{
    int     where, value;
    if( argc != 3 ){
	puts("usage: shmctl setglobal <GV2> newvalue");
	return 1;
    }
    value = atoi(argv[2]);

    for( where = 0 ; GV2str[where] != NULL ; ++where )
	if( strcmp(GV2str[where], argv[1]) == 0 ){
	    printf("GV2.%s = %d -> ", GV2str[where], SHM->GV2.v[where]);
	    printf("%d\n", SHM->GV2.v[where] = value);
	    return 0;
	}
    printf("SHM global variable %s not found\n", argv[1]);

    return 1;
}

int listpid(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int     i;
    for( i = 0 ; i < USHM_SIZE ; ++i )
	if( SHM->uinfo[i].pid > 0 )
	    printf("%d\n", SHM->uinfo[i].pid);
    return 0;
}

int listbrd(int argc, char **argv)
{
    int     i = 0;

    if (argc == 2) 
	i = atoi(argv[1]);

    if(i > 0 && i < MAX_BOARD) 
    {
	int di = i;

	/* print details */
	boardheader_t b = bcache[di-1];
	printf("brdname(bid):\t%s\n", b.brdname);
	printf("title:\t%s\n", b.title);
	printf("BM:\t%s\n", b.BM);
	printf("brdattr:\t%08x ", b.brdattr);

#define SHOWBRDATTR(x) if(b.brdattr & x) printf(#x " ");

	// SHOWBRDATTR(BRD_NOZAP);
	SHOWBRDATTR(BRD_NOCOUNT);
	//SHOWBRDATTR(BRD_NOTRAN);
	SHOWBRDATTR(BRD_GROUPBOARD);
	SHOWBRDATTR(BRD_HIDE);
	SHOWBRDATTR(BRD_POSTMASK);
	SHOWBRDATTR(BRD_ANONYMOUS);
	SHOWBRDATTR(BRD_DEFAULTANONYMOUS);
	SHOWBRDATTR(BRD_NOCREDIT);
	SHOWBRDATTR(BRD_VOTEBOARD);
	SHOWBRDATTR(BRD_WARNEL);
	SHOWBRDATTR(BRD_TOP);
	SHOWBRDATTR(BRD_NORECOMMEND);
	// SHOWBRDATTR(BRD_BLOG);
	SHOWBRDATTR(BRD_BMCOUNT);
	SHOWBRDATTR(BRD_SYMBOLIC);
	SHOWBRDATTR(BRD_NOBOO);
	//SHOWBRDATTR(BRD_LOCALSAVE);
	SHOWBRDATTR(BRD_RESTRICTEDPOST);
	SHOWBRDATTR(BRD_GUESTPOST);
	SHOWBRDATTR(BRD_COOLDOWN);
	SHOWBRDATTR(BRD_CPLOG);
	SHOWBRDATTR(BRD_NOFASTRECMD);
	SHOWBRDATTR(BRD_IPLOGRECMD);
	SHOWBRDATTR(BRD_OVER18);
	SHOWBRDATTR(BRD_ALIGNEDCMT);

	printf("\n");

        printf("post_limit_logins:\t%d\n", b.post_limit_logins);
        printf("post_limit_badpost:\t%d\n", b.post_limit_badpost);
        printf("level:\t%d\n", b.level);
        printf("gid:\t%d\n", b.gid);
        printf("parent:\t%d\n", b.parent);
        printf("childcount:\t%d\n", b.childcount);
        printf("nuser:\t%d\n", b.nuser);

        printf("next[0]:\t%d\n", b.next[0]);
        printf("next[1]:\t%d\n", b.next[1]);
        printf("firstchild[0]:\t%d\n", b.firstchild[0]);
	printf("firstchild[1]:\t%d\n", b.firstchild[1]);
        printf("---- children: ---- \n");
        for (i = 0; i < MAX_BOARD; i++)
        {
            if(bcache[i].gid == di && bcache[i].brdname[0])
                printf("%4d %-13s%-25.25s%s\n",
                        i+1, bcache[i].brdname,
                        bcache[i].BM, bcache[i].title);
        }
    } else
    for( i = 0 ; i < MAX_BOARD ; ++i )
    {
	if(bcache[i].brdname[0])
	    printf("%03d %-13s%-25.25s%s\n", i+1, bcache[i].brdname, bcache[i].BM, bcache[i].title);
    }
    return 0;
}

int fixbrd(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int     i = 0;

    for( i = 0 ; i < MAX_BOARD ; ++i )
    {
	if(!bcache[i].brdname[0])
	    continue;
	/* do whatever you wanna fix below. */
    }
    return 0;
}

int bBMC(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int     i;
    for( i = 0 ; i < MAX_BOARD ; ++i )
	if( bcache[i].brdname[0] )
	    buildBMcache(i + 1); /* XXXbid */
    return 0;
}

int start_services()
{
    int err = 0;
    char buf[PATHLEN];
    const char *services[] = {
        "utmp", "friend", "search",
    };

    for (size_t i = 0; i < ARRAY_SIZE(services); i++) {
        const char *name = services[i];
        char rchar = ' ';
        SNPRINTF(buf, "bin/%s.svc", name);
        if (!dashf(buf)) {
            rchar = '-';
            err++;
        } else {
            STRLCAT(buf, " >/dev/null");
            // All svc should return 0 on success, 1 if a previous service is
            // still serving.
            int r = system(buf);
            if (r < 0 || !WIFEXITED(r)) {
                rchar = '!';
                err++;
            } else if (WEXITSTATUS(r) == 0 || WEXITSTATUS(r) == 1) {
                rchar = '+';
            } else {
                rchar = '?';
                err++;
            }
        }
        fprintf(stderr, "%c%s", rchar, name);
    }
    return err;
}

static int
do_shm_init(int force_reset, int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (force_reset) {
        puts("resetting existing SHM in-place...");
        attach_SHM();
        if (SHM) {
            SHM->UTMPbusystate = 0;
            SHM->Bbusystate = 0;
            SHM->Pbusystate = 0;
            SHM->Fbusystate = 0;
            memset(SHM->uinfo, 0, sizeof(SHM->uinfo));
            SHM->UTMPnumber = 0;
            SHM->UTMPneedupdate = 0;
            init_utmp_user();
            SHM->number = 0;
            SHM->loaded = 0;
            SHM->today_is[0] = '\0';
        }
    }

    puts("loading uhash...");
    system("bin/uhash_loader");

    attach_SHM();
    init_utmp_user();

    puts("loading bcache...");
    reload_bcache();

    puts("building BMcache...");
    bBMC(1, argv);

    /* utmp.svc is started via start_services() */

    puts("\nstarting BBS services...");
    start_services();
    puts("\n");
    return 0;
}

int SHMinit(int argc, char **argv)
{
    return do_shm_init(0, argc, argv);
}

int SHMreset(int argc, char **argv)
{
    return do_shm_init(1, argc, argv);
}

int SHMrebuild_utmp(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if (dashf("bin/utmp.ctl"))
        return system("bin/utmp.ctl rebuild");
    printf("Rebuilding utmp_user table in-place from active sessions (%d online)...\n", SHM->UTMPnumber);
    init_utmp_user();
    puts("Done. utmp_user table rebuilt cleanly.");
    return 0;
}

int hotboard(int argc, char **argv)
{
#define isvisiableboard(bptr)                                              \
        ((bptr)->brdname[0] &&                                             \
         !((bptr)->brdattr & BRD_GROUPBOARD) &&                            \
	 !(((bptr)->brdattr & (BRD_HIDE | BRD_TOP)) ||                     \
	   ((bptr)->level && !((bptr)->brdattr & BRD_POSTMASK) &&          \
	    ((bptr)->level &                                               \
	     ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK)))))

    int     ch, topn = 20, i, nbrds, j, k, nusers;
    struct bs {
	int     nusers;
	boardheader_t *b;
    } *brd;

    while( (ch = getopt(argc, argv, "t:h")) != -1 )
	switch( ch ){
	case 't':
	    topn = atoi(optarg);
	    if( topn <= 0 ){
		goto hotboardusage;
		return 1;
	    }
	    break;
	case 'h':
	default:
	hotboardusage:
	    fprintf(stderr,
		    "usage: shmctl hotboard [-t topn]\n"
		    "    -t topn: # of boards to list. Default: -t %d\n",
		20);
	    return 1;
	}

    brd = (struct bs *)malloc(sizeof(struct bs) * topn);
    brd[0].b = &SHM->bcache[0];
    brd[0].nusers = brd[0].b->brdname[0] ? brd[0].b->nuser : 0;
    nbrds = 1;

    for( i = 1 ; i < MAX_BOARD ; ++i )
	if( (isvisiableboard(&SHM->bcache[i])) &&
	    (nbrds != topn ||
	     SHM->bcache[i].nuser > brd[nbrds - 1].nusers) ){

	    nusers = SHM->bcache[i].nuser; // no race ?
	    for( k = nbrds - 2 ; k >= 0 ; --k )
		if( brd[k].nusers > nusers )
		    break;

	    if( (k + 1) < nbrds && (k + 2) < topn )
		for( j = nbrds - 1 ; j >= k + 1 ; --j )
		    brd[j] = brd[j - 1];
	    brd[k + 1].nusers = nusers;
	    brd[k + 1].b = &SHM->bcache[i];

	    if( nbrds < topn )
		++nbrds;
	}

    for( i = 0 ; i < nbrds ; ++i )
	printf("%05d|%-12s|%s\n",
	       brd[i].nusers, brd[i].b->brdname, brd[i].b->title);
    return 0;
}

int usermode(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int     i, modes[MAX_MODES];
    memset(modes, 0, sizeof(modes));
    for( i = 0 ; i < USHM_SIZE ; ++i )
	if( SHM->uinfo[i].userid[0] )
	    ++modes[ (int)SHM->uinfo[i].mode ];

    for( i = 0 ; i < MAX_MODES ; ++i )
	printf("%03d|%05d|%s\n", i, modes[i], ModeTypeTable[i]);
    return 0;
}

int torb(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    reload_bcache();
    puts("bcache reloaded");
    return 0;
}

void
lockbcache(void)
{
    int     i;
    for (i = 0; i < 10; ++i) {
	if (__sync_bool_compare_and_swap(&SHM->Bbusystate, 0, 1))
	    return;
	printf("SHM->Bbusystate is currently locked (value: %d). "
		"please wait... ", SHM->Bbusystate);
	sleep(1);
    }
    puts("steal bcache lock\n");
    SHM->Bbusystate = 1;
}

void unlockbcache(void)
{
    SHM->Bbusystate = 0;
}

int fixbcache(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int     fd, bid, changed = 0;
    boardheader_t bh;

    if( (fd = open(FN_BOARD, O_RDONLY)) < 0 ){
	perror("open .BRD");
	return 1;
    }

    for( bid = 0 ;
	 (bid < MAX_BOARD && read(fd, &bh, sizeof(bh)) == sizeof(bh)) ;
	 ++bid ){
	if( strcmp(bh.brdname, bcache[bid].brdname) != 0 ){
	    printf("bid: %d, brdname not match(.BRD: %s, bcache: %s). "
		   "fix it!\n",
		   bid + 1, bh.brdname, bcache[bid].brdname);
	    changed = 1;
	    lockbcache();
	    bcache[bid] = bh;
	    unlockbcache();

	    setbottomtotal(bid + 1);
	}
    }
    close(fd);
    if( changed ){
	puts("re-sort bcache");
	sort_bcache();
    }
    return 0;
}

int rlfcache(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    reload_fcache();
    puts("fcache reloaded");
    return 0;
}

int iszero(void *addr, int size)
{
    char *a=(char*)addr;
    int i;
    for(i=0;i<size;i++)
	if(a[i]!=0) return 0;
    return 1;
}

#define TESTZERO(x,i) do { if(!iszero((x), sizeof(x))) printf("%s is dirty(i=%d)\n",#x,i); } while(0);
int testgap(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    int i;
    TESTZERO(SHM->gap_1,0);
    TESTZERO(SHM->gap_2,0);
    TESTZERO(SHM->gap_3,0);
    TESTZERO(SHM->gap_4,0);
    TESTZERO(SHM->gap_5,0);
    TESTZERO(SHM->gap_6,0);
    TESTZERO(SHM->gap_7,0);
    TESTZERO(SHM->gap_8,0);
    TESTZERO(SHM->gap_9,0);
    TESTZERO(SHM->gap_10,0);
    TESTZERO(SHM->gap_11,0);
    TESTZERO(SHM->gap_12,0);
    TESTZERO(SHM->gap_13,0);
    TESTZERO(SHM->gap_14,0);
    TESTZERO(SHM->gap_15,0);
    TESTZERO(SHM->gap_16,0);
    TESTZERO(SHM->gap_17,0);
    TESTZERO(SHM->gap_18,0);
    TESTZERO(SHM->gap_19,0);
    for(i=0; i<USHM_SIZE; i++) {
	TESTZERO(SHM->uinfo[i].gap_1,i);
	TESTZERO(SHM->uinfo[i].gap_2,i);
	TESTZERO(SHM->uinfo[i].gap_3,i);
	TESTZERO(SHM->uinfo[i].gap_4,i);
    }
    return 0;
}

int showstat(int argc GCC_UNUSED, char *argv[])
{
    int i;
    int flag_clear=0;
    const char *stat_desc[]={
	"STAT_LOGIN",
	"STAT_SHELLLOGIN",
	"STAT_VEDIT",
	"STAT_TALKREQUEST",
	"STAT_WRITEREQUEST",
	"STAT_MORE",
	"STAT_SYSWRITESOCKET",
	"STAT_SYSSELECT",
	"STAT_SYSREADSOCKET",
	"STAT_DOSEND",
	"STAT_SEARCHUSER",
	"STAT_THREAD",
	"STAT_SELECTREAD",
	"STAT_QUERY",
	"STAT_DOTALK",
	"STAT_FRIENDDESC",
	"STAT_FRIENDDESC_FILE",
	"STAT_PICKMYFRIEND",
	"STAT_PICKBFRIEND",
	"STAT_GAMBLE",
	"STAT_DOPOST",
	"STAT_READPOST",
	"STAT_RECOMMEND",
	"STAT_TODAYLOGIN_MIN",
	"STAT_TODAYLOGIN_MAX",
	"STAT_SIGINT",
	"STAT_SIGQUIT",
	"STAT_SIGILL",
	"STAT_SIGABRT",
	"STAT_SIGFPE",
	"STAT_SIGBUS",
	"STAT_SIGSEGV",
	"STAT_READPOST_12HR",
	"STAT_READPOST_1DAY",
	"STAT_READPOST_3DAY",
	"STAT_READPOST_7DAY",
	"STAT_READPOST_OLD",
	"STAT_SIGXCPU",
	"STAT_BOARDREC",
	"STAT_BOARDREC_SCPU",
	"STAT_BOARDREC_UCPU",
	"STAT_DORECOMMEND",
	"STAT_DORECOMMEND_SCPU",
	"STAT_DORECOMMEND_UCPU",
	"STAT_QUERY_SCPU",
	"STAT_QUERY_UCPU",
	"STAT_LOGIND_NEWCONN",
	"STAT_LOGIND_OVERLOAD",
	"STAT_LOGIND_BANNED",
	"STAT_LOGIND_AUTHFAIL",
	"STAT_LOGIND_SERVSTART",
	"STAT_LOGIND_SERVFAIL",
	"STAT_LOGIND_PASSWDPROMPT",
	"STAT_MBBSD_ENTER",
	"STAT_MBBSD_EXIT",
	"STAT_MBBSD_ABORTED",
    };

    if(argv[1] && strcmp(argv[1],"-c")==0)
	flag_clear=1;
    for(i=0; i<STAT_NUM; i++) {
	const char *desc= i*sizeof(char*)<sizeof(stat_desc)?stat_desc[i]:"?";
	printf("%s:\t%u\n", desc, SHM->statistic[i]);
    }
    if(flag_clear)
	memset(SHM->statistic, 0, sizeof(SHM->statistic));
    return 0;
}

int dummy(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    return 0;
}

struct Cmd {
    int     (*func)(int, char **);
    const char    *cmd, *descript;
} cmd[] = { 
    {dummy,      "\b\b\b\bStart daemon:", ""},
    {utmpsortd,  "utmpsortd",  "utmp update daemon (active user and hotboard counter). Options: [usec: update interval]"},

    {dummy,      "\b\b\b\bBuild cache/fix tool:", ""},
    {torb,       "reloadbcache", "reload bcache"},
    {fixbcache,  "fixbcache",  "fix bcache"},
    {rlfcache,   "reloadfcache", "reload fcache"},
    {bBMC,       "bBMC",       "build BM cache"},
    {utmpfix,    "utmpfix",    "clear dead userlist entry & kick idle user. Options: [-h: see full usage]"},
    {utmpreset,  "utmpreset",  "SHM->busystate=0"},
    {utmpwatch,  "utmpwatch",  "to see if busystate is always 1 then fix it"},

    {dummy,      "\b\b\b\bShow info:", ""},
    {utmpnum,    "utmpnum",    "print SHM->number for snmpd"},
    {utmpstatus, "utmpstatus", "list utmpstatus"},
    {listpid,    "listpid",    "list all pids of mbbsd"},
    {listbrd,    "listbrd",    "list board info in SHM. Options: [board-id]"},
    {fixbrd,     "fixbrd",     "fix board info in SHM"},
    {hotboard,   "hotboard",   "list boards with the most bfriends. Options: [-h: see full usage]"},
    {usermode,   "usermode",   "list #users in the same mode"},
    {showstat,   "showstat",   "show statistics. Options: [-c: clear stats]"},
    {testgap,    "testgap",    "test SHM->gap zeroness"},

    {dummy,      "\b\b\b\bMisc:", ""},
    {showglobal, "showglobal", "show GLOBALVAR[]"},
    {setglobal,  "setglobal",  "set GLOBALVAR[]. Options: [-h: see full usage]"},
    {SHMinit,    "init",       "initialize: calling uhash_loader to set up SHM, rebuild bcache & BMcache, and start services"},
    {SHMreset,   "reset",      "reset SHM in-place (wipe uinfo/utmp/uhash and re-initialize without recreating SHM)"},
    {SHMrebuild_utmp, "rebuild_utmp", "rebuild utmp_user table in-place from active sessions without kicking users"},
    {NULL, NULL, NULL}
};

extern char ** environ;

int main(int argc, char **argv)
{
    int     i = 0;
	
    chdir(BBSHOME);
    initsetproctitle(argc, argv, environ);
    if( argc >= 2 ){
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    if( strcmp(cmd[i].cmd, argv[1]) == 0 ){
		break;
	    }
    }
    if( argc == 1 || cmd[i].func == NULL ){
	/* usage */
	printf("usage: shmctl [command] [options]\n");
	printf("commands:\n");
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    printf("\t%-15s%s\n", cmd[i].cmd, cmd[i].descript);
	return 0;
    }

    bool do_init = (cmd[i].func == SHMinit || cmd[i].func == SHMreset);
    if (!do_init) {
	attach_SHM();
	/* shmctl doesn't need resolve_boards() first */
	//resolve_boards();
	resolve_garbage();
	resolve_fcache();
    }
    return cmd[i].func(argc - 1, &argv[1]);
}
