/* $Id$ */
#include <math.h>
#include "bbs.h"

enum Turn {
    BLK,
    RED 
};
enum Kind {
    KIND_K=1,
    KIND_A,
    KIND_E,
    KIND_R,
    KIND_H,
    KIND_C,
    KIND_P,
};
#define CENTER(a, b)	(((a) + (b)) >> 1)
#define CHC_TIMEOUT	300
#define CHC_LOG		"chc_log"	/* log file name */

typedef int     (*play_func_t) (int, chcusr_t *, chcusr_t *, board_t, board_t);

typedef struct drc_t {
    rc_t            from, to;
}               drc_t;

static rc_t	    chc_from, chc_to, chc_select, chc_cursor;
static int	    chc_lefttime;
static int	    chc_my; /* 我方執紅或黑, 0 黑, 1 紅. 觀棋=1 */
static int	    chc_turn, chc_selected, chc_firststep;
static char	    chc_mode;
static char	    chc_warnmsg[64];
static char	    chc_last_movestr[36]; /* color(7)+step(4*2)+normal(3)+color(7)+eat(2*2)+normal(3)+1=33 */
static char	    chc_ipass = 0, chc_hepass = 0;
/* chessfp is for logging the step */
static FILE        *chessfp = NULL;
static board_t	   *chc_bp;
static chc_act_list *act_list = NULL;
static const char * const turn_color[2]={BLACK_COLOR, RED_COLOR};


/* some constant variable definition */

static const char * const turn_str[2] = {"黑的", "紅的"};

static const char * const num_str[2][10] = {
    {"", "１", "２", "３", "４", "５", "６", "７", "８", "９"},
    {"", "一", "二", "三", "四", "五", "六", "七", "八", "九"},
};

static const char * const chess_str[2][8] = {
    /* 0     1     2     3     4     5     6     7 */
    {"  ", "將", "士", "象", "車", "馬", "包", "卒"},
    {"  ", "帥", "仕", "相", "車", "傌", "炮", "兵"}
};

static const char * const chess_brd[BRD_ROW * 2 - 1] = {
    /* 0   1   2   3   4   5   6   7   8 */
    "┌─┬─┬─┬─┬─┬─┬─┬─┐",	/* 0 */
    "│  │  │  │＼│／│  │  │  │",
    "├─┼─┼─┼─┼─┼─┼─┼─┤",	/* 1 */
    "│  │  │  │／│＼│  │  │  │",
    "├─┼─┼─┼─┼─┼─┼─┼─┤",	/* 2 */
    "│  │  │  │  │  │  │  │  │",
    "├─┼─┼─┼─┼─┼─┼─┼─┤",	/* 3 */
    "│  │  │  │  │  │  │  │  │",
    "├─┴─┴─┴─┴─┴─┴─┴─┤",	/* 4 */
    "│  楚    河          漢    界  │",
    "├─┬─┬─┬─┬─┬─┬─┬─┤",	/* 5 */
    "│  │  │  │  │  │  │  │  │",
    "├─┼─┼─┼─┼─┼─┼─┼─┤",	/* 6 */
    "│  │  │  │  │  │  │  │  │",
    "├─┼─┼─┼─┼─┼─┼─┼─┤",	/* 7 */
    "│  │  │  │＼│／│  │  │  │",
    "├─┼─┼─┼─┼─┼─┼─┼─┤",	/* 8 */
    "│  │  │  │／│＼│  │  │  │",
    "└─┴─┴─┴─┴─┴─┴─┴─┘"	/* 9 */
};

static char * const hint_str[] = {
    "  q      認輸離開",
    "  p      要求和棋",
    "方向鍵   移動遊標",
    "Enter    選擇/移動"
};

/*
 * Start of the network communication function.
 */
static int
chc_recvmove(int s)
{
    drc_t           buf;

    if (read(s, &buf, sizeof(buf)) != sizeof(buf))
	return 0;
    chc_from = buf.from, chc_to = buf.to;
    return 1;
}

static int
chc_sendmove(int s)
{
    drc_t           buf;

    buf.from = chc_from, buf.to = chc_to;
    if (write(s, &buf, sizeof(buf)) != sizeof(buf))
	return 0;
    return 1;
}

// XXX return value
// XXX die because of SIGPIPE !?

