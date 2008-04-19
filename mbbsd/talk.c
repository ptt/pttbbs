/* $Id$ */
#include "bbs.h"

#define QCAST   int (*)(const void *, const void *)

static char    * const IdleTypeTable[] = {
    "偶在花呆啦", "情人來電", "覓食中", "拜見周公", "假死狀態", "我在思考"
};
static char    * const sig_des[] = {
    "鬥雞", "聊天", "", "下棋", "象棋", "暗棋", "下圍棋", "下黑白棋",
};
static char    * const withme_str[] = {
  "談天", "下五子棋", "鬥寵物", "下象棋", "下暗棋", "下圍棋", "下黑白棋", NULL
};

#define MAX_SHOW_MODE 6
/* M_INT: monitor mode update interval */
#define M_INT 15		
/* P_INT: interval to check for page req. in talk/chat */
#define P_INT 20		
#define BOARDFRI  1

#define TALK_MAXCOL (78)
#define TALK_BUFLEN (TALK_MAXCOL+2)
typedef struct twpic {
    unsigned short len;
    unsigned char data[TALK_BUFLEN]; // bound to specific size
}		twpic_t;

typedef struct talkwin_t {
    int curcol, curln;
    int sline,  eline;
    twpic_t *big_picture;
}               talkwin_t;

typedef struct pickup_t {
    userinfo_t     *ui;
    int             friend, uoffset;
}               pickup_t;

/* 記錄 friend 的 user number */
//
#define PICKUP_WAYS     8

static char    * const fcolor[11] = {
    NULL, ANSI_COLOR(36), ANSI_COLOR(32), ANSI_COLOR(1;32),
    ANSI_COLOR(33), ANSI_COLOR(1;33), ANSI_COLOR(1;37), ANSI_COLOR(1;37),
    ANSI_COLOR(31), ANSI_COLOR(1;35), ANSI_COLOR(1;36)
};
static char     save_page_requestor[40];
static char     page_requestor[40];

userinfo_t *uip;

