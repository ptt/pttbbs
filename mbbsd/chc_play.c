/* $Id$ */
#include "bbs.h"
typedef int     (*play_func_t) (int, chcusr_t *, chcusr_t *, board_t, board_t);

static int      chc_ipass = 0, chc_hepass = 0;

#define CHC_TIMEOUT           300
#define SIDE_ROW          10
#define TURN_ROW          11
#define STEP_ROW          12
#define TIME_ROW          13
#define WARN_ROW          15
#define MYWIN_ROW         17
#define HISWIN_ROW        18

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
	    chc_sendmove(s);
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
		chc_sendmove(s);
		endgame = 3;
		endturn = 1;
	    }
	    break;
	case I_OTHERDATA:
	    if (chc_recvmove(s)) {	/* disconnect */
		endturn = 1;
		endgame = 1;
	    } else {
		if (chc_from.r == -1) {
		    chc_hepass = 1;
		    strlcpy(chc_warnmsg, "\033[1;33m要求和局!\033[m", sizeof(chc_warnmsg));
		    chc_drawline(board, user1, user2, WARN_ROW);
		} else {
		    chc_from.r = 9 - chc_from.r, chc_from.c = 8 - chc_from.c;
		    chc_to.r = 9 - chc_to.r, chc_to.c = 8 - chc_to.c;
		    chc_cursor = chc_to;
		    if (CHE_P(board[chc_to.r][chc_to.c]) == 1)
			endgame = 2;
		    endturn = 1;
		    chc_hepass = 0;
		    chc_drawline(board, user1, user2, STEP_ROW);
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
	    if (chc_recvmove(s)) {	/* disconnect */
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
	    chc_sendmove(s);
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
			chc_movechess(board);
			chc_sendmove(s);
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
    board_t         tmpbrd;

    play_func[chc_my] = myplay;
    if(s != 0)
	play_func[chc_my ^ 1] = hisplay;
    else /* 跟自己下棋 */
	play_func[chc_my ^ 1] = myplay;

    for (chc_turn = 1, endgame = 0; !endgame; chc_turn ^= 1) {
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

    if (s != 0) {
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
    }
    else {
	strlcpy(chc_warnmsg, "結束打譜", sizeof(chc_warnmsg));
    }
    user1->lose--;

    // if not watching
    chcusr_put(&cuser, user1);
    passwd_update(usernum, &cuser);
    chc_drawline(board, user1, user2, WARN_ROW);
    bell();
    oflush();
}

static void
chc_init(int s, chcusr_t *user1, chcusr_t *user2, board_t board)
{
    userinfo_t     *my = currutmp;

    setutmpmode(CHC);
    clear();
    chc_warnmsg[0] = 0;
    if(s != 0){ // XXX mateid -> user2
	chc_my = my->turn;
	chc_mateid = my->mateid;
    }
    else{
	chc_my = 1;
	chc_mateid = cuser.userid;
    }
    chc_firststep = 1;
    chc_init_board(board);
    chc_redraw(user1, user2, board);
    chc_cursor.r = 9, chc_cursor.c = 0;
    add_io(s, 0);

     if (my->turn)
	chc_recvmove(s);

    user1->lose++;
    // if not watching
    passwd_query(usernum, &xuser);
    chcusr_put(&xuser, user1);
    passwd_update(usernum, &xuser);

    if (!my->turn) {
	chc_sendmove(s);
	user2->lose++;
    }
    chc_redraw(user1, user2, board);
}

static void
chc_userinit(char *userid1, char *userid2, chcusr_t *user1, chcusr_t *user2, play_func_t play_func[2])
{
    getuser(userid1);
    chcusr_get(&xuser, user1);

    getuser(userid2);
    chcusr_get(&xuser, user2);
}

void
chc(int s, int type)
{
    board_t         board;
    chcusr_t	    user1, user2;
    play_func_t     play_func[2];

    chc_userinit(cuser.userid, currutmp->mateid, &user1, &user2, play_func);
    chc_init(s, &user1, &user2, board);
    mainloop(s, &user1, &user2, board, play_func);
    if (type == CHC_VERSUS)
	close(s);
    add_io(0, 0);
    if (chc_my)
	pressanykey();
}
