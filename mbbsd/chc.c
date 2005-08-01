/* $Id$ */
#include "bbs.h"

extern const double elo_exp_tab[1000];

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

#define PHOTO_LINE      15
#define PHOTO_COLUMN    (256 + 25)

typedef int     (*play_func_t) (int, const chcusr_t *, const chcusr_t *, board_t, board_t);

typedef struct drc_t {
    rc_t            from, to;
}               drc_t;

struct CHCData {
    rc_t    from, to, select, cursor;

    /* 計時用, [0] = mine, [1] = his */
    int	    lefttime[2];
    int     lefthand[2]; /* 限時限步時用, = 0 表為自由時間或非限時限步模式 */

    int	    my; /* 我方執紅或黑, 0 黑, 1 紅. 觀棋=1 */
    int	    turn, selected, firststep;
    char    mode;
    char    warnmsg[64];
    /* color(7)+step(4*2)+normal(3)+color(7)+eat(2*2)+normal(3)+1=33 */
    char    last_movestr[36];
    char    ipass, hepass;
    /* chessfp is for logging the step */
    FILE      *chessfp;
    board_t   *bp;
    chc_act_list *act_list;
    char      *photo;
};
typedef struct {
    int     limit_hand;
    int     limit_time;
    int     free_time;
    enum {
	CHCTIME_ORIGINAL, CHCTIME_FREE, CHCTIME_LIMIT
    } time_mode;
} CHCTimeLimit;

static struct CHCData *chcd;
static CHCTimeLimit *timelimit;

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
    chcd->from = buf.from, chcd->to = buf.to;
    return 1;
}

static int
chc_sendmove(int s)
{
    drc_t           buf;

    buf.from = chcd->from, buf.to = chcd->to;
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
    void (*orig_handler)(int);
    
    if (!p)
	return;
    
    orig_handler = Signal(SIGPIPE, SIG_IGN);
    if (!chc_sendmove(p->sock)) {
	/* do nothing */
    }

    while(p->next){
	if (!chc_sendmove(p->next->sock)) {
	    chc_act_list *tmp = p->next->next;
	    free(p->next);
	    p->next = tmp;
	} else
	    p = p->next;
    }
    Signal(SIGPIPE, orig_handler);
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
getstep(board_t board, const rc_t *from, const rc_t *to, char buf[])
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
    fc = (turn == (chcd->my ^ 1) ? from->c + 1 : 9 - from->c);
    tc = (turn == (chcd->my ^ 1) ? to->c + 1 : 9 - to->c);
    if (from->r == to->r)
	dir = "平";
    else {
	if (from->c == to->c)
	    tc = from->r - to->r;
	if (tc < 0)
	    tc = -tc;

	if ((turn == (chcd->my ^ 1) && to->r > from->r) ||
	    (turn == chcd->my && to->r < from->r))
	    dir = "進";
	else
	    dir = "退";
    }


    len=sprintf(buf, "%s", turn_color[turn]);
    /* 傌二|前傌 */
    if(twin) {
	len+=sprintf(buf+len, "%s%s",
		((from->r>twin_r)==(turn==(chcd->my^1)))?"前":"後",
		chess_str[turn][CHE_P(board[from->r][from->c])]);
    } else {
	len+=sprintf(buf+len, "%s%s",
		chess_str[turn][CHE_P(board[from->r][from->c])],
		num_str[turn][fc]);
    }
    /* 進三 */
    len+=sprintf(buf+len, "%s%s" ANSI_RESET, dir, num_str[turn][tc]);
    /* ：象 */
    if(board[to->r][to->c]) {
	len+=sprintf(buf+len,"：%s%s" ANSI_RESET,
		turn_color[turn^1],
		chess_str[turn^1][CHE_P(board[to->r][to->c])]);
    }
    return buf;
}

static void
showstep(board_t board)
{
    outs(chcd->last_movestr);
}

