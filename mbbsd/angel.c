#include "bbs.h"
#ifdef PLAY_ANGEL

// PTT-BBS Angel System

#include "daemons.h"
#define FN_ANGELMSG     "angelmsg"
#define FN_ANGELMSG2    "angelmsg2"

// 呼叫天使時顯示的說明
#define FN_ANGEL_USAGE   "etc/angel_usage"
// 呼叫天使時顯示的說明(附自訂訊息)
#define FN_ANGEL_USAGE2     "etc/angel_usage2"
// 天使不在線上時的說明(附自訂訊息)
#define FN_ANGEL_OFFLINE2   "etc/angel_offline2"

#ifndef ANGEL_INACTIVE_DAYS
#define ANGEL_INACTIVE_DAYS (180)
#endif

static const char
*PROMPT_ANGELBEATS = " Angel Beats! 天使公會 ",
*ERR_CONNECTION = "抱歉，無法連線至天使公會，請稍後再試。\n"
                  "若持續發生請至 " BN_BUGREPORT " 看板通知站方管理人員。\n",
*ERR_PROTOCOL = "抱歉，天使公會連線異常，請稍後再試。\n"
                "若持續發生請至 " BN_BUGREPORT " 看板通知站方管理人員。\n",
*ERR_PROTOCOL2 = "抱歉，天使公會似乎已更新，請重新登入。\n"
                 "若登入後仍錯誤請至 " BN_BUGREPORT " 看板通知站方管理人員。\n";

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
angel_register_new(const char *userid) {
    angel_beats_do_request(ANGELBEATS_REQ_REG_NEW, usernum,
                           searchuser(userid, NULL));
}

void
angel_notify_activity(const char *userid) {
    int master;
    static time4_t t = 0;
    time4_t tick;

    // tick: every 1 minutes.
    syncnow();
    tick = now - now % (1 * 60);

    // ping daemon only in different ticks.
    if (tick == t)
        return;

    master = searchuser(userid, NULL);
    t = tick;

    angel_beats_do_request(ANGELBEATS_REQ_HEARTBEAT, master, usernum);
}

