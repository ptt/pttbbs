/* $Id: chc_rule.c,v 1.3 2002/07/05 17:10:27 in2 Exp $ */
#include "bbs.h"

#define CENTER(a, b)      (((a) + (b)) >> 1)

void 
chc_init_board(board_t board)
{
    memset(board, 0, sizeof(board_t));
    board[0][4] = CHE(1, chc_my ^ 1);	/* 將 */
    board[0][3] = board[0][5] = CHE(2, chc_my ^ 1);	/* 士 */
    board[0][2] = board[0][6] = CHE(3, chc_my ^ 1);	/* 象 */
    board[0][0] = board[0][8] = CHE(4, chc_my ^ 1);	/* 車 */
    board[0][1] = board[0][7] = CHE(5, chc_my ^ 1);	/* 馬 */
    board[2][1] = board[2][7] = CHE(6, chc_my ^ 1);	/* 包 */
    board[3][0] = board[3][2] = board[3][4] =
	board[3][6] = board[3][8] = CHE(7, chc_my ^ 1);	/* 卒 */

    board[9][4] = CHE(1, chc_my);	/* 帥 */
    board[9][3] = board[9][5] = CHE(2, chc_my);	/* 仕 */
    board[9][2] = board[9][6] = CHE(3, chc_my);	/* 相 */
    board[9][0] = board[9][8] = CHE(4, chc_my);	/* 車 */
    board[9][1] = board[9][7] = CHE(5, chc_my);	/* 傌 */
    board[7][1] = board[7][7] = CHE(6, chc_my);	/* 炮 */
    board[6][0] = board[6][2] = board[6][4] =
	board[6][6] = board[6][8] = CHE(7, chc_my);	/* 兵 */
}

void 
chc_movechess(board_t board)
{
    board[chc_to.r][chc_to.c] = board[chc_from.r][chc_from.c];
    board[chc_from.r][chc_from.c] = 0;
}

static int 
dist(rc_t from, rc_t to, int rowcol)
{
    int             d;

    d = rowcol ? from.c - to.c : from.r - to.r;
    return d > 0 ? d : -d;
}

static int 
between(board_t board, rc_t from, rc_t to, int rowcol)
{
    int             i, rtv = 0;

    if (rowcol) {
	if (from.c > to.c)
	    i = from.c, from.c = to.c, to.c = i;
	for (i = from.c + 1; i < to.c; i++)
	    if (board[to.r][i])
		rtv++;
    } else {
	if (from.r > to.r)
	    i = from.r, from.r = to.r, to.r = i;
	for (i = from.r + 1; i < to.r; i++)
	    if (board[i][to.c])
		rtv++;
    }
    return rtv;
}

int 
chc_canmove(board_t board, rc_t from, rc_t to)
{
    int             i;
    int             rd, cd, turn;

    rd = dist(from, to, 0);
    cd = dist(from, to, 1);
    turn = CHE_O(board[from.r][from.c]);

    /* general check */
    if (board[to.r][to.c] && CHE_O(board[to.r][to.c]) == turn)
	return 0;

    /* individual check */
    switch (CHE_P(board[from.r][from.c])) {
    case 1:			/* 將 帥 */
	if (!(rd == 1 && cd == 0) &&
	    !(rd == 0 && cd == 1))
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r > 2) ||
	    (turn == chc_my && to.r < 7) ||
	    to.c < 3 || to.c > 5)
	    return 0;
	break;
    case 2:			/* 士 仕 */
	if (!(rd == 1 && cd == 1))
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r > 2) ||
	    (turn == chc_my && to.r < 7) ||
	    to.c < 3 || to.c > 5)
	    return 0;
	break;
    case 3:			/* 象 相 */
	if (!(rd == 2 && cd == 2))
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r > 4) ||
	    (turn == chc_my && to.r < 5))
	    return 0;
	/* 拐象腿 */
	if (board[CENTER(from.r, to.r)][CENTER(from.c, to.c)])
	    return 0;
	break;
    case 4:			/* 車 */
	if (!(rd > 0 && cd == 0) &&
	    !(rd == 0 && cd > 0))
	    return 0;
	if (between(board, from, to, rd == 0))
	    return 0;
	break;
    case 5:			/* 馬 傌 */
	if (!(rd == 2 && cd == 1) &&
	    !(rd == 1 && cd == 2))
	    return 0;
	/* 拐馬腳 */
	if (rd == 2) {
	    if (board[CENTER(from.r, to.r)][from.c])
		return 0;
	} else {
	    if (board[from.r][CENTER(from.c, to.c)])
		return 0;
	}
	break;
    case 6:			/* 包 炮 */
	if (!(rd > 0 && cd == 0) &&
	    !(rd == 0 && cd > 0))
	    return 0;
	i = between(board, from, to, rd == 0);
	if ((i > 1) ||
	    (i == 1 && !board[to.r][to.c]) ||
	    (i == 0 && board[to.r][to.c]))
	    return 0;
	break;
    case 7:			/* 卒 兵 */
	if (!(rd == 1 && cd == 0) &&
	    !(rd == 0 && cd == 1))
	    return 0;
	if (((turn == (chc_my ^ 1) && to.r < 5) ||
	     (turn == chc_my && to.r > 4)) &&
	    cd != 0)
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r < from.r) ||
	    (turn == chc_my && to.r > from.r))
	    return 0;
	break;
    }
    return 1;
}

static void 
findking(board_t board, int turn, rc_t * buf)
{
    int             i, r, c;

    r = (turn == (chc_my ^ 1)) ? 0 : 7;
    for (i = 0; i < 3; r++, i++)
	for (c = 3; c < 6; c++)
	    if (CHE_P(board[r][c]) == 1 &&
		CHE_O(board[r][c]) == turn) {
		buf->r = r, buf->c = c;
		return;
	    }
}

int 
chc_iskfk(board_t board)
{
    rc_t            from, to;

    findking(board, 0, &to);
    findking(board, 1, &from);
    if (from.c == to.c && between(board, from, to, 0) == 0)
	return 1;
    return 0;
}

int 
chc_ischeck(board_t board, int turn)
{
    rc_t            from, to;

    findking(board, turn, &to);
    for (from.r = 0; from.r < BRD_ROW; from.r++)
	for (from.c = 0; from.c < BRD_COL; from.c++)
	    if (board[from.r][from.c] &&
		CHE_O(board[from.r][from.c]) != turn)
		if (chc_canmove(board, from, to))
		    return 1;
    return 0;
}
