/* $Id$ */
#include "bbs.h"

// PTT-BBS Angel System

#ifdef PLAY_ANGEL

void 
angel_toggle_pause()
{
    if (!HasUserPerm(PERM_ANGEL) || !currutmp)
	return;
    currutmp->angelpause ++;
    currutmp->angelpause %= ANGELPAUSE_MODES;

    // maintain deprecated value
    cuser.uflag2 &= ~UF2_ANGEL_OLDMASK;
}

void 
angel_load_data()
{
    // TODO cache angel.msg here.
}

int
t_changeangel(){
    char buf[4];

    /* cuser.myangel == "-" means banned for calling angel */
    if (cuser.myangel[0] == '-' || cuser.myangel[1] == 0) return 0;

    getdata(b_lines - 1, 0,
	    "更換小天使後就無法換回了喔！ 是否要更換小天使？ [y/N]",
	    buf, 3, LCECHO);
    if (buf[0] == 'y' || buf[0] == 'Y') {
	char buf[100];
	snprintf(buf, sizeof(buf), "%s小主人 %s 換掉 %s 小天使\n",
		ctime4(&now), cuser.userid, cuser.myangel);
	buf[24] = ' '; // replace '\n'
	log_file(BBSHOME "/log/changeangel.log", LOG_CREAT, buf);

	cuser.myangel[0] = 0;
	outs("小天使更新完成，下次呼叫時會選出新的小天使");
    }
    return XEASY;
}