/* return false if your adversary is off-line */
static void
chc_broadcast(chc_act_list **head, board_t board){
    chc_act_list *p = *head;
    
    if (!p)
	return;
    
    if (!chc_sendmove(p->sock)) {
	/* do nothing */
    }

    while(p->next){
	if (!chc_sendmove(p->next->sock)) {
	    chc_act_list *tmp = p->next->next;
	    free(p->next);
	    p->next = tmp;
	}
	p = p->next;
    }
}

static int
chc_broadcast_recv(chc_act_list *act_list, board_t board){
    if (!chc_recvmove(act_list->sock))
	return 0;
    chc_broadcast(&act_list->next, board);
    return 1;
}

static int
chc_broadcast_send(chc_act_list *act_list, board_t board){
    chc_broadcast(&act_list, board);
    return 1;
}

/*
 * End of the network communication function.
 */

/*
 * Start of the drawing function.
 */
static void
chc_movecur(int r, int c)
{
    move(r * 2 + 3, c * 4 + 4);
}

static char *
getstep(board_t board, rc_t *from, rc_t *to, char buf[])
{
    int             turn, fc, tc;
    char           *dir;
    int		    twin = 0, twin_r = 0;
    int		    len = 0;

    turn = CHE_O(board[from->r][from->c]);
    if(CHE_P(board[from->r][from->c] != KIND_P)) { // TODO 目前不管兵卒前後
	int i;
	for(i=0;i<10;i++)
	    if(board[i][from->c]==board[from->r][from->c]) {
		if(i!=from->r) {
		    twin=1;
		    twin_r=i;
		}
	    }
    }
    fc = (turn == (chc_my ^ 1) ? from->c + 1 : 9 - from->c);
    tc = (turn == (chc_my ^ 1) ? to->c + 1 : 9 - to->c);
    if (from->r == to->r)
	dir = "平";
    else {
	if (from->c == to->c)
	    tc = from->r - to->r;
	if (tc < 0)
	    tc = -tc;

	if ((turn == (chc_my ^ 1) && to->r > from->r) ||
	    (turn == chc_my && to->r < from->r))
	    dir = "進";
	else
	    dir = "退";
    }


    len=sprintf(buf, "%s", turn_color[turn]);
    /* 傌二|前傌 */
    if(twin) {
	len+=sprintf(buf+len, "%s%s",
		((from->r>twin_r)==(turn==(chc_my^1)))?"前":"後",
		chess_str[turn][CHE_P(board[from->r][from->c])]);
    } else {
	len+=sprintf(buf+len, "%s%s",
		chess_str[turn][CHE_P(board[from->r][from->c])],
		num_str[turn][fc]);
    }
    /* 進三 */
    len+=sprintf(buf+len, "%s%s\033[m", dir, num_str[turn][tc]);
    /* ：象 */
    if(board[to->r][to->c]) {
	len+=sprintf(buf+len,"：%s%s\033[m",
		turn_color[turn^1],
		chess_str[turn^1][CHE_P(board[to->r][to->c])]);
    }
    return buf;
}

static void
showstep(board_t board)
{
    outs(chc_last_movestr);
}

