/* $Id$ */
#include "bbs.h"
typedef int     (*play_func_t) (int, chcusr_t *, chcusr_t *, board_t, board_t);

static char	chc_ipass = 0, chc_hepass = 0;

extern userinfo_t *uip;

#define CHC_TIMEOUT	300
#define CHC_LOG		"chc_log"

static void
chcusr_put(userec_t *userec, chcusr_t *user)
{
    userec->chc_win = user->win;
    userec->chc_lose = user->lose;
    userec->chc_tie = user->tie;
}

static void
chcusr_get(userec_t *userec, chcusr_t *user)
{
    strlcpy(user->userid, userec->userid, sizeof(user->userid));
    user->win = userec->chc_win;
    user->lose = userec->chc_lose;
    user->tie = userec->chc_tie;
}

static int
hisplay(int s, chcusr_t *user1, chcusr_t *user2, board_t board, board_t tmpbrd)
{
    int             start_time;
    int             endgame = 0, endturn = 0;

    start_time = now;
    while (!endturn) {
	chc_lefttime = CHC_TIMEOUT - (now - start_time);
	if (chc_lefttime < 0) {
	    chc_lefttime = 0;

	    /* to make him break out igetkey() */
	    chc_from.r = -2;
	    chc_broadcast_send(act_list, board);
	}
	chc_drawline(board, user1, user2, TIME_ROW);
	move(1, 0);
	oflush();
	switch (igetkey()) {
	case 'q':
	    endgame = 2;
	    endturn = 1;
	    break;
	case 'p':
	    if (chc_hepass) {
		chc_from.r = -1;
		chc_broadcast_send(act_list, board);
		endgame = 3;
		endturn = 1;
	    }
	    break;
	case I_OTHERDATA:
	    if (!chc_broadcast_recv(act_list, board)) {	/* disconnect */
		endturn = 1;
		endgame = 1;
	    } else {
		if (chc_from.r == -1) {
		    chc_hepass = 1;
		    strlcpy(chc_warnmsg, "\033[1;33m要求和局!\033[m", sizeof(chc_warnmsg));
		    chc_drawline(board, user1, user2, WARN_ROW);
		} else {
		    /* 座標變換
		     *   (CHC_WATCH_PERSONAL 設定時
		     *    表觀棋者看的棋局為單人打譜的棋局)
		     *   棋盤需倒置的清況才要轉換
		     */
		    if ( ((chc_mode & CHC_WATCH) && (chc_mode & CHC_WATCH_PERSONAL)) || /* 1.如果在觀棋 且棋局是別人在打譜 且輪到你 或*/
			    (chc_mode & CHC_PERSONAL) || /* 2.自己在打譜 */
			    ((chc_mode & CHC_WATCH) && !chc_turn)
			  )
			; // do nothing
		    else {
			chc_from.r = 9 - chc_from.r, chc_from.c = 8 - chc_from.c;
			chc_to.r = 9 - chc_to.r, chc_to.c = 8 - chc_to.c;
		    }
		    chc_cursor = chc_to;
		    if (CHE_P(board[chc_to.r][chc_to.c]) == 1)
			endgame = 2;
		    endturn = 1;
		    chc_hepass = 0;
		    chc_drawline(board, user1, user2, STEP_ROW);
		    chc_log_step(board, &chc_from, &chc_to);
		    chc_movechess(board);
		    chc_drawline(board, user1, user2, LTR(chc_from.r));
		    chc_drawline(board, user1, user2, LTR(chc_to.r));
		}
	    }
	    break;
	}
    }
    return endgame;
}

