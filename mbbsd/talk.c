#include "bbs.h"
#include "daemons.h"

#define QCAST   int (*)(const void *, const void *)

static char    * const sig_des[] = {
    "", "交談", "", "五子棋", "象棋", "暗棋", "圍棋", "黑白棋", "六子旗",
};
static char    * const withme_str[] = {
  "談天", "五子棋", "", "象棋", "暗棋", "圍棋", "黑白棋", "六子旗", NULL
};

#define MAX_SHOW_MODE 7
/* M_INT: monitor mode update interval */
#define M_INT 15
/* P_INT: interval to check for page req. in talk/chat */
#define P_INT 20
#define BOARDFRI  1

typedef struct pickup_t {
    userinfo_t     *ui;
    int             friend, uoffset;
}               pickup_t;

// If you want to change this, you have to change shm structure and shmctl.c
#define PICKUP_WAYS     8

static char    * const fcolor[11] = {
    NULL, ANSI_COLOR(36), ANSI_COLOR(32), ANSI_COLOR(1;32),
    ANSI_COLOR(33), ANSI_COLOR(1;33), ANSI_COLOR(1;37), ANSI_COLOR(1;37),
    ANSI_COLOR(31), ANSI_COLOR(1;35), ANSI_COLOR(1;36)
};

static userinfo_t *uip;

int
isvisible_stat(const userinfo_t * me, const userinfo_t * uentp, int fri_stat)
{
    if (!uentp || uentp->userid[0] == 0)
	return 0;

    /* to avoid paranoid users get crazy*/
    if (uentp->mode == DEBUGSLEEPING)
	return 0;

    if (PERM_HIDE(uentp) && !(PERM_HIDE(me)))	/* 對方紫色隱形而你沒有 */
	return 0;
    else if ((me->userlevel & PERM_SYSOP) ||
	     ((fri_stat & HRM) && (fri_stat & HFM)))
	/* 站長看的見任何人 */
	return 1;

    if (uentp->invisible && !(me->userlevel & PERM_SEECLOAK))
	return 0;

    return !(fri_stat & HRM);
}

int query_online(const char *userid)
{
    userinfo_t *uentp;

    if (!userid || !*userid)
	return 0;

    if (!isalnum(*userid))
	return 0;

    if (strchr(userid, '.') || SHM->GV2.e.noonlineuser)
	return 0;

    uentp = search_ulist_userid(userid);

    if (!uentp ||!isvisible(currutmp, uentp))
	return 0;

    return 1;
}

const char           *
modestring(const userinfo_t * uentp, int simple)
{
    static char     modestr[40];
    const char *    notonline = "不在站上";
    register int    mode = uentp->mode;
    register char  *word;
    int             fri_stat;

    /* for debugging */
    if (mode >= MAX_MODES) {
	syslog(LOG_WARNING, "what!? mode = %d", mode);
	word = ModeTypeTable[mode % MAX_MODES];
    } else
	word = ModeTypeTable[mode];

    fri_stat = friend_stat(currutmp, uentp);
    if (!(HasUserPerm(PERM_SYSOP) || HasUserPerm(PERM_SEECLOAK)) &&
	((uentp->invisible || (fri_stat & HRM)) &&
	 !((fri_stat & HFM) && (fri_stat & HRM))))
	return notonline;
    else if (mode == EDITING) {
	SNPRINTF(modestr, "E:%s",
		ModeTypeTable[uentp->destuid < EDITING ? uentp->destuid :
			      EDITING]);
	word = modestr;
    } else if (!mode && *uentp->chatid == 1) {
	if (!simple)
	    SNPRINTF(modestr, "回應 %s",
		    isvisible_uid(uentp->destuid) ?
		    getuserid(uentp->destuid) : "空氣");
	else
	    SNPRINTF(modestr, "回應呼叫");
    }
    else if (!mode && *uentp->chatid == 3)
	SNPRINTF(modestr, "水球準備中");
    else if ((!mode) && *uentp->chatid == 2)
	if (uentp->msgcount < 10) {
	    const char *cnum[10] =
	    {"", "一", "兩", "三", "四", "五",
		 "六", "七", "八", "九"};
	    SNPRINTF(modestr, "中%s顆水球", cnum[(int)(uentp->msgcount)]);
	} else
	    SNPRINTF(modestr, "不行了 @_@");
    else if (!mode)
	return (uentp->destuid == 6) ? uentp->chatid : "發呆中";

    else if (simple)
	return word;
    else if (uentp->in_chat && mode == CHATING)
	SNPRINTF(modestr, "%s (%s)", word, uentp->chatid);
    else if (mode == TALK || mode == M_FIVE || mode == CHC || mode == UMODE_GO
	    || mode == DARK || mode == M_CONN6) {
	if (!isvisible_uid(uentp->destuid))	/* Leeym 對方(紫色)隱形 */
	    SNPRINTF(modestr, "%s 空氣", word);
	/* Leeym * 大家自己發揮吧！ */
	else
	    SNPRINTF(modestr, "%s %s", word, getuserid(uentp->destuid));
    } else if (mode == CHESSWATCHING) {
	SNPRINTF(modestr, "觀棋");
    } else if (mode != PAGE && mode != TQUERY)
	return word;
    else
	SNPRINTF(modestr, "%s %s", word, getuserid(uentp->destuid));

    return (modestr);
}

unsigned int
set_friend_bit(const userinfo_t * me, const userinfo_t * ui)
{
    int             unum;
    unsigned int hit = 0;
    const int *myfriends;

    /* 判斷對方是否為我的朋友 ? */
    if( intbsearch(ui->uid, me->myfriend, me->nFriends) )
	hit = IFH;

    /* 判斷我是否為對方的朋友 ? */
    if( intbsearch(me->uid, ui->myfriend, ui->nFriends) )
	hit |= HFM;

    /* 判斷對方是否為我的仇人 ? */
    myfriends = me->reject;
    while ((unum = *myfriends++)) {
	if (unum == ui->uid) {
	    hit |= IRH;
	    break;
	}
    }

    /* 判斷我是否為對方的仇人 ? */
    myfriends = ui->reject;
    while ((unum = *myfriends++)) {
	if (unum == me->uid) {
	    hit |= HRM;
	    break;
	}
    }
    return hit;
}

int
reverse_friend_stat(int stat)
{
    int             stat1 = 0;
    if (stat & IFH)
	stat1 |= HFM;
    if (stat & IRH)
	stat1 |= HRM;
    if (stat & HFM)
	stat1 |= IFH;
    if (stat & HRM)
	stat1 |= IRH;
    if (stat & IBH)
	stat1 |= IBH;
    return stat1;
}

#ifdef UTMPD
int sync_outta_server(int sfd, int do_login)
{
    int i;
    int offset = get_utmp_id(currutmp);

    int cmd, res;
    int nfs;
    ocfs_t  fs[MAX_FRIEND*2];

    cmd = -2;
    if(towrite(sfd, &cmd, sizeof(cmd)) < 0 ||
	    towrite(sfd, &offset, sizeof(offset)) < 0 ||
	    towrite(sfd, &currutmp->uid, sizeof(currutmp->uid)) < 0 ||
	    towrite(sfd, currutmp->myfriend, sizeof(currutmp->myfriend)) < 0 ||
	    towrite(sfd, currutmp->reject, sizeof(currutmp->reject)) < 0)
	return -1;

    if(toread(sfd, &res, sizeof(res)) < 0)
	return -1;

    if(res<0)
	return -1;

    // when we are not doing real login (ex, ctrl-u a or ctrl-u d)
    // the frequency check should be avoided.
    if (!do_login)
    {
	sleep(3);   // utmpserver usually treat 3 seconds as flooding.
	if (res == 2 || res == 1)
	    res = 0;
    }

    if(res==2) {
	close(sfd);
	outs("登入太頻繁, 為避免系統負荷過重, 請稍後再試\n");
	refresh();
	log_usies("REJECTLOGIN", NULL);
        // We can't do u_exit because some resources like friends are not ready.
        currmode = 0;
	memset(currutmp, 0, sizeof(userinfo_t));
        // user will try to disconnect here and cause abort_bbs.
	sleep(30);
	exit(0);
    }

    if(toread(sfd, &nfs, sizeof(nfs)) < 0)
	return -1;
    if(nfs<0 || nfs>MAX_FRIEND*2) {
	fprintf(stderr, "invalid nfs=%d\n",nfs);
	return -1;
    }

    if(toread(sfd, fs, sizeof(fs[0])*nfs) < 0)
	return -1;

    close(sfd);

    for(i=0; i<nfs; i++) {
	if( SHM->uinfo[fs[i].index].uid != fs[i].uid )
	    continue; // double check, server may not know user have logout
	currutmp->friend_online[currutmp->friendtotal++]
	    = fs[i].friendstat;
	/* XXX: race here */
	if( SHM->uinfo[fs[i].index].friendtotal < MAX_FRIEND )
	    SHM->uinfo[fs[i].index].friend_online[ SHM->uinfo[fs[i].index].friendtotal++ ] = fs[i].rfriendstat;
    }

    if(res==1) {
	vmsg("請勿頻繁登入以免造成系統過度負荷");
    }
    return 0;
}
#endif

