/* $Id: chc_play.c,v 1.5 2002/07/21 08:18:41 in2 Exp $ */
#include "bbs.h"
typedef int     (*play_func_t) (int, board_t, board_t);

static int      chc_ipass = 0, chc_hepass = 0;

#define CHC_TIMEOUT           300
#define SIDE_ROW          10
#define TURN_ROW          11
#define STEP_ROW          12
#define TIME_ROW          13
#define WARN_ROW          15
#define MYWIN_ROW         17
#define HISWIN_ROW        18

static int 
hisplay(int s, board_t board, board_t tmpbrd)
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
	chc_drawline(board, TIME_ROW);
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
		    chc_drawline(board, WARN_ROW);
		} else {
		    chc_from.r = 9 - chc_from.r, chc_from.c = 8 - chc_from.c;
		    chc_to.r = 9 - chc_to.r, chc_to.c = 8 - chc_to.c;
		    chc_cursor = chc_to;
		    if (CHE_P(board[chc_to.r][chc_to.c]) == 1)
			endgame = 2;
		    endturn = 1;
		    chc_hepass = 0;
		    chc_drawline(board, STEP_ROW);
		    chc_movechess(board);
		    chc_drawline(board, LTR(chc_from.r));
		    chc_drawline(board, LTR(chc_to.r));
		}
	    }
	    break;
	}
    }
    return endgame;
}

static int 
myplay(int s, board_t board, board_t tmpbrd)
{
    int             ch, start_time;
    int             endgame = 0, endturn = 0;

    chc_ipass = 0, chc_selected = 0;
    start_time = now;
    chc_lefttime = CHC_TIMEOUT - (now - start_time);
    bell();
    while (!endturn) {
	chc_drawline(board, TIME_ROW);
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
	    chc_drawline(board, WARN_ROW);
	    bell();
	    break;
	case '\r':
	case '\n':
	case ' ':
	    if (chc_selected) {
		if (chc_cursor.r == chc_select.r &&
		    chc_cursor.c == chc_select.c) {
		    chc_selected = 0;
		    chc_drawline(board, LTR(chc_cursor.r));
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
			chc_drawline(board, STEP_ROW);
			chc_movechess(board);
			chc_sendmove(s);
			chc_selected = 0;
			chc_drawline(board, LTR(chc_from.r));
			chc_drawline(board, LTR(chc_to.r));
			endturn = 1;
		    } else {
			strlcpy(chc_warnmsg, "\033[1;33m不可以王見王\033[m", sizeof(chc_warnmsg));
			bell();
			chc_drawline(board, WARN_ROW);
		    }
		}
	    } else if (board[chc_cursor.r][chc_cursor.c] &&
		     CHE_O(board[chc_cursor.r][chc_cursor.c]) == chc_turn) {
		chc_selected = 1;
		chc_select = chc_cursor;
		chc_drawline(board, LTR(chc_cursor.r));
	    }
	    break;
	}
    }
    return endgame;
}

static void 
mainloop(int s, board_t board)
{
    int             endgame;
    board_t         tmpbrd;
    play_func_t     play_func[2];

    play_func[chc_my] = myplay;
    play_func[chc_my ^ 1] = hisplay;
    for (chc_turn = 1, endgame = 0; !endgame; chc_turn ^= 1) {
	chc_firststep = 0;
	chc_drawline(board, TURN_ROW);
	if (chc_ischeck(board, chc_turn)) {
	    strlcpy(chc_warnmsg, "\033[1;31m將軍!\033[m", sizeof(chc_warnmsg));
	    bell();
	} else
	    chc_warnmsg[0] = 0;
	chc_drawline(board, WARN_ROW);
	endgame = play_func[chc_turn] (s, board, tmpbrd);
    }

    if (endgame == 1) {
	strlcpy(chc_warnmsg, "對方認輸了!", sizeof(chc_warnmsg));
	cuser.chc_win++;
    } else if (endgame == 2) {
	strlcpy(chc_warnmsg, "你認輸了!", sizeof(chc_warnmsg));
	cuser.chc_lose++;
    } else {
	strlcpy(chc_warnmsg, "和棋", sizeof(chc_warnmsg));
	cuser.chc_tie++;
    }
    cuser.chc_lose--;
    passwd_update(usernum, &cuser);
    chc_drawline(board, WARN_ROW);
    bell();
    oflush();
}

static void 
chc_init(int s, board_t board)
{
    userinfo_t     *my = currutmp;

    setutmpmode(CHC);
    clear();
    chc_warnmsg[0] = 0;
    chc_my = my->turn;
    chc_mateid = my->mateid;
    chc_firststep = 1;
    chc_init_board(board);
    chc_redraw(board);
    chc_cursor.r = 9, chc_cursor.c = 0;
    add_io(s, 0);

    if (my->turn)
	chc_recvmove(s);
    passwd_query(usernum, &xuser);
    cuser.chc_win = xuser.chc_win;
    cuser.chc_lose = xuser.chc_lose + 1;
    cuser.chc_tie = xuser.chc_tie;
    cuser.money = xuser.money;
    passwd_update(usernum, &cuser);

    getuser(chc_mateid);
    chc_hiswin = xuser.chc_win;
    chc_hislose = xuser.chc_lose;
    chc_histie = xuser.chc_tie;

    if (!my->turn) {
	chc_sendmove(s);
	chc_hislose++;
    }
    chc_redraw(board);
}

void 
chc(int s)
{
    board_t         board;

    chc_init(s, board);
    mainloop(s, board);
    close(s);
    add_io(0, 0);
    if (chc_my)
	pressanykey();
}