static int
myplay(int s, chcusr_t *user1, chcusr_t *user2, board_t board, board_t tmpbrd)
{
    int             ch, start_time;
    int             endgame = 0, endturn = 0;

    chc_ipass = 0, chc_selected = 0;
    start_time = now;
    chc_lefttime = CHC_TIMEOUT - (now - start_time);
    bell();
    while (!endturn) {
	chc_drawline(board, user1, user2, TIME_ROW);
	chc_movecur(chc_cursor.r, chc_cursor.c);
	oflush();
	ch = igetkey();
	chc_lefttime = CHC_TIMEOUT - (now - start_time);
	if (chc_lefttime < 0)
	    ch = 'q';
	switch (ch) {
	case I_OTHERDATA:
	    if (!chc_broadcast_recv(act_list, board)) {	/* disconnect */
		endgame = 1;
		endturn = 1;
	    } else if (chc_from.r == -1 && chc_ipass) {
		endgame = 3;
		endturn = 1;
	    }
	    break;
	case KEY_UP:
	    chc_cursor.r--;
	    if (chc_cursor.r < 0)
		chc_cursor.r = BRD_ROW - 1;
	    break;
	case KEY_DOWN:
	    chc_cursor.r++;
	    if (chc_cursor.r >= BRD_ROW)
		chc_cursor.r = 0;
	    break;
	case KEY_LEFT:
	    chc_cursor.c--;
	    if (chc_cursor.c < 0)
		chc_cursor.c = BRD_COL - 1;
	    break;
	case KEY_RIGHT:
	    chc_cursor.c++;
	    if (chc_cursor.c >= BRD_COL)
		chc_cursor.c = 0;
	    break;
	case 'q':
	    endgame = 2;
	    endturn = 1;
	    break;
	case 'p':
	    chc_ipass = 1;
	    chc_from.r = -1;
	    chc_broadcast_send(act_list, board);
	    strlcpy(chc_warnmsg, "\033[1;33m要求和棋!\033[m", sizeof(chc_warnmsg));
	    chc_drawline(board, user1, user2, WARN_ROW);
	    bell();
	    break;
	case '\r':
	case '\n':
	case ' ':
	    if (chc_selected) {
		if (chc_cursor.r == chc_select.r &&
		    chc_cursor.c == chc_select.c) {
		    chc_selected = 0;
		    chc_drawline(board, user1, user2, LTR(chc_cursor.r));
		} else if (chc_canmove(board, chc_select, chc_cursor)) {
		    if (CHE_P(board[chc_cursor.r][chc_cursor.c]) == 1)
			endgame = 1;
		    chc_from = chc_select;
		    chc_to = chc_cursor;
		    if (!endgame) {
			memcpy(tmpbrd, board, sizeof(board_t));
			chc_movechess(tmpbrd);
		    }
		    if (endgame || !chc_iskfk(tmpbrd)) {
			chc_drawline(board, user1, user2, STEP_ROW);
			chc_log_step(board, &chc_from, &chc_to);
			chc_movechess(board);
			chc_broadcast_send(act_list, board);
			chc_selected = 0;
			chc_drawline(board, user1, user2, LTR(chc_from.r));
			chc_drawline(board, user1, user2, LTR(chc_to.r));
			endturn = 1;
		    } else {
			strlcpy(chc_warnmsg, "\033[1;33m不可以王見王\033[m", sizeof(chc_warnmsg));
			bell();
			chc_drawline(board, user1, user2, WARN_ROW);
		    }
		}
	    } else if (board[chc_cursor.r][chc_cursor.c] &&
		     CHE_O(board[chc_cursor.r][chc_cursor.c]) == chc_turn) {
		chc_selected = 1;
		chc_select = chc_cursor;
		chc_drawline(board, user1, user2, LTR(chc_cursor.r));
	    }
	    break;
	}
    }
    return endgame;
}