void login_friend_online(int do_login GCC_UNUSED)
{
    userinfo_t     *uentp;
    int             i;
    unsigned int    stat, stat1;
    int             offset = get_utmp_id(currutmp);

#ifdef UTMPD
    int sfd;
    /* UTMPD is TOO slow, let's prompt user here. */
    move(b_lines-2, 0); clrtobot();
    outs("\n正在更新與同步線上使用者及好友名單，系統負荷量大時會需時較久...\n");
    refresh();

    sfd = toconnect(UTMPD_ADDR);
    if(sfd>=0) {
	int res=sync_outta_server(sfd, do_login);
	if(res==0) // sfd will be closed if return 0
	    return;
	close(sfd);
    }
#endif

    for (i = 0; i < SHM->UTMPnumber && currutmp->friendtotal < MAX_FRIEND; i++) {
	uentp = (&SHM->uinfo[SHM->sorted[SHM->currsorted][0][i]]);
	if (uentp && uentp->uid && (stat = set_friend_bit(currutmp, uentp))) {
	    stat1 = reverse_friend_stat(stat);
	    stat <<= 24;
	    stat |= get_utmp_id(uentp);
	    currutmp->friend_online[currutmp->friendtotal++] = stat;
	    if (uentp != currutmp && uentp->friendtotal < MAX_FRIEND) {
		stat1 <<= 24;
		stat1 |= offset;
		uentp->friend_online[uentp->friendtotal++] = stat1;
	    }
	}
    }
    return;
}

/* TODO merge with util/shmctl.c logout_friend_online() */
int
logout_friend_online(userinfo_t * utmp)
{
    int my_friend_idx, thefriend;
    int k;
    int             offset = get_utmp_id(utmp);
    userinfo_t     *ui;
    for(; utmp->friendtotal>0; utmp->friendtotal--) {
	if( !(0 <= utmp->friendtotal && utmp->friendtotal < MAX_FRIEND) )
	    return 1;
	my_friend_idx=utmp->friendtotal-1;
	thefriend = (utmp->friend_online[my_friend_idx] & 0xFFFFFF);
	utmp->friend_online[my_friend_idx]=0;

	if( !(0 <= thefriend && thefriend < USHM_SIZE) )
	    continue;

	ui = &SHM->uinfo[thefriend];
	if(ui->pid==0 || ui==utmp)
	    continue;
	if(ui->friendtotal > MAX_FRIEND || ui->friendtotal<0)
	    continue;
	for (k = 0; k < ui->friendtotal && k < MAX_FRIEND &&
	    (int)(ui->friend_online[k] & 0xFFFFFF) != offset; k++);
	if (k < ui->friendtotal && k < MAX_FRIEND) {
	  ui->friendtotal--;
	  ui->friend_online[k] = ui->friend_online[ui->friendtotal];
	  ui->friend_online[ui->friendtotal] = 0;
	}
    }
    return 0;
}


int
friend_stat(const userinfo_t * me, const userinfo_t * ui)
{
    int             i, j;
    unsigned int    hit = 0;
    /* 看板好友 (在同看板的其它使用者) */
    if (me->brc_id && ui->brc_id == me->brc_id) {
	hit = IBH;
    }
    for (i = 0; me->friend_online[i] && i < MAX_FRIEND; i++) {
	j = (me->friend_online[i] & 0xFFFFFF);
	if (VALID_USHM_ENTRY(j) && ui == &SHM->uinfo[j]) {
	    hit |= me->friend_online[i] >> 24;
	    break;
	}
    }
    if (PERM_HIDE(ui))
	return hit & ST_FRIEND;
    return hit;
}

int
isvisible_uid(int tuid)
{
    userinfo_t     *uentp;

    if (!tuid || !(uentp = search_ulist(tuid)))
	return 1;
    return isvisible(currutmp, uentp);
}

/* 真實動作 */
static void
my_kick(userinfo_t * uentp)
{
    char            genbuf[200];

    getdata(1, 0, msg_sure_ny, genbuf, 4, LCECHO);
    clrtoeol();
    if (genbuf[0] == 'y') {
	SNPRINTF(genbuf, "%s (%s)", uentp->userid, uentp->nickname);
	log_usies("KICK ", genbuf);
	if ((uentp->pid <= 0 || kill(uentp->pid, SIGHUP) == -1) && (errno == ESRCH))
	    purge_utmp(uentp);
	outs("踢出去囉");
    } else
	outs(msg_cancel);
    pressanykey();
}

int
my_query(const char *uident)
{
    userec_t        muser;
    int             tuid, fri_stat = 0;
    int		    is_self = 0;
    userinfo_t     *uentp;
    static time_t last_query;

    BEGINSTAT(STAT_QUERY);
    if ((tuid = getuser(uident, &muser))) {
	move(1, 0);
	clrtobot();
	move(1, 0);
	setutmpmode(TQUERY);
	currutmp->destuid = tuid;
	reload_money();

	if ((uentp = (userinfo_t *) search_ulist(tuid)))
	    fri_stat = friend_stat(currutmp, uentp);
	if (strcmp(muser.userid, cuser.userid) == 0)
	    is_self =1;

	// ------------------------------------------------------------

	prints( "《ＩＤ暱稱》%s (%s)%*s",
	       muser.userid,
	       muser.nickname,
	       strlen(muser.userid) + strlen(muser.nickname) >= 25 ? 0 :
		   (int)(25 - strlen(muser.userid) - strlen(muser.nickname)), "");

	prints( "《經濟狀況》%s",
	       money_level(muser.money));
	if (uentp && ((fri_stat & HFM && !uentp->invisible) || is_self))
	    prints(" ($%d)", muser.money);
	outc('\n');

	// ------------------------------------------------------------

	prints("《" STR_LOGINDAYS "》%d " STR_LOGINDAYS_QTY, muser.numlogindays);
#ifdef SHOW_LOGINOK
	if (!(muser.userlevel & PERM_LOGINOK))
	    outs(" (尚未通過認證)");
        else
#endif
            outs(" (同天內只計一次)");

	move(vgety(), 40);
	prints("《有效文章》%d 篇", muser.numposts);
#ifdef ASSESS
	prints(" (退:%d)", muser.badpost);
#endif
	outc('\n');

	// ------------------------------------------------------------

	prints(ANSI_COLOR(1;33) "《目前動態》%-28.28s" ANSI_RESET,
	       (uentp && isvisible_stat(currutmp, uentp, fri_stat)) ?
		   modestring(uentp, 0) : "不在站上");

	if ((uentp && ISNEWMAIL(uentp)) || load_mailalert(muser.userid))
	    outs("《私人信箱》有新進信件還沒看\n");
	else
	    outs("《私人信箱》最近無新信件\n");

	// ------------------------------------------------------------
        if (muser.role & ROLE_HIDE_FROM) {
            // do nothing
        } else {
            prints("《上次上站》%-28.28s《上次故鄉》",
                   PERM_HIDE(&muser) ? "秘密" :
                   Cdate(muser.lastseen ? &muser.lastseen : &muser.lastlogin));
            // print out muser.lasthost
#ifdef USE_MASKED_FROMHOST
            if(!HasUserPerm(PERM_SYSOP|PERM_ACCOUNTS))
                obfuscate_ipstr(muser.lasthost);
#endif // !USE_MASKED_FROMHOST
            outs(muser.lasthost[0] ? muser.lasthost : "(不詳)");
            outs("\n");
        }

	// ------------------------------------------------------------

	prints("《 五子棋 》%5d 勝 %5d 敗 %5d 和  "
	       "《象棋戰績》%5d 勝 %5d 敗 %5d 和\n",
	       muser.five_win, muser.five_lose, muser.five_tie,
	       muser.chc_win, muser.chc_lose, muser.chc_tie);

	showplans_userec(&muser);

	ENDSTAT(STAT_QUERY);

	if(HasUserPerm(PERM_SYSOP|PERM_POLICE) )
	{
          if(vmsg("T: 開立罰單")=='T')
		  violate_law(&muser, tuid);
	}
	else
	   pressanykey();
	int diff = (int)time4_diff(now, last_query);
	if (diff < 1)
	    sleep(2);
	else if (diff < 2)
	    sleep(1);
	last_query=now;
	return FULLUPDATE;
    }
    else {
	ENDSTAT(STAT_QUERY);
    }

    return DONOTHING;
}

#define lockreturn(unmode, state) if(lockutmpmode(unmode, state)) return

int make_connection_to_somebody(userinfo_t *uin, int timeout){
    int sock, length, pid, ch;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	perror("sock err");
	unlockutmpmode();
	return -1;
    }
    server.sin_family = PF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = 0;
    if (bind(sock, (struct sockaddr *) & server, sizeof(server)) < 0) {
	close(sock);
	perror("bind err");
	unlockutmpmode();
	return -1;
    }
    length = sizeof(server);
    if (getsockname(sock, (struct sockaddr *) & server, (socklen_t *) & length) < 0) {
	close(sock);
	perror("sock name err");
	unlockutmpmode();
	return -1;
    }
    currutmp->sockactive = YEA;
    currutmp->sockaddr = server.sin_port;
    currutmp->destuid = uin->uid;
    // WORKAROUND setutmpmode() checks currstat as cache of currutmp->mode.
    // however if you invoke page -> rejected -> do something -> page again,
    // the currstat=PAGE but currutmp->mode!=PAGE, and then the paging will fail.
    // so, let's temporary break currstat here.
    currstat = IDLE;
    setutmpmode(PAGE);
    uin->destuip = get_utmp_id(currutmp);
    pid = uin->pid;
    if (pid > 0)
	kill(pid, SIGUSR1);
    clear();
    prints("正呼叫 %s.....\n鍵入 Ctrl-D 中止....", uin->userid);

    if(listen(sock, 1)<0) {
	close(sock);
	return -1;
    }

    vkey_attach(sock);

    while (1) {
	if (vkey_poll(timeout * MILLISECONDS)) {
	    ch = vkey();
	} else { // if (ch == I_TIMEOUT) {
	    ch = uin->mode;
	    if (!ch && uin->chatid[0] == 1 &&
		    uin->destuip == get_utmp_id(currutmp)) {
		bell();
		outmsg("對方回應中...");
		refresh();
	    } else if (ch == EDITING || ch == TALK || ch == CHATING ||
		    ch == PAGE || ch == MAILALL || ch == MONITOR ||
		    ch == M_FIVE || ch == CHC || ch == M_CONN6 ||
		    (!ch && (uin->chatid[0] == 1 ||
			     uin->chatid[0] == 3))) {
		vkey_detach();
		close(sock);
		currutmp->sockactive = currutmp->destuid = 0;
		vmsg("人家在忙啦");
		unlockutmpmode();
		return -1;
	    } else {
		// change to longer timeout
		timeout = 20;
		move(0, 0);
		outs("再");
		bell();
		refresh();

		uin->destuip = get_utmp_id(currutmp);
		if (pid <= 0 || kill(pid, SIGUSR1) == -1) {
		    close(sock);
		    currutmp->sockactive = currutmp->destuid = 0;
		    vkey_detach();
		    vmsg(msg_usr_left);
		    unlockutmpmode();
		    return -1;
		}
		continue;
	    }
	}
	if (ch == I_OTHERDATA)
	    break;

	if (ch == Ctrl('D')) {
	    vkey_detach();
	    close(sock);
	    currutmp->sockactive = currutmp->destuid = 0;
	    unlockutmpmode();
	    return -1;
	}
    }
    return sock;
}