static void
chc_drawline(board_t board, const chcusr_t *user1, const chcusr_t *user2, int line)
{
    int             i, j;

    if (line == TURN_ROW)
	line = chcd->photo ? PHOTO_TURN_ROW : REAL_TURN_ROW;
    else if (line == TIME_ROW) {
	chc_drawline(board, user1, user2,
		chcd->photo ? PHOTO_TIME_ROW1 : REAL_TIME_ROW1);
	line = chcd->photo ? PHOTO_TIME_ROW2 : REAL_TIME_ROW2;
    }

    move(line, 0);
    clrtoeol();
    if (line == 0) {
	prints(ANSI_COLOR(1;46) "   象棋對戰   " ANSI_COLOR(45) "%30s VS %-20s%10s" ANSI_RESET,
	       user1->userid, user2->userid, chcd->mode & CHC_WATCH ? "[觀棋模式]" : "");
    } else if (line >= 3 && line <= 21) {
	outs("   ");
	for (i = 0; i < 9; i++) {
	    j = board[RTL(line)][i];
	    if ((line & 1) == 1 && j) {
		if (chcd->selected &&
		    chcd->select.r == RTL(line) && chcd->select.c == i) {
		    prints("%s%s" ANSI_RESET,
			   CHE_O(j) == BLK ? BLACK_REVERSE : RED_REVERSE,
			   chess_str[CHE_O(j)][CHE_P(j)]);
		}
		else {
		    prints("%s%s" ANSI_RESET,
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

	if (chcd->photo) {
	    outs(" ");
	    if (line >= 3 && line < 3 + PHOTO_LINE)
		outs(chcd->photo + (line - 3) * PHOTO_COLUMN);
	    else if (line == PHOTO_TURN_ROW)
		prints("       %s%s" ANSI_RESET,
			TURN_COLOR,
			chcd->my == chcd->turn ? "輪到你下棋了" : "等待對方下棋");
	    else if (line == PHOTO_TIME_ROW1) {
		if (chcd->lefthand[0])
		    prints("       我方剩餘時間 %d:%02d / %2d 步",
			    chcd->lefttime[0] / 60, chcd->lefttime[0] % 60,
			    chcd->lefthand[0]);
		else
		    prints("       我方剩餘時間 %d:%02d",
			    chcd->lefttime[0] / 60, chcd->lefttime[0] % 60);
	    } else if (line == PHOTO_TIME_ROW2) {
		if (chcd->lefthand[1])
		    prints("       對方剩餘時間 %d:%02d / %2d 步",
			    chcd->lefttime[1] / 60, chcd->lefttime[1] % 60,
			    chcd->lefthand[1]);
		else
		    prints("       對方剩餘時間 %d:%02d",
			    chcd->lefttime[1] / 60, chcd->lefttime[1] % 60);
	    }
	} else {
	    outs("        ");
	    if (line >= 3 && line < 3 + (int)dim(hint_str)) {
		outs(hint_str[line - 3]);
	    } else if (line == SIDE_ROW) {
		prints(ANSI_COLOR(1) "你是%s%s" ANSI_RESET,
			turn_color[chcd->my],
			turn_str[chcd->my]);
	    } else if (line == REAL_TURN_ROW) {
		prints("%s%s" ANSI_RESET,
			TURN_COLOR,
			chcd->my == chcd->turn ? "輪到你下棋了" : "等待對方下棋");
	    } else if (line == STEP_ROW && !chcd->firststep) {
		showstep(board);
	    } else if (line == REAL_TIME_ROW1) {
		if (chcd->lefthand[0])
		    prints("我方剩餘時間 %d:%02d / %2d 步",
			    chcd->lefttime[0] / 60, chcd->lefttime[0] % 60,
			    chcd->lefthand[0]);
		else
		    prints("我方剩餘時間 %d:%02d",
			    chcd->lefttime[0] / 60, chcd->lefttime[0] % 60);
	    } else if (line == REAL_TIME_ROW2) {
		if (chcd->lefthand[1])
		    prints("對方剩餘時間 %d:%02d / %2d 步",
			    chcd->lefttime[1] / 60, chcd->lefttime[1] % 60,
			    chcd->lefthand[1]);
		else
		    prints("對方剩餘時間 %d:%02d",
			    chcd->lefttime[1] / 60, chcd->lefttime[1] % 60);
	    } else if (line == WARN_ROW) {
		outs(chcd->warnmsg);
	    } else if (line == MYWIN_ROW) {
		prints(ANSI_COLOR(1;33) "%12.12s    "
			ANSI_COLOR(1;31) "%2d" ANSI_COLOR(37) "勝 "
			ANSI_COLOR(34) "%2d" ANSI_COLOR(37) "敗 "
			ANSI_COLOR(36) "%2d" ANSI_COLOR(37) "和" ANSI_RESET,
			user1->userid,
			user1->win, user1->lose - 1, user1->tie);
	    } else if (line == HISWIN_ROW) {
		prints(ANSI_COLOR(1;33) "%12.12s    "
			ANSI_COLOR(1;31) "%2d" ANSI_COLOR(37) "勝 "
			ANSI_COLOR(34) "%2d" ANSI_COLOR(37) "敗 "
			ANSI_COLOR(36) "%2d" ANSI_COLOR(37) "和" ANSI_RESET,
			user2->userid,
			user2->win, user2->lose - 1, user2->tie);
	    }
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
chc_redraw(const chcusr_t *user1, const chcusr_t *user2, board_t board)
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
chc_log_open(const chcusr_t *user1, const chcusr_t *user2, const char *file)
{
    char buf[128];
    if ((chcd->chessfp = fopen(file, "w")) == NULL)
	return -1;
    if(chcd->my == RED)
	sprintf(buf, "%s V.S. %s\n", user1->userid, user2->userid);
    else
	sprintf(buf, "%s V.S. %s\n", user2->userid, user1->userid);
    fputs(buf, chcd->chessfp);
    return 0;
}

void
chc_log_close(void)
{
    if (chcd->chessfp) {
	fclose(chcd->chessfp);
	chcd->chessfp=NULL;
    }
}

int
chc_log(const char *desc)
{
    if (chcd->chessfp)
	return fputs(desc, chcd->chessfp);
    return -1;
}

int
chc_log_step(board_t board, const rc_t *from, const rc_t *to)
{
    char buf[80];
    sprintf(buf, "  %s\n", chcd->last_movestr);
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
	sprintf(buf, BBSHOME"/etc/chess/%s", namelist[random() % n]->d_name);
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
    board[0][4] = CHE(KIND_K, chcd->my ^ 1);	/* 將 */
    board[0][3] = board[0][5] = CHE(KIND_A, chcd->my ^ 1);	/* 士 */
    board[0][2] = board[0][6] = CHE(KIND_E, chcd->my ^ 1);	/* 象 */
    board[0][0] = board[0][8] = CHE(KIND_R, chcd->my ^ 1);	/* 車 */
    board[0][1] = board[0][7] = CHE(KIND_H, chcd->my ^ 1);	/* 馬 */
    board[2][1] = board[2][7] = CHE(KIND_C, chcd->my ^ 1);	/* 包 */
    board[3][0] = board[3][2] = board[3][4] =
	board[3][6] = board[3][8] = CHE(KIND_P, chcd->my ^ 1);	/* 卒 */

    board[9][4] = CHE(KIND_K, chcd->my);	/* 帥 */
    board[9][3] = board[9][5] = CHE(KIND_A, chcd->my);	/* 仕 */
    board[9][2] = board[9][6] = CHE(KIND_E, chcd->my);	/* 相 */
    board[9][0] = board[9][8] = CHE(KIND_R, chcd->my);	/* 車 */
    board[9][1] = board[9][7] = CHE(KIND_H, chcd->my);	/* 傌 */
    board[7][1] = board[7][7] = CHE(KIND_C, chcd->my);	/* 炮 */
    board[6][0] = board[6][2] = board[6][4] =
	board[6][6] = board[6][8] = CHE(KIND_P, chcd->my);	/* 兵 */
}

static void
chc_movechess(board_t board)
{
    board[chcd->to.r][chcd->to.c] = board[chcd->from.r][chcd->from.c];
    board[chcd->from.r][chcd->from.c] = 0;
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
	if ((turn == (chcd->my ^ 1) && to.r > 2) ||
	    (turn == chcd->my && to.r < 7) ||
	    to.c < 3 || to.c > 5)
	    return 0;
	break;
    case KIND_A:		/* 士 仕 */
	if (!(rd == 1 && cd == 1))
	    return 0;
	if ((turn == (chcd->my ^ 1) && to.r > 2) ||
	    (turn == chcd->my && to.r < 7) ||
	    to.c < 3 || to.c > 5)
	    return 0;
	break;
    case KIND_E:		/* 象 相 */
	if (!(rd == 2 && cd == 2))
	    return 0;
	if ((turn == (chcd->my ^ 1) && to.r > 4) ||
	    (turn == chcd->my && to.r < 5))
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
	if (((turn == (chcd->my ^ 1) && to.r < 5) ||
	     (turn == chcd->my && to.r > 4)) &&
	    cd != 0)
	    return 0;
	if ((turn == (chcd->my ^ 1) && to.r < from.r) ||
	    (turn == chcd->my && to.r > from.r))
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

    r = (turn == (chcd->my ^ 1)) ? 0 : 7;
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

static int
time_countdown(int who, int length)
{
    chcd->lefttime[who] -= length;

    if (!timelimit) /* traditional mode, only left time is considered */
	return chcd->lefttime[who] < 0;

    if (chcd->lefttime[who] < 0) { /* only allowed when in free time */
	if (chcd->lefthand[who])
	    return 1;
	chcd->lefttime[who] = timelimit->limit_time;
	chcd->lefthand[who] = timelimit->limit_hand;
	return 0;
    }

    return 0;
}

static void
step_made(int who)
{
    if (!timelimit)
	chcd->lefttime[who] = CHC_TIMEOUT;
    else if (
	    (chcd->lefthand[who] && (--(chcd->lefthand[who]) == 0))
	    ||
	    (chcd->lefthand[who] == 0 && chcd->lefttime[who] <= 0)
	    ) {
	chcd->lefthand[who] = timelimit->limit_hand;
	chcd->lefttime[who] = timelimit->limit_time;
    }
}

/*
 * End of the rule function.
 */

static void
chcusr_put(userec_t *userec, const chcusr_t *user)
{
    userec->chc_win = user->win;
    userec->chc_lose = user->lose;
    userec->chc_tie = user->tie;
    userec->chess_elo_rating = user->rating;
}

static void
chcusr_get(const userec_t *userec, chcusr_t *user)
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
hisplay(int s, const chcusr_t *user1, const chcusr_t *user2, board_t board, board_t tmpbrd)
{
    int             last_time;
    int             endgame = 0, endturn = 0;

    last_time = now;
    while (!endturn) {
	if (time_countdown(1, now - last_time)) {
	    chcd->lefttime[1] = 0;

	    /* to make him break out igetch() */
	    chcd->from.r = -2;
	    chc_broadcast_send(chcd->act_list, board);
	}
	last_time = now;
	chc_drawline(board, user1, user2, TIME_ROW);
	move(1, 0);
	oflush();
	switch (igetch()) {
	case 'q':
	    endgame = 2;
	    endturn = 1;
	    break;
	case 'p':
	    if (chcd->hepass) {
		chcd->from.r = -1;
		chc_broadcast_send(chcd->act_list, board);
		endgame = 3;
		endturn = 1;
	    }
	    break;
	case I_OTHERDATA:
	    if (!chc_broadcast_recv(chcd->act_list, board)) {	/* disconnect */
		endturn = 1;
		endgame = 1;
	    } else {
		if (chcd->from.r == -1) {
		    chcd->hepass = 1;
		    strlcpy(chcd->warnmsg, ANSI_COLOR(1;33) "要求和局!" ANSI_RESET, sizeof(chcd->warnmsg));
		    chc_drawline(board, user1, user2, WARN_ROW);
		} else {
		    /* 座標變換
		     *   (CHC_WATCH_PERSONAL 設定時
		     *    表觀棋者看的棋局為單人打譜的棋局)
		     *   棋盤需倒置的清況才要轉換
		     */
		    /* 1.如果在觀棋 且棋局是別人在打譜 且輪到你 或*/
		    if ( ((chcd->mode & CHC_WATCH) && (chcd->mode & CHC_WATCH_PERSONAL)) ||
			    /* 2.自己在打譜 */
			    (chcd->mode & CHC_PERSONAL) ||
			    ((chcd->mode & CHC_WATCH) && !chcd->turn)
			  )
			; // do nothing
		    else {
			chcd->from.r = 9 - chcd->from.r, chcd->from.c = 8 - chcd->from.c;
			chcd->to.r = 9 - chcd->to.r, chcd->to.c = 8 - chcd->to.c;
		    }
		    chcd->cursor = chcd->to;
		    if (CHE_P(board[chcd->to.r][chcd->to.c]) == KIND_K)
			endgame = 2;
		    endturn = 1;
		    chcd->hepass = 0;
		    getstep(board, &chcd->from, &chcd->to, chcd->last_movestr);
		    chc_drawline(board, user1, user2, STEP_ROW);
		    chc_log_step(board, &chcd->from, &chcd->to);
		    chc_movechess(board);
		    step_made(1);
		    chc_drawline(board, user1, user2, LTR(chcd->from.r));
		    chc_drawline(board, user1, user2, LTR(chcd->to.r));
		}
	    }
	    break;
	}
    }
    time_countdown(1, now - last_time);
    return endgame;
}

static int
myplay(int s, const chcusr_t *user1, const chcusr_t *user2, board_t board, board_t tmpbrd)
{
    int             ch, last_time;
    int             endgame = 0, endturn = 0;

    chcd->ipass = 0, chcd->selected = 0;
    last_time = now;
    bell();
    while (!endturn) {
	chc_drawline(board, user1, user2, TIME_ROW);
	chc_movecur(chcd->cursor.r, chcd->cursor.c);
	oflush();
	ch = igetch();
	if (time_countdown(0, now - last_time))
	    ch = 'q';
	last_time = now;
	switch (ch) {
	case I_OTHERDATA:
	    if (!chc_broadcast_recv(chcd->act_list, board)) {	/* disconnect */
		endgame = 1;
		endturn = 1;
	    } else if (chcd->from.r == -1 && chcd->ipass) {
		endgame = 3;
		endturn = 1;
	    }
	    break;
	case KEY_UP:
	    chcd->cursor.r--;
	    if (chcd->cursor.r < 0)
		chcd->cursor.r = BRD_ROW - 1;
	    break;
	case KEY_DOWN:
	    chcd->cursor.r++;
	    if (chcd->cursor.r >= BRD_ROW)
		chcd->cursor.r = 0;
	    break;
	case KEY_LEFT:
	    chcd->cursor.c--;
	    if (chcd->cursor.c < 0)
		chcd->cursor.c = BRD_COL - 1;
	    break;
	case KEY_RIGHT:
	    chcd->cursor.c++;
	    if (chcd->cursor.c >= BRD_COL)
		chcd->cursor.c = 0;
	    break;
	case 'q':
	    endgame = 2;
	    endturn = 1;
	    break;
	case 'p':
	    chcd->ipass = 1;
	    chcd->from.r = -1;
	    chc_broadcast_send(chcd->act_list, board);
	    strlcpy(chcd->warnmsg, ANSI_COLOR(1;33) "要求和棋!" ANSI_RESET, sizeof(chcd->warnmsg));
	    chc_drawline(board, user1, user2, WARN_ROW);
	    bell();
	    break;
	case '\r':
	case '\n':
	case ' ':
	    if (chcd->selected) {
		if (chcd->cursor.r == chcd->select.r &&
		    chcd->cursor.c == chcd->select.c) {
		    chcd->selected = 0;
		    chc_drawline(board, user1, user2, LTR(chcd->cursor.r));
		} else if (chc_canmove(board, chcd->select, chcd->cursor)) {
		    if (CHE_P(board[chcd->cursor.r][chcd->cursor.c]) == KIND_K)
			endgame = 1;
		    chcd->from = chcd->select;
		    chcd->to = chcd->cursor;
		    if (!endgame) {
			memcpy(tmpbrd, board, sizeof(board_t));
			chc_movechess(tmpbrd);
		    }
		    if (endgame || !chc_iskfk(tmpbrd)) {
			getstep(board, &chcd->from, &chcd->to, chcd->last_movestr);
			chc_drawline(board, user1, user2, STEP_ROW);
			chc_log_step(board, &chcd->from, &chcd->to);
			chc_movechess(board);
			step_made(0);
			chc_broadcast_send(chcd->act_list, board);
			chcd->selected = 0;
			chc_drawline(board, user1, user2, LTR(chcd->from.r));
			chc_drawline(board, user1, user2, LTR(chcd->to.r));
			endturn = 1;
		    } else {
			strlcpy(chcd->warnmsg, ANSI_COLOR(1;33) "不可以王見王" ANSI_RESET, sizeof(chcd->warnmsg));
			bell();
			chc_drawline(board, user1, user2, WARN_ROW);
		    }
		}
	    } else if (board[chcd->cursor.r][chcd->cursor.c] &&
		     CHE_O(board[chcd->cursor.r][chcd->cursor.c]) == chcd->turn) {
		chcd->selected = 1;
		chcd->select = chcd->cursor;
		chc_drawline(board, user1, user2, LTR(chcd->cursor.r));
	    }
	    break;
	}
    }
    time_countdown(0, now - last_time);
    return endgame;
}

int round_to_int(double x)
{
    /* assume that double cast to int will drop fraction parts */
    if(x>=0)
	return (int)(x+0.5);
    return (int)(x-0.5);
}
    
/*
 * ELO rating system
 * see http://www.wordiq.com/definition/ELO_rating_system
 */
static void
count_chess_elo_rating(chcusr_t *user1, const chcusr_t *user2, double myres)
{
    double k;
    double exp_res;
    int diff;
    int newrating;

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

    //exp_res = 1.0/(1.0 + pow(10.0, (user2->rating-user1->rating)/400.0));
    //user1->rating += (int)floor(k*(myres-exp_res)+0.5);
    diff=(int)user2->rating-(int)user1->rating;
    if(diff<=-1000 || diff>=1000)
       exp_res=diff>0?0.0:1.0;
    else if(diff>=0)
       exp_res=elo_exp_tab[diff];
    else
       exp_res=1.0-elo_exp_tab[-diff];
    newrating = (int)user1->rating + round_to_int(k*(myres-exp_res));
    if(newrating > 3000) newrating = 3000;
    if(newrating < 1) newrating = 1;
    user1->rating = newrating;
}

static void
mainloop(int s, chcusr_t *user1, chcusr_t *user2, board_t board, play_func_t play_func[2])
{
    int             endgame;
    char	    buf[80];
    board_t         tmpbrd;

    if (!(chcd->mode & CHC_WATCH))
	chcd->turn = 1;
    for (endgame = 0; !endgame; chcd->turn ^= 1) {
	chcd->firststep = 0;
	chc_drawline(board, user1, user2, TURN_ROW);
	if (chc_ischeck(board, chcd->turn)) {
	    strlcpy(chcd->warnmsg, ANSI_COLOR(1;31) "將軍!" ANSI_RESET, sizeof(chcd->warnmsg));
	    bell();
	} else
	    chcd->warnmsg[0] = 0;
	chc_drawline(board, user1, user2, WARN_ROW);
	endgame = play_func[chcd->turn] (s, user1, user2, board, tmpbrd);
    }

    if (chcd->mode & CHC_VERSUS) {
	user1->rating = user1->orig_rating;
	user1->lose--;
	if(chcd->my==RED) {
	    /* 由紅方作 log. 記的是下棋前的原始分數 */
	    /* NOTE, 若紅方斷線則無 log */
	    time_t t=time(NULL);
	    char buf[100];
	    sprintf(buf, "%s %s(%d,W%d/D%d/L%d) %s %s(%d,W%d/D%d/L%d)\n", ctime(&t),
		    user1->userid, user1->rating, user1->win, user1->tie, user1->lose,
		    (endgame==3?"和":endgame==1?"勝":"負"),
		    user2->userid, user2->rating, user2->win, user2->tie, user2->lose);
	    buf[24]=' '; // replace '\n'
	    log_file(BBSHOME"/log/chc.log", LOG_CREAT, buf);
	}
	if (endgame == 1) {
	    strlcpy(chcd->warnmsg, "對方認輸了!", sizeof(chcd->warnmsg));
	    count_chess_elo_rating(user1, user2, 1.0);
	    user1->win++;
	    currutmp->chc_win++;
	} else if (endgame == 2) {
	    strlcpy(chcd->warnmsg, "你認輸了!", sizeof(chcd->warnmsg));
	    count_chess_elo_rating(user1, user2, 0.0);
	    user1->lose++;
	    currutmp->chc_lose++;
	} else {
	    strlcpy(chcd->warnmsg, "和棋", sizeof(chcd->warnmsg));
	    count_chess_elo_rating(user1, user2, 0.5);
	    user1->tie++;
	    currutmp->chc_tie++;
	}
	currutmp->chess_elo_rating = user1->rating;
	chcusr_put(&cuser, user1);
	passwd_update(usernum, &cuser);
    }
    else if (chcd->mode & CHC_WATCH) {
	strlcpy(chcd->warnmsg, "結束觀棋", sizeof(chcd->warnmsg));
    }
    else {
	strlcpy(chcd->warnmsg, "結束打譜", sizeof(chcd->warnmsg));
    }

    chc_log("=> ");
    if (endgame == 3)
	chc_log("和局");
    else{
	sprintf(buf, "%s勝\n", (chcd->my==RED) == (endgame == 1) ? "紅" : "黑");
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
    userec_t        xuser;

    if (chcd->mode & CHC_PERSONAL) {
	strlcpy(userid[0], cuser.userid, sizeof(userid[0]));
	strlcpy(userid[1], cuser.userid, sizeof(userid[1]));
	play_func[0] = play_func[1] = myplay;
    }
    else if (chcd->mode & CHC_WATCH) {
	userinfo_t *uinfo = search_ulist_userid(currutmp->mateid);
	strlcpy(userid[0], uinfo->userid, sizeof(userid[0]));
	strlcpy(userid[1], uinfo->mateid, sizeof(userid[1]));
	play_func[0] = play_func[1] = hisplay;
    }
    else {
	strlcpy(userid[0], cuser.userid, sizeof(userid[0]));
	strlcpy(userid[1], currutmp->mateid, sizeof(userid[1]));
	play_func[chcd->my] = myplay;
	play_func[chcd->my ^ 1] = hisplay;
    }

    getuser(userid[0], &xuser);
    chcusr_get(&xuser, user1);
    getuser(userid[1], &xuser);
    chcusr_get(&xuser, user2);
}

static void
chc_init_photo(void)
{
    char genbuf[256];
    int line;
    FILE* fp;
    static const char * const blank_photo[6] = {
	"┌──────┐",
	"│ 空         │",
	"│    白      │",
	"│       照   │",
	"│          片│",
	"└──────┘" 
    };
    char country[5], level[11];
    userec_t xuser;

    chcd->photo = NULL;
    if (!(chcd->mode & CHC_VERSUS))
	return;

    setuserfile(genbuf, "photo_cchess");
    if (!dashf(genbuf)) {
	sethomefile(genbuf, currutmp->mateid, "photo_cchess");
	if (!dashf(genbuf))
	    return;
    }

    chcd->photo = (char*) calloc(PHOTO_LINE * PHOTO_COLUMN, sizeof(char));

    /* yack, but I copied these from gomo.c (scw) */
    setuserfile(genbuf, "photo_cchess");
    fp = fopen(genbuf, "r");

    if (fp == NULL) {
	strcpy(country, "無");
	level[0] = 0;
    } else {
	int i, j;
	for (line = 1; line < 8; ++line)
	    fgets(genbuf, sizeof(genbuf), fp);

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strip_ansi(genbuf + 11, genbuf + 11,
		STRIP_ALL);        /* country name may have color */
	for (i = 11, j = 0; genbuf[i] && j < 4; ++i)
	    if (genbuf[i] != ' ')  /* and spaces */
		country[j++] = genbuf[i];
	country[j] = 0; /* two chinese words */

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strlcpy(level, genbuf + 11, 11); /* five chinese words*/
	rewind(fp);
    }

    /* simulate chcd->photo as two dimensional array  */
#define PHOTO(X) (chcd->photo + (X) * PHOTO_COLUMN)
    for (line = 0; line < 6; ++line) {
	if (fp != NULL) {
	    if (fgets(genbuf, sizeof(genbuf), fp)) {
		chomp(genbuf);
		sprintf(PHOTO(line), "%s  ", genbuf);
	    } else
		strcpy(PHOTO(line), "                  ");
	} else
	    strcpy(PHOTO(line), blank_photo[line]);

	switch (line) {
	    case 0: sprintf(genbuf, "<代號> %s", cuser.userid);      break;
	    case 1: sprintf(genbuf, "<暱稱> %.16s", cuser.nickname); break;
	    case 2: sprintf(genbuf, "<上站> %d", cuser.numlogins);   break;
	    case 3: sprintf(genbuf, "<文章> %d", cuser.numposts);    break;
	    case 4: sprintf(genbuf, "<職位> %-4s %s", country, level);  break;
	    case 5: sprintf(genbuf, "<來源> %.16s", cuser.lasthost); break;
	    default: genbuf[0] = 0;
	}
	strcat(PHOTO(line), genbuf);
    }
    if (fp != NULL)
	fclose(fp);

    sprintf(PHOTO(6), "      %s%2.2s棋" ANSI_RESET,
	    turn_color[chcd->my], turn_str[chcd->my]);
    strcpy(PHOTO(7), "           Ｖ.Ｓ           ");
    sprintf(PHOTO(8), "                               %s%2.2s棋" ANSI_RESET,
	    turn_color[chcd->my ^ 1], turn_str[chcd->my ^ 1]);

    getuser(currutmp->mateid, &xuser);
    sethomefile(genbuf, currutmp->mateid, "photo_cchess");
    fp = fopen(genbuf, "r");

    if (fp == NULL) {
	strcpy(country, "無");
	level[0] = 0;
    } else {
	int i, j;
	for (line = 1; line < 8; ++line)
	    fgets(genbuf, sizeof(genbuf), fp);

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strip_ansi(genbuf + 11, genbuf + 11,
		STRIP_ALL);        /* country name may have color */
	for (i = 11, j = 0; genbuf[i] && j < 4; ++i)
	    if (genbuf[i] != ' ')  /* and spaces */
		country[j++] = genbuf[i];
	country[j] = 0; /* two chinese words */

	fgets(genbuf, sizeof(genbuf), fp);
	chomp(genbuf);
	strlcpy(level, genbuf + 11, 11); /* five chinese words*/
	rewind(fp);
    }

    for (line = 9; line < 15; ++line) {
	move(line, 37);
	switch (line - 9) {
	    case 0: sprintf(PHOTO(line), "<代號> %-16.16s ", xuser.userid);   break;
	    case 1: sprintf(PHOTO(line), "<暱稱> %-16.16s ", xuser.nickname); break;
	    case 2: sprintf(PHOTO(line), "<上站> %-16d ", xuser.numlogins);   break;
	    case 3: sprintf(PHOTO(line), "<文章> %-16d ", xuser.numposts);    break;
	    case 4: sprintf(PHOTO(line), "<職位> %-4s %-10s  ", country, level); break;
	    case 5: sprintf(PHOTO(line), "<來源> %-16.16s ", xuser.lasthost); break;
	}

	if (fp != NULL) {
	    if (fgets(genbuf, 200, fp)) {
		chomp(genbuf);
		strcat(PHOTO(line), genbuf);
	    } else
		strcat(PHOTO(line), "                ");
	} else
	    strcat(PHOTO(line), blank_photo[line - 9]);
    }
    if (fp != NULL)
	fclose(fp);

#undef PHOTO
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
    if(chcd == NULL) return;
    for(tmp = chcd->act_list; tmp->next != NULL; tmp = tmp->next); // XXX 一定要接在最後嗎?
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
    write(tmp->sock, chcd->bp, sizeof(board_t));
    write(tmp->sock, &chcd->my, sizeof(chcd->my));
    write(tmp->sock, &chcd->turn, sizeof(chcd->turn));
    write(tmp->sock, &currutmp->turn, sizeof(currutmp->turn));
    write(tmp->sock, &chcd->firststep, sizeof(chcd->firststep));
    write(tmp->sock, &chcd->mode, sizeof(chcd->mode));
}

static int 
chc_init(int s, chcusr_t *user1, chcusr_t *user2, board_t board, play_func_t play_func[2])
{
    userinfo_t     *my = currutmp;
    userec_t        xuser;

    if (chcd->mode & CHC_WATCH)
	setutmpmode(CHESSWATCHING);
    else
	setutmpmode(CHC);
    clear();
    chcd->warnmsg[0] = 0;

    /* 從不同來源初始化各個變數 */
    if (!(chcd->mode & CHC_WATCH)) {
	if (chcd->mode & CHC_PERSONAL)
	    chcd->my = RED;
	else
	    chcd->my = my->turn;
	chcd->firststep = 1;
	chc_init_board(board);
	chcd->cursor.r = 9, chcd->cursor.c = 0;
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
	if(read(s, board, sizeof(board_t)) != sizeof(board_t) ||
		read(s, &chcd->my, sizeof(chcd->my)) != sizeof(chcd->my) ||
		read(s, &chcd->turn, sizeof(chcd->turn)) != sizeof(chcd->turn) ||
		read(s, &my->turn, sizeof(my->turn)) != sizeof(my->turn) ||
		read(s, &chcd->firststep, sizeof(chcd->firststep))
		!= sizeof(chcd->firststep) ||
		read(s, &mode, sizeof(mode)) != sizeof(mode)){
	    add_io(0, 0);
	    close(s);
	    return -1;
	}
	if (mode & CHC_PERSONAL)
	    chcd->mode |= CHC_WATCH_PERSONAL;
    }

    chc_init_photo();

    chcd->act_list = (chc_act_list *)malloc(sizeof(*chcd->act_list));
    chcd->act_list->sock = s;
    chcd->act_list->next = 0;

    chc_init_play_func(user1, user2, play_func);

    chc_redraw(user1, user2, board);
    add_io(s, 0);

    if (!(chcd->mode & CHC_WATCH)) {
	Signal(SIGUSR1, chc_watch_request);
    }

//    if (my->turn && !(chcd->mode & CHC_WATCH))
//	chc_broadcast_recv(chcd->act_list, board);

    if (chcd->mode & CHC_VERSUS) {
	user1->lose++;
	count_chess_elo_rating(user1, user2, 0.0);
	passwd_query(usernum, &xuser);
	chcusr_put(&xuser, user1);
	passwd_update(usernum, &xuser);

	/* exchanging timing information */
	if (my->turn) {
	    char mode;
	    read(s, &mode, 1);
	    if (mode == 'L') {
		timelimit = (CHCTimeLimit*) malloc(sizeof(CHCTimeLimit));
		read(s, timelimit, sizeof(CHCTimeLimit));
	    } else
		timelimit = NULL;
	} else {
	    if (!timelimit)
		write(s, "T", 1); /* traditional */
	    else {
		write(s, "L", 1); /* limited */
		write(s, timelimit, sizeof(CHCTimeLimit));
	    }
	}
    }

    if (!my->turn) {
	if (!(chcd->mode & CHC_WATCH))
	    chc_broadcast_send(chcd->act_list, board);
	if (chcd->mode & CHC_VERSUS)
	    user2->lose++;
    }

    chcd->lefthand[0] = chcd->lefthand[1] = 0;
    chcd->lefttime[0] = chcd->lefttime[1] =
	timelimit ? timelimit->free_time : CHC_TIMEOUT;

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

    if(chcd != NULL) {
	vmsg("象棋功\能異常");
	return;
    }
    chcd = (struct CHCData*)malloc(sizeof(struct CHCData));
    if(chcd == NULL) {
	vmsg("執行象棋功\能失敗");
	return;
    }
    memset(chcd, 0, sizeof(struct CHCData));
    chcd->mode = mode;

    if (!(chcd->mode & CHC_WATCH))
	Signal(SIGUSR1, SIG_IGN);

    chcd->bp = &board;
    if (chc_init(s, &user1, &user2, board, play_func) < 0) {
	free(chcd);
	chcd = NULL;
	return;
    }
    
    setuserfile(file, CHC_LOG);
    if (chc_log_open(&user1, &user2, file) < 0)
	vmsg("無法紀錄棋局");
    
    mainloop(s, &user1, &user2, board, play_func);

    /* close these fd */
    if (chcd->mode & CHC_PERSONAL)
	chcd->act_list = chcd->act_list->next;
    while(chcd->act_list){
	close(chcd->act_list->sock);
	chcd->act_list = chcd->act_list->next;
    }

    add_io(0, 0);
    if (chcd->my == RED)
	pressanykey();

    currutmp->mode = mode0;

    if (getans("是否將棋譜寄回信箱？[N/y]") == 'y') {
	char title[80];
	if(chcd->my == RED)
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

    if (!(chcd->mode & CHC_WATCH))
	Signal(SIGUSR1, talk_request);

    if (timelimit)
	free(timelimit);
    if (chcd->photo)
	free(chcd->photo);
    free(chcd);
    chcd = NULL;
    timelimit = NULL;
}

static userinfo_t *
chc_init_utmp(void)
{
    char            uident[16];
    userinfo_t	   *uin;

    stand_title("楚河漢界之爭");
    CompleteOnlineUser(msg_uid, uident);
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
    char buf[4];
    
    if ((uin = chc_init_utmp()) == NULL)
	return -1;
    uin->turn = 1;
    currutmp->turn = 0;
    strlcpy(uin->mateid, currutmp->userid, sizeof(uin->mateid));
    strlcpy(currutmp->mateid, uin->userid, sizeof(currutmp->mateid));

    stand_title("象棋邀局");
    buf[0] = 0;
    getdata(2, 0, "使用傳統模式 (T), 限時限步模式 (L) 或是 讀秒模式 (C)? (T/l/c)",
	    buf, 3, DOECHO);
    if (buf[0] == 'l' || buf[0] == 'L') {
	char display_buf[128];

	timelimit = (CHCTimeLimit*) malloc(sizeof(CHCTimeLimit));
	do {
	    getdata_str(3, 0, "請設定局時 (自由時間) 以分鐘為單位:",
		    buf, 3, DOECHO, "30");
	    timelimit->free_time = atoi(buf);
	} while (timelimit->free_time < 0 || timelimit->free_time > 90);
	timelimit->free_time *= 60; /* minute -> second */

	do {
	    getdata_str(4, 0, "請設定步時, 以分鐘為單位:",
		    buf, 3, DOECHO, "5");
	    timelimit->limit_time = atoi(buf);
	} while (timelimit->limit_time < 0 || timelimit->limit_time > 30);
	timelimit->limit_time *= 60; /* minute -> second */

	snprintf(display_buf, sizeof(display_buf),
		"請設定限步 (每 %d 分鐘需走幾步):",
		timelimit->limit_time / 60);
	do {
	    getdata_str(5, 0, display_buf, buf, 3, DOECHO, "10");
	    timelimit->limit_hand = atoi(buf);
	} while (timelimit->limit_hand < 1);
    } else if (buf[0] == 'c' || buf[0] == 'C') {
	timelimit = (CHCTimeLimit*) malloc(sizeof(CHCTimeLimit));
	do {
	    getdata_str(3, 0, "請設定局時 (自由時間) 以分鐘為單位:",
		    buf, 3, DOECHO, "30");
	    timelimit->free_time = atoi(buf);
	} while (timelimit->free_time < 0 || timelimit->free_time > 90);
	timelimit->free_time *= 60; /* minute -> second */

	timelimit->limit_hand = 1;

	do {
	    getdata_str(4, 0, "請設定讀秒, 以秒為單位",
		    buf, 3, DOECHO, "60");
	    timelimit->limit_time = atoi(buf);
	} while (timelimit->limit_time < 0);
    } else
	timelimit = NULL;

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
