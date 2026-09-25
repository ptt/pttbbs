#include "bbs.h"
#include "psb.h"
#include "daemons.h"

#define QCAST   int (*)(const void *, const void *)

static char * const sig_descriptions[] = {
    [SIG_TALK] = "交談",
    [SIG_GOMO] ="五子棋",
    [SIG_CHC] = "象棋",
};
static const char *MODE_STRING[] = {
    "故鄉", "好友描述", "五子棋戰績", "象棋戰績", "象棋等級分",
};
// this must map to SHM->sorted[active].
static const char * const MSG_PICKUP_WAY[] = {
    "嗨! 朋友", "網友代號", "網友動態", "發呆時間", "來自何方",
};
#define PICKUP_WAYS ARRAY_SIZE(MSG_PICKUP_WAY)
#define MAX_SHOW_MODE ARRAY_SIZE(MODE_STRING)
/* M_INT: monitor mode update interval */
#define M_INT 15
/* P_INT: interval to check for page req. in talk/chat */
#define P_INT 20
#define BOARDFRI  1

typedef struct pickup_t {
    userinfo_t     *ui;
    int             friend, uoffset;
}               pickup_t;

static char    * const fcolor[11] = {
    NULL, ANSI_COLOR(36), ANSI_COLOR(32), ANSI_COLOR(1;32),
    ANSI_COLOR(33), ANSI_COLOR(1;33), ANSI_COLOR(1;37), ANSI_COLOR(1;37),
    ANSI_COLOR(31), ANSI_COLOR(1;35), ANSI_COLOR(1;36)
};

static userinfo_t *uip;

