/* $Id$ */
#include "bbs.h"
#ifdef PLAY_ANGEL

// PTT-BBS Angel System

#include "daemons.h"
#define FN_ANGELMSG "angelmsg"

/////////////////////////////////////////////////////////////////////////////
// Angel Beats! Client

// this works for simple requests: (return 1/angel_uid for success, 0 for fail
static int 
angel_beats_do_request(int op, int master_uid, int angel_uid) {
    int fd;
    int ret = 1;
    angel_beats_data req = {0};

    req.cb = sizeof(req);
    req.operation = op;
    req.master_uid = master_uid;
    req.angel_uid = angel_uid;
    assert(op != ANGELBEATS_REQ_INVALID);

    if ((fd = toconnect(ANGELBEATS_ADDR)) < 0)
        return -1;

    assert(req.operation != ANGELBEATS_REQ_INVALID);

    if (towrite(fd, &req, sizeof(req)) < 0 ||
        toread (fd, &req, sizeof(req)) < 0 ||
        req.cb != sizeof(req)) {
        ret = -2;
    } else {
        ret = (req.angel_uid > 0) ? req.angel_uid : 1;
    }

    close(fd);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Local Angel Service

void 
angel_notify_activity() {
    static time4_t t = 0;
    time4_t tick = now;

    // tick: every 1 minutes.
    tick -= tick % (1 * 60);

    // ping daemon only in different ticks.
    if (tick == t)
        return;
    t = tick;
    angel_beats_do_request(ANGELBEATS_REQ_HEARTBEAT, 0, usernum);
}

void 
angel_toggle_pause()
{
    // TODO record angels that don't do their job
    if (!HasUserPerm(PERM_ANGEL) || !currutmp)
	return;
    currutmp->angelpause ++;
    currutmp->angelpause %= ANGELPAUSE_MODES;
}

void
angel_parse_nick_fp(FILE *fp, char *nick, int sznick)
{
    char buf[PATHLEN];
    // should be in first line
    rewind(fp);
    *buf = 0;
    if (fgets(buf, sizeof(buf), fp))
    {
	// verify first line
	if (buf[0] == '%' && buf[1] == '%' && buf[2] == '[')
	{
	    chomp(buf+3);
	    strlcpy(nick, buf+3, sznick);
	}
    }
}

void
angel_load_my_fullnick(char *buf, int szbuf)
{
    char fn[PATHLEN];
    FILE *fp = NULL;

    *buf = 0;
    setuserfile(fn, FN_ANGELMSG);
    if ((fp = fopen(fn, "rt")))
    {
	angel_parse_nick_fp(fp, buf, szbuf);
	fclose(fp);
    }
    strlcat(buf, "小天使", szbuf);
}

// cache my angel's nickname
static char _myangel[IDLEN+1] = "",
	    _myangel_nick[IDLEN+1] = "";
static time4_t _myangel_touched = 0;
static char _valid_angelmsg = 0;

void 
angel_reload_nick()
{
    char reload = 0;
    char fn[PATHLEN];
    time4_t ts = 0;
    FILE *fp = NULL;

    fn[0] = 0;
    // see if we have angel id change (reload whole)
    if (strcmp(_myangel, cuser.myangel) != 0)
    {
	strlcpy(_myangel, cuser.myangel, sizeof(_myangel));
	reload = 1;
    }
    // see if we need to check file touch date
    if (!reload && _myangel[0] && _myangel[0] != '-')
    {
	sethomefile(fn, _myangel, FN_ANGELMSG);
	ts = dasht(fn);
	if (ts != -1 && ts > _myangel_touched)
	    reload = 1;
    }
    // if no need to reload, reuse current data.
    if (!reload)
    {
	// vmsg("angel_data: no need to reload.");
	return;
    }

    // reset cache
    _myangel_touched = ts;
    _myangel_nick[0] = 0;
    _valid_angelmsg = 0;

    // quick check
    if (_myangel[0] == '-' || !_myangel[0])
	return;

    // do reload data.
    if (!fn[0])
    {
	sethomefile(fn, _myangel, FN_ANGELMSG);
	ts = dasht(fn);
	_myangel_touched = ts;
    }

    assert(*fn);
    // complex load
    fp = fopen(fn, "rt");
    if (fp)
    {
	_valid_angelmsg = 1;
	angel_parse_nick_fp(fp, _myangel_nick, sizeof(_myangel_nick));
	fclose(fp);
    }
}

const char * 
angel_get_nick()
{
    angel_reload_nick();
    return _myangel_nick;
}

static int
do_changeangel(int force) {
    char buf[4];
    static time_t last_time = 0;
    const char *prompt = "登記完成，下次呼叫時會從上線的天使中選出新的小天使";

    /* cuser.myangel == "-" means banned for calling angel */
    if (cuser.myangel[0] == '-')
        return 0;

    if (!cuser.myangel[0]) {
        vmsg(prompt);
        return 0;
    }

    // TODO Allow changing only if user really tried to contact angel.
#ifdef ANGEL_CHANGE_TIMELIMIT_MINS
    if (force)
        last_time = 0;

    if (last_time &&
        (now - last_time < ANGEL_CHANGE_TIMELIMIT_MINS * 60)) {
        vmsgf("每次更換小天使最少間隔 %d 分鐘。",
              ANGEL_CHANGE_TIMELIMIT_MINS);
        return 0;
    }

#endif

    mvouts(b_lines - 3, 0, "\n"
           "請注意若未呼叫過目前的小天使就申請更換，很可能會再換到同個天使\n");
    getdata(b_lines - 1, 0,
	    "更換小天使後就無法換回了喔！ 是否要更換小天使？ [y/N]",
	    buf, 3, LCECHO);
    if (buf[0] == 'y') {
	log_filef(BBSHOME "/log/changeangel.log",LOG_CREAT,
                  "%s 小主人 %s 換掉 %s 小天使\n",
                  Cdatelite(&now), cuser.userid, cuser.myangel);
        if (cuser.myangel[0])
            angel_beats_do_request(ANGELBEATS_REQ_REMOVE_LINK,
                    usernum, searchuser(cuser.myangel, NULL));
	pwcuSetMyAngel("");
        last_time = now;
        vmsg(prompt);
    }
    return 0;
}

int a_changeangel(void) {
    return do_changeangel(0);
}

int 
a_angelmsg(){
    char msg[3][75] = { "", "", "" };
    char nick[10] = "";
    char buf[512];
    int i;
    FILE* fp;

    move(1, 0); clrtobot();
    setuserfile(buf, "angelmsg");
    fp = fopen(buf, "r");
    if (fp) {
	i = 0;
	if (fgets(msg[0], sizeof(msg[0]), fp)) {
	    chomp(msg[0]);
	    if (strncmp(msg[0], "%%[", 3) == 0) {
		strlcpy(nick, msg[0] + 3, 7);
		move(4, 0);
		prints("原有暱稱：%s小天使", nick);
		msg[0][0] = 0;
	    } else
		i = 1;
	} else
	    msg[0][0] = 0;

	move(5, 0);
	outs("原有留言：\n");
	if(msg[0][0])
	    outs(msg[0]);
	for (; i < 3; ++i) {
	    if(fgets(msg[i], sizeof(msg[0]), fp)) {
		outs(msg[i]);
		chomp(msg[i]);
	    } else
		break;
	}
	fclose(fp);
    }

    getdata_buf(11, 0, "小天使暱稱：", nick, 7, 1);
    do {
	move(12, 0);
	clrtobot();
	outs("不在的時候要跟小主人說什麼呢？"
	     "最多三行，按[Enter]結束");
        // the -1 is for newline and we use to fgets() to read later
	for (i = 0; i < 3 &&
		getdata_buf(14 + i, 0, "：", msg[i], sizeof(msg[i])-1, DOECHO);
		++i);
	getdata(b_lines - 2, 0, "(S)儲存 (E)重新來過 (Q)取消？[S]",
		buf, 4, LCECHO);
    } while (buf[0] == 'e');
    if (buf[0] == 'q')
	return 0;
    setuserfile(buf, "angelmsg");
    if (msg[0][0] == 0)
	unlink(buf);
    else {
	FILE* fp = fopen(buf, "w");
	if (!fp)
	    return 0;
	if(nick[0])
	    fprintf(fp, "%%%%[%s\n", nick);
	for (i = 0; i < 3 && msg[i][0]; ++i) {
	    fputs(msg[i], fp);
	    fputc('\n', fp);
	}
	fclose(fp);
    }
    return 0;
}

int a_angelreport() {
    angel_beats_report rpt = {0};
    angel_beats_data   req = {0};
    int fd;

    vs_hdr2(" Angel Beats! 天使公會 ", " 天使狀態報告 ");
    outs("\n");

    if ((fd = toconnect(ANGELBEATS_ADDR)) < 0) {
        outs("抱歉，無法連線至天使公會。\n"
             "請至 " BN_BUGREPORT " 看板通知站方管理人員。\n");
        pressanykey();
        return 0;
    }

    req.cb = sizeof(req);
    req.operation = ANGELBEATS_REQ_REPORT;
    req.master_uid = usernum;

    if (HasUserPerm(PERM_ANGEL))
        req.angel_uid = usernum;

    if (towrite(fd, &req, sizeof(req)) < 0 ||
        toread(fd, &rpt, sizeof(rpt)) < 0 ||
        rpt.cb != sizeof(rpt)) {
        outs("抱歉，天使公會連線異常。\n"
                "請至 " BN_BUGREPORT " 看板通知站方管理人員。\n");
    } else {
        prints(
            "\t 現在時間: %s\n\n"
            "\n\t 系統內已登記的天使為 %d 位。\n"
            "\n\t 目前有 %d 位天使在線上，其中 %d 位神諭呼叫器為設定開放；\n",
            Cdatelite(&now),
            rpt.total_angels,
            rpt.total_online_angels,
            rpt.total_active_angels);

        if (HasUserPerm(PERM_SYSOP)) {
            prints(
                "\n\t 上線天使中，擁有小主人數目最少為 %d 位，最多為 %d 位\n"
                "\n\t 上線且開放收主人的天使中，主人最少 %d 位，最多 %d 位\n",
                rpt.min_masters_of_online_angels,
                rpt.max_masters_of_online_angels,
                rpt.min_masters_of_active_angels,
                rpt.max_masters_of_active_angels);
        } else {
            // some people with known min/max signature may leak their own 
            // identify and then complain about privacy. well, I believe this
            // is their own fault but anyway let's make them happy
            // TODO avg+std is better?
            double base1 = rpt.min_masters_of_online_angels,
                   base2 = rpt.min_masters_of_active_angels;
            if (!base1) base1 = 1;
            if (!base2) base2 = 1;
            prints(
                    "\n\t 上線天使中，擁有最多小主人數目是最少的 %.1f 倍；\n"
                    "\n\t 上線開放收主人的天使中，主人數目差異為 %.1f 倍\n",
                    rpt.max_masters_of_online_angels/base1,
                    rpt.max_masters_of_active_angels/base2);
        }

        if (HasUserPerm(PERM_ANGEL)) {
            if (currutmp->angelpause != ANGELPAUSE_NONE)
                prints("\n\t 由於您目前拒收小主人所以無順位資訊\n");
            else if (rpt.my_active_index == 0)
                prints("\n\t 您似乎有其它登入停收主人，所以目前無小天使順位。\n");
            else
                prints("\n\t 您的線上小天使順位為 %d。"
                       "\n\t 此順位可能會因其它小天使上線或改變呼叫器而變大\n",
                       rpt.my_active_index);
            prints("\n\t 您目前大約有 %d 位小主人。\n", rpt.my_active_masters);
        }
    }
    close(fd);
    pressanykey();
    return 0;
}

int a_angelreload() {
    vs_hdr2(" Angel Beats! 天使公會 ", " 重整天使資訊 ");
    outs("\n"
         "\t 由於重新統計天使小主人數目非常花時間，所以目前系統\n"
         "\t 一般只會更新已知的天使。  若站方有新增或刪除天使時\n"
         "\t 請在改完所有天使權限後利用此功\能來重整天使資訊。\n"
         "\n"
         "\t 另外由於重整時所有跟天使有關的功\能都會暫停 30 秒 ~ 一兩分鐘，\n"
         "\t 請不要沒事就重整，而是真的有調整天使名單後才使用。\n");
    if (vans("請問確定要重整天使資訊了嗎? [y/N]: ") != 'y') {
        vmsg("放棄。");
        return 0;
    }

    move(1, 0); clrtobot();
    outs("\n連線中...\n"); refresh();

    if (angel_beats_do_request(ANGELBEATS_REQ_RELOAD, usernum, 0) < 0) {
        outs("抱歉，無法連線至天使公會。\n"
             "請至 " BN_BUGREPORT " 看板通知站方管理人員。\n");
    } else {
        outs("\n完成!\n");
    }

    pressanykey();
    return 0;
}

inline int
angel_reject_me(userinfo_t * uin){
    int* iter = uin->reject;
    int unum;
    while ((unum = *iter++)) {
	if (unum == currutmp->uid) {
            // 超級好友?
            if (intbsearch(unum, uin->myfriend, uin->nFriends))
                return 0;
	    return 1;
	}
    }
    return 0;
}

static int
FindAngel(void){

    int angel_uid = 0;
    userinfo_t *angel = NULL;
    int retries = 2;
   
    do {
        angel_uid = angel_beats_do_request(ANGELBEATS_REQ_SUGGEST_AND_LINK,
                                           usernum, 0);
        if (angel_uid < 0)  // connection failure
            return -1;
        if (angel_uid > 0)
            angel = search_ulist(angel_uid);
    } while ((retries-- < 1) && !(angel && (angel->userlevel & PERM_ANGEL)));

    if (angel) {
        // got new angel
        pwcuSetMyAngel(angel->userid);
        return 1;
    }

    return 0;
}

#ifdef BN_NEWBIE
static inline void
GotoNewHand(){
    char old_board[IDLEN + 1] = "";
    int canRead = 1;

    if (currutmp && currutmp->mode == EDITING)
	return;

    // usually crashed as 'assert(currbid == brc_currbid)'
    if (currboard[0]) {
	strlcpy(old_board, currboard, IDLEN + 1);
	currboard = ""; // force enter_board
    }

    if (enter_board(BN_NEWBIE) == 0)
	canRead = 1;

    if (canRead)
	Read();

    if (canRead && old_board[0])
	enter_board(old_board);
}
#endif


static inline void
NoAngelFound(const char* msg){
    // don't worry about the screen - 
    // it should have been backuped before entering here.
    
    grayout(0, b_lines-3, GRAYOUT_DARK);
    move(b_lines-4, 0); clrtobot();
    outs(msg_separator);
    move(b_lines-2, 0);
    if (!msg)
	msg = "你的小天使現在不在線上";
    outs(msg);
#ifdef BN_NEWBIE
    if (currutmp == NULL || currutmp->mode != EDITING)
	outs("，請先在新手板上尋找答案或按 Ctrl-P 發問");
    if (vmsg("請按任意鍵繼續，若想直接進入新手板發文請按 p") == 'p')
	GotoNewHand();
#else
    pressanykey();
#endif
}

static inline void
AngelNotOnline(){
    char buf[PATHLEN];
    FILE *fp;

    // use cached angel data (assume already called before.)
    // angel_reload_nick();
    if (!_valid_angelmsg)
    {
	NoAngelFound(NULL);
	return;
    }

    // valid angelmsg is ready for being loaded.
    sethomefile(buf, cuser.myangel, FN_ANGELMSG);
    fp = fopen(buf, "rt");
    if (!fp)
    {
	// safer
	NoAngelFound(NULL);
	return;
    }
    clear();
    showtitle("小天使留言", BBSNAME);
    move(4, 0);
    buf[0] = 0;
    prints("您的%s小天使現在不在線上", _myangel_nick);

    outs("\n祂留言給你：\n");
    outs(ANSI_COLOR(1;31;44) "⊙┬──────────────┤" ANSI_COLOR(37) ""
	    "小天使留言" ANSI_COLOR(31) "├──────────────┬⊙" ANSI_RESET "\n");
    outs(ANSI_COLOR(1;31) "╭┤" ANSI_COLOR(32) " 小天使                          "
	    "                                     " ANSI_COLOR(31) "├╮" ANSI_RESET "\n");
    fgets(buf, sizeof(buf), fp); // skip first line: entry for nick
    while (fgets(buf, sizeof(buf), fp))
    {
	chomp(buf);
	prints(ANSI_COLOR(1;31) "│" ANSI_RESET "%-74.74s" ANSI_COLOR(1;31) "│" ANSI_RESET "\n", buf);
    }
    fclose(fp);
    outs(ANSI_COLOR(1;31) "╰┬──────────────────────"
	    "─────────────┬╯" ANSI_RESET "\n");
    outs(ANSI_COLOR(1;31;44) "⊙┴─────────────────────"
	    "──────────────┴⊙" ANSI_RESET "\n");
    prints("%55s%s", "留言日期: ", Cdatelite(&_myangel_touched));


    move(b_lines - 4, 0);
#ifdef BN_NEWBIE
    outs("臨時找不到小天使可到新手版(" BN_NEWBIE ")\n"
#endif
#ifdef BN_ANGELPRAY
	 "想留言給小天使請到許\願版(" BN_ANGELPRAY ")\n"
#endif
#ifdef BN_ASKBOARD
	 "想找看板在哪的話可到(" BN_ASKBOARD ")\n"
#endif
	 "請先在各板上尋找答案或按 Ctrl-P 發問");

    // Query if user wants to go to newbie board
    switch(tolower(vmsg("想換成目前在線上的小天使請按 h, "
#ifdef BN_NEWBIE
                    "進新手板請按 p, "
#endif
                    "其它任意鍵離開"))) {
        case 'h':
            move(b_lines - 4, 0); clrtobot();
            do_changeangel(1);
            break;
#ifdef BN_NEWBIE
        case 'p':
            GotoNewHand();
            break;
#endif
    }
}

static void
TalkToAngel(){
    static char AngelPermChecked = 0;
    static userinfo_t* lastuent = NULL;
    userinfo_t *uent;

    if (strcmp(cuser.myangel, "-") == 0){
	NoAngelFound("你沒有小天使");
	return;
    }

    if (cuser.myangel[0] && !AngelPermChecked) {
	userec_t xuser;
	memset(&xuser, 0, sizeof(xuser));
	getuser(cuser.myangel, &xuser); // XXX if user doesn't exist
	if (!(xuser.userlevel & PERM_ANGEL))
	    pwcuSetMyAngel("");
    }
    AngelPermChecked = 1;

    if (cuser.myangel[0] == 0) {
        int ret = FindAngel();
        if (ret <= 0) {
            lastuent = NULL;
            NoAngelFound(
                (ret < 0) ? "抱歉，天使系統故障，請至" BN_BUGREPORT "回報。" :
                            "現在沒有小天使在線上");
            return;
        }
    }

    // now try to load angel data.
    // This MUST be done before calling AngelNotOnline,
    // because it relies on this data.
    angel_reload_nick();

    uent = search_ulist_userid(cuser.myangel);
    if (uent == NULL || angel_reject_me(uent) || uent->mode == DEBUGSLEEPING){
	lastuent = NULL;
	AngelNotOnline();
	return;
    }

    // check angelpause: if talked then should accept.
    if (uent == lastuent) {
	// we've talked to angel.
	// XXX what if uentp reused by other? chance very, very low... 
	if (uent->angelpause >= ANGELPAUSE_REJALL)
	{
	    AngelNotOnline();
	    return;
	}
    } else {
	if (uent->angelpause) {
	    // lastuent = NULL;
	    AngelNotOnline();
	    return;
	}
    }

    more("etc/angel_usage", NA);

    /* 這段話或許可以在小天使回答問題時 show 出來
    move(b_lines - 1, 0);
    outs("現在你的id受到保密，回答你問題的小天使並不知道你是誰       \n"
         "你可以選擇不向對方透露自己身份來保護自己                   ");
	 */

    {
	char xnick[IDLEN+1], prompt[IDLEN*2];
	snprintf(xnick, sizeof(xnick), "%s小天使", _myangel_nick);
	snprintf(prompt, sizeof(prompt), "問%s小天使: ", _myangel_nick);
	// if success, record uent.
	if (my_write(uent->pid, prompt, xnick, WATERBALL_ANGEL, uent)) {
	    lastuent = uent;
        }
    }
    return;
}

void
CallAngel(){
    static int      entered = 0;
    screen_backup_t old_screen;

    if (entered)
	return;
    entered = 1;
    scr_dump(&old_screen);
    TalkToAngel();
    scr_restore(&old_screen);
    entered = 0;
}

void
pressanykey_or_callangel(){
    int w = t_columns - 2; // see vtuikit.c, SAFE_MAX_COL

    if (!HasBasicUserPerm(PERM_LOGINOK) ||
        strcmp(cuser.myangel, "-") == 0) {
	pressanykey();
	return;
    }

    move(b_lines, 0); clrtoeol();

    // message string length = 38
    outs(VCLR_PAUSE_PAD " ");
    w -= 1 + 38;
    vpad(w / 2, VMSG_PAUSE_PAD);
    outs(VCLR_PAUSE     " 請按 " ANSI_COLOR(36) "空白鍵" 
           VCLR_PAUSE     " 繼續，或 " ANSI_COLOR(36) "H"
           VCLR_PAUSE     " 呼叫小天使協助 " VCLR_PAUSE_PAD);
    vpad(w - w / 2, VMSG_PAUSE_PAD);
    outs(" " ANSI_RESET);

    if (tolower(vkey()) == 'h')
        CallAngel();

    move(b_lines, 0);
    clrtoeol();
}

#endif // PLAY_ANGEL