void
my_talk(userinfo_t * uin, int fri_stat, char defact)
{
    int             sock, msgsock, ch;
    pid_t           pid;
    char            c;
    char            genbuf[4];
    unsigned char   mode0 = currutmp->mode;

    genbuf[0] = defact;
    ch = uin->mode;

    if (ch == EDITING || ch == TALK || ch == CHATING || ch == PAGE ||
	ch == MAILALL || ch == MONITOR || ch == M_FIVE || ch == CHC ||
	ch == DARK || ch == UMODE_GO || ch == CHESSWATCHING || ch == REVERSI ||
	ch == M_CONN6 ||
	(!ch && (uin->chatid[0] == 1 || uin->chatid[0] == 3)) ||
	uin->lockmode == M_FIVE || uin->lockmode == M_CONN6 || uin->lockmode == CHC) {
	if (ch == CHC || ch == M_FIVE || ch == UMODE_GO ||
	    ch == M_CONN6 || ch == CHESSWATCHING || ch == REVERSI) {
	    sock = make_connection_to_somebody(uin, 20);
	    if (sock < 0)
		vmsg("無法建立連線");
	    else {
		msgsock = accept(sock, (struct sockaddr *) 0, (socklen_t *) 0);
		if (msgsock == -1) {
		    perror("accept");
		    close(sock);
		    return;
		}
		close(sock);
		STRLCPY(currutmp->mateid, uin->userid);

		switch (uin->sig) {
		    case SIG_CHC:
			chc(msgsock, CHESS_MODE_WATCH);
			break;

		    case SIG_GOMO:
			gomoku(msgsock, CHESS_MODE_WATCH);
			break;

		    case SIG_GO:
			gochess(msgsock, CHESS_MODE_WATCH);
			break;

		    case SIG_REVERSI:
			reversi(msgsock, CHESS_MODE_WATCH);
			break;

		    case SIG_CONN6:
			connect6(msgsock, CHESS_MODE_WATCH);
			break;
		}
	    }
	}
	else
	    outs("人家在忙啦");
    } else if (!HasUserPerm(PERM_SYSOP) &&
	       (((fri_stat & HRM) && !(fri_stat & HFM)) ||
		((!uin->pager) && !(fri_stat & HFM)))) {
	outs("對方關掉呼叫器了");
    } else if (!HasUserPerm(PERM_SYSOP) &&
	     (((fri_stat & HRM) && !(fri_stat & HFM)) || uin->pager == PAGER_DISABLE)) {
	outs("對方拔掉呼叫器了");
    } else if (!HasUserPerm(PERM_SYSOP) &&
	       !(fri_stat & HFM) && uin->pager == PAGER_FRIENDONLY) {
	outs("對方只接受好友的呼叫");
    } else if (!(pid = uin->pid) /* || (kill(pid, 0) == -1) */ ) {
	//resetutmpent();
	outs(msg_usr_left);
    } else {
	int i,j;

	if (!defact) {
	    showplans(uin->userid);
	    move(2, 0);
	    for(i=0;i<2;i++) {
		if(uin->withme & (WITHME_ALLFLAG<<i)) {
		    if(i==0)
			outs("歡迎跟我：");
		    else
			outs("請別找我：");
		    for(j=0; j<32 && withme_str[j/2]; j+=2)
			if(uin->withme & (1<<(j+i)))
			    if(withme_str[j/2]) {
				outs(withme_str[j/2]);
				outc(' ');
			    }
		    outc('\n');
		}
	    }
	    move(4, 0);
	    outs("要和他(她) (T)談天(F)五子棋"
		    "(C)象棋(D)暗棋(G)圍棋(R)黑白棋(6)六子旗");
	    getdata(5, 0, "           (N)沒事找錯人了?[N] ", genbuf, 4, LCECHO);
	}

	switch (*genbuf) {
	case 'y':
	case 't':
	    uin->sig = SIG_TALK;
	    break;
	case 'f':
	    lockreturn(M_FIVE, LOCK_THIS);
	    uin->sig = SIG_GOMO;
	    break;
	case 'c':
	    lockreturn(CHC, LOCK_THIS);
	    uin->sig = SIG_CHC;
	    break;
	case '6':
	    lockreturn(M_CONN6, LOCK_THIS);
	    uin->sig = SIG_CONN6;
	    break;
	case 'd':
	    uin->sig = SIG_DARK;
	    break;
	case 'g':
	    uin->sig = SIG_GO;
	    break;
	case 'r':
	    uin->sig = SIG_REVERSI;
	    break;
	default:
	    return;
	}

	uin->turn = 1;
	currutmp->turn = 0;
	STRLCPY(uin->mateid, currutmp->userid);
	STRLCPY(currutmp->mateid, uin->userid);

	sock = make_connection_to_somebody(uin, 5);
	if(sock==-1) {
	    vmsg("無法建立連線");
	    return;
	}

	msgsock = accept(sock, (struct sockaddr *) 0, (socklen_t *) 0);
	if (msgsock == -1) {
	    perror("accept");
	    close(sock);
	    unlockutmpmode();
	    return;
	}
	vkey_detach();
	close(sock);
	currutmp->sockactive = NA;

	if (uin->sig == SIG_CHC || uin->sig == SIG_GOMO ||
	    uin->sig == SIG_GO || uin->sig == SIG_REVERSI ||
	    uin->sig == SIG_CONN6)
	    ChessEstablishRequest(msgsock);

	vkey_attach(msgsock);
	while ((ch = vkey()) != I_OTHERDATA) {
	    if (ch == Ctrl('D')) {
		vkey_detach();
		close(msgsock);
		unlockutmpmode();
		return;
	    }
	}

	if (read(msgsock, &c, sizeof(c)) != sizeof(c))
	    c = 'n';
	vkey_detach();
        // alert that we dot a response
        bell();

	if (c == 'y') {
	    switch (uin->sig) {
	    case SIG_DARK:
		main_dark(msgsock, uin);
		break;
	    case SIG_GOMO:
		gomoku(msgsock, CHESS_MODE_VERSUS);
		break;
	    case SIG_CHC:
		chc(msgsock, CHESS_MODE_VERSUS);
		break;
	    case SIG_GO:
		gochess(msgsock, CHESS_MODE_VERSUS);
		break;
	    case SIG_REVERSI:
		reversi(msgsock, CHESS_MODE_VERSUS);
		break;
	    case SIG_CONN6:
		connect6(msgsock, CHESS_MODE_VERSUS);
		break;
	    case SIG_TALK:
	    default:
		ccw_talk(msgsock, currutmp->destuid);
		setutmpmode(XINFO);
		break;
	    }
	} else {
	    move(9, 9);
	    outs("【回音】 ");
	    switch (c) {
	    case 'a':
		outs("我現在很忙，請等一會兒再 call 我，好嗎？");
		break;
	    case 'b':
		prints("對不起，我有事情不能跟你 %s....", sig_des[uin->sig]);
		break;
	    case 'd':
		outs("我要離站囉..下次再聊吧..........");
		break;
	    case 'c':
		outs("請不要吵我好嗎？");
		break;
	    case 'e':
		outs("找我有事嗎？請先來信唷....");
		break;
	    case 'f':
		{
		    char            msgbuf[60];

		    read(msgsock, msgbuf, 60);
		    prints("對不起，我現在不能跟你 %s，因為\n", sig_des[uin->sig]);
		    move(10, 18);
		    outs(msgbuf);
		}
		break;
	    case '1':
		prints("%s？先拿$100來..", sig_des[uin->sig]);
		break;
	    case '2':
		prints("%s？先拿$1000來..", sig_des[uin->sig]);
		break;
	    default:
		prints("我現在不想 %s 啦.....:)", sig_des[uin->sig]);
	    }
	    close(msgsock);
	}
    }
    currutmp->mode = mode0;
    currutmp->destuid = 0;
    unlockutmpmode();
    pressanykey();
}

/* 選單式聊天介面 */
#define US_PICKUP       1234
#define US_RESORT       1233
#define US_ACTION       1232
#define US_REDRAW       1231