const char *get_sig_des(int sig)
{
    if (sig < 0 || sig >= (int)ARRAY_SIZE(sig_descriptions))
        return "";
    const char *r = sig_descriptions[sig];
    if (!r)
        return "";
    return r;
}
int
isvisible_stat(const userinfo_t * me, const userinfo_t * uentp, int fri_stat)
{
    if (!uentp || uentp->userid[0] == 0)
	return 0;

    /* to avoid paranoid users get crazy*/
    if (uentp->mode == DEBUGSLEEPING)
	return 0;

    fri_stat = friend_normalize_stat(fri_stat);

    if (PERM_HIDE(uentp) && !(PERM_HIDE(me)))	/* 對方紫色隱形而你沒有 */
	return 0;
    else if ((me->userlevel & PERM_SYSOP) || (fri_stat & HSM))
	/* 站長或超級好友看的見任何人 */
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

    fri_stat = friend_normalize_stat(friend_stat(currutmp, uentp));
    if (!(HasUserPerm(PERM_SYSOP) || HasUserPerm(PERM_SEECLOAK)) &&
	(uentp->invisible || (fri_stat & HRM)) && !(fri_stat & HSM))
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
    else if ((!mode) && *uentp->chatid == 2) {
        // TODO(hungte) The msgcount is not reliable and we're seeing 'zero
        // messages'. Before that is fixed, have a workaround without numbers.
        if (uentp->msgcount == 0) {
            SNPRINTF(modestr, "中了水球");
        } else if (uentp->msgcount > 0 && uentp->msgcount < MAX_MSGS) {
            SNPRINTF(modestr, "中%d顆水球", (int)uentp->msgcount);
        } else
            SNPRINTF(modestr, "不行了 @_@");
    }
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

int
friend_stat(const userinfo_t * me, const userinfo_t * ui)
{
    int             i, j;
    unsigned int    hit = 0;
    /* 看板好友 (在同看板的其它使用者) */
    if (me->brc_id && ui->brc_id == me->brc_id) {
	hit = IBH;
    }
    /* Legacy binaries may still append/swap entries unsorted before cutoff. */
    if (me->friend_svc_flag && now >= FRIEND_LEGACY_COMPAT_CUTOFF) {
	int target_slot = (int)(ui - &SHM->uinfo[0]);
	int total = me->friendtotal;
	if (VALID_USHM_ENTRY(target_slot) && total > 0) {
	    if (total > MAX_FRIEND_ONLINE)
		total = MAX_FRIEND_ONLINE;
	    int lo = 0, hi = total - 1;
	    while (lo <= hi) {
		int mid = lo + ((hi - lo) >> 1);
		unsigned int entry = me->friend_online[mid];
		if (!entry) {
		    hi = mid - 1;
		    continue;
		}
		j = FRIEND_ONLINE_SLOT(entry);
		if (j == target_slot) {
		    if (FRIEND_ONLINE_VALID_UID(entry, ui->uid))
			hit |= FRIEND_ONLINE_STAT(entry);
		    break;
		} else if (j < target_slot) {
		    lo = mid + 1;
		} else {
		    hi = mid - 1;
		}
	    }
	}
    } else {
	for (i = 0; i < MAX_FRIEND_ONLINE && me->friend_online[i]; i++) {
	    unsigned int entry = me->friend_online[i];
	    j = FRIEND_ONLINE_SLOT(entry);
	    if (VALID_USHM_ENTRY(j) && ui == &SHM->uinfo[j] &&
		FRIEND_ONLINE_VALID_UID(entry, ui->uid)) {
		hit |= FRIEND_ONLINE_STAT(entry);
		break;
	    }
	}
    }
    if (PERM_HIDE(ui))
	return hit & (ST_FRIEND | ST_SUPER | HRM);
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

    getdata(1, 0, MSG_SURE_NY, genbuf, 4, LCECHO);
    clrtoeol();
    if (genbuf[0] == 'y') {
	SNPRINTF(genbuf, "%s (%s)", uentp->userid, uentp->nickname);
	log_usies("KICK ", genbuf);
	if ((uentp->pid <= 0 || kill(uentp->pid, SIGHUP) == -1) && (errno == ESRCH))
	    purge_utmp(uentp);
	outs("踢出去囉");
    } else
	outs(MSG_CANCEL);
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
	       stream_width(muser.userid) + stream_width(muser.nickname) >= 25 ? 0 :
		   (int)(25 - stream_width(muser.userid) - stream_width(muser.nickname)), "");

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
    uin->destuip = get_utmp_slot(currutmp);
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
		    uin->destuip == get_utmp_slot(currutmp)) {
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

		uin->destuip = get_utmp_slot(currutmp);
		if (pid <= 0 || kill(pid, SIGUSR1) == -1) {
		    close(sock);
		    currutmp->sockactive = currutmp->destuid = 0;
		    vkey_detach();
		    vmsg(MSG_USR_LEFT);
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
	ch == CHESSWATCHING  ||
		(!ch && (uin->chatid[0] == 1 || uin->chatid[0] == 3)) ||
	uin->lockmode == M_FIVE || uin->lockmode == M_CONN6 || uin->lockmode == CHC) {
	if (ch == CHC || ch == M_FIVE || ch == CHESSWATCHING) {
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
		}
	    }
	}
	else
	    outs("人家在忙啦");
    } else if (!HasUserPerm(PERM_SYSOP) &&
	       ((fri_stat & HRM) || ((!uin->pager) && !(fri_stat & HFM)))) {
	outs("對方關掉呼叫器了");
    } else if (!HasUserPerm(PERM_SYSOP) &&
	       ((fri_stat & HRM) || uin->pager == PAGER_DISABLE)) {
	outs("對方拔掉呼叫器了");
    } else if (!HasUserPerm(PERM_SYSOP) &&
	       !(fri_stat & HFM) && uin->pager == PAGER_FRIENDONLY) {
	outs("對方只接受好友的呼叫");
    } else if (!(pid = uin->pid) /* || (kill(pid, 0) == -1) */ ) {
	//resetutmpent();
	outs(MSG_USR_LEFT);
    } else {
	if (!defact) {
	    showplans(uin->userid);
	    getdata(4, 0, "要和對方 (T)談天(F)五子棋(C)象棋(N)取消?[N] ",
                    genbuf, 4, LCECHO);
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

	if (uin->sig == SIG_CHC || uin->sig == SIG_GOMO)
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
	    case SIG_GOMO:
		gomoku(msgsock, CHESS_MODE_VERSUS);
		break;
	    case SIG_CHC:
		chc(msgsock, CHESS_MODE_VERSUS);
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
	    case 'e':
		outs("找我有事嗎？請先來信唷....");
		break;
	    case 'f':
		{
		    char            msgbuf[60];

		    read(msgsock, msgbuf, 60);
		    prints("我現在不方便 %s，因為\n", get_sig_des(uin->sig));
		    move(10, 18);
		    outs(msgbuf);
		}
		break;
	    default:
		prints("我現在不方便 %s .....:)", get_sig_des(uin->sig));
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
    if ((friend_stat(currutmp, uentp) & IFH) == 0)
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
    // Map to MODE_STRING
    switch (show_mode) {
    case 0:
	return (((uentp->pager != PAGER_DISABLE && uentp->pager != PAGER_ANTIWB && diff) ||
		 HasUserPerm(PERM_SYSOP)) ?  uentp->from : "*");
    case 1:
	return friend_descript(uentp, description, len);
    case 2:
	snprintf(description, len, "%4d/%4d/%2d",
                 uentp->five_win, uentp->five_lose, uentp->five_tie);
	return description;
    case 3:
	snprintf(description, len, "%4d/%4d/%2d",
                 uentp->chc_win, uentp->chc_lose, uentp->chc_tie);
	return description;
    case 4:
	snprintf(description, len,
		 "%4d", uentp->chess_elo_rating);
	return description;

    default:
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
    for (i = 0; i < MAX_FRIEND_ONLINE && currutmp->friend_online[i]; ++i) {
	unsigned int entry = currutmp->friend_online[i];
	where = FRIEND_ONLINE_SLOT(entry);
	if (VALID_USHM_ENTRY(where) &&
	    (uentp = &SHM->uinfo[where]) && uentp->pid &&
	    FRIEND_ONLINE_VALID_UID(entry, uentp->uid) &&
	    uentp != currutmp &&
	    isvisible_stat(currutmp, uentp,
			   frstate =
			   FRIEND_ONLINE_STAT(entry))){
	    if (frstate & IRH) {
		++*badfriend;
	    } else if (frstate & (ST_FRIEND | ST_SUPER)) {
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
    friends[ngets].uoffset = get_utmp_slot(currutmp);
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
    for (i = 0; i < number && ngets < MAX_FRIEND_ONLINE - base; ++i) {
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
	pickup_t        friends[MAX_FRIEND_ONLINE + 1]; /* +1 include self */

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
        assert(sorted_way < (int)ARRAY_SIZE(SHM->sorted[0]));
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

static const cmd_t userlist_cmds[];

typedef struct {
    pickup_t   *currpickup;
    userinfo_t *uentp;
    int         fri_stat;
    int         page;
    int         offset;
    int         nfriend, myfriend, friendme, bfriend, badfriend;
    char       *show_mode;
    char       *show_uid;
#if defined(SHOWBOARD) && defined(DEBUG)
    char       *show_board;
#endif
    char       *show_pid;
    int        *pickup_way;
    char        skippickup;
    time4_t     lastupdate;
} userlist_ctx_t;

static int ulist_scrw = 0, ulist_scrh = 0;
static VCOLW ulist_cols[ULISTCOLS];

static void
t_showhelp(void)
{
    clear();
    showtitle("休閒聊天", "使用說明");
    outs(
        ANSI_COLOR(1;36) "  熱鍵控制說明" ANSI_RESET "\n"
        "  [" ANSI_COLOR(1;37) "e/←" ANSI_RESET "] 離開            "
        "[" ANSI_COLOR(1;37) "h" ANSI_RESET "]    顯示本畫面       "
        "[" ANSI_COLOR(1;37) "t/Enter/→" ANSI_RESET "] 聊天\n"
        "  [" ANSI_COLOR(1;37) "p" ANSI_RESET "]    切換呼叫器模式  "
        "[" ANSI_COLOR(1;37) "C" ANSI_RESET "]    隱身術           "
        "[" ANSI_COLOR(1;37) "S" ANSI_RESET "]          變更顯示內容\n"
        "  [" ANSI_COLOR(1;37) "q" ANSI_RESET "]    查詢網友        "
        "[" ANSI_COLOR(1;37) "w" ANSI_RESET "]    丟水球           "
        "[" ANSI_COLOR(1;37) "l" ANSI_RESET "]          看上幾次水球\n"
        "  [" ANSI_COLOR(1;37) "f" ANSI_RESET "]    列出全部/好友   "
        "[" ANSI_COLOR(1;37) "m" ANSI_RESET "]    寫信給他         "
        "[" ANSI_COLOR(1;37) "r" ANSI_RESET "]          看信件\n"
        "  [" ANSI_COLOR(1;37) "s" ANSI_RESET "]    搜尋該ID位置    "
        "[" ANSI_COLOR(1;37) "g" ANSI_RESET "]    塞錢給他         "
        "[" ANSI_COLOR(1;37) "c" ANSI_RESET "]          看寵物\n"
        "  [" ANSI_COLOR(1;37) "a/d" ANSI_RESET "]  增刪好友        "
        "[" ANSI_COLOR(1;37) "o" ANSI_RESET "]    編輯好友名單     "
        "[" ANSI_COLOR(1;37) "b" ANSI_RESET "]          對好友廣播\n"
        "  [" ANSI_COLOR(1;37) "N" ANSI_RESET "]    修改暱稱        "
        "[" ANSI_COLOR(1;37) "Q" ANSI_RESET "]    查詢指定網友     "
        "[" ANSI_COLOR(1;37) "TAB" ANSI_RESET "]        變更排序方式\n"
#ifdef PLAY_ANGEL
        "  [" ANSI_COLOR(1;37) "^P" ANSI_RESET "]   切換小天使呼叫器\n"
#endif
        "\n"
        ANSI_COLOR(1;36) "  名單顏色說明" ANSI_RESET "\n"
        "  " ANSI_COLOR(1;37) "白色" ANSI_RESET " - 我的朋友        "
        "  " ANSI_COLOR(1;33) "黃色" ANSI_RESET " - 與我為友        "
        "  " ANSI_COLOR(1;32) "綠色" ANSI_RESET " - 雙向好友\n"
        "  " ANSI_COLOR(1;36) "青色" ANSI_RESET " - 板友            "
        "  " ANSI_COLOR(0;31) "暗紅" ANSI_RESET " - 壞人\n");
    if (HasUserPerm(PERM_SYSOP))
        outs("\n  " ANSI_COLOR(1;36) "站長專區" ANSI_RESET "\n"
             "  [" ANSI_COLOR(1;37) "u" ANSI_RESET "]    設定使用者資料  "
             "[" ANSI_COLOR(1;37) "K" ANSI_RESET "]    把人踢出去       "
             "[" ANSI_COLOR(1;37) "H" ANSI_RESET "]          切換幽靈模式\n"
             "  [" ANSI_COLOR(1;37) "#" ANSI_RESET "]    切換顯示 PID    "
#ifdef SHOWUID
             "[" ANSI_COLOR(1;37) "U" ANSI_RESET "]    切換顯示 UID     "
#endif
#if defined(SHOWBOARD) && defined(DEBUG)
             "[" ANSI_COLOR(1;37) "Y" ANSI_RESET "]          切換顯示 board"
#endif
             "\n");
    pressanykey();
}

static int
userlist_header(PSB_CTX *ctx)
{
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->cmd.priv;
    int idletime = 0;
#ifdef SHOW_IDLE_TIME
    idletime = 1;
#endif
    if (ulist_scrw != t_columns || ulist_scrh != t_lines) {
        vs_cols_layout(ulist_coldef, ulist_cols, ULISTCOLS);
        ulist_scrw = t_columns;
        ulist_scrh = t_lines;
    }

    showtitle((HasUserFlag(UF_FRIEND)) ? "好友列表" : "休閒聊天", BBSNAME);

    move(1, 0);
    prints("  排序:[%s] 上站人數:%-4d "
           ANSI_COLOR(1;32) "我的朋友:%-3d "
           ANSI_COLOR(33) "與我為友:%-3d "
           ANSI_COLOR(36) "板友:%-4d "
           ANSI_COLOR(31) "壞人:%-2d"
           ANSI_RESET "\n",
           MSG_PICKUP_WAY[*cx->pickup_way], SHM->UTMPnumber,
           cx->myfriend, cx->friendme, currutmp->brc_id ? cx->bfriend : 0, cx->badfriend);

    move(2, 0);
    outs(ANSI_REVERSE);
    vs_cols(ulist_coldef, ulist_cols, ULISTCOLS,
            *cx->show_uid ? "UID" : "編號",
            "P",
            "代號",
            "暱稱",
            MODE_STRING[(int)*cx->show_mode],
#if defined(SHOWBOARD) && defined(DEBUG)
            *cx->show_board ? "看板" :
#endif
            "動態",
            *cx->show_pid ? "PID" : "",
            idletime ? "發呆" : "",
            "");
    outs(ANSI_RESET);
    return 0;
}

static int
userlist_footer(PSB_CTX *ctx GCC_UNUSED)
{
    if (HAS_ANGEL && HasUserPerm(PERM_ANGEL) && currutmp) {
        static const char *modestr[ANGELPAUSE_MODES] = {
            ANSI_COLOR(0;30;47) "開放",
            ANSI_COLOR(0;32;47) "停收",
            ANSI_COLOR(0;31;47) "關閉",
        };
        move(b_lines, 0);
        vbarlr(ANSI_COLOR(34;46) " 休閒聊天 "
               ANSI_COLOR(31;47) " (TAB/f)" ANSI_COLOR(30) "排序/好友 "
               ANSI_COLOR(31) "(p)" ANSI_COLOR(30) "一般呼叫器 "
               ANSI_COLOR(31) "(^P)" ANSI_COLOR(30) "神諭呼叫器", TEMPFORMAT(STRLEN, ANSI_COLOR(1;30;47) "[神諭呼叫器] %s ",
               modestr[currutmp->angelpause % ANGELPAUSE_MODES]));
    } else {
        vs_footer(" 休閒聊天 ",
                  " (TAB/f)排序/好友 (a/o)交友 (q/w)查詢/丟水球 (t/m)聊天/寫信\t(h)說明");
    }
    return 0;
}

static int
userlist_renderer(int idx, PSB_CTX *ctx)
{
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->cmd.priv;
    int i = idx - ctx->cmd.base;
    int ch = idx + 1;
    char pagerchar[6] = "* -Wf";
    userinfo_t *uentp;
    int state, friend;
    char pager[3];
    char num[10];
    char xuid[IDLEN+1+20];
    char description[30];
    char idlestr[32];
    char *mind = "";
#ifdef SHOW_IDLE_TIME
    int idletime;
#endif

    if (ulist_scrw != t_columns || ulist_scrh != t_lines) {
        vs_cols_layout(ulist_coldef, ulist_cols, ULISTCOLS);
        ulist_scrw = t_columns;
        ulist_scrh = t_lines;
    }

    if (i < 0 || i >= nPickups) {
        clrtoeol();
        return 0;
    }
    uentp = cx->currpickup[i].ui;
    friend = cx->currpickup[i].friend;
    if (uentp == NULL) {
        clrtoeol();
        return 0;
    }
    if (!uentp->pid) {
        vs_cols(ulist_coldef, ulist_cols, 3,
                "", "", "< 離站中..>");
        return 0;
    }

    if (PERM_HIDE(uentp))
        state = 9;
    else if (currutmp == uentp)
        state = 10;
    else if (friend & IRH)
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

    if (uentp->userlevel & PERM_VIOLATELAW)
        mind = ANSI_COLOR(1;31) "違規";

    SNPRINTF(num, "%d",
#ifdef SHOWUID
             *cx->show_uid ? uentp->uid :
#endif
             ch);

    pager[0] = (friend & HRM) ? 'X' : pagerchar[uentp->pager % 5];
    pager[1] = (uentp->invisible ? ')' : ' ');
    pager[2] = 0;

    if (fcolor[state])
        SNPRINTF(xuid, "%s%s", fcolor[state], uentp->userid);

    vs_cols(ulist_coldef, ulist_cols, ULISTCOLS,
            num,
            pager,
            fcolor[state] ? xuid : uentp->userid,
            uentp->nickname,
            descript(*cx->show_mode, uentp, uentp->pager & !(friend & HRM),
                     description, sizeof(description)),
#if defined(SHOWBOARD) && defined(DEBUG)
            *cx->show_board ? (uentp->brc_id == 0 ? "" :
                getbcache(uentp->brc_id)->brdname) :
#endif
                modestring(uentp, 0),
            mind,
            idlestr,
            "");
    return 0;
}

static int
userlist_search_online_user(pickup_t *currpickup, int pickup_way, int *page, int *offset,
                            int *myfriend, int *friendme, int *badfriend)
{
    if (HasUserFlag(UF_FRIEND))
        return 0;

    char swid[IDLEN + 1];
    move(1, 0);

    int si = CompleteOnlineUser(MSG_UID, swid);
    if (si < 0)
        return 0;

    pickup_t friends[MAX_FRIEND_ONLINE + 1];
    int *ulist = SHM->sorted[SHM->currsorted][((pickup_way == 0) ? 0 : (pickup_way - 1))];
    int fi = ulist[si];
    int nGots = pickup_myfriend(friends, myfriend, friendme, badfriend);

    int i;
    for (i = 0; i < nGots; ++i) {
        if (friends[i].uoffset == fi)
            break;
    }

    fi = 0;
    *offset = 0;
    if (i != nGots) {
        *page = i / nPickups;
        for (; i < nGots && fi < nPickups; ++i) {
            if (isvisible(currutmp, friends[i].ui))
                currpickup[fi++] = friends[i];
        }
        i = 0;
    } else {
        *page = (si + nGots) / nPickups;
        i = si;
    }

    for (; fi < nPickups && i < SHM->UTMPnumber; ++i) {
        userinfo_t *u = &SHM->uinfo[ulist[i]];
        if (isvisible(currutmp, u)) {
            currpickup[fi].ui = u;
            currpickup[fi++].friend = 0;
        }
    }
    for (; fi < nPickups; ++fi)
        currpickup[fi].ui = NULL;
    return 1;
}

static void
userlist_broadcast(void)
{
    if (HasUserFlag(UF_FRIEND) || HasUserPerm(PERM_SYSOP)) {
        char genbuf[60] = "[廣播]";
        char ans[4];

        if (!getdata(0, 0, "廣播訊息:", genbuf + 6, 54, DOECHO))
            return;

        if (!getdata(0, 0, "確定廣播? [N]", ans, sizeof(ans), LCECHO) || ans[0] != 'y')
            return;

        if (!(HasUserFlag(UF_FRIEND)) && HasUserPerm(PERM_SYSOP)) {
            getdata(1, 0, "再次確定站長廣播? [N]", ans, sizeof(ans), LCECHO);
            if (ans[0] != 'y') {
                vmsg("abort");
                return;
            }

            char msgbuf[PATHLEN];
            SNPRINTF(msgbuf, "[廣播]%s", genbuf);
            for (int i = 0; i < SHM->UTMPnumber; ++i) {
                int uslot = SHM->sorted[SHM->currsorted][0][i];
                userinfo_t *uentp = &SHM->uinfo[uslot];
                if (uentp->pid && kill(uentp->pid, 0) != -1) {
                    write_message(uslot, uentp->pid, currpid, cuser.userid, msgbuf, MSGMODE_WRITE);
                }
            }
        } else {
            userinfo_t *uentp;
            int where, frstate;
            for (int i = 0; i < MAX_FRIEND_ONLINE && currutmp->friend_online[i]; ++i) {
                unsigned int entry = currutmp->friend_online[i];
                where = FRIEND_ONLINE_SLOT(entry);
                if (!VALID_USHM_ENTRY(where))
                    continue;
                uentp = &SHM->uinfo[where];
                if (!uentp || !uentp->pid || !FRIEND_ONLINE_VALID_UID(entry, uentp->uid))
                    continue;
                frstate = FRIEND_ONLINE_STAT(entry);
                if (!(frstate & IFH))
                    continue;
                if (!isvisible_stat(currutmp, uentp, frstate))
                    continue;
                if (uentp->pager == PAGER_ANTIWB)
                    continue;
                if (uentp->pager == PAGER_FRIENDONLY && !(frstate & HFM))
                    continue;
                if (frstate & HRM)
                    continue;
                if (kill(uentp->pid, 0) == -1)
                    continue;
                my_write(uentp->pid, genbuf, uentp->userid, WATERBALL_PREEDIT, NULL);
            }
        }
    }
}

static int
userlist_cmd_quit(cmd_ctx_t *ctx) {
    ctx->quit = true;
    return 0;
}

static int
userlist_cmd_tab(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    *cx->pickup_way = (*cx->pickup_way + 1) % PICKUP_WAYS;
    ctx->reload = true;
    return 0;
}

static int
userlist_count_valid(const pickup_t *currpickup) {
    int vis = 0;
    while (vis < nPickups && currpickup[vis].ui != NULL)
        vis++;
    return vis;
}

static int
userlist_cmd_down(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    int vis = userlist_count_valid(cx->currpickup);
    if (++cx->offset >= vis) {
        int maxp = pickup_maxpages(*cx->pickup_way, cx->nfriend);
        if (maxp <= 1) {
            cx->offset = 0;
            ctx->curr = ctx->base + cx->offset;
        } else {
            if (++cx->page >= maxp)
                cx->offset = cx->page = 0;
            else
                cx->offset = 0;
            ctx->reload = true;
        }
    } else {
        ctx->curr = ctx->base + cx->offset;
    }
    return 0;
}

static int
userlist_cmd_up(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    int vis = userlist_count_valid(cx->currpickup);
    if (--cx->offset < 0) {
        int maxp = pickup_maxpages(*cx->pickup_way, cx->nfriend);
        if (maxp <= 1) {
            cx->offset = vis - 1;
            ctx->curr = ctx->base + cx->offset;
        } else {
            cx->offset = -1;
            if (--cx->page < 0)
                cx->page = maxp - 1;
            ctx->reload = true;
        }
    } else {
        ctx->curr = ctx->base + cx->offset;
    }
    return 0;
}

static int
userlist_cmd_home(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (cx->page != 0) {
        cx->page = cx->offset = 0;
        ctx->reload = true;
    } else {
        cx->offset = 0;
        ctx->curr = ctx->base;
    }
    return 0;
}

static int
userlist_cmd_end(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    int lastp = pickup_maxpages(*cx->pickup_way, cx->nfriend) - 1;
    if (lastp < 0)
        lastp = 0;
    cx->page = lastp;
    cx->offset = -1;
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_pgdn(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    int newpage;
    if ((newpage = cx->page + 1) >= pickup_maxpages(*cx->pickup_way, cx->nfriend))
        newpage = cx->offset = 0;
    if (newpage != cx->page) {
        cx->page = newpage;
        ctx->reload = true;
    } else {
        ctx->curr = ctx->base + cx->offset;
        if (time4_ge(now, cx->lastupdate + 2))
            ctx->reload = true;
    }
    return 0;
}

static int
userlist_cmd_pgup(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (--cx->page < 0)
        cx->page = pickup_maxpages(*cx->pickup_way, cx->nfriend) - 1;
    if (cx->page < 0)
        cx->page = 0;
    cx->offset = 0;
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_sysophide(cmd_ctx_t *ctx) {
    currutmp->userlevel ^= PERM_SYSOPHIDE;
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_cloak(cmd_ctx_t *ctx) {
    currutmp->invisible ^= 1;
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_search(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (userlist_search_online_user(cx->currpickup, *cx->pickup_way, &cx->page, &cx->offset,
                                    &cx->myfriend, &cx->friendme, &cx->badfriend)) {
        cx->skippickup = 1;
        ctx->reload = true;
    } else {
        ctx->redraw = true;
    }
    return 0;
}

static int
userlist_cmd_num(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    int ch = ctx->key;
    int tmp;
    if ((tmp = search_num(ch, SHM->UTMPnumber)) >= 0) {
        cx->page = tmp / nPickups;
        cx->offset = tmp % nPickups;
        ctx->reload = true;
    } else {
        ctx->redraw_footer_lines = 1;
    }
    return 0;
}

#ifdef SHOWUID
static int
userlist_cmd_showuid(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    *cx->show_uid ^= 1;
    ctx->reload = true;
    return 0;
}
#endif

#if defined(SHOWBOARD) && defined(DEBUG)
static int
userlist_cmd_showboard(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    *cx->show_board ^= 1;
    ctx->reload = true;
    return 0;
}
#endif

#ifdef SHOWPID
static int
userlist_cmd_showpid(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    *cx->show_pid ^= 1;
    ctx->reload = true;
    return 0;
}
#endif

static int
userlist_cmd_broadcast(cmd_ctx_t *ctx) {
    userlist_broadcast();
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_showmode(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    *cx->show_mode = (*cx->show_mode + 1) % MAX_SHOW_MODE;
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_edituser(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    int id;
    userec_t muser;
    vs_hdr("使用者設定");
    move(1, 0);
    if ((id = getuser(cx->uentp->userid, &muser)) > 0) {
        user_display(&muser, 1);
        if (HasUserPerm(PERM_ACCOUNTS))
            uinfo_query(muser.userid, 1, id);
        else
            pressanykey();
    }
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_talk(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (cx->uentp->pid != currpid &&
        strcmp(cx->uentp->userid, cuser.userid) != 0) {
        move(1, 0);
        clrtobot();
        move(3, 0);
        my_talk(cx->uentp, cx->fri_stat, 0);
        ctx->reload = true;
    }
    return 0;
}

static int
userlist_cmd_kick(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    my_kick(cx->uentp);
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_write(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (call_in(cx->uentp, cx->fri_stat))
        ctx->reload = true;
    return 0;
}

static int
userlist_cmd_addfriend(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (!(cx->fri_stat & IFH)) {
        if (vans("確定要加入好友嗎 [N/y]") == 'y') {
            friend_add(cx->uentp->userid, FRIEND_OVERRIDE, cx->uentp->nickname);
            friend_load(FRIEND_OVERRIDE, 0);
        }
        ctx->reload = true;
    }
    return 0;
}

static int
userlist_cmd_delfriend(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (cx->fri_stat & IFH) {
        if (vans("確定要刪除好友嗎 [N/y]") == 'y') {
            friend_delete(cx->uentp->userid, FRIEND_OVERRIDE);
            friend_load(FRIEND_OVERRIDE, 0);
        }
        ctx->reload = true;
    }
    return 0;
}

static int
userlist_cmd_override(cmd_ctx_t *ctx) {
    t_override();
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_friendlist(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    pwcuToggleFriendList();
    cx->page = cx->offset = 0;
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_givemoney(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    if (cuser.money) {
        give_money_ui(cx->uentp->userid);
        ctx->reload = true;
    }
    return 0;
}

static int
userlist_cmd_mail(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    char userid[IDLEN + 1];
    STRLCPY(userid, cx->uentp->userid);
    vs_hdr("寄  信");
    prints("[寄信] 收信人：%s", userid);
    my_send(userid);
    setutmpmode(LUSERS);
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_query(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    my_query(cx->uentp->userid);
    setutmpmode(LUSERS);
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_query_input(cmd_ctx_t *ctx) {
    t_query();
    setutmpmode(LUSERS);
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_chicken(cmd_ctx_t *ctx) {
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->priv;
    chicken_query(cx->uentp->userid);
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_review(cmd_ctx_t *ctx) {
    pager_show_log();
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_pager(cmd_ctx_t *ctx) {
    pager_toggle_mode();
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_angel_pause(cmd_ctx_t *ctx) {
    if (HAS_ANGEL && currutmp) {
        angel_toggle_pause();
        ctx->reload = true;
    }
    return 0;
}

static int
userlist_cmd_readmail(cmd_ctx_t *ctx) {
    m_read();
    setutmpmode(LUSERS);
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_nickname(cmd_ctx_t *ctx) {
    char tmp_nick[sizeof(cuser.nickname)];
    if (getdata_str(1, 0, "新的暱稱: ",
                    tmp_nick, sizeof(tmp_nick), DOECHO, cuser.nickname) > 0) {
        pwcuSetNickname(tmp_nick);
        STRLCPY(currutmp->nickname, cuser.nickname);
    }
    ctx->reload = true;
    return 0;
}

static int
userlist_cmd_noop(cmd_ctx_t *ctx GCC_UNUSED) {
    return 0;
}

static int
userlist_cmd_help(cmd_ctx_t *ctx) {
    t_showhelp();
    ctx->redraw = true;
    return 0;
}

static const cmd_t userlist_cmds[] = {
    { 'h', "說明", "顯示操作說明", userlist_cmd_help, 0, CMD_PRIO_NONE },
    { KEY_LEFT, "離開", "離開使用者名單", userlist_cmd_quit, 0, CMD_PRIO_MAX },
    { 'e', NULL, NULL, userlist_cmd_quit, 0, CMD_PRIO_NONE },
    { 'E', NULL, NULL, userlist_cmd_quit, 0, CMD_PRIO_NONE },
    { 'w', "丟水球", "發送水球給選取的使用者", userlist_cmd_write, 0, CMD_PRIO_HIGH, true },
    { 'q', "查詢", "查詢選取的使用者名片檔", userlist_cmd_query, 0, CMD_PRIO_HIGH, true },
    { KEY_RIGHT, "聊天", "邀請選取的使用者聊天", userlist_cmd_talk, PERM_LOGINOK, CMD_PRIO_HIGH, true },
    { KEY_ENTER, NULL, NULL, userlist_cmd_talk, PERM_LOGINOK, CMD_PRIO_NONE, true },
    { 't', NULL, NULL, userlist_cmd_talk, PERM_LOGINOK, CMD_PRIO_NONE, true },
    { 'm', "寄信", "寄站內信給使用者", userlist_cmd_mail, PERM_LOGINOK, CMD_PRIO_HIGH, true },
    { KEY_TAB, "排序", "切換名單排序方式", userlist_cmd_tab, 0, CMD_PRIO_NORM },
    { 'f', "好友", "切換顯示全部/好友名單", userlist_cmd_friendlist, PERM_LOGINOK, CMD_PRIO_NORM },
    { KEY_DOWN, NULL, "向下移動", userlist_cmd_down, 0, CMD_PRIO_NONE, true },
    { 'n', NULL, NULL, userlist_cmd_down, 0, CMD_PRIO_NONE, true },
    { 'j', NULL, NULL, userlist_cmd_down, 0, CMD_PRIO_NONE, true },
    { KEY_UP, NULL, "向上移動", userlist_cmd_up, 0, CMD_PRIO_NONE, true },
    { 'k', NULL, NULL, userlist_cmd_up, 0, CMD_PRIO_NONE, true },
    { '0', NULL, "移至第一頁首筆", userlist_cmd_home, 0, CMD_PRIO_NONE, true },
    { KEY_HOME, NULL, NULL, userlist_cmd_home, 0, CMD_PRIO_NONE, true },
    { KEY_END, NULL, "移至最後一頁末筆", userlist_cmd_end, 0, CMD_PRIO_NONE, true },
    { '$', NULL, NULL, userlist_cmd_end, 0, CMD_PRIO_NONE, true },
    { ' ', NULL, "向下翻頁", userlist_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { KEY_PGDN, NULL, NULL, userlist_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { Ctrl('F'), NULL, NULL, userlist_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { KEY_PGUP, NULL, "向上翻頁", userlist_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { Ctrl('B'), NULL, NULL, userlist_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { 'P', NULL, NULL, userlist_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { 'H', NULL, "切換站長隱身狀態", userlist_cmd_sysophide, PERM_SYSOP | PERM_OLDSYSOP, CMD_PRIO_NONE },
    { 'C', NULL, "切換隱身模式", userlist_cmd_cloak, 0, CMD_PRIO_NONE },
    { '/', NULL, NULL, userlist_cmd_noop, 0, CMD_PRIO_NONE },
    { Ctrl('S'), NULL, NULL, userlist_cmd_noop, 0, CMD_PRIO_NONE },
    { 's', "搜尋", "搜尋線上使用者", userlist_cmd_search, 0, CMD_PRIO_NORM },
    { '1', NULL, "輸入編號跳轉", userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '2', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '3', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '4', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '5', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '6', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '7', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '8', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
    { '9', NULL, NULL, userlist_cmd_num, 0, CMD_PRIO_NONE, true },
#ifdef SHOWUID
    { 'U', NULL, "顯示使用者 UID", userlist_cmd_showuid, PERM_SYSOP, CMD_PRIO_NONE },
#endif
#if defined(SHOWBOARD) && defined(DEBUG)
    { 'Y', NULL, "顯示使用者所在看板", userlist_cmd_showboard, PERM_SYSOP, CMD_PRIO_NONE },
#endif
#ifdef SHOWPID
    { '#', NULL, "顯示使用者 PID", userlist_cmd_showpid, PERM_SYSOP, CMD_PRIO_NONE },
#endif
    { 'b', "廣播", "發送好友廣播水球", userlist_cmd_broadcast, 0, CMD_PRIO_NORM },
    { 'S', "顯示切換", "切換故鄉/描述/戰績顯示模式", userlist_cmd_showmode, 0, CMD_PRIO_NORM },
    { 'u', "查改資料", "查詢或修改使用者帳號資料", userlist_cmd_edituser, PERM_ACCOUNTS | PERM_SYSOP, CMD_PRIO_LOW, true },

    { 'K', "踢人", "將使用者踢出系統", userlist_cmd_kick, PERM_ACCOUNTS | PERM_SYSOP, CMD_PRIO_LOW, true },

    { 'a', "加好友", "將使用者加入好友名單", userlist_cmd_addfriend, PERM_LOGINOK, CMD_PRIO_NORM, true },
    { 'd', "刪好友", "將使用者從好友名單移除", userlist_cmd_delfriend, PERM_LOGINOK, CMD_PRIO_NORM, true },
    { 'o', "名單設定", "編輯好友名單設定", userlist_cmd_override, PERM_LOGINOK, CMD_PRIO_NORM },

    { 'g', "給P幣", "贈送 P 幣給使用者", userlist_cmd_givemoney, PERM_LOGINOK, CMD_PRIO_LOW, true },


    { 'Q', "查指定人", "輸入帳號查詢使用者", userlist_cmd_query_input, 0, CMD_PRIO_NORM, true },
    { 'c', "寵物", "查看使用者的電子雞寵物", userlist_cmd_chicken, PERM_LOGINOK, CMD_PRIO_LOW, true },
    { 'l', "回顧水球", "檢視水球歷史記錄", userlist_cmd_review, PERM_LOGINOK, CMD_PRIO_LOW },
    { 'p', "呼叫器", "切換呼叫器開關模式", userlist_cmd_pager, PERM_BASIC, CMD_PRIO_NORM },
    { Ctrl('P'), "神諭呼叫", "切換小天使暫停狀態", userlist_cmd_angel_pause, PERM_ANGEL, CMD_PRIO_LOW },
    { 'r', "讀信", "閱\讀個人信箱", userlist_cmd_readmail, PERM_READMAIL, CMD_PRIO_NORM },
    { 'N', "改暱稱", "修改個人暫時暱稱", userlist_cmd_nickname, PERM_LOGINOK, CMD_PRIO_NORM },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

static bool
userlist_ensure_valid_cursor(userlist_ctx_t *cx) {
    int vis = userlist_count_valid(cx->currpickup);
    if (vis > 0) {
        if (cx->offset < 0 || cx->offset >= vis)
            cx->offset = vis - 1;
        return true;
    }
    if (cx->page <= 0) {
        cx->page = 0;
        cx->offset = 0;
        return true;
    }
    if (cx->offset >= 0) {
        cx->page = 0;
        cx->offset = 0;
    } else {
        --cx->page;
        cx->offset = -1;
    }
    return false;
}

static int
userlist_loader(PSB_CTX *ctx)
{
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->cmd.priv;
    if (nPickups != b_lines - 3) {
        nPickups = b_lines - 3;
        cx->currpickup = (pickup_t *)realloc(cx->currpickup, sizeof(pickup_t) * nPickups);
    }
    int rows = nPickups;

    if (ctx->cmd.base != ctx->cached_base && !ctx->cmd.reload) {
        cx->page = ctx->cmd.base / rows;
        cx->offset = ctx->cmd.curr % rows;
    }

    while (1) {
        if (!cx->skippickup) {
            pickup(cx->currpickup, *cx->pickup_way, &cx->page,
                   &cx->nfriend, &cx->myfriend, &cx->friendme, &cx->bfriend, &cx->badfriend);
        }
        cx->skippickup = 0;
        if (userlist_ensure_valid_cursor(cx))
            break;
    }
    cx->lastupdate = now;

    int vis = userlist_count_valid(cx->currpickup);
    int maxp = pickup_maxpages(*cx->pickup_way, cx->nfriend);
    if (maxp < 1)
        maxp = 1;
    ctx->cmd.base = cx->page * rows;
    ctx->cmd.curr = ctx->cmd.base + cx->offset;
    if (vis == 0 && cx->page == 0)
        ctx->cmd.total = 0;
    else if (cx->page + 1 >= maxp)
        ctx->cmd.total = ctx->cmd.base + vis;
    else
        ctx->cmd.total = maxp * rows;

    cx->uentp = (vis > 0) ? cx->currpickup[cx->offset].ui : NULL;
    cx->fri_stat = (vis > 0) ? cx->currpickup[cx->offset].friend : 0;
    return 0;
}

static int
userlist_cursor(int y, PSB_CTX *ctx)
{
    userlist_ctx_t *cx = (userlist_ctx_t *)ctx->cmd.priv;
    int vis = userlist_count_valid(cx->currpickup);
    cx->offset = ctx->cmd.curr - ctx->cmd.base;
    if (cx->offset < 0 || cx->offset >= vis) {
        cx->uentp = NULL;
        cx->fri_stat = 0;
    } else {
        cx->uentp = cx->currpickup[cx->offset].ui;
        cx->fri_stat = cx->currpickup[cx->offset].friend;
    }
    cursor_show(y, 0);
    return 0;
}

static int
userlist_on_key(PSB_CTX *ctx)
{
    int ret = cmd_dispatch_layers(ctx->layers, &ctx->cmd, ctx->cmd.caption);
    if (ret == PSB_NA) {
        userlist_ctx_t *cx = (userlist_ctx_t *)ctx->cmd.priv;
        if (time4_ge(now, cx->lastupdate + 2))
            ctx->cmd.reload = true;
        return 0;
    }
    return ret;
}

static void
userlist(void)
{
    static char     show_mode = 0;
    static char     show_uid = 0;
#if defined(SHOWBOARD) && defined(DEBUG)
    static char     show_board = 0;
#endif
    static char     show_pid = 0;
    static int      pickup_way = 0;
    userlist_ctx_t  cx = { 0 };
    cmd_layer_t layers[] = {
        { userlist_cmds, &cx },
        { bbs_global_cmds, NULL },
        { NULL, NULL }
    };

    nPickups = b_lines - 3;
    cx.currpickup = (pickup_t *)malloc(sizeof(pickup_t) * nPickups);
    cx.page = cx.offset = 0;
    cx.nfriend = cx.myfriend = cx.friendme = cx.bfriend = cx.badfriend = 0;
    cx.show_mode = &show_mode;
    cx.show_uid = &show_uid;
#if defined(SHOWBOARD) && defined(DEBUG)
    cx.show_board = &show_board;
#endif
    cx.show_pid = &show_pid;
    cx.pickup_way = &pickup_way;

    PSB_CTX psbctx = {
        .cmd = {
            .curr = 0,
            .priv = &cx,
            .caption = " 休閒聊天 ",
        },
        .header_lines = 3,
        .footer_lines = 1,
        .layers = layers,
        .loader = userlist_loader,
        .header = userlist_header,
        .footer = userlist_footer,
        .renderer = userlist_renderer,
        .cursor = userlist_cursor,
        .on_key = userlist_on_key,
    };

    psb_main(&psbctx);

    free(cx.currpickup);
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
    usercomplete(MSG_UID, uident);
    if (uident[0])
	chicken_query(uident);
    return 0;
}

int
t_query(void)
{
    char            uident[STRLEN];

    vs_hdr("查詢網友");
    usercomplete(MSG_UID, uident);
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
    CompleteOnlineUser(MSG_UID, uident);
    if (uident[0] == '\0')
	return 0;

    move(3, 0);
    if (!(tuid = searchuser(uident, uident)) || tuid == usernum) {
	outs(ERR_UID);
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
    char            buf[4];

    if (uip->mode != PAGE) {
	getdata(0, 0, TEMPFORMAT(STRLEN, "%s 已停止呼叫，按Enter繼續...", uip->userid),
		buf, sizeof(buf), LCECHO);
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

    if (!VALID_USHM_ENTRY(currutmp->destuip)) {
        currstat = currstat0;
        return;
    }
    uip = &SHM->uinfo[currutmp->destuip];
    currutmp->destuid = uip->uid;
    currstat = REPLY;		/* 避免出現動畫 */

    is_chess = (sig == SIG_CHC || sig == SIG_GOMO);

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
    prints("       (Y) 讓我們 %s 吧！\n", get_sig_des(sig));
    prints("       (N) 我現在不方便 %s。\n", get_sig_des(sig));
    prints("       (E) 有事嗎？請先來信\n");
    prints("       (F) " ANSI_COLOR(1;33) "<自行輸入理由>..." ANSI_RESET "\n\n");

    getuser(uip->userid, &xuser);
    currutmp->msgs[0].pid = uip->pid;
    STRLCPY(currutmp->msgs[0].userid, uip->userid);
    STRLCPY(currutmp->msgs[0].last_call_in, "呼叫、呼叫，聽到請回答 (Ctrl-R)");
    currutmp->msgs[0].msgmode = MSGMODE_TALK;
    currutmp->msgs[0].uslot = get_utmp_slot(uip);
    prints("對方來自 [%s]，" STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY "，文章共 %d 篇\n",
	    uip->from, xuser.numlogindays, xuser.numposts);

    if (is_chess)
	ChessShowRequest();
    else {
	showplans(uip->userid);
	show_call_in(0, 0);
    }

    SNPRINTF(genbuf, "你想跟 %s (%s) %s嗎？請選擇[N]: ",
	    uip->userid, uip->nickname, get_sig_des(sig));
    getdata(0, 0, genbuf, buf, sizeof(buf), LCECHO);

    if (!buf[0] || !strchr("yef", buf[0]))
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

    uip->destuip = get_utmp_slot(currutmp);
    if (buf[0] == 'y')
	switch (sig) {
	case SIG_GOMO:
	    gomoku(a, CHESS_MODE_VERSUS);
	    break;
	case SIG_CHC:
	    chc(a, CHESS_MODE_VERSUS);
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
        int             old_newmail = ISNEWMAIL(currutmp);

        scr_dump(&old_screen);
        my_newfd = vkey_detach();

        t_users();

        vkey_attach(my_newfd);
        scr_restore(&old_screen);
        if (ZA_Waiting())
            return Ctrl('Z');
        if (ISNEWMAIL(currutmp) != old_newmail)
            return 0;
    }
    return KEY_INCOMPLETE;
}

void
talk_init_hooks(void)
{
    vkey_register_hook(VKEY_HOOK_PRIO_NORMAL, talk_key_hook);
}