static void
chc_drawline(board_t board, chcusr_t *user1, chcusr_t *user2, int line)
{
    int             i, j;

    move(line, 0);
    clrtoeol();
    if (line == 0) {
	prints("\033[1;46m   象棋對戰   \033[45m%30s VS %-20s%10s\033[m",
	       user1->userid, user2->userid, chc_mode & CHC_WATCH ? "[觀棋模式]" : "");
    } else if (line >= 3 && line <= 21) {
	outs("   ");
	for (i = 0; i < 9; i++) {
	    j = board[RTL(line)][i];
	    if ((line & 1) == 1 && j) {
		if (chc_selected &&
		    chc_select.r == RTL(line) && chc_select.c == i) {
		    prints("%s%s\033[m",
			   CHE_O(j) == BLK ? BLACK_REVERSE : RED_REVERSE,
			   chess_str[CHE_O(j)][CHE_P(j)]);
		}
		else {
		    prints("%s%s\033[m",
			   turn_color[CHE_O(j)],
			   chess_str[CHE_O(j)][CHE_P(j)]);
		}
	    } else
		prints("%c%c", chess_brd[line - 3][i * 4],
		       chess_brd[line - 3][i * 4 + 1]);
	    if (i != 8)
		prints("%c%c", chess_brd[line - 3][i * 4 + 2],
		       chess_brd[line - 3][i * 4 + 3]);
	}
	outs("        ");

	if (line >= 3 && line < 3 + (int)dim(hint_str)) {
	    outs(hint_str[line - 3]);
	} else if (line == SIDE_ROW) {
	    prints("\033[1m你是%s%s\033[m",
		    turn_color[chc_my],
		    turn_str[chc_my]);
	} else if (line == TURN_ROW) {
	    prints("%s%s\033[m",
		   TURN_COLOR,
		   chc_my == chc_turn ? "輪到你下棋了" : "等待對方下棋");
	} else if (line == STEP_ROW && !chc_firststep) {
	    showstep(board);
	} else if (line == TIME_ROW) {
	    prints("剩餘時間 %d:%02d", chc_lefttime / 60, chc_lefttime % 60);
	} else if (line == WARN_ROW) {
	    outs(chc_warnmsg);
	} else if (line == MYWIN_ROW) {
	    prints("\033[1;33m%12.12s    "
		   "\033[1;31m%2d\033[37m勝 "
		   "\033[34m%2d\033[37m敗 "
		   "\033[36m%2d\033[37m和\033[m",
		   user1->userid,
		   user1->win, user1->lose - 1, user1->tie);
	} else if (line == HISWIN_ROW) {
	    prints("\033[1;33m%12.12s    "
		   "\033[1;31m%2d\033[37m勝 "
		   "\033[34m%2d\033[37m敗 "
		   "\033[36m%2d\033[37m和\033[m",
		   user2->userid,
		   user2->win, user2->lose - 1, user2->tie);
	}
    } else if (line == 2 || line == 22) {
	outs("   ");
	if (line == 2)
	    for (i = 1; i <= 9; i++)
		prints("%s  ", num_str[0][i]);
	else
	    for (i = 9; i >= 1; i--)
		prints("%s  ", num_str[1][i]);
    }
}

static void
chc_redraw(chcusr_t *user1, chcusr_t *user2, board_t board)
{
    int             i;
    for (i = 0; i <= 22; i++)
	chc_drawline(board, user1, user2, i);
}
/*
 * End of the drawing function.
 */


/*
 * Start of the log function.
 */
int
chc_log_open(chcusr_t *user1, chcusr_t *user2, char *file)
{
    char buf[128];
    if ((chessfp = fopen(file, "w")) == NULL)
	return -1;
    if(chc_my == RED)
	sprintf(buf, "%s V.S. %s\n", user1->userid, user2->userid);
    else
	sprintf(buf, "%s V.S. %s\n", user2->userid, user1->userid);
    fputs(buf, chessfp);
    return 0;
}

void
chc_log_close(void)
{
    if (chessfp)
	fclose(chessfp);
}

int
chc_log(char *desc)
{
    if (chessfp)
	return fputs(desc, chessfp);
    return -1;
}

int
chc_log_step(board_t board, rc_t *from, rc_t *to)
{
    char buf[80];
    sprintf(buf, "  %s\n", chc_last_movestr);
    return chc_log(buf);
}

static int
#if defined(__linux__)
chc_filter(const struct dirent *dir)
#else
chc_filter(struct dirent *dir)
#endif
{
    if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 )
	return 0;
    return strstr(dir->d_name, ".poem") != NULL;
}

int
chc_log_poem(void)
{
    struct dirent **namelist;
    int n;

    // TODO use readdir(), don't use lots of memory
    n = scandir(BBSHOME"/etc/chess", &namelist, chc_filter, alphasort);
    if (n < 0)
	perror("scandir");
    else {
	char buf[80];
	FILE *fp;
	sprintf(buf, BBSHOME"/etc/chess/%s", namelist[rand() % n]->d_name);
	if ((fp = fopen(buf, "r")) == NULL)
	    return -1;

	while(fgets(buf, sizeof(buf), fp) != NULL)
	    chc_log(buf);
	while(n--)
	    free(namelist[n]);
	free(namelist);
	fclose(fp);
    }
    return 0;
}
/*
 * End of the log function.
 */


/*
 * Start of the rule function.
 */