static const char
*hlp_talkbasic[] = {
    "【移動游標】", NULL,
    "  往上一行", "↑ k",
    "  往下一行", "↓ j n",
    "  往前翻頁", "^B PgUp",
    "  往後翻頁", "^F PgDn 空白鍵",
    "  列表開頭", "Home 0",
    "  列表結尾", "End  $",
    "  跳至...",  "1-9數字鍵",
    "  搜尋ID",	  "s",
    "  結束離開", "←   e",
    NULL,
},
*hlp_talkcfg[] = {
    "【修改資料】", NULL,
    "  修改暱稱",    "N",
    "  切換隱身",    "C",
    "  切換呼叫器",  "p",
    "  水球模式",    "^W",
    "  增加好友",    "a",
    "  刪除好友",    "d",
    "  修改好友",    "o",
    "  互動回應方式","y",
    NULL,
},
*hlp_talkdisp[] = {
    "【查詢資訊】", NULL,
    "  查詢此人",    "q",
    "  輸入查詢ID",  "Q",
    "  查詢寵物",    "c",
    "", "",
    "【顯示方式】", NULL,
    "  調整排序",	"TAB",
    "  來源/描述/戰績",	"S",
    "  全部/好友 列表",	"f",
    NULL,
},
*hlp_talktalk[] = {
    "【交談互動】", NULL,
    "  與他聊天",    "→ t Enter",
    "  熱線水球",    "w",
    "  即時回應",    "^R (要先收到水球)",
    "  好友廣播",    " b (要在好友列表)",
    "  回顧訊息",    "l",
    "  寄信給他",    "m",
    "  給予" MONEYNAME,"g",
    NULL,
},
*hlp_talkmisc[] = {
    "【其它】", NULL,
    "  閱\讀信件",   "r",
    "  使用說明",    "h",
    NULL,
},
*hlp_talkadmin[] = {
    "【站長專用】", NULL,
    "  設定使用者",   "u",
    "  切換隱形模式", "H",
    "  踢人",	      "K",
#if defined(SHOWBOARD) && defined(DEBUG)
    "  顯示所在看板", "Y",
#endif
    NULL,
};

static void
t_showhelp(void)
{
    const char ** p1[3] = { hlp_talkbasic, hlp_talkdisp, hlp_talkcfg },
	       ** p2[3] = { hlp_talktalk,  hlp_talkmisc, hlp_talkadmin };
    const int  cols[3] = { 31, 25, 22 },    // column witdh
               desc[3] = { 12, 18, 16 };    // desc width
    clear();
    showtitle("休閒聊天", "使用說明");
    outs("\n");
    vs_multi_T_table_simple(p1, 3, cols, desc,
	    HLP_CATEGORY_COLOR, HLP_DESCRIPTION_COLOR, HLP_KEYLIST_COLOR);
    if (HasUserPerm(PERM_PAGE))
    vs_multi_T_table_simple(p2, HasUserPerm(PERM_SYSOP)?3:2, cols, desc,
	    HLP_CATEGORY_COLOR, HLP_DESCRIPTION_COLOR, HLP_KEYLIST_COLOR);
    PRESSANYKEY();
}

/* Kaede show friend description */
static char    *
friend_descript(const userinfo_t * uentp, char *desc_buf, int desc_buflen)
{
    char           *space_buf = "", *flag;
    char            fpath[80], name[IDLEN + 2], *desc, *ptr;
    int             len;
    FILE           *fp;
    char            genbuf[STRLEN];

    STATINC(STAT_FRIENDDESC);
    if((set_friend_bit(currutmp,uentp)&IFH)==0)
	return space_buf;

    setuserfile(fpath, friend_file[0]);

    STATINC(STAT_FRIENDDESC_FILE);
    if ((fp = fopen(fpath, "r"))) {
	SNPRINTF(name, "%s ", uentp->userid);
	len = strlen(name);
	desc = genbuf + 13;

	/* TODO maybe none linear search, or fread, or cache */
	while ((flag = fgets(genbuf, STRLEN, fp))) {
	    if (!memcmp(genbuf, name, len)) {
		if ((ptr = strchr(desc, '\n')))
		    ptr[0] = '\0';
		break;
	    }
	}
	fclose(fp);
	if (flag)
	    strlcpy(desc_buf, desc, desc_buflen);
	else
	    return space_buf;

	return desc_buf;
    } else
	return space_buf;
}

static const char    *
descript(int show_mode, const userinfo_t * uentp, int diff, char *description, int len)
{
    switch (show_mode) {
    case 1:
	return friend_descript(uentp, description, len);
    case 0:
	return (((uentp->pager != PAGER_DISABLE && uentp->pager != PAGER_ANTIWB && diff) ||
		 HasUserPerm(PERM_SYSOP)) ?  uentp->from : "*");
    case 2:
	snprintf(description, len, "%4d/%4d/%2d %c",
                 uentp->five_win, uentp->five_lose, uentp->five_tie,
		 (uentp->withme&WITHME_FIVE)?'o':
                 (uentp->withme&WITHME_NOFIVE)?'x':' ');
	return description;
    case 3:
	snprintf(description, len, "%4d/%4d/%2d %c",
                 uentp->chc_win, uentp->chc_lose, uentp->chc_tie,
		 (uentp->withme&WITHME_CHESS)?'o':
                 (uentp->withme&WITHME_NOCHESS)?'x':' ');
	return description;
    case 4:
	snprintf(description, len,
		 "%4d %s", uentp->chess_elo_rating,
		 (uentp->withme&WITHME_CHESS)?"找我下棋":
                 (uentp->withme&WITHME_NOCHESS)?"別找我":"");
	return description;
    case 5:
	snprintf(description, len, "%4d/%4d/%2d %c",
                 uentp->go_win, uentp->go_lose, uentp->go_tie,
		 (uentp->withme&WITHME_GO)?'o':
                 (uentp->withme&WITHME_NOGO)?'x':' ');
	return description;
    case 6:
	snprintf(description, len, "%4d/%4d/%2d %c",
                 uentp->dark_win, uentp->dark_lose, uentp->dark_tie,
		 (uentp->withme&WITHME_DARK)?'o':
                 (uentp->withme&WITHME_NODARK)?'x':' ');
	return description;
    case 7:
	snprintf(description, len, "%s",
		 (uentp->withme&WITHME_CONN6)?"找我下棋":
                 (uentp->withme&WITHME_NOCONN6)?"別找我":"");
	return description;

    default:
	syslog(LOG_WARNING, "damn!!! what's wrong?? show_mode = %d",
	       show_mode);
	return "";
    }
}

/*
 * userlist
 *
 * 有別於其他大部份 bbs在實作使用者名單時, 都是將所有 online users 取一份到
 * local space 中, 按照所須要的方式 sort 好 (如按照 userid , 五子棋, 來源等
 * 等) . 這將造成大量的浪費: 為什麼每個人都要為了產生這一頁僅 20 個人的資料
 * 而去 sort 其他一萬人的資料?
 *
 * 一般來說, 一份完整的使用者名單可以分成「好友區」和「非好友區」. 不同人的
 * 「好友區」應該會長的不一樣, 不過「非好友區」應該是長的一樣的. 針對這項特
 * 性, 兩區有不同的實作方式.
 *
 * + 好友區
 *   好友區只有在排列方式為 [嗨! 朋友] 的時候「可能」會用到.
 *   每個 process可以透過 currutmp->friend_online[]得到互相間有好友關係的資
 *   料 (不包括板友, 板友是另外生出來的) 不過 friend_online[]是 unorder的.
 *   所以須要先把所有的人拿出來, 重新 sort 一次.
 *   好友區 (互相任一方有設好友+ 板友) 最多只會有 MAX_FRIENDS個
 *   因為產生好友區的 cost 相當高, "能不產生就不要產生"
 *
 * + 非好友區
 *   透過 shmctl utmpsortd , 定期 (通常一秒一次) 將全站的人按照各種不同的方
 *   式 sort 好, 放置在 SHM->sorted中.
 *
 * 接下來, 我們每次只從確定的起始位置拿, 特別是除非有須要, 不然不會去產生好
 * 友區.
 *
 * 各個 function 摘要
 * sort_cmpfriend()   sort function, key: friend type
 * pickup_maxpages()  # pages of userlist
 * pickup_myfriend()  產生好友區
 * pickup_bfriend()   產生板友
 * pickup()           產生某一頁使用者名單
 * draw_pickup()      把畫面輸出
 * userlist()         主函式, 負責呼叫 pickup()/draw_pickup() 以及按鍵處理
 *
 * SEE ALSO
 *     include/pttstruct.h
 *
 * BUGS
 *     搜尋的時候沒有辦法移到該人上面
 *
 * AUTHOR
 *     in2 <in2@in2home.org>
 */
char    nPickups;

static int
sort_cmpfriend(const void *a, const void *b)
{
    if (((((pickup_t *) a)->friend) & ST_FRIEND) ==
	((((pickup_t *) b)->friend) & ST_FRIEND))
	return strcasecmp(((pickup_t *) a)->ui->userid,
			  ((pickup_t *) b)->ui->userid);
    else
	return (((pickup_t *) b)->friend & ST_FRIEND) -
	    (((pickup_t *) a)->friend & ST_FRIEND);
}

int
pickup_maxpages(int pickupway, int nfriends)
{
    int             number;
    if (HasUserFlag(UF_FRIEND))
	number = nfriends;
    else
	number = SHM->UTMPnumber +
	    (pickupway == 0 ? nfriends : 0);
    return (number - 1) / nPickups + 1;
}

static int
pickup_myfriend(pickup_t * friends,
		int *myfriend, int *friendme, int *badfriend)
{
    userinfo_t     *uentp;
    int             i, where, frstate, ngets = 0;

    STATINC(STAT_PICKMYFRIEND);
    *badfriend = 0;
    *myfriend = *friendme = 1;
    for (i = 0; i < MAX_FRIEND && currutmp->friend_online[i]; ++i) {
	where = currutmp->friend_online[i] & 0xFFFFFF;
	if (VALID_USHM_ENTRY(where) &&
	    (uentp = &SHM->uinfo[where]) && uentp->pid &&
	    uentp != currutmp &&
	    isvisible_stat(currutmp, uentp,
			   frstate =
			   currutmp->friend_online[i] >> 24)){
	    if( frstate & IRH )
		++*badfriend;
	    if( !(frstate & IRH) || ((frstate & IRH) && (frstate & IFH)) ){
		friends[ngets].ui = uentp;
		friends[ngets].uoffset = where;
		friends[ngets++].friend = frstate;
		if (frstate & IFH)
		    ++* myfriend;
		if (frstate & HFM)
		    ++* friendme;
	    }
	}
    }
    /* 把自己加入好友區 */
    friends[ngets].ui = currutmp;
    friends[ngets++].friend = (IFH | HFM);
    return ngets;
}