static void
mainloop(int s, chcusr_t *user1, chcusr_t *user2, board_t board, play_func_t play_func[2])
{
    int             endgame;
    char	    buf[80];
    board_t         tmpbrd;

    if (!(chc_mode & CHC_WATCH))
	chc_turn = 1;
    for (endgame = 0; !endgame; chc_turn ^= 1) {
	chc_firststep = 0;
	chc_drawline(board, user1, user2, TURN_ROW);
	if (chc_ischeck(board, chc_turn)) {
	    strlcpy(chc_warnmsg, "\033[1;31m將軍!\033[m", sizeof(chc_warnmsg));
	    bell();
	} else
	    chc_warnmsg[0] = 0;
	chc_drawline(board, user1, user2, WARN_ROW);
	endgame = play_func[chc_turn] (s, user1, user2, board, tmpbrd);
    }

    if (chc_mode & CHC_VERSUS) {
	if (endgame == 1) {
	    strlcpy(chc_warnmsg, "對方認輸了!", sizeof(chc_warnmsg));
	    user1->win++;
	    currutmp->chc_win++;
	} else if (endgame == 2) {
	    strlcpy(chc_warnmsg, "你認輸了!", sizeof(chc_warnmsg));
	    user1->lose++;
	    currutmp->chc_lose++;
	} else {
	    strlcpy(chc_warnmsg, "和棋", sizeof(chc_warnmsg));
	    user1->tie++;
	    currutmp->chc_tie++;
	}
	user1->lose--;
	chcusr_put(&cuser, user1);
	passwd_update(usernum, &cuser);
    }
    else if (chc_mode & CHC_WATCH) {
	strlcpy(chc_warnmsg, "結束觀棋", sizeof(chc_warnmsg));
    }
    else {
	strlcpy(chc_warnmsg, "結束打譜", sizeof(chc_warnmsg));
    }

    chc_log("=> ");
    if (endgame == 3)
	chc_log("和局");
    else{
	sprintf(buf, "%s勝\n", chc_my && endgame == 1 ? "紅" : "黑");
	chc_log(buf);
    }

    chc_drawline(board, user1, user2, WARN_ROW);
    bell();
    oflush();
}

static void
chc_init_play_func(chcusr_t *user1, chcusr_t *user2, play_func_t play_func[2])
{
    char	    userid[2][IDLEN + 1];

    if (chc_mode & CHC_PERSONAL) {
	strlcpy(userid[0], cuser.userid, sizeof(userid[0]));
	strlcpy(userid[1], cuser.userid, sizeof(userid[1]));
	play_func[0] = play_func[1] = myplay;
    }
    else if (chc_mode & CHC_WATCH) {
	userinfo_t *uinfo = search_ulist_userid(currutmp->mateid);
	strlcpy(userid[0], uinfo->userid, sizeof(userid[0]));
	strlcpy(userid[1], uinfo->mateid, sizeof(userid[1]));
	play_func[0] = play_func[1] = hisplay;
    }
    else {
	strlcpy(userid[0], cuser.userid, sizeof(userid[0]));
	strlcpy(userid[1], currutmp->mateid, sizeof(userid[1]));
	play_func[chc_my] = myplay;
	play_func[chc_my ^ 1] = hisplay;
    }

    getuser(userid[0]);
    chcusr_get(&xuser, user1);
    getuser(userid[1]);
    chcusr_get(&xuser, user2);
}

static void
chc_watch_request(int signo)
{
    if (!(currstat & CHC))
	return;
    chc_act_list *tmp;
    for(tmp = act_list; tmp->next != NULL; tmp = tmp->next);
    tmp->next = (chc_act_list *)malloc(sizeof(chc_act_list));
    tmp = tmp->next;
    tmp->sock = reply_connection_request(uip);
    if (tmp->sock < 0)
	return;
    tmp->next = NULL;
    write(tmp->sock, chc_bp, sizeof(board_t));
    write(tmp->sock, &chc_my, sizeof(chc_my));
    write(tmp->sock, &chc_turn, sizeof(chc_turn));
    write(tmp->sock, &currutmp->turn, sizeof(currutmp->turn));
    write(tmp->sock, &chc_firststep, sizeof(chc_firststep));
    write(tmp->sock, &chc_mode, sizeof(chc_mode));
}