void
angel_toggle_pause()
{
    if (!HasUserPerm(PERM_ANGEL) || !currutmp)
	return;
    currutmp->angelpause ++;
    currutmp->angelpause %= ANGELPAUSE_MODES;
    if (cuser.uflag & UF_NEW_ANGEL_PAGER) {
        // pmore_QuickRawModePref-like conf
        currutmp->angelpause = vs_quick_pref(
            currutmp->angelpause % ANGELPAUSE_MODES,
            "設定小天使神諭呼叫器(可直接按數字選取,方便設定熱鍵也避免誤按)",
            "請選取神諭呼叫器的新狀態: ",
            "開放\t停收\t關閉",
            NULL) % ANGELPAUSE_MODES;
    }
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
    static char mynick[IDLEN + 1] = "";
    static time4_t touched = 0;
    time4_t modtime = 0;

    *buf = 0;
    setuserfile(fn, FN_ANGELMSG);
    modtime = dasht(fn);
    if (modtime != touched) {
        touched = modtime;
        *mynick = 0;
        // reload file
        if ((fp = fopen(fn, "rt")))
        {
            angel_parse_nick_fp(fp, mynick, sizeof(mynick));
            fclose(fp);
        }
    }
    strlcpy(buf, mynick, szbuf);
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

int
select_angel() {
    angel_beats_uid_list list = {0};
    angel_beats_data req = {0};
    int i;
    int fd;

    vs_hdr2(PROMPT_ANGELBEATS, " 選取天使 ");
    outs("\n");

    if ((fd = toconnect(ANGELBEATS_ADDR)) < 0) {
        outs(ERR_CONNECTION);
        pressanykey();
        return 0;
    }

    req.cb = sizeof(req);
    req.operation = ANGELBEATS_REQ_GET_ONLINE_LIST;
    req.master_uid = usernum;

    if (towrite(fd, &req, sizeof(req)) < 0 ||
        toread(fd, &list, sizeof(list)) < 0 ||
        list.cb != sizeof(list)) {
        close(fd);
        outs(ERR_PROTOCOL);
        pressanykey();
        return 0;
    }
    close(fd);

    if (!list.angels) {
        vmsg("抱歉，目前沒有可呼叫的天使在線上。");
        return 0;
    }

    // list all angels
    for (i = 0; i < list.angels; i++) {
        char fn[PATHLEN];
        char nick[IDLEN + 1] = "";
        int uid = list.uids[i];
        const char *userid = getuserid(uid);
        FILE *fp = NULL;
        userinfo_t *uinfo = search_ulist_userid(userid);
        const char *pause_msg = "";

        sethomefile(fn, userid, FN_ANGELMSG);
        if ((fp = fopen(fn, "rt")) != NULL) {
            angel_parse_nick_fp(fp, nick, sizeof(nick));
            strlcat(nick, "小天使", sizeof(nick));
            fclose(fp);
        } else {
            strlcpy(nick, "(未設定暱稱)", sizeof(nick));
        }
        if (uinfo && uinfo->angelpause == 1)
            pause_msg = ANSI_COLOR(1;32) "(停收新問題/新主人) " ANSI_RESET;
        else if (uinfo && uinfo->angelpause == 2)
            pause_msg = ANSI_COLOR(1;31) "(關閉呼叫器) " ANSI_RESET;
        prints(" %3i. %s %s [UID: %d]\n", i + 1, nick, pause_msg, uid);
    }
    while (list.angels) {
        char ans[5];
        int idx;

        if (!getdata(b_lines - 1, 0, "請問要選取哪位小天使 (輸入數字): ",
                     ans, sizeof(ans), NUMECHO)) {
            vmsg("未選取小天使。");
            return 0;
        }
        idx = atoi(ans);
        if (idx < 1 || idx > list.angels) {
            vmsg("數字不正確。");
            return 0;
        }
        // No need to tell AngelBeats since this is only for ANGEL_CIA_ACCOUNT.
        pwcuSetMyAngel(getuserid(list.uids[idx - 1]));
	log_filef(BBSHOME "/log/changeangel.log",LOG_CREAT,
                  "%s 品管 %s 抽測 %s 小天使\n",
                  Cdatelite(&now), cuser.userid, cuser.myangel);
        vmsg("小天使已更換完成。");
        break;
    }
    return 0;
}

static int
do_changeangel(int force) {
    char buf[4];
    const char *prompt = "登記完成，下次呼叫時會從上線的天使中選出新的小天使";
    static int is_bad_master = -1;

    /* cuser.myangel == "-" means banned for calling angel */
    if (cuser.myangel[0] == '-')
        return 0;

    if (HasUserRole(ROLE_ANGEL_CIA))
        return select_angel();

    if (!cuser.myangel[0]) {
        vmsg(prompt);
        return 0;
    }

    // get/cache "bad_master" info
    if (is_bad_master < 0) {
        char bad_master_file[PATHLEN];
        setuserfile(bad_master_file, ".bad_master");
        is_bad_master = dashf(bad_master_file);
        if (is_bad_master &&
            dasht(bad_master_file) < (now - ANGEL_INACTIVE_DAYS * DAY_SECONDS)) {
            log_filef("log/bad_master.log", LOG_CREAT,
                      "%s %s removed from bad master list (%d)\n",
                      Cdatelite(&now), cuser.userid, dasht(bad_master_file));
            remove(bad_master_file);
        }
    }

    if (!(force || HasUserPerm(PERM_ADMIN)))
    {
#ifdef ANGEL_CHANGE_TIMELIMIT_MINS
        int duration = ANGEL_CHANGE_TIMELIMIT_MINS;
        if (is_bad_master)
            duration *= 3;
        if (cuser.timesetangel &&
            (now - cuser.timesetangel < duration * 60)) {
            vmsgf("每次更換小天使最少間隔 %d 分鐘。", duration);
            return 0;
        }
#endif
       if (is_bad_master)
           log_filef("log/bad_master.log", LOG_CREAT,
                     "%s %s change angel.\n", Cdatelite(&now), cuser.userid);
        if (is_bad_master &&
            !verify_captcha("為避免大量非正常更換小天使，\n"))
            return 0;
    }

    getdata(b_lines - 1, 0, "確定要更換小天使？ [y/N]", buf, 3, LCECHO);
    if (buf[0] == 'y') {
	log_filef(BBSHOME "/log/changeangel.log",LOG_CREAT,
                  "%s 小主人 %s 換掉 %s 小天使\n",
                  Cdatelite(&now), cuser.userid, cuser.myangel);
        angel_beats_do_request(ANGELBEATS_REQ_REMOVE_LINK,
                               usernum, searchuser(cuser.myangel, NULL));
	pwcuSetMyAngel("");
        vmsg(prompt);
    }
    return 0;
}

int a_changeangel(void) {
    return do_changeangel(0);
}

const char *
angel_order_song(char *receiver, size_t sz_receiver) {
    userec_t udata;
    char prompt[STRLEN], ans[3];
    const char *angel_nick = NULL;

    if (!*cuser.myangel)
        return NULL;

#ifdef ANGEL_ORDER_SONG_DAY
    // check day
    {
        struct tm tm;
        localtime4_r(&now, &tm);
        if (tm.tm_mday != ANGEL_ORDER_SONG_DAY)
            return NULL;
    }
#endif

    // ensure if my angel is still valid.
    if (passwd_load_user(cuser.myangel, &udata) <= 0 ||
        !(udata.userlevel & PERM_ANGEL))
        return NULL;

    angel_nick = angel_get_nick();
    snprintf(prompt, sizeof(prompt), "要留言給你的%s小天使嗎? [y/N]: ",
             angel_nick);
    if (getdata(20, 0, prompt, ans, sizeof(ans), LCECHO) && *ans == 'y') {
        snprintf(receiver, sz_receiver, "%s小天使", angel_nick);
        return angel_nick;
    }
    return NULL;
}

int angel_check_master(void) {
    char uid[IDLEN + 1];
    userec_t xuser;
    int is_my_master;

    vs_hdr2(PROMPT_ANGELBEATS, " 查詢主人狀態 ");
    usercomplete("想查詢的主人 ID: ", uid);
    move(2, 0); clrtobot();
    if (!*uid)
        return 0;
    if (getuser(uid, &xuser) < 1) {
        vmsg("此 ID 不存在。");
        return 0;
    }
    wait_penalty(1);
    is_my_master = (strcasecmp(xuser.myangel, cuser.userid) == 0);
    move(7, 0);
    if (is_my_master) {
        prints(ANSI_COLOR(1;32) "%s 是你的主人。" ANSI_RESET "\n",
               xuser.userid);
        if (xuser.timesetangel)
            prints("小天使與主人的關係已維持了 %d 天。\n",
                   (now - xuser.timesetangel) / DAY_SECONDS + 1);
        if (xuser.timeplayangel && xuser.timeplayangel > xuser.timesetangel)
            prints("小主人最後與天使互動(hh成功\呼叫或點歌)的時間: %s\n",
                   Cdatelite(&xuser.timeplayangel));
        else if (xuser.timesetangel)
            prints("但小主人似乎從來沒與你互動(成功\呼叫或點歌)過\n"
                   " (常見於洗天使或誤按的主人)。\n");
    } else {
        prints(ANSI_COLOR(1;31) "%s 不是你的小主人。" ANSI_RESET "\n",
               xuser.userid);
    }
    log_filef("log/angel_query_master.log", LOG_CREAT,
             "%s [%s] query [%s]\n", Cdatelite(&now), cuser.userid, uid);
    pressanykey();
    return 0;
}

void
angel_log_order_song(const char *angel_nick) {
    char angel_exp[STRLEN];

    syncnow();
    if (cuser.timesetangel && now >= cuser.timesetangel)
        snprintf(angel_exp, sizeof(angel_exp),
                 "%d天", (now - cuser.timesetangel) / DAY_SECONDS + 1);
    else
        strlcpy(angel_exp, "很久", sizeof(angel_exp));

    log_filef("log/osong_angel.log", LOG_CREAT,
              "%s %*s 點歌給 %*s小天使 (%s - %s)\n",
              Cdatelite(&now), IDLEN, cuser.userid,
              IDLEN - 6, angel_nick, fromhost, angel_exp);
    pwcuPlayAngel();
}

void
angel_log_msg_to_angel(void) {
    if (cuser.timeplayangel > cuser.timesetangel) {
        // Try to avoid mass logs
        if ((now - cuser.timeplayangel) >= ANGEL_CHANGE_TIMELIMIT_MINS * 60)
            return;
    }
    pwcuPlayAngel();
}

int a_angelreport() {
    angel_beats_report rpt = {0};
    angel_beats_data   req = {0};
    int fd;

    vs_hdr2(PROMPT_ANGELBEATS, " 天使狀態報告 ");
    outs("\n");

    if ((fd = toconnect(ANGELBEATS_ADDR)) < 0) {
        outs(ERR_CONNECTION);
        pressanykey();
        return 0;
    }

    req.cb = sizeof(req);
    req.operation = ANGELBEATS_REQ_REPORT;
    req.master_uid = usernum;

    if (HasUserPerm(PERM_ANGEL))
        req.angel_uid = usernum;

    if (towrite(fd, &req, sizeof(req)) < 0 ||
        toread(fd, &rpt, sizeof(rpt)) < 0) {
        outs(ERR_PROTOCOL);
    } else if (rpt.cb != sizeof(rpt)) {
        outs(ERR_PROTOCOL2);
    } else {
        prints(
            "\t 現在時間: %s\n"
            "\t 系統內已登記的天使為 %d 位。\n"
            "\t 目前有 %d 位天使在線上，其中 %d 位神諭呼叫器為設定開放；\n",
            Cdatelite(&now),
            rpt.total_angels,
            rpt.total_online_angels,
            rpt.total_active_angels);

       if (!rpt.inactive_days)
           rpt.inactive_days = ANGEL_INACTIVE_DAYS;

        prints(
            "\t 上線天使中，擁有活躍小主人數目最少為 %d 位，最多為 %d 位\n"
            "\t 上線且開放收主人的天使中，活躍主人最少 %d 位，最多 %d 位\n"
            "\t 活躍小主人定義為 %d 天內有對任一(包含前任)小天使傳過訊息\n",
            rpt.min_masters_of_online_angels,
            rpt.max_masters_of_online_angels,
            rpt.min_masters_of_active_angels,
            rpt.max_masters_of_active_angels,
            rpt.inactive_days);
#ifdef ANGELBEATS_ACTIVE_MASTER_RECORD_START
       int days = (now - ANGELBEATS_ACTIVE_MASTER_RECORD_START) / DAY_SECONDS;
       days += 1;
       if (days < rpt.inactive_days)
           prints("\t (由於活躍小主人是新增的統計項目，"
                   "目前實際只有 %d 天內的數據)\n", days);
#endif

       prints("\n\t " ANSI_COLOR(1;33) "提醒您以下數據有包含您私人的記錄，"
               "任意公佈可能會洩露身份。\n" ANSI_RESET);

#ifdef ANGEL_REPORT_INDEX
        if (HasUserPerm(PERM_ANGEL)) {
            if (currutmp->angelpause != ANGELPAUSE_NONE)
                prints("\t 由於您目前拒收小主人所以無順位資訊\n");
            else if (rpt.my_active_index == 0)
                prints("\t 您似乎有其它登入停收或拒收所以目前無天使順位\n");
            else
                prints("\t 您的線上小天使順位為 %d。\n"
                       "\t 此順位可能會因其它小天使上線或改變呼叫器而變大\n",
                       rpt.my_active_index);
        }
#endif
        prints("\t 您目前大約有 %d 位活躍小主人。\n", rpt.my_active_masters);

        if (rpt.last_assigned_master > 0) {
            // TODO check if last_assigned is already invalid.
            prints("\t 你最後收到的新小主人是 %s (%s)\n",
                   getuserid(rpt.last_assigned_master),
                   Cdatelite(&rpt.last_assigned));
           prints("\n"
                  "\t (很多新的小主人可能是誤按或洗天使總之都不會送訊息，\n"
                  "\t  如果你等很久都沒看到新主人的訊息，就可以明白為何之前\n"
                  "\t  會以為都沒有新主人，其實是有收到但對方不講話)\n");
        }
#ifdef ANGEL_ASSIGN_DOCUMENT
       prints("\n\t 若覺得很久都沒收到新主人可按 [a] 鍵查看分配機制說明。\n");
#endif
    }
    close(fd);
#ifdef ANGEL_ASSIGN_DOCUMENT
    if (tolower(pressanykey()) == 'a') {
       more(ANGEL_ASSIGN_DOCUMENT, YEA);
    }
#else
    pressanykey();
#endif
    wait_penalty(1);
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

static void
angel_display_message(const char *template_fn,
                      const char *message_fn,
                      int skip_lines,
                      int row, int col,
                      int date_row, int date_col) {
    char buf[ANSILINELEN];
    FILE *fp = fopen(message_fn, "rt");
    time4_t ts = dasht(message_fn);

    show_file(template_fn, vgety(), b_lines - vgety(), SHOWFILE_ALLOW_ALL);
    while (skip_lines-- > 0)
        fgets(buf, sizeof(buf), fp);

    while (fgets(buf, sizeof(buf), fp))
    {
	chomp(buf);
        move_ansi(row++, col);
        outs(buf);
    }
    fclose(fp);
    move_ansi(date_row, date_col);
    outs(ANSI_RESET);
    outs(Cdatelite(&ts));
}

enum ANGEL_MSG_FORMAT {
    FORMAT_NICK_MSG = 1,
    FORMAT_PLAIN_MSG,
};

int
angel_edit_msg(const char *prompt, const char *filename,
               enum ANGEL_MSG_FORMAT format) {
    char nick[IDLEN - 6 + 1] = ""; // 6=strlen("小天使")
    char old_nick[IDLEN] = "";
    char msg[3][STRLEN] = {"", "", ""};
    char fpath[PATHLEN];
    char buf[512];
    FILE *fp;
    int i, do_delete_file = 0;

    vs_hdr2(PROMPT_ANGELBEATS, prompt);
    setuserfile(fpath, filename);

    outs("原設定: \n");
    fp = fopen(fpath, "r");
    if (fp) {
        if (format == FORMAT_NICK_MSG) {
            fgets(buf, sizeof(buf), fp);
            if (strstr(buf, "%%[") == buf) {
                chomp(buf);
                strlcpy(nick, buf + 3, sizeof(nick));
                strlcpy(old_nick, nick, sizeof(old_nick));
                prints(" 暱稱: %s小天使\n", nick);
            }
        }
        for (i = 0; i < 3; i++) {
            if (!fgets(msg[i], sizeof(msg[i]), fp))
                break;
            outs(" : ");
            outs(msg[i]);
            chomp(msg[i]);
        }

        fclose(fp);
    } else {
        outs("(目前無設定)\n");
    }
    mvouts(10, 0, "新設定:\n");
    if (format == FORMAT_NICK_MSG) {
        getdata_buf(11, 0, " 小天使暱稱：", nick, sizeof(nick), DOECHO);
        if (!*nick) {
            mvouts(12, 0, "空白將導致刪除小天使暱稱與訊息。\n");
            do_delete_file = 1;
        }
    }
    if (!do_delete_file) {
        mvouts(12, 0, " 編輯訊息 (最多三行，按[ENTER]結束):\n");
        for (i = 0; i < 3; i++) {
            if (!getdata_buf(13 + i, 0, " : ", msg[i], 73, DOECHO)) {
                for (i++; i < 3; i++)
                    msg[i][0] = 0;
                break;
            }
        }
        if (format == FORMAT_PLAIN_MSG && !*msg[0]) {
            mvouts(15, 0, "空白將導致刪除小天使訊息。\n");
            do_delete_file = 1;
        }
    }

    if (!getdata(20, 0, "確定儲存？ [y/N]: ", buf, 3, LCECHO) ||
        buf[0] != 'y') {
        return 0;
    }

    if (strcmp(nick, old_nick) != 0) {
        log_filef("log/change_angel_nick.log", LOG_CREAT,
                  "%s %s (%s小天使)更換暱稱為「%s小天使」\n",
                  Cdatelite(&now), cuser.userid, old_nick, nick);
    }

    if (do_delete_file) {
        if (dashf(fpath) && remove(fpath) != 0)
            vmsg("系統錯誤 - 無法刪除。");
        return 1;
    }

    // write file
    fp = fopen(fpath, "w");
    if (!fp) {
        vmsg("系統錯誤 - 無法寫入。");
        return 0;
    }
    if (format == FORMAT_NICK_MSG) {
        fputs("%%[", fp);
        fputs(nick, fp);
        fputs("\n", fp);
    }
    for (i = 0; i < 3; i++) {
        fputs(msg[i], fp);
        fputc('\n', fp);
    }

    fclose(fp);
    return 1;
}

int
a_angelmsg(){
    return angel_edit_msg("編輯小天使暱稱與離線訊息", FN_ANGELMSG,
                          FORMAT_NICK_MSG);
}

int
a_angelmsg2(){
    return angel_edit_msg("編輯小天使呼叫畫面個性留言", FN_ANGELMSG2,
                          FORMAT_PLAIN_MSG);
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
    char msg_fn[PATHLEN];

    // use cached angel data (assume already called before.)
    // angel_reload_nick();
    if (!_valid_angelmsg)
    {
	NoAngelFound(NULL);
	return;
    }

    // valid angelmsg is ready for being loaded.
    sethomefile(msg_fn, cuser.myangel, FN_ANGELMSG);
    if (dashs(msg_fn) < 1) {
	NoAngelFound(NULL);
	return;
    }

    showtitle("小天使留言", BBSNAME);
    move(2, 0);
    prints("您的%s小天使現在不在線上，祂留言給你：\n", _myangel_nick);
    angel_display_message(FN_ANGEL_OFFLINE2, msg_fn, 1, 5, 4, 9, 53);

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
    int supervisor = 0;
    char msg_fn[PATHLEN];

    if (strcmp(cuser.myangel, "-") == 0){
	NoAngelFound("你沒有小天使");
	return;
    }

    if (HasUserRole(ROLE_ANGEL_CIA))
        supervisor = 1;

    // 若使用者多重登入並分開呼叫則可能會連續換多個小天使。
    // 所以 !AngelPermChecked 時 reload user data.
    // 聽來很腦殘但是似乎真的有使用者會這樣。
    if (!AngelPermChecked) {
        pwcuReload();
    }
    if (cuser.myangel[0] && !AngelPermChecked) {
	userec_t xuser = {0};
	if (getuser(cuser.myangel, &xuser) < 1 ||
            !(xuser.userlevel & PERM_ANGEL)) {
	    pwcuSetMyAngel("");
#ifdef USE_FREE_ANGEL_FOR_INACTIVE_MASTER
        } else if (!supervisor &&
                   (now - cuser.timeplayangel >
                    ANGEL_INACTIVE_DAYS * DAY_SECONDS)) {
            // Inactive master.
            uent = search_ulist_userid(cuser.myangel);
            if (uent == NULL || angel_reject_me(uent) ||
                uent->angelpause || uent->mode == DEBUGSLEEPING) {
                log_filef("log/auto_change_angel.log", LOG_CREAT,
                          "%s master %s (%d days), angel %s, state (%s)\n",
                          Cdatelite(&now), cuser.userid,
                          (now - cuser.timeplayangel) / DAY_SECONDS,
                          cuser.myangel,
                          !uent ? "not online" : angel_reject_me(uent) ?
                          "reject" : uent->angelpause ? "pause" : "debugsleep");
                pwcuSetMyAngel("");
                angel_beats_do_request(
                    ANGELBEATS_REQ_REMOVE_LINK, usernum,
                    searchuser(cuser.myangel, NULL));
            }
#endif
        }
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
    if (uent == NULL || (!supervisor && angel_reject_me(uent)) ||
        uent->mode == DEBUGSLEEPING){
	lastuent = NULL;
	AngelNotOnline();
	return;
    }

    // check angelpause: if talked then should accept.
    if (supervisor && uent->angelpause == ANGELPAUSE_REJNEW) {
        // The only case to override angelpause.
    } else if (uent == lastuent) {
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

    sethomefile(msg_fn, cuser.myangel, FN_ANGELMSG2);
    if (dashs(msg_fn) > 0) {
        // render per-user message
        move(1, 0);
        clrtobot();
        angel_display_message(FN_ANGEL_USAGE2, msg_fn, 0, 2, 4, 6, 24);
    } else {
        more(FN_ANGEL_USAGE, NA);
    }

    {
	char xnick[IDLEN+1], prompt[IDLEN*2];
	snprintf(xnick, sizeof(xnick), "%s小天使", _myangel_nick);
	snprintf(prompt, sizeof(prompt), "問%s小天使: ", _myangel_nick);
	// if success, record uent.
	if (my_write(uent->pid, prompt, xnick, WATERBALL_ANGEL, uent)) {
	    lastuent = uent;
        }
    }
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