static int
pickup_bfriend(pickup_t * friends, int base)
{
    userinfo_t     *uentp;
    int             i, ngets = 0;
    int             currsorted = SHM->currsorted, number = SHM->UTMPnumber;

    STATINC(STAT_PICKBFRIEND);
    friends = friends + base;
    for (i = 0; i < number && ngets < MAX_FRIEND - base; ++i) {
	uentp = &SHM->uinfo[SHM->sorted[currsorted][0][i]];
	/* TODO isvisible() 重複用到了 friend_stat() */
	if (uentp && uentp->pid && uentp->brc_id == currutmp->brc_id &&
	    currutmp != uentp && isvisible(currutmp, uentp) &&
	    (base || !(friend_stat(currutmp, uentp) & (IFH | HFM)))) {
	    friends[ngets].ui = uentp;
	    friends[ngets++].friend = IBH;
	}
    }
    return ngets;
}

static void
pickup(pickup_t * currpickup, int pickup_way, int *page,
       int *nfriend, int *myfriend, int *friendme, int *bfriend, int *badfriend)
{
    /* avoid race condition */
    int             currsorted = SHM->currsorted;
    int             utmpnumber = SHM->UTMPnumber;
    int             friendtotal = currutmp->friendtotal;

    int    *ulist;
    userinfo_t *u;
    int             which, sorted_way, size = 0, friend;

    if (friendtotal == 0)
	*myfriend = *friendme = 1;

    /* 產生好友區 */
    which = *page * nPickups;
    if( (HasUserFlag(UF_FRIEND)) || /* 只顯示好友模式 */
	((pickup_way == 0) &&          /* [嗨! 朋友] mode */
	 (
	  /* 含板友, 好友區最多只會有 (friendtotal + 板友) 個*/
	  (currutmp->brc_id && which < (friendtotal + 1 +
					getbcache(currutmp->brc_id)->nuser)) ||

	  /* 不含板友, 最多只會有 friendtotal個 */
	  (!currutmp->brc_id && which < friendtotal + 1)
	  ))) {
	pickup_t        friends[MAX_FRIEND + 1]; /* +1 include self */

	/* TODO 當 friendtotal<which 時只需顯示板友, 不需 pickup_myfriend */
	*nfriend = pickup_myfriend(friends, myfriend, friendme, badfriend);

	if (pickup_way == 0 && currutmp->brc_id != 0
#ifdef USE_COOLDOWN
		&& !(getbcache(currutmp->brc_id)->brdattr & BRD_COOLDOWN)
#endif
		){
	    /* TODO 只需要 which+nPickups-*nfriend 個板友, 不一定要整個掃一遍 */
	    *nfriend += pickup_bfriend(friends, *nfriend);
	    *bfriend = SHM->bcache[currutmp->brc_id - 1].nuser;
	}
	else
	    *bfriend = 0;
	if (*nfriend > which) {
	    /* 只有在要秀出才有必要 sort */
	    /* TODO 好友跟板友可以分開 sort, 可能只需要其一 */
	    /* TODO 好友上下站才需要 sort 一次, 不需要每次 sort.
	     * 可維護一個 dirty bit 表示是否 sort 過.
	     * suggested by WYchuang@ptt */
	    qsort(friends, *nfriend, sizeof(pickup_t), sort_cmpfriend);
	    size = *nfriend - which;
	    if (size > nPickups)
		size = nPickups;
	    memcpy(currpickup, friends + which, sizeof(pickup_t) * size);
	}
    } else
	*nfriend = 0;

    if (!(HasUserFlag(UF_FRIEND)) && size < nPickups) {
	sorted_way = ((pickup_way == 0) ? 7 : (pickup_way - 1));
	ulist = SHM->sorted[currsorted][sorted_way];
	which = *page * nPickups - *nfriend;
	if (which < 0)
	    which = 0;

	for (; which < utmpnumber && size < nPickups; which++) {
	    u = &SHM->uinfo[ulist[which]];

	    friend = friend_stat(currutmp, u);
	    /* TODO isvisible() 重複用到了 friend_stat() */
	    if ((pickup_way ||
		 (currutmp != u && !(friend & ST_FRIEND))) &&
		isvisible(currutmp, u)) {
		currpickup[size].ui = u;
		currpickup[size++].friend = friend;
	    }
	}
    }

    for (; size < nPickups; ++size)
	currpickup[size].ui = 0;
}

#define ULISTCOLS (9)

// userlist column definition
static const VCOL ulist_coldef[ULISTCOLS] = {
    {NULL, 8, 9, 0, {0, 1}},	// "編號" 因為游標靠這所以也不該長太大
    {NULL, 2, 2, 0, {0, 0, 1}}, // "P" (pager, no border)
    {NULL, IDLEN+1, IDLEN+3}, // "代號"
    {NULL, 17,25, 2}, // "暱稱", sizeof(userec_t::nickname)
    {NULL, 17,27, 1}, // "故鄉/棋類戰績/等級分"
    {NULL, 12,23, 1}, // "動態" (最大多少才合理？) modestring size=40 但...
    {NULL, 4, 4, 0, {0, 0, 1}}, // "<通緝>" (原心情)
    {NULL, 6, 6, -1, {0, 1, 1}}, // "發呆" (optional?)
    {NULL, 0, VCOL_MAXW, -1}, // for middle alignment
};

static void
draw_pickup(int drawall, pickup_t * pickup, int pickup_way,
	    int page, int show_mode, int show_uid, int show_board,
	    int show_pid, int myfriend, int friendme, int bfriend, int badfriend)
{
    char           *msg_pickup_way[PICKUP_WAYS] = {
        "嗨! 朋友", "網友代號", "網友動態", "發呆時間", "來自何方",
        " 五子棋 ", "  象棋  ", "  圍棋  ",
    };
    char           *MODE_STRING[MAX_SHOW_MODE] = {
	"故鄉", "好友描述", "五子棋戰績", "象棋戰績", "象棋等級分", "圍棋戰績",
        "暗棋戰績",
    };
    char            pagerchar[6] = "* -Wf";

    userinfo_t     *uentp;
    int             i, ch, state, friend;

    // print buffer
    char pager[3];
    char num[10];
    char xuid[IDLEN+1+20]; // must carry IDLEN + ANSI code.
    char description[30];
    char idlestr[32];
    int idletime = 0;

    static int scrw = 0, scrh = 0;
    static VCOLW cols[ULISTCOLS];

#ifdef SHOW_IDLE_TIME
    idletime = 1;
#endif

    // re-layout if required.
    if (scrw != t_columns || scrh != t_lines)
    {
	vs_cols_layout(ulist_coldef, cols, ULISTCOLS);
	scrw = t_columns; scrh = t_lines;
    }

    if (drawall) {
	showtitle((HasUserFlag(UF_FRIEND)) ? "好友列表" : "休閒聊天",
		  BBSName);

	move(2, 0);
	outs(ANSI_REVERSE);
	vs_cols(ulist_coldef, cols, ULISTCOLS,
                // Columns Header (9 args)
		show_uid ? "UID" : "編號",
		"P",
                "代號",
                "暱稱",
		MODE_STRING[show_mode],
		show_board ? "看板" : "動態",
		show_pid ? "PID" : "",
                idletime ? "發呆": "",
		"");
	outs(ANSI_RESET);

if (HAS_ANGEL && HasUserPerm(PERM_ANGEL) && currutmp)
	{
	    // modes should match ANGELPAUSE*
	    static const char *modestr[ANGELPAUSE_MODES] = {
		ANSI_COLOR(0;30;47) "開放",
		ANSI_COLOR(0;32;47) "停收",
		ANSI_COLOR(0;31;47) "關閉",
	    };
	    // reduced version
	    // TODO use vs_footer to replace.
	    move(b_lines, 0);
	    vbarf( ANSI_COLOR(34;46) " 休閒聊天 "
		   ANSI_COLOR(31;47) " (TAB/f)" ANSI_COLOR(30) "排序/好友 "
		   ANSI_COLOR(31) "(p)" ANSI_COLOR(30) "一般呼叫器 "
		   ANSI_COLOR(31) "(^P)" ANSI_COLOR(30) "神諭呼叫器\t"
		   ANSI_COLOR(1;30;47) "[神諭呼叫器] %s ",
		   modestr[currutmp->angelpause % ANGELPAUSE_MODES]);
	} else
	vs_footer(" 休閒聊天 ",
		" (TAB/f)排序/好友 (a/o)交友 (q/w)查詢/丟水球 (t/m)聊天/寫信\t(h)說明");
    }

    move(1, 0);
    prints("  排序:[%s] 上站人數:%-4d "
	    ANSI_COLOR(1;32) "我的朋友:%-3d "
	   ANSI_COLOR(33) "與我為友:%-3d "
	   ANSI_COLOR(36) "板友:%-4d "
	   ANSI_COLOR(31) "壞人:%-2d"
	   ANSI_RESET "\n",
	   msg_pickup_way[pickup_way], SHM->UTMPnumber,
	   myfriend, friendme, currutmp->brc_id ? bfriend : 0, badfriend);

    for (i = 0, ch = page * nPickups + 1; i < nPickups; ++i, ++ch) {
        char *mind = "";

	move(i + 3, 0);
	SOLVE_ANSI_CACHE();
	uentp = pickup[i].ui;
	friend = pickup[i].friend;
	if (uentp == NULL) {
	    outc('\n');
	    continue;
	}

	if (!uentp->pid) {
	    vs_cols(ulist_coldef, cols, 3,
		    "", "", "< 離站中..>");
	    continue;
	}

	// prepare user data

	if (PERM_HIDE(uentp))
	    state = 9;
	else if (currutmp == uentp)
	    state = 10;
	else if (friend & IRH && !(friend & IFH))
	    state = 8;
	else
	    state = (friend & ST_FRIEND) >> 2;

        idlestr[0] = 0;
#ifdef SHOW_IDLE_TIME
	idletime = time4_diff(now, uentp->lastact);
	if (idletime > DAY_SECONDS)
	    STRLCPY(idlestr, " -----");
	else if (idletime >= 3600)
	    SNPRINTF(idlestr, "%dh%02d",
		     idletime / 3600, (idletime / 60) % 60);
	else if (idletime > 0)
	    SNPRINTF(idlestr, "%d'%02d",
		     idletime / 60, idletime % 60);
#endif

	if ((uentp->userlevel & PERM_VIOLATELAW))
            mind = ANSI_COLOR(1;31) "違規";

	SNPRINTF(num, "%d",
#ifdef SHOWUID
		show_uid ? uentp->uid :
#endif
		ch);

	pager[0] = (friend & HRM) ? 'X' : pagerchar[uentp->pager % 5];
	pager[1] = (uentp->invisible ? ')' : ' ');
	pager[2] = 0;

	/* color of userid, userid */
	if(fcolor[state])
	    SNPRINTF(xuid, "%s%s",
		    fcolor[state], uentp->userid);

	vs_cols(ulist_coldef, cols, ULISTCOLS,
                // Columns data (9 params)
		num,
                pager,
		fcolor[state] ? xuid : uentp->userid,
		uentp->nickname,
                descript(show_mode, uentp, uentp->pager & !(friend & HRM),
                         description, sizeof(description)),
#if defined(SHOWBOARD) && defined(DEBUG)
		show_board ? (uentp->brc_id == 0 ? "" :
		    getbcache(uentp->brc_id)->brdname) :
#endif
                    modestring(uentp, 0),
                mind,
		idlestr,
	        "");
    }
}