int
iswritable_stat(const userinfo_t * uentp, int fri_stat)
{
    if (uentp == currutmp)
	return 0;

    if (HasUserPerm(PERM_SYSOP))
	return 1;

    if (!HasUserPerm(PERM_LOGINOK) || HasUserPerm(PERM_VIOLATELAW))
	return 0;

    return (uentp->pager != PAGER_ANTIWB && 
	    (fri_stat & HFM || uentp->pager != PAGER_FRIENDONLY));
}

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
	snprintf(modestr, sizeof(modestr), "E:%s",
		ModeTypeTable[uentp->destuid < EDITING ? uentp->destuid :
			      EDITING]);
	word = modestr;
    } else if (!mode && *uentp->chatid == 1) {
	if (!simple)
	    snprintf(modestr, sizeof(modestr), "回應 %s",
		    isvisible_uid(uentp->destuid) ?
		    getuserid(uentp->destuid) : "空氣");
	else
	    snprintf(modestr, sizeof(modestr), "回應呼叫");
    } 
    else if (!mode && *uentp->chatid == 3)
	snprintf(modestr, sizeof(modestr), "水球準備中");
    else if (
#ifdef NOKILLWATERBALL
	     uentp->msgcount > 0
#else
	     (!mode) && *uentp->chatid == 2
#endif
	     )
	if (uentp->msgcount < 10) {
	    const char *cnum[10] =
	    {"", "一", "兩", "三", "四", "五", 
		 "六", "七", "八", "九"};
	    snprintf(modestr, sizeof(modestr),
		     "中%s顆水球", cnum[(int)(uentp->msgcount)]);
	} else
	    snprintf(modestr, sizeof(modestr), "不行了 @_@");
    else if (!mode)
	return (uentp->destuid == 6) ? uentp->chatid :
	    IdleTypeTable[(0 <= uentp->destuid && uentp->destuid < 6) ?
			  uentp->destuid : 0];
    else if (simple)
	return word;
    else if (uentp->in_chat && mode == CHATING)
	snprintf(modestr, sizeof(modestr), "%s (%s)", word, uentp->chatid);
    else if (mode == TALK || mode == M_FIVE || mode == CHC || mode == UMODE_GO
	    || mode == DARK) {
	if (!isvisible_uid(uentp->destuid))	/* Leeym 對方(紫色)隱形 */
	    snprintf(modestr, sizeof(modestr), "%s 空氣", word);
	/* Leeym * 大家自己發揮吧！ */
	else
	    snprintf(modestr, sizeof(modestr),
		     "%s %s", word, getuserid(uentp->destuid));
    } else if (mode == CHESSWATCHING) {
	snprintf(modestr, sizeof(modestr), "觀棋");
    } else if (mode != PAGE && mode != TQUERY)
	return word;
    else
	snprintf(modestr, sizeof(modestr),
		 "%s %s", word, getuserid(uentp->destuid));

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
int sync_outta_server(int sfd)
{
    int i;
    int offset = (int)(currutmp - &SHM->uinfo[0]);

    int cmd, res;
    int nfs;
    ocfs_t  fs[MAX_FRIEND*2];

    cmd = -2;
    if(towrite(sfd, &cmd, sizeof(cmd))<0 ||
	    towrite(sfd, &offset, sizeof(offset))<0 ||
	    towrite(sfd, &currutmp->uid, sizeof(currutmp->uid)) < 0 ||
	    towrite(sfd, currutmp->myfriend, sizeof(currutmp->myfriend))<0 ||
	    towrite(sfd, currutmp->reject, sizeof(currutmp->reject))<0)
	return -1;

    if(toread(sfd, &res, sizeof(res))<0)
	return -1;

    if(res<0)
	return -1;
    if(res==2) {
	close(sfd);
	outs("登入太頻繁, 為避免系統負荷過重, 請稍後再試\n");
	refresh();
	sleep(30);
	log_usies("REJECTLOGIN", NULL);
	memset(currutmp, 0, sizeof(userinfo_t));
	exit(0);
    }

    if(toread(sfd, &nfs, sizeof(nfs))<0)
	return -1;
    if(nfs<0 || nfs>MAX_FRIEND*2) {
	fprintf(stderr, "invalid nfs=%d\n",nfs);
	return -1;
    }

    if(toread(sfd, fs, sizeof(fs[0])*nfs)<0)
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

void login_friend_online(void)
{
    userinfo_t     *uentp;
    int             i;
    unsigned int    stat, stat1;
    int             offset = (int)(currutmp - &SHM->uinfo[0]);

#ifdef UTMPD
    int sfd;
    /* UTMPD is TOO slow, let's prompt user here. */
    move(b_lines-2, 0); clrtobot();
    outs("\n正在更新與同步線上使用者及好友名單，系統負荷量大時會需時較久...\n");
    refresh();

    sfd = toconnect(UTMPD_ADDR);
    if(sfd>=0) {
	int res=sync_outta_server(sfd);
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
	    stat |= (int)(uentp - &SHM->uinfo[0]);
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
    int             offset = (int)(utmp - &SHM->uinfo[0]);
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
    /* 看板好友 */
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
	snprintf(genbuf, sizeof(genbuf),
		 "%s (%s)", uentp->userid, uentp->nickname);
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
    userinfo_t     *uentp;
    const char *sex[8] =
    {MSG_BIG_BOY, MSG_BIG_GIRL,
	MSG_LITTLE_BOY, MSG_LITTLE_GIRL,
    MSG_MAN, MSG_WOMAN, MSG_PLANT, MSG_MIME};
    static time_t last_query;

    STATINC(STAT_QUERY);
    if ((tuid = getuser(uident, &muser))) {
	move(1, 0);
	clrtobot();
	move(1, 0);
	setutmpmode(TQUERY);
	currutmp->destuid = tuid;

	if ((uentp = (userinfo_t *) search_ulist(tuid)))
	    fri_stat = friend_stat(currutmp, uentp);

	prints("《ＩＤ暱稱》%s (%s)%*s《經濟狀況》%s",
	       muser.userid,
	       muser.nickname,
	       strlen(muser.userid) + strlen(muser.nickname) >= 25 ? 0 :
	       (int)(25 - strlen(muser.userid) - strlen(muser.nickname)), "",
	       money_level(muser.money));
	if (uentp && ((fri_stat & HFM && !uentp->invisible) || strcmp(muser.userid,cuser.userid) == 0))
	    prints(" ($%d)", muser.money);
	outc('\n');

	prints("《上站次數》%d次", muser.numlogins);
#ifdef SHOW_LOGINOK
	if (!(muser.userlevel & PERM_LOGINOK))
	    outs(" (尚未通過認證)");
#endif
	move(2, 40);
#ifdef ASSESS
	prints("《文章篇數》%d篇 (優:%d/劣:%d)\n", muser.numposts, muser.goodpost, muser.badpost);
#else
	prints("《文章篇數》%d篇\n", muser.numposts);
#endif

	prints(ANSI_COLOR(1;33) "《目前動態》%-28.28s" ANSI_RESET,
	       (uentp && isvisible_stat(currutmp, uentp, fri_stat)) ?
	       modestring(uentp, 0) : "不在站上");

	outs(((uentp && ISNEWMAIL(uentp)) || load_mailalert(muser.userid))
	     ? "《私人信箱》有新進信件還沒看\n" :
	     "《私人信箱》所有信件都看過了\n");
	prints("《上次上站》%-28.28s《上次故鄉》",
	       Cdate(&muser.lastlogin));
	// print out muser.lasthost
#ifdef USE_MASKED_FROMHOST
	if(!HasUserPerm(PERM_SYSOP|PERM_ACCOUNTS)) 
	    obfuscate_ipstr(muser.lasthost);
#endif // !USE_MASKED_FROMHOST
	outs(muser.lasthost[0] ? muser.lasthost : "(不詳)");
	outs("\n");

	prints("《五子棋戰績》%3d 勝 %3d 敗 %3d 和      "
	       "《象棋戰績》%3d 勝 %3d 敗 %3d 和\n",
	       muser.five_win, muser.five_lose, muser.five_tie,
	       muser.chc_win, muser.chc_lose, muser.chc_tie);
#ifdef ASSESS
	prints("《競標評比》 優 %d / 劣 %d", muser.goodsale, muser.badsale);
	move(6, 40);
#endif
	if ((uentp && ((fri_stat & HFM) || strcmp(muser.userid,cuser.userid) == 0) && !uentp->invisible))
	    prints("《 性  別 》%-28.28s\n", sex[muser.sex % 8]);

	showplans_userec(&muser);
	if(HasUserPerm(PERM_SYSOP|PERM_POLICE) ) 
	{
          if(vmsg("T: 開立罰單")=='T')
		  violate_law(&muser, tuid);
	}
	else
	   pressanykey();
	if(now-last_query<1)
	    sleep(2);
	else if(now-last_query<2)
	    sleep(1);
	last_query=now;
	return FULLUPDATE;
    }
    return DONOTHING;
}

static char     t_last_write[80];

void check_water_init(void)
{
    if(water==NULL) {
	water = (water_t*)malloc(sizeof(water_t)*6);
	memset(water, 0, sizeof(water_t)*6);
	water_which = &water[0];

	strlcpy(water[0].userid, " 全部 ", sizeof(water[0].userid));
    }
}
		
static void
water_scr(const water_t * tw, int which, char type)
{
    if (type == 1) {
	int             i;
	const int colors[] = {33, 37, 33, 37, 33};
	move(8 + which, 28);
	outc(' ');
	move(8 + which, 28);
	prints(ANSI_COLOR(1;37;45) "  %c %-14s " ANSI_COLOR(0) "",
	       tw->uin ? ' ' : 'x',
	       tw->userid);
	for (i = 0; i < 5; ++i) {
	    move(16 + i, 4);
	    outc(' ');
	    move(16 + i, 4);
	    if (tw->msg[(tw->top - i + 4) % 5].last_call_in[0] != 0)
		prints(ANSI_COLOR(0) "   " ANSI_COLOR(1;%d;44) "★%-64s" ANSI_COLOR(0) "   \n",
		       colors[i],
		       tw->msg[(tw->top - i + 4) % 5].last_call_in);
	    else
		outs(ANSI_COLOR(0) "　\n");
	}

	move(21, 4);
	outc(' ');
	move(21, 4);
	prints(ANSI_COLOR(0) "   " ANSI_COLOR(1;37;46) "%-66s" ANSI_COLOR(0) "   \n",
	       tw->msg[5].last_call_in);

	move(0, 0);
	outc(' ');
	move(0, 0);
#ifdef PLAY_ANGEL
	if (tw->msg[0].msgmode == MSGMODE_TOANGEL)
	    outs(ANSI_COLOR(0) "回答小主人:");
	else
#endif
	prints(ANSI_COLOR(0) "反擊 %s:", tw->userid);
	clrtoeol();
	move(0, strlen(tw->userid) + 6);
    } else {

#ifndef USE_PFTERM
	// workaround poor terminal, made by in2.
	move(8 + which, 28);
	outs("123456789012345678901234567890");
#endif // !USE_PFTERM

	move(8 + which, 28);
	prints(ANSI_COLOR(1;37;44) "  %c %-13s　" ANSI_COLOR(0) "",
	       tw->uin ? ' ' : 'x',
	       tw->userid);
    }
}

void
my_write2(void)
{
    int             i, ch, currstat0;
    char            genbuf[256], msg[80], done = 0, c0, which;
    water_t        *tw;
    unsigned char   mode0;

    check_water_init();
    if (swater[0] == NULL)
	return;
    wmofo = REPLYING;
    currstat0 = currstat;
    c0 = currutmp->chatid[0];
    mode0 = currutmp->mode;
    currutmp->mode = 0;
    currutmp->chatid[0] = 3;
    currstat = DBACK;

    //init screen
    move(WB_OFO_USER_TOP, WB_OFO_USER_LEFT);
    outs(ANSI_COLOR(1;33;46) " ↑ 水球反擊對象 ↓" ANSI_COLOR(0) "");
    for (i = 0; i < WB_OFO_USER_HEIGHT;++i)
	if (swater[i] == NULL || swater[i]->pid == 0)
	    break;
	else {
	    if (swater[i]->uin &&
		(swater[i]->pid != swater[i]->uin->pid ||
		 swater[i]->userid[0] != swater[i]->uin->userid[0]))
		swater[i]->uin = search_ulist_pid(swater[i]->pid);
	    water_scr(swater[i], i, 0);
	}
    move(WB_OFO_MSG_TOP, WB_OFO_MSG_LEFT);
    outs(ANSI_COLOR(0) " " ANSI_COLOR(1;35) "◇" ANSI_COLOR(1;36) "────────────────"
	   "─────────────────" ANSI_COLOR(1;35) "◇" ANSI_COLOR(0) " ");
    move(WB_OFO_MSG_BOTTOM, WB_OFO_MSG_LEFT);
    outs(" " ANSI_COLOR(1;35) "◇" ANSI_COLOR(1;36) "────────────────"
	   "─────────────────" ANSI_COLOR(1;35) "◇" ANSI_COLOR(0) " ");
    water_scr(swater[0], 0, 1);
    refresh();

    which = 0;
    do {
	switch ((ch = igetch())) {
	case Ctrl('T'):
	case KEY_UP:
	    if (water_usies != 1) {
		water_scr(swater[(int)which], which, 0);
		which = (which - 1 + water_usies) % water_usies;
		water_scr(swater[(int)which], which, 1);
		refresh();
	    }
	    break;

	case KEY_DOWN:
	case Ctrl('R'):
	    if (water_usies != 1) {
		water_scr(swater[(int)which], which, 0);
		which = (which + 1 + water_usies) % water_usies;
		water_scr(swater[(int)which], which, 1);
		refresh();
	    }
	    break;

	case KEY_LEFT:
	    done = 1;
	    break;

	case KEY_UNKNOWN:
	    break;

	default:
	    done = 1;
	    tw = swater[(int)which];

	    if (!tw->uin)
		break;

	    if (ch != '\r' && ch != '\n') {
		msg[0] = ch, msg[1] = 0;
	    } else
		msg[0] = 0;
	    move(0, 0);
	    outs(ANSI_RESET);
	    clrtoeol();
#ifndef PLAY_ANGEL
	    snprintf(genbuf, sizeof(genbuf), "攻擊 %s:", tw->userid);
	    i = WATERBALL_CONFIRM;
#else
	    if (tw->msg[0].msgmode == MSGMODE_WRITE) {
		snprintf(genbuf, sizeof(genbuf), "攻擊 %s:", tw->userid);
		i = WATERBALL_CONFIRM;
	    } else if (tw->msg[0].msgmode == MSGMODE_TOANGEL) {
		strlcpy(genbuf, "回答小主人:", sizeof(genbuf));
		i = WATERBALL_CONFIRM_ANSWER;
	    } else { /* tw->msg[0].msgmode == MSGMODE_FROMANGEL */
		strlcpy(genbuf, "再問他一次：", sizeof(genbuf));
		i = WATERBALL_CONFIRM_ANGEL;
	    }
#endif
	    if (!getdata_buf(0, 0, genbuf, msg,
			80 - strlen(tw->userid) - 6, DOECHO))
		break;

	    if (my_write(tw->pid, msg, tw->userid, i, tw->uin))
		strlcpy(tw->msg[5].last_call_in, t_last_write,
			sizeof(tw->msg[5].last_call_in));
	    break;
	}
    } while (!done);

    currstat = currstat0;
    currutmp->chatid[0] = c0;
    currutmp->mode = mode0;
    if (wmofo == RECVINREPLYING) {
	wmofo = NOTREPLYING;
	write_request(0);
    }
    wmofo = NOTREPLYING;
}

/*
 * 被呼叫的時機:
 * 1. 丟群組水球 flag = WATERBALL_PREEDIT, 1 (pre-edit)
 * 2. 回水球     flag = WATERBALL_GENERAL, 0
 * 3. 上站aloha  flag = WATERBALL_ALOHA,   2 (pre-edit)
 * 4. 廣播       flag = WATERBALL_SYSOP,   3 if SYSOP
 *               flag = WATERBALL_PREEDIT, 1 otherwise
 * 5. 丟水球     flag = WATERBALL_GENERAL, 0
 * 6. my_write2  flag = WATERBALL_CONFIRM, 4 (pre-edit but confirm)
 * 7. (when defined PLAY_ANGEL)
 *    呼叫小天使 flag = WATERBALL_ANGEL,   5 (id = "小天使")
 * 8. (when defined PLAY_ANGEL)
 *    回答小主人 flag = WATERBALL_ANSWER,  6 (隱藏 id)
 * 9. (when defined PLAY_ANGEL)
 *    呼叫小天使 flag = WATERBALL_CONFIRM_ANGEL, 7 (pre-edit)
 * 10. (when defined PLAY_ANGEL)
 *    回答小主人 flag = WATERBALL_CONFIRM_ANSWER, 8 (pre-edit)
 */
int
my_write(pid_t pid, const char *prompt, const char *id, int flag, userinfo_t * puin)
{
    int             len, currstat0 = currstat, fri_stat = -1;
    char            msg[80], destid[IDLEN + 1];
    char            genbuf[200], buf[200], c0 = currutmp->chatid[0];
    unsigned char   mode0 = currutmp->mode;
    userinfo_t     *uin;
    uin = (puin != NULL) ? puin : (userinfo_t *) search_ulist_pid(pid);
    strlcpy(destid, id, sizeof(destid));
    check_water_init();

    /* what if uin is NULL but other conditions are not true?
     * will this situation cause SEGV?
     * should this "!uin &&" replaced by "!uin ||" ?
     */
    if ((!uin || !uin->userid[0]) && !((flag == WATERBALL_GENERAL
#ifdef PLAY_ANGEL
		|| flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER 
#endif
            )
	    && water_which->count > 0)) {
	vmsg("糟糕! 對方已落跑了(不在站上)! ");
	watermode = -1;
	return 0;
    }
    currutmp->mode = 0;
    currutmp->chatid[0] = 3;
    currstat = DBACK;

    if (flag == WATERBALL_GENERAL
#ifdef PLAY_ANGEL
	    || flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER
#endif
	    ) {
	/* 一般水球 */
	watermode = 0;

	/* should we alert if we're in disabled mode? */
	switch(currutmp->pager)
	{
	    case PAGER_DISABLE:
	    case PAGER_ANTIWB:
		move(1, 0);  clrtoeol();
		outs(ANSI_COLOR(1;31) "你的呼叫器目前不接受別人丟水球，對方可能無法回話。" ANSI_RESET);
		break;

	    case PAGER_FRIENDONLY:
#if 0
		// 如果對方正在下站，這個好像不太穩會 crash (?) */
		if (uin && uin->userid[0])
		{
		    fri_stat = friend_stat(currutmp, uin);
		    if(fri_stat & HFM)
			break;
		}
#endif
		move(1, 0);  clrtoeol();
		outs(ANSI_COLOR(1;31) "你的呼叫器目前只接受好友丟水球，若對方非好友則可能無法回話。" ANSI_RESET);
		break;
	}

	if (!(len = getdata(0, 0, prompt, msg, 56, DOECHO))) {
	    currutmp->chatid[0] = c0;
	    currutmp->mode = mode0;
	    currstat = currstat0;
	    watermode = -1;
	    return 0;
	}

	if (watermode > 0) {
	    int             i;

	    i = (water_which->top - watermode + MAX_REVIEW) % MAX_REVIEW;
	    uin = (userinfo_t *) search_ulist_pid(water_which->msg[i].pid);
#ifdef PLAY_ANGEL
	    if (water_which->msg[i].msgmode == MSGMODE_FROMANGEL)
		flag = WATERBALL_ANGEL;
	    else if (water_which->msg[i].msgmode == MSGMODE_TOANGEL)
		flag = WATERBALL_ANSWER;
	    else
		flag = WATERBALL_GENERAL;
#endif
	    strlcpy(destid, water_which->msg[i].userid, sizeof(destid));
	}
    } else {
	/* pre-edit 的水球 */
	strlcpy(msg, prompt, sizeof(msg));
	len = strlen(msg);
    }

    strip_ansi(msg, msg, STRIP_ALL);
    if (uin && *uin->userid &&
	    (flag == WATERBALL_GENERAL || flag == WATERBALL_CONFIRM
#ifdef PLAY_ANGEL
	     || flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER
	     || flag == WATERBALL_CONFIRM_ANGEL
	     || flag == WATERBALL_CONFIRM_ANSWER
#endif
	     )) 
    {
	snprintf(buf, sizeof(buf), "丟 %s: %s [Y/n]?", destid, msg);

	getdata(0, 0, buf, genbuf, 3, LCECHO);
	if (genbuf[0] == 'n') {
	    currutmp->chatid[0] = c0;
	    currutmp->mode = mode0;
	    currstat = currstat0;
	    watermode = -1;
	    return 0;
	}
    }
    watermode = -1;
    if (!uin || !*uin->userid || (strcasecmp(destid, uin->userid)
#ifdef PLAY_ANGEL
	    && flag != WATERBALL_ANGEL && flag != WATERBALL_CONFIRM_ANGEL) ||
	    // check if user is changed of angelpause.
	    // XXX if flag == WATERBALL_ANGEL, shuold be (uin->angelpause) only.
	    ((flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)
	     && (strcasecmp(cuser.myangel, uin->userid) || uin->angelpause >= ANGELPAUSE_REJALL)
#endif
	    )) {
	bell();
	vmsg("糟糕! 對方已落跑了(不在站上)! ");
	currutmp->chatid[0] = c0;
	currutmp->mode = mode0;
	currstat = currstat0;
	return 0;
    }
    if(fri_stat < 0)
	fri_stat = friend_stat(currutmp, uin);
    // else, fri_stat was already calculated. */

    if (flag != WATERBALL_ALOHA) {	/* aloha 的水球不用存下來 */
	/* 存到自己的水球檔 */
	if (!fp_writelog) {
	    sethomefile(genbuf, cuser.userid, fn_writelog);
	    fp_writelog = fopen(genbuf, "a");
	    assert(fp_writelog);
	}
	if (fp_writelog) {
	    fprintf(fp_writelog, "To %s: %s [%s]\n",
		    destid, msg, Cdatelite(&now));
	    snprintf(t_last_write, 66, "To %s: %s", destid, msg);
	}
    }
    if (flag == WATERBALL_SYSOP && uin->msgcount) {
	/* 不懂 */
	uin->destuip = currutmp - &SHM->uinfo[0];
	uin->sig = 2;
	if (uin->pid > 0)
	    kill(uin->pid, SIGUSR1);
    } else if ((flag != WATERBALL_ALOHA &&
#ifdef PLAY_ANGEL
	       flag != WATERBALL_ANGEL &&
	       flag != WATERBALL_ANSWER &&
	       flag != WATERBALL_CONFIRM_ANGEL &&
	       flag != WATERBALL_CONFIRM_ANSWER &&
	       /* Angel accept or not is checked outside.
		* Avoiding new users don't know what pager is. */
#endif
	       !HasUserPerm(PERM_SYSOP) &&
	       (uin->pager == PAGER_ANTIWB ||
		uin->pager == PAGER_DISABLE ||
		(uin->pager == PAGER_FRIENDONLY &&
		 !(fri_stat & HFM))))
#ifdef PLAY_ANGEL
	       || ((flag == WATERBALL_ANGEL || flag == WATERBALL_CONFIRM_ANGEL)
		   && angel_reject_me(uin))
#endif
	       ) {
	outmsg(ANSI_COLOR(1;33;41) "糟糕! 對方防水了! " ANSI_COLOR(37) "~>_<~" ANSI_RESET);
    } else {
	int     write_pos = uin->msgcount; /* try to avoid race */
	if ( write_pos < (MAX_MSGS - 1) ) { /* race here */
	    unsigned char   pager0 = uin->pager;

	    uin->msgcount = write_pos + 1;
	    uin->pager = PAGER_DISABLE;
	    uin->msgs[write_pos].pid = currpid;
#ifdef PLAY_ANGEL
	    if (flag == WATERBALL_ANSWER || flag == WATERBALL_CONFIRM_ANSWER)
		strlcpy(uin->msgs[write_pos].userid, "小天使", 
		    sizeof(uin->msgs[write_pos].userid));
	    else
#endif
		strlcpy(uin->msgs[write_pos].userid, cuser.userid,
			sizeof(uin->msgs[write_pos].userid));
	    strlcpy(uin->msgs[write_pos].last_call_in, msg,
		    sizeof(uin->msgs[write_pos].last_call_in));
#ifndef PLAY_ANGEL
	    uin->msgs[write_pos].msgmode = MSGMODE_WRITE;
#else
	    switch (flag) {
		case WATERBALL_ANGEL:
		case WATERBALL_CONFIRM_ANGEL:
		    uin->msgs[write_pos].msgmode = MSGMODE_TOANGEL;
		    break;
		case WATERBALL_ANSWER:
		case WATERBALL_CONFIRM_ANSWER:
		    uin->msgs[write_pos].msgmode = MSGMODE_FROMANGEL;
		    break;
		default:
		    uin->msgs[write_pos].msgmode = MSGMODE_WRITE;
	    }
#endif
	    uin->pager = pager0;
	} else if (flag != WATERBALL_ALOHA)
	    outmsg(ANSI_COLOR(1;33;41) "糟糕! 對方不行了! (收到太多水球) " ANSI_COLOR(37) "@_@" ANSI_RESET);

	if (uin->msgcount >= 1 &&
#ifdef NOKILLWATERBALL
	    !(uin->wbtime = now) /* race */
#else
	    (uin->pid <= 0 || kill(uin->pid, SIGUSR2) == -1) 
#endif
	    && flag != WATERBALL_ALOHA)
	    outmsg(ANSI_COLOR(1;33;41) "糟糕! 沒打中! " ANSI_COLOR(37) "~>_<~" ANSI_RESET);
	else if (uin->msgcount == 1 && flag != WATERBALL_ALOHA)
	    outmsg(ANSI_COLOR(1;33;44) "水球砸過去了! " ANSI_COLOR(37) "*^o^*" ANSI_RESET);
	else if (uin->msgcount > 1 && uin->msgcount < MAX_MSGS &&
		flag != WATERBALL_ALOHA)
	    outmsg(ANSI_COLOR(1;33;44) "再補上一粒! " ANSI_COLOR(37) "*^o^*" ANSI_RESET);

#if defined(NOKILLWATERBALL) && defined(PLAY_ANGEL)
	/* Questioning and answering should better deliver immediately. */
	if ((flag == WATERBALL_ANGEL || flag == WATERBALL_ANSWER ||
	    flag == WATERBALL_CONFIRM_ANGEL ||
	    flag == WATERBALL_CONFIRM_ANSWER) && uin->pid)
	    kill(uin->pid, SIGUSR2);
#endif
    }

    clrtoeol();

    currutmp->chatid[0] = c0;
    currutmp->mode = mode0;
    currstat = currstat0;
    return 1;
}

void
getmessage(msgque_t msg)
{
    int     write_pos = currutmp->msgcount; 
    if ( write_pos < (MAX_MSGS - 1) ) { 
            unsigned char pager0 = currutmp->pager;
 	    currutmp->msgcount = write_pos+1;
            memcpy(&currutmp->msgs[write_pos], &msg, sizeof(msgque_t));
            currutmp->pager = pager0;
   	    write_request(SIGUSR1);
        }
}

void
t_display_new(void)
{
    static int      t_display_new_flag = 0;
    int             i, off = 2;
    if (t_display_new_flag)
	return;
    else
	t_display_new_flag = 1;

    check_water_init();
    if (WATERMODE(WATER_ORIG))
	water_which = &water[0];
    else
	off = 3;

    if (water[0].count && watermode > 0) {
	move(1, 0);
	outs("───────水─球─回─顧───");
	outs(WATERMODE(WATER_ORIG) ?
	     "──────用[Ctrl-R Ctrl-T]鍵切換─────" :
	     "用[Ctrl-R Ctrl-T Ctrl-F Ctrl-G ]鍵切換────");
	if (WATERMODE(WATER_NEW)) {
	    move(2, 0);
	    clrtoeol();
	    for (i = 0; i < 6; i++) {
		if (i > 0)
		    if (swater[i - 1]) {

			if (swater[i - 1]->uin &&
			    (swater[i - 1]->pid != swater[i - 1]->uin->pid ||
			     swater[i - 1]->userid[0] != swater[i - 1]->uin->userid[0]))
			    swater[i - 1]->uin = (userinfo_t *) search_ulist_pid(swater[i - 1]->pid);
			prints("%s%c%-13.13s" ANSI_RESET,
			       swater[i - 1] != water_which ? "" :
			       swater[i - 1]->uin ? ANSI_COLOR(1;33;47) :
			       ANSI_COLOR(1;33;45),
			       !swater[i - 1]->uin ? '#' : ' ',
			       swater[i - 1]->userid);
		    } else
			outs("              ");
		else
		    prints("%s 全部  " ANSI_RESET,
			   water_which == &water[0] ? ANSI_COLOR(1;33;47) " " :
			   " "
			);
	    }
	}
	for (i = 0; i < water_which->count; i++) {
	    int a = (water_which->top - i - 1 + MAX_REVIEW) % MAX_REVIEW;
	    int len = 75 - strlen(water_which->msg[a].last_call_in)
		- strlen(water_which->msg[a].userid);
	    if (len < 0)
		len = 0;

	    move(i + (WATERMODE(WATER_ORIG) ? 2 : 3), 0);
	    clrtoeol();
	    if (watermode - 1 != i)
		prints(ANSI_COLOR(1;33;46) " %s " ANSI_COLOR(37;45) " %s " ANSI_RESET "%*s",
		       water_which->msg[a].userid,
		       water_which->msg[a].last_call_in, len,
		       "");
	    else
		prints(ANSI_COLOR(1;44) ">" ANSI_COLOR(1;33;47) "%s "
		       ANSI_COLOR(37;45) " %s " ANSI_RESET "%*s",
		       water_which->msg[a].userid,
		       water_which->msg[a].last_call_in,
		       len, "");
	}

	if (t_last_write[0]) {
	    move(i + off, 0);
	    clrtoeol();
	    outs(t_last_write);
	    i++;
	}
	move(i + off, 0);
	outs("──────────────────────"
	     "─────────────────");
	if (WATERMODE(WATER_NEW))
	    while (i++ <= water[0].count) {
		move(i + off, 0);
		clrtoeol();
	    }
    }
    t_display_new_flag = 0;
}

int
t_display(void)
{
    char            genbuf[200], ans[4];
    if (fp_writelog) {
	fclose(fp_writelog);
	fp_writelog = NULL;
    }
    setuserfile(genbuf, fn_writelog);
    if (more(genbuf, YEA) != -1) {
	grayout(0, b_lines-5, GRAYOUT_DARK);
	move(b_lines - 4, 0);
	clrtobot();
	outs(ANSI_COLOR(1;33;45) "★水球整理程式 " ANSI_RESET "\n"
	     "提醒您: 可將水球存入信箱(M)後, 到【郵件選單】該信件前按 u,\n"
	     "系統會將水球紀錄重新整理後寄送給您唷! " ANSI_RESET "\n");

	getdata(b_lines - 1, 0, "清除(C) 存入信箱(M) 保留(R) (C/M/R)?[R]",
		ans, sizeof(ans), LCECHO);
	if (*ans == 'm') {
	    // only delete if success because the file can be re-used.
	    if (mail_log2id(cuser.userid, "熱線記錄", genbuf, "[備.忘.錄]", 0, 1) == 0)
		unlink(genbuf);
	    else
		vmsg("信箱儲存失敗。");
	} else if (*ans == 'c')
	    unlink(genbuf);
	return FULLUPDATE;
    }
    return DONOTHING;
}

static void
do_talk_nextline(talkwin_t * twin)
{
    twin->curcol = 0;
    if (twin->curln < twin->eline)
	++(twin->curln);
    else
    {
	int i, iend = twin->eline - twin->sline;
	region_scroll_up(twin->sline, twin->eline);
	// also scroll up our buffer
	for (i = 0; i < iend; i++)
	{
	    memcpy(&twin->big_picture[i], &twin->big_picture[i+1], 
		sizeof(twpic_t));
	}
	// zero last line
	memset(&twin->big_picture[iend], 0, sizeof(twpic_t));
    }
    move(twin->curln, twin->curcol);
}

static int 
iscompletedbcs(const unsigned char* str)
{
    int isdbcs = 0;
    while (*str)
    {
	if (isdbcs == 1) isdbcs = 2;
	else if ((unsigned char)*str > 0x80) isdbcs = 1;
	else isdbcs = 0;
	str++;
    }

    return (isdbcs == 1) ? 0 : 1;
}

static void 
talk_refreshline(talkwin_t *twin)
{
    // dirty screen
    twpic_t *line = twin->big_picture + (twin->curln - twin->sline);
    int iscomplete = iscompletedbcs(line->data);
    int len = strlen((char*)line->data);

    move(twin->curln, 0);
    clrtoeol();
    if (!iscomplete) len--;
    outns((char*)line->data, len);
    if (!iscomplete) outc('?');
    move(twin->curln, twin->curcol);
}

static void
do_talk_char(talkwin_t * twin, int ch, FILE *flog)
{
    twpic_t	    *line;
    char            buf[TALK_BUFLEN] = "";

    line = twin->big_picture+(twin->curln - twin->sline);

    if (isprint2(ch)) {


	if (line->len < TALK_MAXCOL)
	    move(twin->curln, twin->curcol);
	else
	    do_talk_nextline(twin);

	// do_talk_nextline may change current line
	line = twin->big_picture + (twin->curln -twin->sline);
	memmove(line->data + twin->curcol+1, line->data + twin->curcol,
		line->len - twin->curcol +1);
	line->data[twin->curcol++] = ch;
	line->data[++line->len] = 0;

	// dirty screen
	talk_refreshline(twin);

	if (line->len < TALK_MAXCOL)
	    return;

	// max buffer, log it.
	strlcpy(buf, (char *)line->data, line->len + 1);
	
    } else switch (ch) {

    case Ctrl('G'):
	bell();
	return;

    case Ctrl('A'):
	twin->curcol = 0;
	move(twin->curln, twin->curcol);
	return;
    case Ctrl('E'):
	twin->curcol = line->len;
	move(twin->curln, twin->curcol);
	return;


    case Ctrl('K'):
	line->len = twin->curcol;
	memset(line->data+line->len, 0, TALK_MAXCOL - line->len);
	move(twin->curln, twin->curcol);
	clrtoeol();
	return;
    case Ctrl('Y'):
	twin->curcol = 0;
	line->len = 0;
	memset(line->data, 0, TALK_MAXCOL);
	move(twin->curln, twin->curcol);
	clrtoeol();
	return;

    case Ctrl('M'):
    case Ctrl('J'):
	strlcpy(buf, (char *)line->data, line->len + 1);
	buf[line->len] = 0;
	do_talk_nextline(twin);
	break;

    case Ctrl('B'):
	if (twin->curcol > 0) {
	    --(twin->curcol);
#ifdef DBCSAWARE
	    if(twin->curcol > 0 && twin->curcol < line->len && ISDBCSAWARE())
	    {
		if(DBCS_Status((char*)line->data, twin->curcol) == DBCS_TRAILING)
		    twin->curcol --;
	    }
#endif
	    move(twin->curln, twin->curcol);
	}
	return;

    case Ctrl('F'):
	if (twin->curcol < line->len) {
	    ++(twin->curcol);
#ifdef DBCSAWARE
	    if(twin->curcol < TALK_MAXCOL && twin->curcol < line->len && ISDBCSAWARE())
	    {
		if(DBCS_Status((char*)line->data, twin->curcol) == DBCS_TRAILING)
		    twin->curcol++;
	    }
#endif
	    move(twin->curln, twin->curcol);
	}
	return;


    case Ctrl('P'):
	strlcpy(buf, (char *)line->data, line->len + 1);
	if (twin->curln > twin->sline) {
	    --twin->curln;
	    line = twin->big_picture + (twin->curln -twin->sline);
	}
	if (twin->curcol > line->len)
	    twin->curcol = line->len;

#ifdef DBCSAWARE
	// curln may be changed.
	if(twin->curcol > 0 && twin->curcol < line->len &&
		DBCS_Status((char*)line->data, twin->curcol) == DBCS_TRAILING)
	    twin->curcol--;
#endif
	move(twin->curln, twin->curcol);
	// XXX break here (for log)?
	break;

    case Ctrl('N'):
	strlcpy(buf, (char *)line->data, line->len + 1);
	if (twin->curln < twin->eline) {
	    ++twin->curln;
	    line = twin->big_picture + (twin->curln -twin->sline);
	}
	if (twin->curcol > line->len)
	    twin->curcol = line->len;

#ifdef DBCSAWARE
	// curln may be changed.
	line = twin->big_picture + (twin->curln -twin->sline); 
	if(twin->curcol > 0 && twin->curcol < line->len &&
		DBCS_Status((char*)line->data, twin->curcol) == DBCS_TRAILING)
	    twin->curcol--;
#endif
	move(twin->curln, twin->curcol);
	// XXX break here (for log)?
	break;

	// complex data change
    case Ctrl('H'):
    case KEY_BS2:
	if (twin->curcol > 0)
	{
	    int delta = 1;

#ifdef DBCSAWARE
	    if (twin->curcol > 1 && ISDBCSAWARE() && 
		    DBCS_Status((char*)line->data, twin->curcol-1) == DBCS_TRAILING)
		delta++;
#endif
	    memmove(line->data + twin->curcol-delta, line->data + twin->curcol,
		    line->len - twin->curcol +1);
	    line->len    -= delta;
	    twin->curcol -= delta;
	    // dirty screen
	    talk_refreshline(twin);
	}
	return;

    case Ctrl('D'):
	if (twin->curcol < line->len) 
	{
	    int delta = 1;

#ifdef DBCSAWARE
	    if (ISDBCSAWARE() && 
		    DBCS_Status((char*)line->data, twin->curcol) == DBCS_LEADING)
		delta++;
#endif
	    memmove(line->data + twin->curcol, line->data + twin->curcol+delta,
		    line->len - twin->curcol -delta+1);
	    line->len -= delta;
	    memset(line->data + line->len, 0, TALK_MAXCOL - line->len);
	    // dirty screen
	    talk_refreshline(twin);
	}
	return;
    }

    trim(buf);
    if (*buf)
	fprintf(flog, "%s%s: %s%s\n",
		(twin->eline == b_lines - 1) ? ANSI_COLOR(1;35) : "",
		(twin->eline == b_lines - 1) ?
		getuserid(currutmp->destuid) : cuser.userid, buf,
		(ch == Ctrl('P')) ? ANSI_COLOR(37;45) "(Up)" ANSI_RESET : ANSI_RESET);
}

static void
do_talk(int fd)
{
    struct talkwin_t mywin, itswin;
    char            mid_line[128], data[200];
    int             i, datac, ch;
    int             im_leaving = 0;
    FILE           *log, *flog;
    struct tm      *ptime;
    char            genbuf[200], fpath[100];

    STATINC(STAT_DOTALK);
    ptime = localtime4(&now);

    setuserfile(fpath, "talk_XXXXXX");
    flog = fdopen(mkstemp(fpath), "w");
    assert(flog);

    setuserfile(genbuf, fn_talklog);

    if ((log = fopen(genbuf, "w")))
	fprintf(log, "[%d/%d %d:%02d] & %s\n",
		ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_hour,
		ptime->tm_min, save_page_requestor);
    setutmpmode(TALK);

    ch = 58 - strlen(save_page_requestor);
    snprintf(genbuf, sizeof(genbuf), "%s【%s", cuser.userid, cuser.nickname);
    i = ch - strlen(genbuf);
    if (i >= 0)
	i = (i >> 1) + 1;
    else {
	genbuf[ch] = '\0';
	i = 1;
    }
    memset(data, ' ', i);
    data[i] = '\0';

    snprintf(mid_line, sizeof(mid_line),
	     ANSI_COLOR(1;46;37) "  談天說地  " ANSI_COLOR(45) "%s%s】"
	     " 與  %s%s" ANSI_COLOR(0) "", 
	     data, genbuf, save_page_requestor, data);

    memset(&mywin, 0, sizeof(mywin));
    memset(&itswin, 0, sizeof(itswin));

    i = b_lines >> 1;
    mywin.eline = i - 1;
    itswin.curln = itswin.sline = i + 1;
    itswin.eline = b_lines - 1;

    // memory allocation
    mywin.big_picture  = (twpic_t*) malloc (
	    sizeof(twpic_t) * (mywin.eline - mywin.sline +1));
    itswin.big_picture = (twpic_t*) malloc (
	    sizeof(twpic_t) * (itswin.eline- itswin.sline +1));

    // reset buffer
    for (i = mywin.sline; i <= mywin.eline; i++)
    {
	mywin.big_picture[i - mywin.sline].len = 0;
	memset(mywin.big_picture[i-mywin.sline].data, 0, TALK_BUFLEN);
    }
    for (i = itswin.sline; i <= itswin.eline; i++)
    {
	itswin.big_picture[i - itswin.sline].len = 0;
	memset(itswin.big_picture[i-itswin.sline].data, 0, TALK_BUFLEN);
    }

    clear();
    move(mywin.eline+1, 0);
    outs(mid_line);
    move(0, 0);

    add_io(fd, 0);

    while (1) {
	ch = igetch();
	if (ch == I_OTHERDATA) {
	    datac = recv(fd, data, sizeof(data), 0);
	    if (datac <= 0)
		break;
	    for (i = 0; i < datac; i++)
		do_talk_char(&itswin, data[i], flog);
	} else if (ch == KEY_UNKNOWN) {
	  // skip
	} else {
	    if (ch == Ctrl('C')) {
		if (im_leaving)
		    break;
		move(b_lines, 0);
		clrtoeol();
		outs("再按一次 Ctrl-C 就正式中止談話囉！");
		im_leaving = 1;
		continue;
	    }
	    if (im_leaving) {
		move(b_lines, 0);
		clrtoeol();
		im_leaving = 0;
	    }
	    switch (ch) {
	    case KEY_LEFT:	/* 把2byte的鍵改為一byte */
		ch = Ctrl('B');
		break;
	    case KEY_RIGHT:
		ch = Ctrl('F');
		break;
	    case KEY_UP:
		ch = Ctrl('P');
		break;
	    case KEY_DOWN:
		ch = Ctrl('N');
		break;
	    }
	    data[0] = (char)ch;
	    if (send(fd, data, 1, 0) != 1)
		break;
	    if (log)
		fputc((ch == Ctrl('M')) ? '\n' : (char)*data, log);
	    do_talk_char(&mywin, *data, flog);
	}
    }
    if (log)
	fclose(log);

    add_io(0, 0);
    close(fd);

    if (flog) {
	char            ans[4];

	fprintf(flog, "\n" ANSI_COLOR(33;44) "離別畫面 [%s] ...     " ANSI_RESET "\n",
		Cdatelite(&now));

	fprintf(flog, "[%s]:\n", cuser.userid);
	for (i = 0; i < mywin.eline - mywin.sline +1; i++)
	    if (mywin.big_picture[i].len)
		fprintf(flog, "%.*s\n", mywin.big_picture[i].len, mywin.big_picture[i].data);

	fprintf(flog, "[%s]:\n", getuserid(currutmp->destuid));
	for (i = 0; i < itswin.eline - itswin.sline +1; i++)
	    if (itswin.big_picture[i].len)
		fprintf(flog, "%.*s\n", itswin.big_picture[i].len, itswin.big_picture[i].data);

	fclose(flog);
	redrawwin();
	more(fpath, NA);

	// force user decide how to deal with the log
	while (1) {
	    getdata(b_lines - 1, 0, "清除(C) 移至備忘錄(M). (c/m)? ",
		    ans, sizeof(ans), LCECHO);
	    if (ans[0] == 'c' || ans[0] == 'm')
		break;
	    move(b_lines-2, 0);
	    prints(ANSI_COLOR(0;1;3%d) "請正確輸入 c 或 m 的指令。" ANSI_RESET,
		    (int)((now % 7)+1));
	    if (ans[0] == 0) outs("為避免誤按所以只 ENTER 是不行的。");
	}

	if (*ans == 'm') {
	    char title[STRLEN];
	    const char *tuid = NULL;
	    if (currutmp) tuid = getuserid(currutmp->destuid);
	    snprintf(title, sizeof(title), "對話記錄 (%s)", tuid ? tuid : "未知");
	    if (mail_log2id(cuser.userid, title, fpath, "[備.忘.錄]", 0, 1) < 0)
		vmsg("儲存失敗。");
	}
	unlink(fpath);
	flog = 0;
    }

    // free memory
    free(mywin.big_picture);
    free(itswin.big_picture);
    setutmpmode(XINFO);
    redrawwin();
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
    setutmpmode(PAGE);
    uin->destuip = currutmp - &SHM->uinfo[0];
    pid = uin->pid;
    if (pid > 0)
	kill(pid, SIGUSR1);
    clear();
    prints("正呼叫 %s.....\n鍵入 Ctrl-D 中止....", uin->userid);

    if(listen(sock, 1)<0) {
	close(sock);
	return -1;
    }
    add_io(sock, timeout);

    while (1) {
	ch = igetch();
	if (ch == I_TIMEOUT) {
	    ch = uin->mode;
	    if (!ch && uin->chatid[0] == 1 &&
		    uin->destuip == currutmp - &SHM->uinfo[0]) {
		bell();
		outmsg("對方回應中...");
		refresh();
	    } else if (ch == EDITING || ch == TALK || ch == CHATING ||
		    ch == PAGE || ch == MAILALL || ch == MONITOR ||
		    ch == M_FIVE || ch == CHC ||
		    (!ch && (uin->chatid[0] == 1 ||
			     uin->chatid[0] == 3))) {
		add_io(0, 0);
		close(sock);
		currutmp->sockactive = currutmp->destuid = 0;
		vmsg("人家在忙啦");
		unlockutmpmode();
		return -1;
	    } else {
		// change to longer timeout
		add_io(sock, 20);
		move(0, 0);
		outs("再");
		bell();

		uin->destuip = currutmp - &SHM->uinfo[0];
		if (pid <= 0 || kill(pid, SIGUSR1) == -1) {
		    close(sock);
		    currutmp->sockactive = currutmp->destuid = 0;
		    add_io(0, 0);
		    vmsg(msg_usr_left);
		    unlockutmpmode();
		    return -1;
		}
		continue;
	    }
	}
	if (ch == I_OTHERDATA)
	    break;

	if (ch == '\004') {
	    add_io(0, 0);
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
    strlcpy(currauthor, uin->userid, sizeof(currauthor));

    if (ch == EDITING || ch == TALK || ch == CHATING || ch == PAGE ||
	ch == MAILALL || ch == MONITOR || ch == M_FIVE || ch == CHC ||
	ch == DARK || ch == UMODE_GO || ch == CHESSWATCHING || ch == REVERSI ||
	(!ch && (uin->chatid[0] == 1 || uin->chatid[0] == 3)) ||
	uin->lockmode == M_FIVE || uin->lockmode == CHC) {
	if (ch == CHC || ch == M_FIVE || ch == UMODE_GO ||
		ch == CHESSWATCHING || ch == REVERSI) {
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
		strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));

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
	    outs("要和他(她) (T)談天(F)下五子棋"
#ifdef USE_CHICKEN_PK
		    "(P)鬥寵物"
#endif // USE_CHICKEN_PK
		    "(C)下象棋(D)下暗棋(G)下圍棋(R)下黑白棋");
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
	case 'd':
	    uin->sig = SIG_DARK;
	    break;
	case 'g':
	    uin->sig = SIG_GO;
	    break;
	case 'r':
	    uin->sig = SIG_REVERSI;
	    break;
#ifdef USE_CHICKEN_PK
	case 'p':
	    {
		userec_t xuser;
		chicken_t xchk;
		int error = 0;

		getuser(uin->userid, &xuser);
		if (uin->lockmode == CHICKEN || currutmp->lockmode == CHICKEN)
		    error = 1;
		else if (!load_chicken(cuser.userid, &xchk) ||
			 !load_chicken(xuser.userid, &xchk))
		    error = 2;

		if (error) {
		    vmsg(error == 2 ? "並非兩人都養寵物" :
			    "有一方的寵物正在使用中");
		    return;
		}
		uin->sig = SIG_PK;
	    }
	    break;
#endif // USE_CHICKEN_PK
	default:
	    return;
	}

	uin->turn = 1;
	currutmp->turn = 0;
	strlcpy(uin->mateid, currutmp->userid, sizeof(uin->mateid));
	strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));

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
	add_io(0, 0);
	close(sock);
	currutmp->sockactive = NA;

	if (uin->sig == SIG_CHC || uin->sig == SIG_GOMO ||
		uin->sig == SIG_GO || uin->sig == SIG_REVERSI)
	    ChessEstablishRequest(msgsock);

	add_io(msgsock, 0);
	while ((ch = igetch()) != I_OTHERDATA) {
	    if (ch == Ctrl('D')) {
		add_io(0, 0);
		close(msgsock);
		unlockutmpmode();
		return;
	    }
	}

	if (read(msgsock, &c, sizeof(c)) != sizeof(c))
	    c = 'n';
	add_io(0, 0);

	if (c == 'y') {
	    snprintf(save_page_requestor, sizeof(save_page_requestor),
		     "%s (%s)", uin->userid, uin->nickname);
	    switch (uin->sig) {
	    case SIG_DARK:
		main_dark(msgsock, uin);
		break;
#ifdef USE_CHICKEN_PK
	    case SIG_PK:
		chickenpk(msgsock);
		break;
#endif
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
	    case SIG_TALK:
	    default:
		do_talk(msgsock);
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
		prints("%s？先拿100銀兩來..", sig_des[uin->sig]);
		break;
	    case '2':
		prints("%s？先拿1000銀兩來..", sig_des[uin->sig]);
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

static void
t_showhelp(void)
{
    clear();

    outs(ANSI_COLOR(36) "【 休閒聊天使用說明 】" ANSI_RESET "\n\n"
	 "(←)(e)         結束離開             (h)             看使用說明\n"
	 "(↑)/(↓)(n)    上下移動             (TAB)           切換排序方式\n"
	 "(PgUp)(^B)      上頁選單             ( )(PgDn)(^F)   下頁選單\n"
	 "(Hm)/($)(Ed)    首/尾                (S)             來源/好友描述/戰績 切換\n"
	 "(m)             寄信                 (q/c)           查詢網友/寵物\n"
	 "(r)             閱\讀信件             (l/C)           看上次熱訊/切換隱身\n"
	 "(f)             全部/好友列表        (數字)          跳至該使用者\n"
	 "(p)             切換呼叫器           (g/i)           給錢/切換心情\n"
	 "(a/d/o)         好友 增加/刪除/修改  (s)             網友 ID 搜尋\n"
	 "(N)             修改暱稱             (y)             我想找人聊天、下棋…\n");

    if (HasUserPerm(PERM_PAGE)) {
	outs("\n" ANSI_COLOR(36) "【 交談專用鍵 】" ANSI_RESET "\n"
	     "(→)(t)(Enter)  跟他／她聊天\n"
	     "(w)             熱線 Call in\n"
	     "(^W)切換水球方式 一般 / 進階 / 未來\n"
	     "(b)             對好友廣播 (一定要在好友列表中)\n"
	     "(^R)            即時回應 (有人 Call in 你時)\n");
    }
    if (HasUserPerm(PERM_SYSOP)) {
	outs("\n" ANSI_COLOR(36) "【 站長專用鍵 】" ANSI_RESET "\n\n");
	outs("(u)/(H)         設定使用者資料/切換隱形模式\n");
	outs("(K)             把壞蛋踢出去\n");
#if defined(SHOWBOARD) && defined(DEBUG)
	outs("(Y)             顯示正在看什麼板\n");
#endif
    }
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
	snprintf(name, sizeof(name), "%s ", uentp->userid);
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
descript(int show_mode, const userinfo_t * uentp, int diff)
{
    static char     description[30];
    switch (show_mode) {
    case 1:
	return friend_descript(uentp, description, sizeof(description));
    case 0:
	return (((uentp->pager != PAGER_DISABLE && uentp->pager != PAGER_ANTIWB && diff) ||
		 HasUserPerm(PERM_SYSOP)) ?
#ifdef WHERE
		uentp->from_alias ? SHM->home_desc[uentp->from_alias] :
		uentp->from
#else
		uentp->from
#endif
		: "*");
    case 2:
	snprintf(description, sizeof(description),
		 "%4d/%4d/%2d %c", uentp->five_win,
		 uentp->five_lose, uentp->five_tie,
		 (uentp->withme&WITHME_FIVE)?'o':(uentp->withme&WITHME_NOFIVE)?'x':' ');
	return description;
    case 3:
	snprintf(description, sizeof(description),
		 "%4d/%4d/%2d %c", uentp->chc_win,
		 uentp->chc_lose, uentp->chc_tie,
		 (uentp->withme&WITHME_CHESS)?'o':(uentp->withme&WITHME_NOCHESS)?'x':' ');
	return description;
    case 4:
	snprintf(description, sizeof(description),
		 "%4d %s", uentp->chess_elo_rating, 
		 (uentp->withme&WITHME_CHESS)?"找我下棋":(uentp->withme&WITHME_NOCHESS)?"別找我":"");
	return description;
    case 5:
	snprintf(description, sizeof(description),
		 "%4d/%4d/%2d %c", uentp->go_win,
		 uentp->go_lose, uentp->go_tie,
		 (uentp->withme&WITHME_GO)?'o':(uentp->withme&WITHME_NOGO)?'x':' ');
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
    if (cuser.uflag & FRIEND_FLAG)
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
    for (i = 0; currutmp->friend_online[i] && i < MAX_FRIEND; ++i) {
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
    if( (cuser.uflag & FRIEND_FLAG) || /* 只顯示好友模式 */
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

    if (!(cuser.uflag & FRIEND_FLAG) && size < nPickups) {
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
    {NULL, 8, VCOL_MAXW, -1, {0, 1}},	// "   編號"
    {NULL, 2, 2, 0, {0, 0, 1}}, // "P" (pager, no border)
    {NULL, IDLEN+1, IDLEN+3}, // "代號"
    {NULL, 17,25, 2}, // "暱稱", sizeof(userec_t::nickname)
    {NULL, 17,27, 1}, // "故鄉/棋類戰績/等級分"
    {NULL, 12,23, 1}, // "動態" (最大多少才合理？) modestring size=40 但...
    {NULL, 4, 4, 0, {0, 0, 1}}, // "心情"
    {NULL, 6, 6, -1, {0, 1, 1}}, // "發呆" (optional?)
    {NULL, 0, VCOL_MAXW, -1}, // for middle alignment
};

static void
draw_pickup(int drawall, pickup_t * pickup, int pickup_way,
	    int page, int show_mode, int show_uid, int show_board,
	    int show_pid, int myfriend, int friendme, int bfriend, int badfriend)
{
    char           *msg_pickup_way[PICKUP_WAYS] = {
	"嗨! 朋友", "網友代號", "網友動態", "發呆時間", "來自何方", " 五子棋 ", "  象棋  ", "  圍棋  ",
    };
    char           *MODE_STRING[MAX_SHOW_MODE] = {
	"故鄉", "好友描述", "五子棋戰績", "象棋戰績", "象棋等級分", "圍棋戰績",
    };
    char            pagerchar[6] = "* -Wf";

    userinfo_t     *uentp;
    int             i, ch, state, friend;

    // print buffer
    char pager[3];
    char num[10];
    char xuid[IDLEN+1+20]; // must carry IDLEN + ANSI code.
    char xmind[5+10] = ANSI_COLOR(33);
    char *mind = xmind+strlen(xmind);

#ifdef SHOW_IDLE_TIME
    char            idlestr[32];
    int             idletime;
#endif

    static int scrw = 0, scrh = 0;
    static VCOLW cols[ULISTCOLS];

    // re-layout if required.
    if (scrw != t_columns || scrh != t_lines)
    {
	vs_cols_layout(ulist_coldef, cols, ULISTCOLS);
	scrw = t_columns; scrh = t_lines;
    }

    if (drawall) {
	showtitle((cuser.uflag & FRIEND_FLAG) ? "好友列表" : "休閒聊天",
		  BBSName);

	move(2, 0);
	outs(ANSI_COLOR(7));
	vs_cols(ulist_coldef, cols, ULISTCOLS,
		show_uid ? "UID" : "編號",
		"P", "代號", "暱稱",
		MODE_STRING[show_mode],
		show_board ? "看板" : "動態",
		// because this field has ANSI...
		show_pid ? "PID" : "心情",
#ifdef SHOW_IDLE_TIME
		"發呆",
#else
		"    ",
#endif
		"");
	outs(ANSI_RESET);

#ifdef PLAY_ANGEL
	if (HasUserPerm(PERM_ANGEL) && currutmp)
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
#endif
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

#ifdef SHOW_IDLE_TIME
	idletime = (now - uentp->lastact);
	if (idletime > 86400)
	    strlcpy(idlestr, " -----", sizeof(idlestr));
	else if (idletime >= 3600)
	    snprintf(idlestr, sizeof(idlestr), "%dh%02d",
		     idletime / 3600, (idletime / 60) % 60);
	else if (idletime > 0)
	    snprintf(idlestr, sizeof(idlestr), "%d'%02d",
		     idletime / 60, idletime % 60);
	else
	    idlestr[0] = 0;
#endif

	if ((uentp->userlevel & PERM_VIOLATELAW))
	    memcpy(mind, "通緝", 4);
	else if (uentp->birth)
	    memcpy(mind, "壽星", 4);
	else
	    memcpy(mind, uentp->mind, 4);
	mind[4] = 0;

	snprintf(num, sizeof(num), "%d",
#ifdef SHOWUID
		show_uid ? uentp->uid :
#endif
		ch);

	pager[0] = (friend & HRM) ? 'X' : pagerchar[uentp->pager % 5];
	pager[1] = (uentp->invisible ? ')' : ' ');
	pager[2] = 0;

	/* color of userid, userid */
	if(fcolor[state])
	    snprintf(xuid, sizeof(xuid), "%s%s",
		    fcolor[state], uentp->userid);

	vs_cols(ulist_coldef, cols, ULISTCOLS,
		num, pager, 
		fcolor[state] ? xuid : uentp->userid, 
		uentp->nickname,
		descript(show_mode, uentp,
		    uentp->pager & !(friend & HRM)),
#if defined(SHOWBOARD) && defined(DEBUG)
		show_board ? (uentp->brc_id == 0 ? "" :
		    getbcache(uentp->brc_id)->brdname) :
#endif
		modestring(uentp, 0),
		*mind ? xmind : "",
#ifdef SHOW_IDLE_TIME
		idlestr
#else
		""
#endif
	       );
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

int
call_in(const userinfo_t * uentp, int fri_stat)
{
    if (iswritable_stat(uentp, fri_stat)) {
	char            genbuf[60];
	snprintf(genbuf, sizeof(genbuf), "丟 %s 水球: ", uentp->userid);
	my_write(uentp->pid, genbuf, uentp->userid, WATERBALL_GENERAL, NULL);
	return 1;
    }
    return 0;
}

inline static void
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
#if !HAVE_FREECLOAK
		if (HasUserPerm(PERM_CLOAK))
#endif
		{
		    currutmp->invisible ^= 1;
		    redrawall = redraw = 1;
		}
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
		    } else if (now >= lastupdate + 2)
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
		if (!(cuser.uflag & FRIEND_FLAG)) {
		    int             si;	/* utmpshm->sorted[X][0][si] */
		    int             fi;	/* allpickuplist[fi] */
		    char            swid[IDLEN + 1];
		    move(1, 0);
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
		if (cuser.uflag & FRIEND_FLAG || HasUserPerm(PERM_SYSOP)) {
		    char            genbuf[60]="[廣播]";
		    char            ans[4];

		    if (!getdata(0, 0, "廣播訊息:", genbuf+6, 54, DOECHO))
			break;
                    
		    if (!getdata(0, 0, "確定廣播? [N]",
				ans, sizeof(ans), LCECHO) ||
			ans[0] != 'y')
			break;
		    if (!(cuser.uflag & FRIEND_FLAG) && HasUserPerm(PERM_SYSOP)) {
			msgque_t msg;
			getdata(1, 0, "再次確定站長廣播? [N]",
				ans, sizeof(ans), LCECHO);
			if( ans[0] != 'y' && ans[0] != 'Y' ){
			    vmsg("abort");
			    break;
			}

			msg.pid = currpid;
			strlcpy(msg.userid, cuser.userid, sizeof(msg.userid));
			snprintf(msg.last_call_in, sizeof(msg.last_call_in),
				 "[廣播]%s", genbuf);
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
#ifdef NOKILLWATERBALL
				    uentp->wbtime = now;
#else
				    kill(uentp->pid, SIGUSR2);
#endif
				}
			    }
			}
		    } else {
			userinfo_t     *uentp;
			int             where, frstate;
			for (i = 0; currutmp->friend_online[i] &&
			     i < MAX_FRIEND; ++i) {
			    where = currutmp->friend_online[i] & 0xFFFFFF;
			    if (VALID_USHM_ENTRY(where) &&
				(uentp = &SHM->uinfo[where]) &&
				uentp->pid &&
				isvisible_stat(currutmp, uentp,
					       frstate =
					   currutmp->friend_online[i] >> 24)
				&& kill(uentp->pid, 0) != -1 &&
				uentp->pager != PAGER_ANTIWB &&
				(uentp->pager != PAGER_FRIENDONLY || frstate & HFM) &&
				!(frstate & IRH)) {
				my_write(uentp->pid, genbuf, uentp->userid,
					WATERBALL_PREEDIT, NULL);
			    }
			}
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'S':		/* 顯示好友描述 */
		show_mode = (show_mode+1) % MAX_SHOW_MODE;
#ifdef CHESSCOUNTRY
		if (show_mode == 2)
		    user_query_mode = 1;
		else if (show_mode == 3 || show_mode == 4)
		    user_query_mode = 2;
		else if (show_mode == 5)
		    user_query_mode = 3;
		else
		    user_query_mode = 0;
#endif /* defined(CHESSCOUNTRY) */
		redrawall = redraw = 1;
		break;

	    case 'u':		/* 線上修改資料 */
		if (HasUserPerm(PERM_ACCOUNTS|PERM_SYSOP)) {
		    int             id;
		    userec_t        muser;
		    strlcpy(currauthor, uentp->userid, sizeof(currauthor));
		    vs_hdr("使用者設定");
		    move(1, 0);
		    if ((id = getuser(uentp->userid, &muser)) > 0) {
			user_display(&muser, 1);
			if( HasUserPerm(PERM_ACCOUNTS) )
			    uinfo_query(&muser, 1, id);
			else
			    pressanykey();
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'i':{
		    char            mindbuf[5];
		    getdata(b_lines - 1, 0, "現在的心情? ",
			    mindbuf, sizeof(mindbuf), DOECHO);
		    if (strcmp(mindbuf, "通緝") == 0)
			vmsg("不可以把自己設通緝啦!");
		    else if (strcmp(mindbuf, "壽星") == 0)
			vmsg("你不是今天生日欸!");
		    else
			memcpy(currutmp->mind, mindbuf, 4);
		}
		redrawall = redraw = 1;
		break;

	    case Ctrl('S'):
		break;

	    case KEY_RIGHT:
	    case '\n':
	    case '\r':
	    case 't':
		if (HasUserPerm(PERM_LOGINOK)) {
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
		if (HasUserPerm(PERM_LOGINOK) && !(fri_stat & IFH)) {
		    if (vans("確定要加入好友嗎 [N/y]") == 'y') {
			friend_add(uentp->userid, FRIEND_OVERRIDE,uentp->nickname);
			friend_load(FRIEND_OVERRIDE);
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'd':
		if (HasUserPerm(PERM_LOGINOK) && (fri_stat & IFH)) {
		    if (vans("確定要刪除好友嗎 [N/y]") == 'y') {
			friend_delete(uentp->userid, FRIEND_OVERRIDE);
			friend_load(FRIEND_OVERRIDE);
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'o':
		if (HasUserPerm(PERM_LOGINOK)) {
		    t_override();
		    redrawall = redraw = 1;
		}
		break;

	    case 'f':
		if (HasUserPerm(PERM_LOGINOK)) {
		    cuser.uflag ^= FRIEND_FLAG;
		    redrawall = redraw = 1;
		}
		break;

	    case 'g':
		if (HasUserPerm(PERM_LOGINOK) &&
		    strcmp(uentp->userid, cuser.userid) != 0) {
		    give_money_ui(uentp->userid);
		    redrawall = redraw = 1;
		}
		break;

	    case 'm':
		if (HasUserPerm(PERM_LOGINOK)) {
		    char   userid[IDLEN + 1];
		    strlcpy(userid, uentp->userid, sizeof(userid));
		    vs_hdr("寄  信");
		    prints("[寄信] 收信人：%s", userid);
		    my_send(userid);
		    setutmpmode(LUSERS);
		    redrawall = redraw = 1;
		}
		break;

	    case 'q':
		strlcpy(currauthor, uentp->userid, sizeof(currauthor));
		my_query(uentp->userid);
		setutmpmode(LUSERS);
		redrawall = redraw = 1;
		break;

	    case 'c':
		if (HasUserPerm(PERM_LOGINOK)) {
		    chicken_query(uentp->userid);
		    redrawall = redraw = 1;
		}
		break;

	    case 'l':
		if (HasUserPerm(PERM_LOGINOK)) {
		    t_display();
		    redrawall = redraw = 1;
		}
		break;

	    case 'h':
		t_showhelp();
		redrawall = redraw = 1;
		break;

	    case 'p':
		if (HasUserPerm(PERM_BASIC)) {
		    t_pager();
		    redrawall = redraw = 1;
		}
		break;

#ifdef PLAY_ANGEL
	    case Ctrl('P'):
		if (HasUserPerm(PERM_ANGEL) && currutmp) {
		    /*
		    static const char *msgs[ANGELPAUSE_MODES] = {
			" 接受所有小主人發問 [●] ",
			" 只接受已發問的小主人的問題 [△] ",
			" 停止接受所有小主人的問題 [Ｘ] ",
		    };
		    */
		    angel_toggle_pause();
		    // vmsg(msgs[currutmp->angelpause]);
		    redrawall = redraw = 1;
		}
		break;
#endif // PLAY_ANGLE

	    case Ctrl('W'):
		if (HasUserPerm(PERM_LOGINOK)) {
		    int             tmp;
		    char           *wm[3] = {"一般", "進階", "未來"};
		    tmp = cuser.uflag2 & WATER_MASK;
		    cuser.uflag2 -= tmp;
		    tmp = (tmp + 1) % 3;
		    cuser.uflag2 |= tmp;
		    /* vmsg cannot support multi lines */
		    move(b_lines - 4, 0);
		    clrtobot();
		    move(b_lines - 3, 0);
		    outs("系統提供 一般 進階 未來 三種模式\n"
		    "在切換後請正常下線再重新登入, 以確保結構正確\n");
		    vmsgf( "目前切換到 %s 水球模式", wm[tmp]);
		    redrawall = redraw = 1;
		}
		break;

	    case 'r':
		if (HasUserPerm(PERM_LOGINOK)) {
		    if (curredit & EDIT_MAIL) {
			/* deny reentrance, which may cause many problems */
			vmsg("你進入使用者列表前就已經在閱\讀信件了");
		    } else {
			// XXX in fact we should check size here...
			// chkmailbox();
			m_read();
			setutmpmode(LUSERS);
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'N':
		if (HasUserPerm(PERM_LOGINOK)) {
		    char tmp_nick[sizeof(cuser.nickname)];
		    if (getdata_str(1, 0, "新的暱稱: ",
				tmp_nick, sizeof(tmp_nick), DOECHO, cuser.nickname) > 0)
		    {
			strlcpy(cuser.nickname, tmp_nick, sizeof(cuser.nickname));
			strlcpy(currutmp->nickname, cuser.nickname, sizeof(currutmp->nickname));
		    }
		    redrawall = redraw = 1;
		}
		break;

	    case 'y':
		set_withme_flag();
		redrawall = redraw = 1;
		break;

	    default:
		if (now >= lastupdate + 2)
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

    assert(strncmp(cuser.userid, currutmp->userid, IDLEN)==0);
    if( strncmp(cuser.userid , currutmp->userid, IDLEN) != 0 ){
	abort_bbs(0);
    }

    setutmpmode(LUSERS);
    userlist();
    currutmp->mode = mode0;
    currutmp->destuid = destuid0;
    currstat = stat0;
    return 0;
}

int
t_pager(void)
{
    currutmp->pager = (currutmp->pager + 1) % PAGER_MODES;
    return 0;
}

int
t_idle(void)
{
    int             destuid0 = currutmp->destuid;
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;
    char            genbuf[20];
    char            passbuf[PASSLEN];
    int             idle_type;
    char            idle_reason[sizeof(currutmp->chatid)];


    setutmpmode(IDLE);
    getdata(b_lines - 1, 0, "理由：[0]發呆 (1)接電話 (2)覓食 (3)打瞌睡 "
	    "(4)裝死 (5)羅丹 (6)其他 (Q)沒事？", genbuf, 3, DOECHO);
    if (genbuf[0] == 'q' || genbuf[0] == 'Q') {
	currutmp->mode = mode0;
	currstat = stat0;
	return 0;
    } else if (genbuf[0] >= '1' && genbuf[0] <= '6')
	idle_type = genbuf[0] - '0';
    else
	idle_type = 0;

    if (idle_type == 6) {
	if (cuser.userlevel && getdata(b_lines - 1, 0, "發呆的理由：", idle_reason, sizeof(idle_reason), DOECHO)) {
	    strlcpy(currutmp->chatid, idle_reason, sizeof(currutmp->chatid));
	} else {
	    idle_type = 0;
	}
    }
    currutmp->destuid = idle_type;
    do {
	move(b_lines - 2, 0);
	clrtobot();
	prints("(鎖定螢幕)發呆原因: %s", (idle_type != 6) ?
		 IdleTypeTable[idle_type] : idle_reason);
	refresh();
	getdata(b_lines - 1, 0, MSG_PASSWD, passbuf, sizeof(passbuf), NOECHO);
	passbuf[8] = '\0';
    }
    while (!checkpasswd(cuser.passwd, passbuf) &&
	   strcmp(STR_GUEST, cuser.userid));

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
	getdata(1, 33, "請選擇一個聊天對象 [0]：", genbuf, 4, DOECHO);
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

int
reply_connection_request(const userinfo_t *uip)
{
    char            buf[4], genbuf[200];

    if (uip->mode != PAGE) {
	snprintf(genbuf, sizeof(genbuf),
		 "%s已停止呼叫，按Enter繼續...", page_requestor);
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
    strlcpy(save_page_requestor, page_requestor, sizeof(save_page_requestor));
    memset(page_requestor, 0, sizeof(page_requestor));
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

    is_chess = (sig == SIG_CHC || sig == SIG_GOMO || sig == SIG_GO || sig == SIG_REVERSI);

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
    assert(sig>=0 && sig<sizeof(sig_des)/sizeof(sig_des[0]));
    prints("       (Y) 讓我們 %s 吧！"
	    "     (A) 我現在很忙，請等一會兒再 call 我\n", sig_des[sig]);
    prints("       (N) 我現在不想 %s"
	    "      (B) 對不起，我有事情不能跟你 %s\n",
	    sig_des[sig], sig_des[sig]);
    prints("       (C) 請不要吵我好嗎？"
	    "       (D) 我要離站囉..下次再聊吧.......\n");
    prints("       (E) 有事嗎？請先來信"
	    "       (F) " ANSI_COLOR(1;33) "我自己輸入理由好了..." ANSI_RESET "\n");
    prints("       (1) %s？先拿100銀兩來"
	    "  (2) %s？先拿1000銀兩來..\n\n", sig_des[sig], sig_des[sig]);

    snprintf(page_requestor, sizeof(page_requestor),
	    "%s (%s)", uip->userid, uip->nickname);
    getuser(uip->userid, &xuser);
    currutmp->msgs[0].pid = uip->pid;
    strlcpy(currutmp->msgs[0].userid, uip->userid, sizeof(currutmp->msgs[0].userid));
    strlcpy(currutmp->msgs[0].last_call_in, "呼叫、呼叫，聽到請回答 (Ctrl-R)",
	    sizeof(currutmp->msgs[0].last_call_in));
    currutmp->msgs[0].msgmode = MSGMODE_TALK;
    prints("對方來自 [%s]，共上站 %d 次，文章 %d 篇\n",
	    uip->from, xuser.numlogins, xuser.numposts);

    if (is_chess)
	ChessShowRequest();
    else {
	showplans(uip->userid);
	show_call_in(0, 0);
    }

    snprintf(genbuf, sizeof(genbuf),
	    "你想跟 %s %s啊？請選擇(Y/N/A/B/C/D/E/F/1/2)[N] ",
	    page_requestor, sig_des[sig]);
    getdata(0, 0, genbuf, buf, sizeof(buf), LCECHO);

    if (!buf[0] || !strchr("yabcdef12", buf[0]))
	buf[0] = 'n';

    sig_pipe_handle = Signal(SIGPIPE, SIG_IGN);
    r = write(a, buf, 1);
    if (buf[0] == 'f' || buf[0] == 'F') {
	if (!getdata(b_lines, 0, "不能的原因：", genbuf, 60, DOECHO))
	    strlcpy(genbuf, "不告訴你咧 !! ^o^", sizeof(genbuf));
	r = write(a, genbuf, 60);
    }
    Signal(SIGPIPE, sig_pipe_handle);

    if (r == -1) {
	snprintf(genbuf, sizeof(genbuf),
		 "%s已停止呼叫，按Enter繼續...", page_requestor);
	getdata(0, 0, genbuf, buf, sizeof(buf), LCECHO);
	clear();
	currstat = currstat0;
	return;
    }

    uip->destuip = currutmp - &SHM->uinfo[0];
    if (buf[0] == 'y')
	switch (sig) {
	case SIG_DARK:
	    main_dark(a, uip);
	    break;
#ifdef USE_CHICKEN_PK
	case SIG_PK:
	    chickenpk(a);
	    break;
#endif // USE_CHICKEN_PK
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
	case SIG_TALK:
	default:
	    do_talk(a);
	}
    else
	close(a);
    clear();
    currstat = currstat0;
}