int 
t_angelmsg(){
    char msg[3][74] = { "", "", "" };
    char nick[10] = "";
    char buf[512];
    int i;
    FILE* fp;

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
	for (i = 0; i < 3 &&
		getdata_buf(14 + i, 0, "：", msg[i], sizeof(msg[i]), DOECHO);
		++i);
	getdata(b_lines - 2, 0, "(S)儲存 (E)重新來過 (Q)取消？[S]",
		buf, 4, LCECHO);
    } while (buf[0] == 'E' || buf[0] == 'e');
    if (buf[0] == 'Q' || buf[0] == 'q')
	return 0;
    setuserfile(buf, "angelmsg");
    if (msg[0][0] == 0)
	unlink(buf);
    else {
	FILE* fp = fopen(buf, "w");
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

inline int
angel_reject_me(userinfo_t * uin){
    int* iter = uin->reject;
    int unum;
    while ((unum = *iter++)) {
	if (unum == currutmp->uid) {
	    return 1;
	}
    }
    return 0;
}


static int
FindAngel(void){
    int nAngel;
    int i, j;
    int choose;
    int trial = 0;
    int mask;

    if (cuser.sex < 6) /* 正常性別 */
	mask = 1 | (2 << (cuser.sex & 1));
    else
	mask = 7;

    do{
	nAngel = 0;
	j = SHM->currsorted;
	for (i = 0; i < SHM->UTMPnumber; ++i)
	    if (SHM->uinfo[SHM->sorted[j][0][i]].userlevel & PERM_ANGEL)
		++nAngel;

	if (nAngel == 0)
	    return 0;

	choose = random() % nAngel + 1;
	j = SHM->currsorted;
	for (i = 0; i < SHM->UTMPnumber && choose; ++i)
	    if (SHM->uinfo[SHM->sorted[j][0][i]].userlevel & PERM_ANGEL)
		--choose;

	if (choose == 0 && SHM->uinfo[SHM->sorted[j][0][i - 1]].uid != currutmp->uid
		&& (SHM->uinfo[SHM->sorted[j][0][i - 1]].userlevel & PERM_ANGEL)
		&& !angel_reject_me(&SHM->uinfo[SHM->sorted[j][0][i - 1]]) ){
	    strlcpy(cuser.myangel, SHM->uinfo[SHM->sorted[j][0][i - 1]].userid, IDLEN + 1);
	    passwd_update(usernum, &cuser);
	    return 1;
	}
    }while(++trial < 5);
    return 0;
}

static inline void
GotoNewHand(){
    char old_board[IDLEN + 1] = "";
    int canRead = 1;

    if (currutmp && currutmp->mode == EDITING)
	return;

    // usually crashed as 'assert(currbid == brc_currbid)'
    if (currboard[0]) {
	strlcpy(old_board, currboard, IDLEN + 1);
	currboard = "";// force enter_board
    }

    if (enter_board(BN_NEWBIE) == 0)
	canRead = 1;

    if (canRead)
	Read();

    if (canRead && old_board[0])
	enter_board(old_board);
}


static inline void
NoAngelFound(const char* msg){
    move(b_lines, 0);
    outs(msg);
    if (currutmp == NULL || currutmp->mode != EDITING)
	outs("，請先在新手板上尋找答案或按 Ctrl-P 發問");
    clrtoeol();
    refresh();
    sleep(1);
    GotoNewHand();
    return;
}

static inline void
AngelNotOnline(){
    char buf[PATHLEN] = "";
    const static char* const not_online_message = "您的小天使現在不在線上";

    // TODO cache angel's nick name!

    if (cuser.myangel[0] != '-')
	sethomefile(buf, cuser.myangel, "angelmsg");
    if (cuser.myangel[0] == '-' || !dashf(buf))
	NoAngelFound(not_online_message);
    else {
	time4_t mod = dasht(buf);
	FILE* fp = fopen(buf, "r");
	clear();
	showtitle("小天使留言", BBSNAME);
	move(4, 0);
	buf[0] = 0;
	fgets(buf, sizeof(buf), fp);
	if (strncmp(buf, "%%[", 3) == 0) {
	    chomp(buf);
	    prints("您的%s小天使現在不在線上", buf + 3);
	    fgets(buf, sizeof(buf), fp);
	} else
	    outs(not_online_message);

	outs("\n祂留言給你：\n");
	outs(ANSI_COLOR(1;31;44) "⊙┬──────────────┤" ANSI_COLOR(37) ""
	     "小天使留言" ANSI_COLOR(31) "├──────────────┬⊙" ANSI_RESET "\n");
	outs(ANSI_COLOR(1;31) "╭┤" ANSI_COLOR(32) " 小天使                          "
	     "                                     " ANSI_COLOR(31) "├╮" ANSI_RESET "\n");
	do {
	    chomp(buf);
	    prints(ANSI_COLOR(1;31) "│" ANSI_RESET "%-74.74s" ANSI_COLOR(1;31) "│" ANSI_RESET "\n", buf);
	} while (fgets(buf, sizeof(buf), fp));

	outs(ANSI_COLOR(1;31) "╰┬──────────────────────"
		"─────────────┬╯" ANSI_RESET "\n");
	outs(ANSI_COLOR(1;31;44) "⊙┴─────────────────────"
		"──────────────┴⊙" ANSI_RESET "\n");
	prints("%55s%s", "留言日期: ", Cdatelite(&mod));


	move(b_lines - 4, 0);
	outs("小主人使用上問題找不到小天使請到新手版(" BN_NEWBIE ")\n"
	     "              想留言給小天使請到許\願版(AngelPray)\n"
	     "                  想找看板在哪的話可到(AskBoard)\n"
	     "請先在各板上尋找答案或按 Ctrl-P 發問");
	pressanykey();

	GotoNewHand();
    }
}

static void
TalkToAngel(){
    static int AngelPermChecked = 0;
    userinfo_t* uent;
    userec_t xuser;

    if (strcmp(cuser.myangel, "-") == 0){
	AngelNotOnline();
	return;
    }

    if (cuser.myangel[0] && !AngelPermChecked) {
	getuser(cuser.myangel, &xuser); // XXX if user doesn't exist
	if (!(xuser.userlevel & PERM_ANGEL))
	    cuser.myangel[0] = 0;
    }
    AngelPermChecked = 1;

    if (cuser.myangel[0] == 0 && ! FindAngel()){
	NoAngelFound("現在沒有小天使在線上");
	return;
    }

    uent = search_ulist_userid(cuser.myangel);
    if (uent == 0 || uent->angelpause || angel_reject_me(uent)){
	AngelNotOnline();
	return;
    }

    more("etc/angel_usage", NA);

    /* 這段話或許可以在小天使回答問題時 show 出來
    move(b_lines - 1, 0);
    outs("現在你的id受到保密，回答你問題的小天使並不知道你是誰       \n"
         "你可以選擇不向對方透露自己身份來保護自己                   ");
	 */

    my_write(uent->pid, "問小天使： ", "小天使", WATERBALL_ANGEL, uent);
    return;
}

void
CallAngel(){
    static int      entered = 0;
    screen_backup_t old_screen;

    if (!HasUserPerm(PERM_LOGINOK) || entered)
	return;
    entered = 1;

    scr_dump(&old_screen);

    TalkToAngel();

    scr_restore(&old_screen);

    entered = 0;
}

#endif // PLAY_ANGEL