void set_withme_flag(void)
{
    int i;
    char genbuf[20];
    int line;

    move(1, 0);
    clrtobot();

    do {
	move(1, 0);
	line=1;
	for(i=0; i<16 && withme_str[i]; i++) {
	    clrtoeol();
            if (!*withme_str[i])
                continue;
	    if(currutmp->withme&(1<<(i*2)))
		prints("[%c] 我很想跟人%s, 歡迎任何人找我\n",'a'+i, withme_str[i]);
	    else if(currutmp->withme&(1<<(i*2+1)))
		prints("[%c] 我不太想%s\n",'a'+i, withme_str[i]);
	    else
		prints("[%c] (%s)沒意見\n",'a'+i, withme_str[i]);
	    line++;
	}
	getdata(line,0,"用字母切換 [想/不想/沒意見]",genbuf, sizeof(genbuf), DOECHO);
	for(i=0;genbuf[i];i++) {
	    int ch=genbuf[i];
	    ch=tolower(ch);
	    if('a'<=ch && ch<'a'+16) {
		ch-='a';
		if(currutmp->withme&(1<<ch*2)) {
		    currutmp->withme&=~(1<<ch*2);
		    currutmp->withme|=1<<(ch*2+1);
		} else if(currutmp->withme&(1<<(ch*2+1))) {
		    currutmp->withme&=~(1<<(ch*2+1));
		} else {
		    currutmp->withme|=1<<(ch*2);
		}
	    }
	}
    } while(genbuf[0]!='\0');
}