static void
chc_init(int s, chcusr_t *user1, chcusr_t *user2, board_t board, play_func_t play_func[2])
{
    userinfo_t     *my = currutmp;

    setutmpmode(CHC);
    clear();
    chc_warnmsg[0] = 0;

    /* 從不同來源初始化各個變數 */
    if (!(chc_mode & CHC_WATCH)) {
	if (chc_mode & CHC_PERSONAL)
	    chc_my = 1;
	else
	    chc_my = my->turn;
	chc_firststep = 1;
	chc_init_board(board);
	chc_cursor.r = 9, chc_cursor.c = 0;
    }
    else {
	char mode;
	read(s, board, sizeof(board_t));
	read(s, &chc_my, sizeof(chc_my));
	read(s, &chc_turn, sizeof(chc_turn));
	read(s, &my->turn, sizeof(my->turn));
	read(s, &chc_firststep, sizeof(chc_firststep));
	read(s, &mode, sizeof(mode));
	if (mode & CHC_PERSONAL)
	    chc_mode |= CHC_WATCH_PERSONAL;
    }

    act_list = (chc_act_list *)malloc(sizeof(*act_list));
    act_list->sock = s;
    act_list->next = 0;

    chc_init_play_func(user1, user2, play_func);

    chc_redraw(user1, user2, board);
    add_io(s, 0);

    signal(SIGUSR1, chc_watch_request);

    if (my->turn && !(chc_mode & CHC_WATCH))
	chc_broadcast_recv(act_list, board);

    user1->lose++;

    if (chc_mode & CHC_VERSUS) {
	passwd_query(usernum, &xuser);
	chcusr_put(&xuser, user1);
	passwd_update(usernum, &xuser);
    }

    if (!my->turn) {
	if (!(chc_mode & CHC_WATCH))
	    chc_broadcast_send(act_list, board);
    	user2->lose++;
    }
    chc_redraw(user1, user2, board);
}

void
chc(int s, int mode)
{
    chcusr_t	    user1, user2;
    play_func_t     play_func[2];
    board_t	    board;
    char	    mode0 = currutmp->mode;
    char	    file[80];

    signal(SIGUSR1, SIG_IGN);

    chc_mode = mode;
    chc_bp = &board;

    chc_init(s, &user1, &user2, board, play_func);
    
    setuserfile(file, CHC_LOG);
    if (chc_log_open(&user1, &user2, file) < 0)
	vmsg("無法紀錄棋局");
    
    mainloop(s, &user1, &user2, board, play_func);

    /* close these fd */
    if (chc_mode & CHC_PERSONAL)
	act_list = act_list->next;
    while(act_list){
	close(act_list->sock);
	act_list = act_list->next;
    }

    add_io(0, 0);
    if (chc_my)
	pressanykey();

    currutmp->mode = mode0;

    if (getans("是否將棋譜寄回信箱？[N/y]") == 'y') {
	char title[80];
	sprintf(title, "%s V.S. %s", user1.userid, user2.userid);
	chc_log("\n--\n\n");
	chc_log_poem();
	chc_log_close();
	mail_id(cuser.userid, title, file, "[楚河漢界]");
    }
    else
	chc_log_close();
    signal(SIGUSR1, talk_request);
}

static userinfo_t *
chc_init_utmp(void)
{
    char            uident[16];
    userinfo_t	   *uin;

    stand_title("楚河漢界之爭");
    generalnamecomplete(msg_uid, uident, sizeof(uident),
			SHM->UTMPnumber,
			completeutmp_compar,
			completeutmp_permission,
			completeutmp_getname);
    if (uident[0] == '\0')
	return NULL;

    if ((uin = search_ulist_userid(uident)) == NULL)
	return NULL;

    uin->sig = SIG_CHC;
    return uin;
}

int
chc_main(void)
{
    userinfo_t     *uin;
    
    if ((uin = chc_init_utmp()) == NULL)
	return -1;
    uin->turn = 1;
    currutmp->turn = 0;
    strlcpy(uin->mateid, currutmp->userid, sizeof(uin->mateid));
    strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));
    
    my_talk(uin, friend_stat(currutmp, uin), 'c');
    return 0;
}

int
chc_personal(void)
{
    chc(0, CHC_PERSONAL);
    return 0;
}

int
chc_watch(void)
{
    int 	    sock, msgsock;
    userinfo_t     *uin;

    if ((uin = chc_init_utmp()) == NULL)
	return -1;

    if (uin->uid == currutmp->uid)
	return -1;

    if ((sock = make_connection_to_somebody(uin, 10)) < 0) {
	vmsg("無法建立連線");
	return -1;
    }
    msgsock = accept(sock, (struct sockaddr *) 0, (socklen_t *) 0);
    close(sock);
    if (msgsock < 0)
	return -1;

    strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));
    chc(msgsock, CHC_WATCH);
    close(msgsock);
    return 0;
}