static void
chc_init_board(board_t board)
{
    memset(board, 0, sizeof(board_t));
    board[0][4] = CHE(KIND_K, chc_my ^ 1);	/* 將 */
    board[0][3] = board[0][5] = CHE(KIND_A, chc_my ^ 1);	/* 士 */
    board[0][2] = board[0][6] = CHE(KIND_E, chc_my ^ 1);	/* 象 */
    board[0][0] = board[0][8] = CHE(KIND_R, chc_my ^ 1);	/* 車 */
    board[0][1] = board[0][7] = CHE(KIND_H, chc_my ^ 1);	/* 馬 */
    board[2][1] = board[2][7] = CHE(KIND_C, chc_my ^ 1);	/* 包 */
    board[3][0] = board[3][2] = board[3][4] =
	board[3][6] = board[3][8] = CHE(KIND_P, chc_my ^ 1);	/* 卒 */

    board[9][4] = CHE(KIND_K, chc_my);	/* 帥 */
    board[9][3] = board[9][5] = CHE(KIND_A, chc_my);	/* 仕 */
    board[9][2] = board[9][6] = CHE(KIND_E, chc_my);	/* 相 */
    board[9][0] = board[9][8] = CHE(KIND_R, chc_my);	/* 車 */
    board[9][1] = board[9][7] = CHE(KIND_H, chc_my);	/* 傌 */
    board[7][1] = board[7][7] = CHE(KIND_C, chc_my);	/* 炮 */
    board[6][0] = board[6][2] = board[6][4] =
	board[6][6] = board[6][8] = CHE(KIND_P, chc_my);	/* 兵 */
}

static void
chc_movechess(board_t board)
{
    board[chc_to.r][chc_to.c] = board[chc_from.r][chc_from.c];
    board[chc_from.r][chc_from.c] = 0;
}

/* 求兩座標行或列(rowcol)的距離 */
static int
dist(rc_t from, rc_t to, int rowcol)
{
    int             d;

    d = rowcol ? from.c - to.c : from.r - to.r;
    return d > 0 ? d : -d;
}

/* 兩座標(行或列rowcol)中間有幾顆棋子 */
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

static int
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
    case KIND_K:		/* 將 帥 */
	if (!(rd == 1 && cd == 0) &&
	    !(rd == 0 && cd == 1))
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r > 2) ||
	    (turn == chc_my && to.r < 7) ||
	    to.c < 3 || to.c > 5)
	    return 0;
	break;
    case KIND_A:		/* 士 仕 */
	if (!(rd == 1 && cd == 1))
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r > 2) ||
	    (turn == chc_my && to.r < 7) ||
	    to.c < 3 || to.c > 5)
	    return 0;
	break;
    case KIND_E:		/* 象 相 */
	if (!(rd == 2 && cd == 2))
	    return 0;
	if ((turn == (chc_my ^ 1) && to.r > 4) ||
	    (turn == chc_my && to.r < 5))
	    return 0;
	/* 拐象腿 */
	if (board[CENTER(from.r, to.r)][CENTER(from.c, to.c)])
	    return 0;
	break;
    case KIND_R:		/* 車 */
	if (!(rd > 0 && cd == 0) &&
	    !(rd == 0 && cd > 0))
	    return 0;
	if (between(board, from, to, rd == 0))
	    return 0;
	break;
    case KIND_H:		/* 馬 傌 */
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
    case KIND_C:		/* 包 炮 */
	if (!(rd > 0 && cd == 0) &&
	    !(rd == 0 && cd > 0))
	    return 0;
	i = between(board, from, to, rd == 0);
	if ((i > 1) ||
	    (i == 1 && !board[to.r][to.c]) ||
	    (i == 0 && board[to.r][to.c]))
	    return 0;
	break;
    case KIND_P:		/* 卒 兵 */
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

/* 找 turn's king 的座標 */
static void
findking(board_t board, int turn, rc_t * buf)
{
    int             i, r, c;

    r = (turn == (chc_my ^ 1)) ? 0 : 7;
    for (i = 0; i < 3; r++, i++)
	for (c = 3; c < 6; c++)
	    if (CHE_P(board[r][c]) == KIND_K &&
		CHE_O(board[r][c]) == turn) {
		buf->r = r, buf->c = c;
		return;
	    }
}

static int
chc_iskfk(board_t board)
{
    rc_t            from, to;

    findking(board, BLK, &to);
    findking(board, RED, &from);
    if (from.c == to.c && between(board, from, to, 0) == 0)
	return 1;
    return 0;
}

static int
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

/*
 * End of the rule function.
 */

static void
chcusr_put(userec_t *userec, chcusr_t *user)
{
    userec->chc_win = user->win;
    userec->chc_lose = user->lose;
    userec->chc_tie = user->tie;
    userec->chess_elo_rating = user->rating;
}