static void
userlist(void)
{
    pickup_t       *currpickup;
    userinfo_t     *uentp;
    static char     show_mode = 0;
    static char     show_uid = 0;
    static char     show_board = 0;
    static char     show_pid = 0;
    static int      pickup_way = 0;
    char            skippickup = 0, redraw, redrawall;
    int             page, offset, ch, leave, fri_stat;
    int             nfriend, myfriend, friendme, bfriend, badfriend, i;
    time4_t          lastupdate;

    nPickups = b_lines - 3;
    currpickup = (pickup_t *)malloc(sizeof(pickup_t) * nPickups);
    page = offset = 0 ;
    nfriend = myfriend = friendme = bfriend = badfriend = 0;
    leave = 0;
    redrawall = 1;
    /*
     * 各個 flag :
     * redraw:    重新 pickup(), draw_pickup() (僅中間區, 不含標題列等等)
     * redrawall: 全部重畫 (含標題列等等, 須再指定 redraw 才會有效)
     * leave:     離開使用者名單
     */
    while (!leave && !ZA_Waiting()) {
	if( !skippickup )
	    pickup(currpickup, pickup_way, &page,
		   &nfriend, &myfriend, &friendme, &bfriend, &badfriend);
	draw_pickup(redrawall, currpickup, pickup_way, page,
		    show_mode, show_uid, show_board, show_pid,
		    myfriend, friendme, bfriend, badfriend);

	/*
	 * 如果因為換頁的時候, 這一頁有的人數比較少,
	 * (通常都是最後一頁人數不滿的時候) 那要重新計算 offset
	 * 以免指到沒有人的地方
	 */
	if (offset == -1 || currpickup[offset].ui == NULL) {
	    for (offset = (offset == -1 ? nPickups - 1 : offset);
		 offset >= 0; --offset)
		if (currpickup[offset].ui != NULL)
		    break;
	    if (offset == -1) {
		if (--page < 0)
		    page = pickup_maxpages(pickup_way, nfriend) - 1;
		offset = 0;
		continue;
	    }
	}
	skippickup = redraw = redrawall = 0;
	lastupdate = now;
	while (!redraw && !ZA_Waiting()) {
	    ch = cursor_key(offset + 3, 0);
	    uentp = currpickup[offset].ui;
	    fri_stat = currpickup[offset].friend;

	    switch (ch) {
	    case Ctrl('Z'):
		redrawall = redraw = 1;
		if (ZA_Select())
		    leave = 1;
		break;

	    case KEY_LEFT:
	    case 'e':
	    case 'E':
		redraw = leave = 1;
		break;

	    case KEY_TAB:
		pickup_way = (pickup_way + 1) % PICKUP_WAYS;
		redraw = 1;
		redrawall = 1;
		break;

	    case KEY_DOWN:
	    case 'n':
	    case 'j':
		if (++offset == nPickups || currpickup[offset].ui == NULL) {
		    redraw = 1;
		    if (++page >= pickup_maxpages(pickup_way,
						  nfriend))
			offset = page = 0;
		    else
			offset = 0;
		}
		break;

	    case '0':
	    case KEY_HOME:
		page = offset = 0;
		redraw = 1;
		break;

	    case 'H':
		if (HasUserPerm(PERM_SYSOP)||HasUserPerm(PERM_OLDSYSOP)) {
		    currutmp->userlevel ^= PERM_SYSOPHIDE;
		    redrawall = redraw = 1;
		}
		break;

	    case 'C':
		currutmp->invisible ^= 1;
		redrawall = redraw = 1;
		break;

	    case ' ':
	    case KEY_PGDN:
	    case Ctrl('F'):{
		    int             newpage;
		    if ((newpage = page + 1) >= pickup_maxpages(pickup_way,
								nfriend))
			newpage = offset = 0;
		    if (newpage != page) {
			page = newpage;
			redraw = 1;
		    } else if (time4_ge(now, lastupdate + 2))
			redrawall = redraw = 1;
		}
		break;

	    case KEY_UP:
	    case 'k':
		if (--offset == -1) {
		    offset = nPickups - 1;
		    if (--page == -1)
			page = pickup_maxpages(pickup_way, nfriend)
			    - 1;
		    redraw = 1;
		}
		break;

	    case KEY_PGUP:
	    case Ctrl('B'):
	    case 'P':
		if (--page == -1)
		    page = pickup_maxpages(pickup_way, nfriend) - 1;
		offset = 0;
		redraw = 1;
		break;

	    case KEY_END:
	    case '$':
		page = pickup_maxpages(pickup_way, nfriend) - 1;
		offset = -1;
		redraw = 1;
		break;

	    case '/':
		/*
		 * getdata_buf(b_lines-1,0,"請輸入暱稱關鍵字:", keyword,
		 * sizeof(keyword), DOECHO); state = US_PICKUP;
		 */
		break;

	    case 's':
		if (!(HasUserFlag(UF_FRIEND))) {
		    int             si;	/* utmpshm->sorted[X][0][si] */
		    int             fi;	/* allpickuplist[fi] */
		    char            swid[IDLEN + 1];
		    move(1, 0);

		    // XXX si 已經寫死了 pickup_way = 0
		    // 若使用者在 pickup_way != 0 時按 's'...

		    si = CompleteOnlineUser(msg_uid, swid);
		    if (si >= 0) {
			pickup_t        friends[MAX_FRIEND + 1];
			int             nGots;
			int *ulist =
			    SHM->sorted[SHM->currsorted]
			    [((pickup_way == 0) ? 0 : (pickup_way - 1))];

			fi = ulist[si];
			nGots = pickup_myfriend(friends, &myfriend,
						&friendme, &badfriend);
			for (i = 0; i < nGots; ++i)
			    if (friends[i].uoffset == fi)
				break;

			fi = 0;
			offset = 0;
			if( i != nGots ){
			    page = i / nPickups;
			    for( ; i < nGots && fi < nPickups ; ++i )
				if( isvisible(currutmp, friends[i].ui) )
				    currpickup[fi++] = friends[i];
			    i = 0;
			}
			else{
			    page = (si + nGots) / nPickups;
			    i = si;
			}

			for( ; fi < nPickups && i < SHM->UTMPnumber ; ++i )
			{
			    userinfo_t *u;
			    u = &SHM->uinfo[ulist[i]];
			    if( isvisible(currutmp, u) ){
				currpickup[fi].ui = u;
				currpickup[fi++].friend = 0;
			    }
			}
			skippickup = 1;
		    }
		    redrawall = redraw = 1;
		}
		/*
		 * if ((i = search_pickup(num, actor, pklist)) >= 0) num = i;
		 * state = US_ACTION;
		 */
		break;

	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		{		/* Thor: 可以打數字跳到該人 */
		    int             tmp;
		    if ((tmp = search_num(ch, SHM->UTMPnumber)) >= 0) {
			if (tmp / nPickups == page) {
			    /*
			     * in2:目的在目前這一頁, 直接 更新 offset ,
			     * 不用重畫畫面
			     */
			    offset = tmp % nPickups;
			} else {
			    page = tmp / nPickups;
			    offset = tmp % nPickups;
			}
			redrawall = redraw = 1;
		    }
		}
		break;

#ifdef SHOWUID
	    case 'U':
		if (HasUserPerm(PERM_SYSOP)) {
		    show_uid ^= 1;
		    redrawall = redraw = 1;
		}
		break;
#endif
#if defined(SHOWBOARD) && defined(DEBUG)
	    case 'Y':
		if (HasUserPerm(PERM_SYSOP)) {
		    show_board ^= 1;
		    redrawall = redraw = 1;
		}
		break;
#endif
#ifdef  SHOWPID
	    case '#':
		if (HasUserPerm(PERM_SYSOP)) {
		    show_pid ^= 1;
		    redrawall = redraw = 1;
		}
		break;
#endif

	    case 'b':		/* broadcast */
		if (HasUserFlag(UF_FRIEND) || HasUserPerm(PERM_SYSOP)) {
		    char            genbuf[60]="[廣播]";
		    char            ans[4];

		    if (!getdata(0, 0, "廣播訊息:", genbuf+6, 54, DOECHO))
			break;

		    if (!getdata(0, 0, "確定廣播? [N]",
				ans, sizeof(ans), LCECHO) ||
			ans[0] != 'y')
			break;
		    if (!(HasUserFlag(UF_FRIEND)) && HasUserPerm(PERM_SYSOP)) {
			msgque_t msg;
			getdata(1, 0, "再次確定站長廣播? [N]",
				ans, sizeof(ans), LCECHO);
			if( ans[0] != 'y'){
			    vmsg("abort");
			    break;
			}

			msg.pid = currpid;
			STRLCPY(msg.userid, cuser.userid);
			SNPRINTF(msg.last_call_in, "[廣播]%s", genbuf);
			for (i = 0; i < SHM->UTMPnumber; ++i) {
			    // XXX why use sorted list?
			    //     can we just scan uinfo with proper checking?
			    uentp = &SHM->uinfo[
                                      SHM->sorted[SHM->currsorted][0][i]];
			    if (uentp->pid && kill(uentp->pid, 0) != -1){
				int     write_pos = uentp->msgcount;
				if( write_pos < (MAX_MSGS - 1) ){
				    uentp->msgcount = write_pos + 1;
				    memcpy(&uentp->msgs[write_pos], &msg,
					   sizeof(msg));
				    kill(uentp->pid, SIGUSR2);
				}
			    }
			}
		    } else {
			userinfo_t     *uentp;
			int             where, frstate;
			for (i = 0; i < MAX_FRIEND && currutmp->friend_online[i]; ++i) {
			    where = currutmp->friend_online[i] & 0xFFFFFF;
                            if (!VALID_USHM_ENTRY(where))
                                continue;
                            uentp = &SHM->uinfo[where];
                            if (!uentp || !uentp->pid)
                                continue;
                            frstate = currutmp->friend_online[i] >> 24;
                            // Only to people who I've friended him.
                            if (!(frstate & IFH))
                                continue;
                            if (!isvisible_stat(currutmp, uentp, frstate))
                                continue;
                            if (uentp->pager == PAGER_ANTIWB)
                                continue;
                            if (uentp->pager == PAGER_FRIENDONLY &&
                                !(frstate & HFM))
                                continue;
                            // HRM + HFM = super friend.
                            if ((frstate & HRM) && !(frstate & HFM))
                                continue;
                            if (kill(uentp->pid, 0) == -1)
                                continue;
                            my_write(uentp->pid, genbuf, uentp->userid,
                                     WATERBALL_PREEDIT, NULL);
			}
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'S':		/* 顯示好友描述 */
#ifdef HAVE_DARK_CHESS_LOG
		show_mode = (show_mode+1) % MAX_SHOW_MODE;
#else
		show_mode = (show_mode+1) % (MAX_SHOW_MODE - 1);
#endif
#ifdef CHESSCOUNTRY
		if (show_mode == 2)
		    user_query_mode = 1;
		else if (show_mode == 3 || show_mode == 4)
		    user_query_mode = 2;
		else if (show_mode == 5)
		    user_query_mode = 3;
		else if (show_mode == 6)
		    user_query_mode = 4;
		else
		    user_query_mode = 0;
#endif /* defined(CHESSCOUNTRY) */
		redrawall = redraw = 1;
		break;

	    case 'u':		/* 線上修改資料 */
		if (HasUserPerm(PERM_ACCOUNTS|PERM_SYSOP)) {
		    int             id;
		    userec_t        muser;
		    vs_hdr("使用者設定");
		    move(1, 0);
		    if ((id = getuser(uentp->userid, &muser)) > 0) {
			user_display(&muser, 1);
			if( HasUserPerm(PERM_ACCOUNTS) )
			    uinfo_query(muser.userid, 1, id);
			else
			    pressanykey();
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case Ctrl('S'):
		break;

	    case KEY_RIGHT:
	    case KEY_ENTER:
	    case 't':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    if (uentp->pid != currpid &&
			    strcmp(uentp->userid, cuser.userid) != 0) {
			move(1, 0);
			clrtobot();
			move(3, 0);
			my_talk(uentp, fri_stat, 0);
			redrawall = redraw = 1;
		    }
		}
		break;
	    case 'K':
		if (HasUserPerm(PERM_ACCOUNTS|PERM_SYSOP)) {
		    my_kick(uentp);
		    redrawall = redraw = 1;
		}
		break;
	    case 'w':
		if (call_in(uentp, fri_stat))
		    redrawall = redraw = 1;
		break;
	    case 'a':
		if (HasBasicUserPerm(PERM_LOGINOK) && !(fri_stat & IFH)) {
		    if (vans("確定要加入好友嗎 [N/y]") == 'y') {
			friend_add(uentp->userid, FRIEND_OVERRIDE,uentp->nickname);
			friend_load(FRIEND_OVERRIDE, 0);
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'd':
		if (HasBasicUserPerm(PERM_LOGINOK) && (fri_stat & IFH)) {
		    if (vans("確定要刪除好友嗎 [N/y]") == 'y') {
			friend_delete(uentp->userid, FRIEND_OVERRIDE);
			friend_load(FRIEND_OVERRIDE, 0);
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'o':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    t_override();
		    redrawall = redraw = 1;
		}
		break;

	    case 'f':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    pwcuToggleFriendList();
                    // reset cursor
                    page = offset = 0;
		    redrawall = redraw = 1;
		}
		break;

		/*
	    case 'G':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    p_give();
		    // give_money_ui(NULL);
		    redrawall = redraw = 1;
		}
		break;
		*/

	    case 'g':
		// Give money requires mail permission
		if (HasSendMailUserPerm() && cuser.money) {
		    give_money_ui(uentp->userid);
		    redrawall = redraw = 1;
		}
		break;

	    case 'm':
		if (HasSendMailUserPerm()) {
		    char   userid[IDLEN + 1];
		    STRLCPY(userid, uentp->userid);
		    vs_hdr("寄  信");
		    prints("[寄信] 收信人：%s", userid);
		    my_send(userid);
		    setutmpmode(LUSERS);
		    redrawall = redraw = 1;
		}
		break;

	    case 'q':
		my_query(uentp->userid);
		setutmpmode(LUSERS);
		redrawall = redraw = 1;
		break;

	    case 'Q':
		t_query();
		setutmpmode(LUSERS);
		redrawall = redraw = 1;
		break;

	    case 'c':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    chicken_query(uentp->userid);
		    redrawall = redraw = 1;
		}
		break;

	    case 'l':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    pager_show_log();
		    redrawall = redraw = 1;
		}
		break;

	    case 'h':
		t_showhelp();
		redrawall = redraw = 1;
		break;

	    case 'p':
		if (HasUserPerm(PERM_BASIC)) {
		    pager_toggle_mode();
		    redrawall = redraw = 1;
		}
		break;

	    case Ctrl('P'):
		if (HAS_ANGEL && HasBasicUserPerm(PERM_ANGEL) && currutmp) {
                    angel_toggle_pause();
		    redrawall = redraw = 1;
		}
		break;

	    case Ctrl('W'):
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    static const char *wm[PAGER_UI_TYPES] = {"一般", "進階", "未來"};

		    pwcuSetPagerUIType((cuser.pager_ui_type +1) % PAGER_UI_TYPES_USER);
		    /* vmsg cannot support multi lines */
		    move(b_lines - 4, 0);
		    clrtobot();
		    move(b_lines - 3, 0);
		    outs("系統提供數種水球模式可供選擇\n"
		    "在切換後請正常下線再重新登入, 以確保結構正確\n");
		    vmsgf( "目前切換到 [%s] 水球模式", wm[cuser.pager_ui_type%PAGER_UI_TYPES]);
		    redrawall = redraw = 1;
		}
		break;

	    case 'r':
		if (HasUserPerm(PERM_READMAIL)) {
                    // XXX in fact we should check size here...
                    // chkmailbox();
                    m_read();
                    setutmpmode(LUSERS);
		    redrawall = redraw = 1;
		}
		break;

	    case 'N':
		if (HasBasicUserPerm(PERM_LOGINOK)) {
		    char tmp_nick[sizeof(cuser.nickname)];
		    if (getdata_str(1, 0, "新的暱稱: ",
				tmp_nick, sizeof(tmp_nick), DOECHO, cuser.nickname) > 0)
		    {
			pwcuSetNickname(tmp_nick);
			STRLCPY(currutmp->nickname, cuser.nickname);
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'y':
		set_withme_flag();
		redrawall = redraw = 1;
		break;

	    default:
		if (time4_ge(now, lastupdate + 2))
		    redraw = 1;
	    }
	}
    }
    free(currpickup);
}

int
t_users(void)
{
    int             destuid0 = currutmp->destuid;
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;

    if (!HasBasicUserPerm(PERM_LOGINOK) ||
        HasUserPerm(PERM_VIOLATELAW))
        return 0;

    assert(strncmp(cuser.userid, currutmp->userid, IDLEN)==0);
    if( strncmp(cuser.userid , currutmp->userid, IDLEN) != 0 ){
	abort_bbs(0);
    }

    // cannot do ZA for re-entrant.
    // usually happens when doing ^U, ^Z with non-return
    // env like editor.
    if (ZA_Waiting())
	ZA_Drop();

    // TODO drop if we were already in t_users?

    setutmpmode(LUSERS);
    userlist();
    currutmp->mode = mode0;
    currutmp->destuid = destuid0;
    currstat = stat0;
    return 0;
}

int
t_qchicken(void)
{
    char            uident[STRLEN];

    vs_hdr("查詢寵物");
    usercomplete(msg_uid, uident);
    if (uident[0])
	chicken_query(uident);
    return 0;
}

int
t_query(void)
{
    char            uident[STRLEN];

    vs_hdr("查詢網友");
    usercomplete(msg_uid, uident);
    if (uident[0])
	my_query(uident);
    return 0;
}

int
t_talk(void)
{
    char            uident[16];
    int             tuid, unum, ucount;
    userinfo_t     *uentp;
    char            genbuf[4];
    /*
     * if (count_ulist() <= 1){ outs("目前線上只有您一人，快邀請朋友來光臨【"
     * BBSNAME "】吧！"); return XEASY; }
     */
    vs_hdr("打開話匣子");
    CompleteOnlineUser(msg_uid, uident);
    if (uident[0] == '\0')
	return 0;

    move(3, 0);
    if (!(tuid = searchuser(uident, uident)) || tuid == usernum) {
	outs(err_uid);
	pressanykey();
	return 0;
    }
    /* multi-login check */
    unum = 1;
    while ((ucount = count_logins(tuid, 0)) > 1) {
	outs("(0) 不想 talk 了...\n");
	count_logins(tuid, 1);
	getdata(1, 33, "請選擇一個交談對象 [0]：", genbuf, 4, DOECHO);
	unum = atoi(genbuf);
	if (unum == 0)
	    return 0;
	move(3, 0);
	clrtobot();
	if (unum > 0 && unum <= ucount)
	    break;
    }

    if ((uentp = (userinfo_t *) search_ulistn(tuid, unum)))
	my_talk(uentp, friend_stat(currutmp, uentp), 0);

    return 0;
}

static int
reply_connection_request(const userinfo_t *uip)
{
    char            buf[4], genbuf[200];

    if (uip->mode != PAGE) {
	SNPRINTF(genbuf, "%s 已停止呼叫，按Enter繼續...", uip->userid);
	getdata(0, 0, genbuf, buf, sizeof(buf), LCECHO);
	return -1;
    }
    return establish_talk_connection(uip);
}

int
establish_talk_connection(const userinfo_t *uip)
{
    int                    a;
    struct sockaddr_in sin;

    currutmp->msgcount = 0;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = PF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = uip->sockaddr;
    if ((a = socket(sin.sin_family, SOCK_STREAM, 0)) < 0) {
	perror("socket err");
	return -1;
    }
    if ((connect(a, (struct sockaddr *) & sin, sizeof(sin)))) {
	//perror("connect err");
	close(a);
	return -1;
    }
    return a;
}

/* 有人來串門子了，回應呼叫器 */
void
talkreply(void)
{
    char            buf[4];
    char            genbuf[200];
    int             a, sig = currutmp->sig;
    int             currstat0 = currstat;
    int             r;
    int             is_chess;
    userec_t        xuser;
    void          (*sig_pipe_handle)(int);

    uip = &SHM->uinfo[currutmp->destuip];
    currutmp->destuid = uip->uid;
    currstat = REPLY;		/* 避免出現動畫 */

    is_chess = (sig == SIG_CHC || sig == SIG_GOMO || sig == SIG_CONN6 ||
		sig == SIG_GO || sig == SIG_REVERSI);

    a = reply_connection_request(uip);
    if (a < 0) {
	clear();
	currstat = currstat0;
	return;
    }
    if (is_chess)
	ChessAcceptingRequest(a);

    clear();

    outs("\n\n");
    // FIXME CRASH here
    assert(sig>=0 && sig<(int) ARRAY_SIZE(sig_des));
    prints("       (Y) 讓我們 %s 吧！"
	   "       (A) 我現在很忙，請等一會兒再 call 我\n", sig_des[sig]);
    prints("       (N) 我現在不想 %s "
	   "       (B) 對不起，我有事情不能跟你 %s\n",
	    sig_des[sig], sig_des[sig]);
    prints("       (C) 請不要吵我好嗎？"
	   "       (D) 我要離站囉..下次再聊吧.......\n");
    prints("       (E) 有事嗎？請先來信"
	   "       (F) " ANSI_COLOR(1;33) "<自行輸入理由>..." ANSI_RESET "\n");
    prints("       (1) %s？先拿$100來"
	   "       (2) %s？先拿$1000來..\n\n", sig_des[sig], sig_des[sig]);

    getuser(uip->userid, &xuser);
    currutmp->msgs[0].pid = uip->pid;
    STRLCPY(currutmp->msgs[0].userid, uip->userid);
    STRLCPY(currutmp->msgs[0].last_call_in, "呼叫、呼叫，聽到請回答 (Ctrl-R)");
    currutmp->msgs[0].msgmode = MSGMODE_TALK;
    prints("對方來自 [%s]，" STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY "，文章共 %d 篇\n",
	    uip->from, xuser.numlogindays, xuser.numposts);

    if (is_chess)
	ChessShowRequest();
    else {
	showplans(uip->userid);
	show_call_in(0, 0);
    }

    SNPRINTF(genbuf, "你想跟 %s (%s) %s嗎？請選擇[N]: ",
	    uip->userid, uip->nickname, sig_des[sig]);
    getdata(0, 0, genbuf, buf, sizeof(buf), LCECHO);

    if (!buf[0] || !strchr("yabcdef12", buf[0]))
	buf[0] = 'n';

    sig_pipe_handle = Signal(SIGPIPE, SIG_IGN);
    r = write(a, buf, 1);
    if (buf[0] == 'f' || buf[0] == 'F') {
	if (!getdata(b_lines, 0, "不能的原因：", genbuf, 60, DOECHO))
	    STRLCPY(genbuf, "不告訴你咧 !! ^o^");
	r = write(a, genbuf, 60);
    }
    Signal(SIGPIPE, sig_pipe_handle);

    if (r == -1) {
	close(a);
	SNPRINTF(genbuf, "%s 已停止呼叫，按Enter繼續...", uip->userid);
	getdata(0, 0, genbuf, buf, sizeof(buf), LCECHO);
	clear();
	currstat = currstat0;
	return;
    }

    uip->destuip = get_utmp_id(currutmp);
    if (buf[0] == 'y')
	switch (sig) {
	case SIG_DARK:
	    main_dark(a, uip);
	    break;
	case SIG_GOMO:
	    gomoku(a, CHESS_MODE_VERSUS);
	    break;
	case SIG_CHC:
	    chc(a, CHESS_MODE_VERSUS);
	    break;
	case SIG_GO:
	    gochess(a, CHESS_MODE_VERSUS);
	    break;
	case SIG_REVERSI:
	    reversi(a, CHESS_MODE_VERSUS);
	    break;
	case SIG_CONN6:
	    connect6(a, CHESS_MODE_VERSUS);
	    break;
	case SIG_TALK:
	default:
	    ccw_talk(a, currutmp->destuid);
	    setutmpmode(XINFO);
	    break;
	}
    else
	close(a);
    clear();
    currstat = currstat0;
}

int
t_chat(void)
{
    static time4_t lastEnter GCC_UNUSED = 0;
    int    fd;

    if (!HasBasicUserPerm(PERM_CHAT)) {
	vmsg("權限不足，無法進入聊天室。");
	return -1;
    }

    if(HasUserPerm(PERM_VIOLATELAW))
    {
       vmsg("請先繳罰單才能使用聊天室!");
       return -1;
    }

#ifdef CHAT_GAPMINS
    if (time4_diff(now, lastEnter) / 60 < CHAT_GAPMINS)
    {
       vmsg("您才剛離開聊天室，裡面正在整理中。請稍後再試。");
       return 0;
    }
#endif

#ifdef CHAT_REGDAYS
    if (cuser.numlogindays < CHAT_REGDAYS) {
	vmsgf("您尚未達到進入條件限制 (" STR_LOGINDAYS ": %d, 需要: %d)",
              cuser.numlogindays, CHAT_REGDAYS);
	return 0;
    }
#endif

    // start to create connection.
    syncnow();
    move(b_lines, 0); clrtoeol();
    outs(" 正在與聊天室連線... ");
    refresh();
    fd = toconnect(XCHATD_ADDR);
    move(b_lines-1, 0); clrtobot();
    if (fd < 0) {
	outs(" 聊天室正在整理中，請稍候再試。");
	system("bin/xchatd");
	pressanykey();
	return -1;
    }

    // mark for entering
    syncnow();
    lastEnter = now;

    return ccw_chat(fd);
}

/* ----------------------------------------------------- */
/* Global Talk Hotkeys (Ctrl-U Userlist)                 */
/* ----------------------------------------------------- */

static int
talk_key_hook(int ch)
{
    if (ch != Ctrl('U'))
        return ch;

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

void
talk_init_hooks(void)
{
    vkey_register_hook(VKEY_HOOK_PRIO_NORMAL, talk_key_hook);
}