static void
chcusr_get(userec_t *userec, chcusr_t *user)
{
    strlcpy(user->userid, userec->userid, sizeof(user->userid));
    user->win = userec->chc_win;
    user->lose = userec->chc_lose;
    user->tie = userec->chc_tie;
    user->rating = userec->chess_elo_rating;
    if(user->rating == 0)
	user->rating = 1500; /* ELO initial value */
    user->orig_rating = user->rating;
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

	    /* to make him break out igetch() */
	    chc_from.r = -2;
	    chc_broadcast_send(act_list, board);
	}
	chc_drawline(board, user1, user2, TIME_ROW);
	move(1, 0);
	oflush();
	switch (igetch()) {
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
		    /* 1.如果在觀棋 且棋局是別人在打譜 且輪到你 或*/
		    if ( ((chc_mode & CHC_WATCH) && (chc_mode & CHC_WATCH_PERSONAL)) ||
			    /* 2.自己在打譜 */
			    (chc_mode & CHC_PERSONAL) ||
			    ((chc_mode & CHC_WATCH) && !chc_turn)
			  )
			; // do nothing
		    else {
			chc_from.r = 9 - chc_from.r, chc_from.c = 8 - chc_from.c;
			chc_to.r = 9 - chc_to.r, chc_to.c = 8 - chc_to.c;
		    }
		    chc_cursor = chc_to;
		    if (CHE_P(board[chc_to.r][chc_to.c]) == KIND_K)
			endgame = 2;
		    endturn = 1;
		    chc_hepass = 0;
		    getstep(board, &chc_from, &chc_to, chc_last_movestr);
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
	ch = igetch();
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
		    if (CHE_P(board[chc_cursor.r][chc_cursor.c]) == KIND_K)
			endgame = 1;
		    chc_from = chc_select;
		    chc_to = chc_cursor;
		    if (!endgame) {
			memcpy(tmpbrd, board, sizeof(board_t));
			chc_movechess(tmpbrd);
		    }
		    if (endgame || !chc_iskfk(tmpbrd)) {
			getstep(board, &chc_from, &chc_to, chc_last_movestr);
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

/*
 * ELO rating system
 * see http://www.wordiq.com/definition/ELO_rating_system
 */
static void
count_chess_elo_rating(chcusr_t *user1, chcusr_t *user2, double myres)
{
    double k;
    double exp_res;

    if(user1->rating < 1800)
	k = 30;
    else if(user1->rating < 2000)
	k = 25;
    else if(user1->rating < 2200)
	k = 20;
    else if(user1->rating < 2400)
	k = 15;
    else
	k = 10;

    exp_res = 1.0/(1.0 + pow(10.0, (user2->rating-user1->rating)/400.0));
    user1->rating += (int)floor(k*(myres-exp_res)+0.5);
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
	user1->rating = user1->orig_rating;
	user1->lose--;
	if (endgame == 1) {
	    strlcpy(chc_warnmsg, "對方認輸了!", sizeof(chc_warnmsg));
	    count_chess_elo_rating(user1, user2, 1.0);
	    user1->win++;
	    currutmp->chc_win++;
	} else if (endgame == 2) {
	    strlcpy(chc_warnmsg, "你認輸了!", sizeof(chc_warnmsg));
	    count_chess_elo_rating(user1, user2, 0.0);
	    user1->lose++;
	    currutmp->chc_lose++;
	} else {
	    strlcpy(chc_warnmsg, "和棋", sizeof(chc_warnmsg));
	    count_chess_elo_rating(user1, user2, 0.5);
	    user1->tie++;
	    currutmp->chc_tie++;
	}
	currutmp->chess_elo_rating = user1->rating;
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
	sprintf(buf, "%s勝\n", (chc_my==RED) == (endgame == 1) ? "紅" : "黑");
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
    chc_act_list *tmp;
#if _TO_SYNC_
    sigset_t mask;
#endif

    if (!(currstat & CHC))
	return;
    for(tmp = act_list; tmp->next != NULL; tmp = tmp->next);
    tmp->next = (chc_act_list *)malloc(sizeof(chc_act_list));
    tmp->next->sock = establish_talk_connection(&SHM->uinfo[currutmp->destuip]);
    if (tmp->next->sock < 0) {
       free(tmp->next);
       tmp->next = NULL;
       return;
    }

    tmp = tmp->next;
    tmp->next = NULL;

#if _TO_SYNC_
    /* 借用 SIGALRM */
    sigfillset(&mask);
    sigdelset(&mask, SIGALRM);
    sigsuspend(&mask);
#endif

    /* what if the spectator get off-line intentionally !? (SIGPIPE) */
    write(tmp->sock, chc_bp, sizeof(board_t));
    write(tmp->sock, &chc_my, sizeof(chc_my));
    write(tmp->sock, &chc_turn, sizeof(chc_turn));
    write(tmp->sock, &currutmp->turn, sizeof(currutmp->turn));
    write(tmp->sock, &chc_firststep, sizeof(chc_firststep));
    write(tmp->sock, &chc_mode, sizeof(chc_mode));
}

static int 
chc_init(int s, chcusr_t *user1, chcusr_t *user2, board_t board, play_func_t play_func[2])
{
    userinfo_t     *my = currutmp;

    if (chc_mode & CHC_WATCH)
	setutmpmode(CHESSWATCHING);
    else
	setutmpmode(CHC);
    clear();
    chc_warnmsg[0] = 0;

    /* 從不同來源初始化各個變數 */
    if (!(chc_mode & CHC_WATCH)) {
	if (chc_mode & CHC_PERSONAL)
	    chc_my = RED;
	else
	    chc_my = my->turn;
	chc_firststep = 1;
	chc_init_board(board);
	chc_cursor.r = 9, chc_cursor.c = 0;
    }
    else {
	char mode;
	userinfo_t *uin = &SHM->uinfo[currutmp->destuip];
	if (uin == NULL)
	    return -1;
#if _TO_SYNC_
	// choose one signal execpt SIGUSR1
	kill(uin->pid, SIGALRM);
#endif
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

    if (!(chc_mode & CHC_WATCH)) {
	Signal(SIGUSR1, chc_watch_request);
    }

//    if (my->turn && !(chc_mode & CHC_WATCH))
//	chc_broadcast_recv(act_list, board);

    user1->lose++;
    count_chess_elo_rating(user1, user2, 0.0);

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

    return 0;
}

/* 象棋功能進入點:
 * chc_main: 對奕
 * chc_personal: 打譜
 * chc_watch: 觀棋
 * talk.c: 對奕
 */
void
chc(int s, int mode)
{
    chcusr_t	    user1, user2;
    play_func_t     play_func[2];
    board_t	    board;
    char	    mode0 = currutmp->mode;
    char	    file[80];

    /* CHC_WATCH is unstable!! */
    if (mode & CHC_WATCH) {
	vmsg("觀棋功\能不穩定，暫時停止使用。");
	return;
    }

    chc_mode = mode;

    if (!(chc_mode & CHC_WATCH))
	Signal(SIGUSR1, SIG_IGN);

    chc_bp = &board;
    if (chc_init(s, &user1, &user2, board, play_func) < 0)
	return;
    
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
    if (chc_my == RED)
	pressanykey();

    currutmp->mode = mode0;

    if (getans("是否將棋譜寄回信箱？[N/y]") == 'y') {
	char title[80];
	if(chc_my == RED)
	    sprintf(title, "%s V.S. %s", user1.userid, user2.userid);
	else
	    sprintf(title, "%s V.S. %s", user2.userid, user1.userid);
	chc_log("\n--\n\n");
	chc_log_poem();
	chc_log_close();
	mail_id(cuser.userid, title, file, "[楚河漢界]");
    }
    else
	chc_log_close();

    if (!(chc_mode & CHC_WATCH))
	Signal(SIGUSR1, talk_request);
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

    if (uin->uid == currutmp->uid || uin->mode != CHC)
	return -1;

    if (getans("是否進行觀棋? [N/y]") != 'y')
	return 0;

    if ((sock = make_connection_to_somebody(uin, 10)) < 0) {
	vmsg("無法建立連線");
	return -1;
    }
#if defined(Solaris) && __OS_MAJOR_VERSION__ == 5 && __OS_MINOR_VERSION__ < 7
    msgsock = accept(sock, (struct sockaddr *) 0, 0);
#else
    msgsock = accept(sock, (struct sockaddr *) 0, (socklen_t *) 0);
#endif
    close(sock);
    if (msgsock < 0)
	return -1;

    strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));
    chc(msgsock, CHC_WATCH);
    close(msgsock);
    return 0;
}
