/* $Id$ */

#include "bbs.h"
#include <sys/socket.h> 

#define BBLANK 	(-1)  	/* 空白 */
#define BWHITE 	(0)  	/* 白子, 後手 */
#define BBLACK 	(1)  	/* 黑子, 先手 */
#define LWHITE  (2)     /* 白空 */
#define LBLACK  (3)     /* 黑空 */

/* only used for communicating */
#define SETHAND (5)    /* 讓子 */
#define CLEAN   (6)    /* 清除死子 */
#define UNCLEAN (7)    /* 清除錯子，重新來過*/
#define CLEANDONE (8)  /* 開始計地 */

#define MAX_TIME (300)

#define BOARD_LINE_ON_SCREEN(X) ((X) + 2)

#define BRDSIZ 	(19) 	/* 棋盤單邊大小 */

static const char* turn_color[] = { ANSI_COLOR(37;43), ANSI_COLOR(30;43) };

static const rc_t SetHandPoints[] =
{
    /* 1 */ { 0,  0},
    /* 2 */ { 3,  3}, {15, 15},
    /* 3 */ { 3,  3}, { 3, 15}, {15, 15},
    /* 4 */ { 3,  3}, { 3, 15}, {15,  3}, {15, 15},
    /* 5 */ { 3,  3}, { 3, 15}, { 9,  9}, {15,  3}, {15, 15},
    /* 6 */ { 3,  3}, { 3, 15}, { 9,  3}, { 9, 15}, {15,  3}, {15, 15},
    /* 7 */ { 3,  3}, { 3, 15}, { 9,  3}, { 9,  9}, { 9, 15}, {15,  3},
	    {15, 15},
    /* 8 */ { 3,  3}, { 3,  9}, { 3, 15}, { 9,  3}, { 9, 15}, {15,  3},
	    {15,  9}, {15, 15},
    /* 9 */ { 3,  3}, { 3,  9}, { 3, 15}, { 9,  3}, { 9,  9}, { 9, 15},
	    {15,  3}, {15,  9}, {15, 15},
};

typedef char board_t[BRDSIZ][BRDSIZ];
typedef char (*board_p)[BRDSIZ];

typedef struct {
    ChessStepType type;  /* necessary one */
    int           color;
    rc_t          loc;
} go_step_t;
#define RC_T_EQ(X,Y) ((X).r == (Y).r && (X).c == (Y).c)

typedef struct {
    board_t backup_board;
    char    game_end;
    char    clean_end;    /* bit 1 => I, bit 2 => he */
    char    need_redraw;
    float   feed_back;    /* 貼還 */
    int     eaten[2];
    int     backup_eaten[2];
    rc_t    forbidden[2]; /* 打劫之禁手 */
} go_tag_t;
#define GET_TAG(INFO) ((go_tag_t*)(INFO)->tag)

static char	* const locE = "ABCDEFGHJKLMNOPQRST";

static void go_init_user(const userinfo_t* uinfo, ChessUser* user);
static void go_init_user_userec(const userec_t* urec, ChessUser* user);
static void go_init_board(board_t board);
static void go_drawline(const ChessInfo* info, int line);
static void go_movecur(int r, int c);
static int  go_prepare_play(ChessInfo* info);
static int  go_process_key(ChessInfo* info, int key, ChessGameResult* result);
static int  go_select(ChessInfo* info, rc_t location,
	ChessGameResult* result);
static void go_prepare_step(ChessInfo* info, const go_step_t* step);
static ChessGameResult go_apply_step(board_t board, const go_step_t* step);
static void go_drawstep(ChessInfo* info, const go_step_t* step);
static ChessGameResult go_post_game(ChessInfo* info);
static void go_gameend(ChessInfo* info, ChessGameResult result);
static void go_genlog(ChessInfo* info, FILE* fp, ChessGameResult result);

static const ChessActions go_actions = {
    &go_init_user,
    &go_init_user_userec,
    (void (*)(void*)) &go_init_board,
    &go_drawline,
    &go_movecur,
    &go_prepare_play,
    &go_process_key,
    &go_select,
    (void (*)(ChessInfo*, const void*)) &go_prepare_step,
    (ChessGameResult (*)(void*, const void*)) &go_apply_step,
    (void (*)(ChessInfo*, const void*)) &go_drawstep,
    &go_post_game,
    &go_gameend,
    &go_genlog
};

static const ChessConstants go_constants = {
    sizeof(go_step_t),
    MAX_TIME,
    BRDSIZ,
    BRDSIZ,
    1,
    "圍棋",
    "photo_go",
#ifdef BN_GOCHESS_LOG
    BN_GOCHESS_LOG,
#else
    NULL,
#endif
    { ANSI_COLOR(37;43), ANSI_COLOR(30;43) },
    { "白棋", "黑棋" },
};

static void
go_sethand(board_t board, int n)
{
    if (n >= 2 && n <= 9) {
	const int lower = n * (n - 1) / 2;
	const int upper = lower + n;
	int i;
	for (i = lower; i < upper; ++i)
	    board[SetHandPoints[i].r][SetHandPoints[i].c] = BBLACK;
    }
}

/* 計算某子的氣數, recursion part of go_countlib() */
static int
go_count(board_t board, board_t mark, int x, int y, int color)
{
    static const int diff[][2] = {
	{1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
    int i;
    int total = 0;

    mark[x][y] = 0;

    for (i = 0; i < 4; ++i) {
	int xx = x + diff[i][0];
	int yy = y + diff[i][1];

	if (xx >= 0 && xx < BRDSIZ && yy >= 0 && yy < BRDSIZ) {
	    if (board[xx][yy] == BBLANK && mark[xx][yy]) {
		++total;
		mark[xx][yy] = 0;
	    } else if (board[xx][yy] == color && mark[xx][yy])
		total += go_count(board, mark, xx, yy, color);
	}
    }

    return total;
}

/* 計算某子的氣數 */
static int
go_countlib(board_t board, int x, int y, char color)
{
    int i, j;
    board_t mark;

    for (i = 0; i < BRDSIZ; i++)
	for (j = 0; j < BRDSIZ; j++)
	    mark[i][j] = 1;

    return go_count(board, mark, x, y, color);
}

/* 計算盤面上每個子的氣數 */
static void 
go_eval(board_t board, int lib[][BRDSIZ], char color)
{
    int i, j;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    if (board[i][j] == color)
		lib[i][j] = go_countlib(board, i, j, color);
}

/* 檢查一步是否合法 */
static int 
go_check(ChessInfo* info, const go_step_t* step)
{
    board_p board = (board_p) info->board;
    int lib = go_countlib(board, step->loc.r, step->loc.c, step->color);

    if (lib == 0) {
	int i, j;
	int board_lib[BRDSIZ][BRDSIZ];
	go_tag_t* tag = (go_tag_t*) info->tag;

	board[step->loc.r][step->loc.c] = step->color;
	go_eval(board, board_lib, !step->color);
	board[step->loc.r][step->loc.c] = BBLANK;   /* restore to open */

	lib = 0;
	for (i = 0; i < BRDSIZ; i++)
	    for (j = 0; j < BRDSIZ; j++)
		if (board[i][j] == !step->color && !board_lib[i][j])
		    ++lib;

	if (lib == 0 ||
		(lib == 1 && RC_T_EQ(step->loc, tag->forbidden[step->color])))
	    return 0;
	else
	    return 1;
    } else
	return 1;
}

/* Clean up the dead chess of color `color,' summarize number of
 * eaten chesses and set the forbidden point.
 *
 * `info' might be NULL which means no forbidden point check is
 * needed and don't have to count the number of eaten chesses.
 *
 * Return: 1 if any chess of color `color' was eaten; 0 otherwise. */
static int
go_examboard(board_t board, int color, ChessInfo* info)
{
    int i, j, n;
    int lib[BRDSIZ][BRDSIZ];

    rc_t  dummy_rc;
    rc_t *forbidden;
    int   dummy_eaten;
    int  *eaten;

    if (info) {
	go_tag_t* tag = (go_tag_t*) info->tag;
	forbidden = &tag->forbidden[color];
	eaten     = &tag->eaten[!color];
    } else {
	forbidden = &dummy_rc;
	eaten     = &dummy_eaten;
    }

    go_eval(board, lib, color);

    forbidden->r = -1;
    forbidden->c = -1;

    n = 0;
    for (i = 0; i < BRDSIZ; i++)
	for (j = 0; j < BRDSIZ; j++)
	    if (board[i][j] == color && lib[i][j] == 0) {
		board[i][j] = BBLANK;
		forbidden->r = i;
		forbidden->c = j;
		++*eaten;
		++n;
	    }

    if ( n != 1 ) {
	/* No or more than one chess were eaten,
	 * no forbidden points, then. */
	forbidden->r = -1;
	forbidden->c = -1;
    }

    return (n > 0);
}

static int
go_clean(board_t board, int mark[][BRDSIZ], int x, int y, int color)
{
    static const int diff[][2] = {
	{1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
    int i;
    int total = 1;

    mark[x][y] = 0;
    board[x][y] = BBLANK;

    for (i = 0; i < 4; ++i) {
	int xx = x + diff[i][0];
	int yy = y + diff[i][1];

	if (xx >= 0 && xx < BRDSIZ && yy >= 0 && yy < BRDSIZ) {
	    if ((board[xx][yy] == color) && mark[xx][yy])
		total += go_clean(board, mark, xx, yy, color);
	}
    }

    return total;
}

static int
go_cleandead(board_t board, int x, int y)
{
    int mark[BRDSIZ][BRDSIZ];
    int i, j;

    if (board[x][y] == BBLANK)
	return 0;

    for (i = 0; i < BRDSIZ; i++)
	for (j = 0; j < BRDSIZ; j++)
	    mark[i][j] = 1;

    return go_clean(board, mark, x, y, board[x][y]);
}

static int
go_findcolor(board_p board, int x, int y)
{
    int k, result = 0, color[4];

    if (board[x][y] != BBLANK)
	return BBLANK;

    if (x > 0)
    {
	k = x;
	do --k;
	while ((board[k][y] == BBLANK) && (k > 0));
	color[0] = board[k][y];
    }
    else 
	color[0] = board[x][y];

    if (x < 18)
    {
	k = x;
	do ++k;
	while ((board[k][y] == BBLANK) && (k < 18));
	color[1] = board[k][y];
    }
    else
	color[1] = board[x][y];

    if (y > 0)
    {
	k = y;
	do --k;
	while ((board[x][k] == BBLANK) && (k > 0));
	color[2] = board[x][k];
    }
    else color[2] = board[x][y];

    if (y < 18)
    {
	k = y;
	do ++k;
	while ((board[x][k] == BBLANK) && (k < 18));
	color[3] = board[x][k];
    }
    else
	color[3] = board[x][y];

    for (k = 0; k < 4; k++)
    {
	if (color[k] == BBLANK)
	    continue;
	else
	{
	    result = color[k];
	    break;
	}
    }
    if (k == 4)
	return BBLANK;

    for (k = 0; k < 4; k++)
    {
	if ((color[k] != BBLANK) && (color[k] != result))
	    return BBLANK;
    }

    return result;
}

static int
go_result(ChessInfo* info)
{
    int       i, j;
    int       count[2];
    board_p   board = (board_p) info->board;
    go_tag_t *tag   = (go_tag_t*) info->tag;
    board_t   result_board;

    memcpy(result_board, board, sizeof(result_board));
    count[0] = count[1] = 0;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    if (board[i][j] == BBLANK)
	    {
		int result = go_findcolor(board, i, j);
		if (result != BBLANK) {
		    count[result]++;

		    /* BWHITE => LWHITE, BBLACK => LBLACK */
		    result_board[i][j] = result + 2;
		}
	    }
	    else
		count[(int) board[i][j]]++;

    memcpy(board, result_board, sizeof(result_board));

    /* 死子回填 */
    count[0] -= tag->eaten[1];
    count[1] -= tag->eaten[0];

    tag->eaten[0] = count[0];
    tag->eaten[1] = count[1];

    if (tag->feed_back < 0.01 && tag->eaten[0] == tag->eaten[1])
	return BBLANK; /* tie */
    else
	return tag->eaten[0] + tag->feed_back > tag->eaten[1] ?
	    BWHITE : BBLACK;
}

static char*
go_getstep(const go_step_t* step, char buf[])
{
    static const char* const ColName = "ＡＢＣＤＥＦＧＨＪＫＬＭＮＯＰＱＲＳＴ";
    static const char* const RawName = "19181716151413121110９８７６５４３２１";
    static const int ansi_length     = sizeof(ANSI_COLOR(30;43)) - 1;

    strcpy(buf, turn_color[step->color]);
    buf[ansi_length    ] = ColName[step->loc.c * 2];
    buf[ansi_length + 1] = ColName[step->loc.c * 2 + 1];
    buf[ansi_length + 2] = RawName[step->loc.r * 2];
    buf[ansi_length + 3] = RawName[step->loc.r * 2 + 1];
    strcpy(buf + ansi_length + 4, ANSI_RESET "     ");

    return buf;
}

static void
go_init_tag(go_tag_t* tag)
{
    tag->game_end        = 0;
    tag->need_redraw     = 0;
    tag->feed_back       = 5.5;
    tag->eaten[0]        = 0;
    tag->eaten[1]        = 0;
    tag->forbidden[0].r  = -1;
    tag->forbidden[0].c  = -1;
    tag->forbidden[1].r  = -1;
    tag->forbidden[1].c  = -1;
}

static void
go_init_user(const userinfo_t* uinfo, ChessUser* user)
{
    strlcpy(user->userid, uinfo->userid, sizeof(user->userid));
    user->win  = uinfo->go_win;
    user->lose = uinfo->go_lose;
    user->tie  = uinfo->go_tie;
}

static void
go_init_user_userec(const userec_t* urec, ChessUser* user)
{
    strlcpy(user->userid, urec->userid, sizeof(user->userid));
    user->win  = urec->go_win;
    user->lose = urec->go_lose;
    user->tie  = urec->go_tie;
}

static void
go_init_board(board_t board)
{
    memset(board, BBLANK, sizeof(board_t));
}

static void
go_drawline(const ChessInfo* info, int line)
{
    static const char* const BoardPic[] = {
	"╔", "╤", "╤", "╗",
	"╟", "┼", "┼", "╢",
	"╟", "┼", "＋", "╢",
	"╚", "╧", "╧", "╝",
    };
    static const int BoardPicIndex[] =
    { 0, 1, 1, 2, 1,
      1, 1, 1, 1, 2,
      1, 1, 1, 1, 1,
      2, 1, 1, 3 };

    board_p board = (board_p) info->board;
    go_tag_t* tag = (go_tag_t*) info->tag;

    if (line == 0) {
	prints(ANSI_COLOR(1;46) "   圍棋對戰   " ANSI_COLOR(45)
		"%30s VS %-20s%10s" ANSI_RESET,
	       info->user1.userid, info->user2.userid,
	       info->mode == CHESS_MODE_WATCH ? "[觀棋模式]" : "");
    } else if (line == 1) {
	outs("   A B C D E F G H J K L M N O P Q R S T");
    } else if (line >= 2 && line <= 20) {
	const int board_line = line - 2;
	const char* const* const pics =
	    &BoardPic[BoardPicIndex[board_line] * 4];
	int i;

	prints("%2d" ANSI_COLOR(30;43), 21 - line);

	for (i = 0; i < BRDSIZ; ++i)
	    if (board[board_line][i] == BBLANK)
		outs(pics[BoardPicIndex[i]]);
	    else
		outs(bw_chess[(int) board[board_line][i]]);

	outs(ANSI_RESET);
    } else if (line >= 21 && line < b_lines)
	prints("%40s", "");
    else if (line == b_lines) {
	if (info->mode == CHESS_MODE_VERSUS ||
		info->mode == CHESS_MODE_PERSONAL) {
	    if (tag->game_end)
		outs(ANSI_COLOR(31;47) "(w)" ANSI_COLOR(30) "計地" ANSI_RESET);
	    else if (info->history.used == 0 && (info->myturn == BWHITE
			|| info->mode == CHESS_MODE_PERSONAL))
		outs(ANSI_COLOR(31;47) "(x)" ANSI_COLOR(30) "授子" ANSI_RESET);
	}
    }

    if (line == 1 || line == 2) {
	int color = line - 1; /* BWHITE or BBLACK */

	if (tag->game_end && tag->clean_end == 3)
	    prints("   " ANSI_COLOR(30;43) "%s" ANSI_RESET
		    " 方子空：%3.1f", bw_chess[color],
		    tag->eaten[color] +
		    (color == BWHITE ? tag->feed_back : 0.0));
	else
	    prints("   " ANSI_COLOR(30;43) "%s" ANSI_RESET
		    " 方提子數：%3d", bw_chess[color], tag->eaten[color]);
    } else
	ChessDrawExtraInfo(info, line, 3);
}

static void
go_movecur(int r, int c)
{
    move(r + 2, c * 2 + 3);
}

static int
go_prepare_play(ChessInfo* info)
{
    if (((go_tag_t*) info->tag)->game_end) {
	strlcpy(info->warnmsg, "請清除死子，以便計算勝負",
		sizeof(info->warnmsg));
	if (info->last_movestr[0] != ' ')
	    strcpy(info->last_movestr, "        ");
    }

    if (info->history.used == 1)
	ChessDrawLine(info, b_lines); /* clear the 'x' instruction */

    return 0;
}

static int
go_process_key(ChessInfo* info, int key, ChessGameResult* result)
{
    go_tag_t* tag = (go_tag_t*) info->tag;
    if (tag->game_end) {
	if (key == 'w') {
	    if (!(tag->clean_end & 1)) {
		go_step_t step = { .type = CHESS_STEP_SPECIAL, .color = CLEANDONE };
		ChessStepSend(info, &step);
		tag->clean_end |= 1;
	    }

	    if (tag->clean_end & 2 || info->mode == CHESS_MODE_PERSONAL) {
		/* both sides agree */
		int winner = go_result(info);

		tag->clean_end = 3;

		if (winner == BBLANK)
		    *result = CHESS_RESULT_TIE;
		else
		    *result = (winner == info->myturn ?
			    CHESS_RESULT_WIN : CHESS_RESULT_LOST);

		ChessRedraw(info);
		return 1;
	    }
	} else if (key == 'u') {
	    char buf[4];
	    getdata(b_lines, 0, "是否真的要重新點死子? (y/N)",
		    buf, sizeof(buf), DOECHO);
	    ChessDrawLine(info, b_lines);

	    if (buf[0] == 'y' || buf[0] == 'Y') {
		go_step_t step = { .type = CHESS_STEP_SPECIAL, .color = UNCLEAN };
		ChessStepSend(info, &step);

		memcpy(info->board, tag->backup_board, sizeof(tag->backup_board));
		tag->eaten[0] = tag->backup_eaten[0];
		tag->eaten[1] = tag->backup_eaten[1];
	    }
	}
    } else if (key == 'x' && info->history.used == 0 &&
	    ((info->mode == CHESS_MODE_VERSUS && info->myturn == BWHITE) ||
	      info->mode == CHESS_MODE_PERSONAL)) {
	char buf[4];
	int  n;

	getdata(22, 43, "要授多少子呢(2 - 9)？ ", buf, sizeof(buf), DOECHO);
	n = atoi(buf);

	if (n >= 2 && n <= 9) {
	    go_step_t step = { CHESS_STEP_NORMAL, SETHAND, {n, 0} };
	    
	    ChessStepSend(info, &step);
	    ChessHistoryAppend(info, &step);

	    go_sethand(info->board, n);
	    ((go_tag_t*)info->tag)->feed_back = 0.0;

	    snprintf(info->last_movestr, sizeof(info->last_movestr),
		    ANSI_COLOR(1) "授 %d 子" ANSI_RESET, n);
	    ChessRedraw(info);
	    return 1;
	} else
	    ChessDrawLine(info, 22);
    }
    return 0;
}

static int
go_select(ChessInfo* info, rc_t location, ChessGameResult* result GCC_UNUSED)
{
    board_p   board = (board_p) info->board;

    if (GET_TAG(info)->game_end) {
	go_step_t step = { CHESS_STEP_SPECIAL, CLEAN, location };
	if (board[location.r][location.c] == BBLANK)
	    return 0;

	GET_TAG(info)->eaten[!board[location.r][location.c]] +=
	    go_cleandead(board, location.r, location.c);

	ChessStepSend(info, &step);
	ChessRedraw(info);
	return 0; /* don't have to return from ChessPlayFuncMy() */
    } else {
	go_step_t step = { CHESS_STEP_NORMAL, info->turn, location };

	if (board[location.r][location.c] != BBLANK)
	    return 0;

	if (go_check(info, &step)) {
	    board[location.r][location.c] = info->turn;
	    ChessStepSend(info, &step);
	    ChessHistoryAppend(info, &step);

	    go_getstep(&step, info->last_movestr);
	    if (go_examboard(board, !info->myturn, info))
		ChessRedraw(info);
	    else
		ChessDrawLine(info, BOARD_LINE_ON_SCREEN(location.r));
	    return 1;
	} else
	    return 0;
    }
}

static void
go_prepare_step(ChessInfo* info, const go_step_t* step)
{
    go_tag_t* tag = GET_TAG(info);
    if (tag->game_end) {
	/* some actions need tag so are done here */
	if (step->color == CLEAN) {
	    board_p board = (board_p) info->board;
	    tag->eaten[!board[step->loc.r][step->loc.c]] +=
		go_cleandead(board, step->loc.r, step->loc.c);
	} else if (step->color == UNCLEAN) {
	    memcpy(info->board, tag->backup_board, sizeof(tag->backup_board));
	    tag->eaten[0] = tag->backup_eaten[0];
	    tag->eaten[1] = tag->backup_eaten[1];
	} else if (step->color == CLEANDONE) {
	    if (tag->clean_end & 1) {
		/* both sides agree */
		int winner = go_result(info);

		tag->clean_end = 3;

		if (winner == BBLANK)
		    ((go_step_t*)step)->loc.r = (int) CHESS_RESULT_TIE;
		else
		    ((go_step_t*)step)->loc.r = (int)
			(winner == info->myturn ?
			 CHESS_RESULT_WIN : CHESS_RESULT_LOST);

		ChessRedraw(info);
	    } else {
		((go_step_t*)step)->color = BBLANK; /* tricks apply */
		tag->clean_end |= 2;
	    }
	}
    } else if (step->type == CHESS_STEP_NORMAL) {
	if (step->color != SETHAND) {
	    go_getstep(step, info->last_movestr);

	    memcpy(tag->backup_board, info->board, sizeof(board_t));
	    tag->backup_board[step->loc.r][step->loc.c] = step->color;

	    /* if any chess was eaten, wholely redraw is needed */
	    tag->need_redraw =
		go_examboard(tag->backup_board, !step->color, info);
	} else {
	    snprintf(info->last_movestr, sizeof(info->last_movestr),
		    ANSI_COLOR(1) "授 %d 子" ANSI_RESET, step->loc.r);
	    tag->need_redraw = 1;
	    ((go_tag_t*)info->tag)->feed_back = 0.0;
	}
    } else if (step->type == CHESS_STEP_PASS)
	strcpy(info->last_movestr, "虛手");
}

static ChessGameResult
go_apply_step(board_t board, const go_step_t* step)
{
    if (step->type != CHESS_STEP_NORMAL)
	return CHESS_RESULT_CONTINUE;

    switch (step->color) {
	case BWHITE:
	case BBLACK:
	    board[step->loc.r][step->loc.c] = step->color;
	    go_examboard(board, !step->color, NULL);
	    break;

	case SETHAND:
	    go_sethand(board, step->loc.r);
	    break;

	case CLEAN:
	    go_cleandead(board, step->loc.r, step->loc.c);
	    break;

	case CLEANDONE:
	    /* should be agreed by both sides, [see go_prepare_step()] */
	    return (ChessGameResult) step->loc.r;
    }
    return CHESS_RESULT_CONTINUE;
}

static void
go_drawstep(ChessInfo* info, const go_step_t* step)
{
    go_tag_t* tag = GET_TAG(info);
    if (tag->game_end || tag->need_redraw)
	ChessRedraw(info);
    else
	ChessDrawLine(info, BOARD_LINE_ON_SCREEN(step->loc.r));
}

static ChessGameResult
go_post_game(ChessInfo* info)
{
    extern ChessGameResult ChessPlayFuncMy(ChessInfo* info);

    go_tag_t       *tag        = (go_tag_t*) info->tag;
    ChessTimeLimit *orig_limit = info->timelimit;
    ChessGameResult result;

    info->timelimit = NULL;
    info->turn      = info->myturn;
    strcpy(info->warnmsg, "請點除死子");
    ChessDrawLine(info, CHESS_DRAWING_WARN_ROW);

    memcpy(tag->backup_board, info->board, sizeof(tag->backup_board));
    tag->game_end  = 1;
    tag->clean_end = 0;
    tag->backup_eaten[0] = tag->eaten[0];
    tag->backup_eaten[1] = tag->eaten[1];

    ChessDrawLine(info, b_lines); /* 'w' instruction */

    while ((result = ChessPlayFuncMy(info)) == CHESS_RESULT_CONTINUE);

    ChessRedraw(info);

    info->timelimit = orig_limit;

    return result;
}

static void
go_gameend(ChessInfo* info, ChessGameResult result)
{
    if (info->mode == CHESS_MODE_VERSUS) {

	// lost was already initialized
	if (result != CHESS_RESULT_LOST)
	    pwcuChessResult(SIG_GO, result);

    } else if (info->mode == CHESS_MODE_REPLAY) {
	free(info->board);
	free(info->tag);
    }
}

static void
go_genlog(ChessInfo* info, FILE* fp, ChessGameResult result GCC_UNUSED)
{
    static const char ColName[] = "ABCDEFGHJKLMNOPQRST";
    const int nStep = info->history.used;
    char buf[ANSILINELEN] = "";
    int   i, sethand = 0;
    VREFCUR cur;

    if (nStep > 0) {
	const go_step_t* const step =
	    (const go_step_t*) ChessHistoryRetrieve(info, 0);
	if (step->color == SETHAND)
	    sethand = step->loc.r;
    }

    cur = vcur_save();
    for (i = 1; i <= 22; i++)
    {
	move(i, 0);
	inansistr(buf, sizeof(buf)-1);
	fprintf(fp, "%s\n", buf);
    }
    vcur_restore(cur);

    fprintf(fp, "\n");
    fprintf(fp, "按 z 可進入打譜模式\n");
    fprintf(fp, "\n");

    if (sethand) {
	fprintf(fp, "[   1] 授 %d 子\n                ", sethand);
	i = 1;
    } else
	i = 0;

    for (; i < nStep; ++i) {
	const go_step_t* const step =
	    (const go_step_t*) ChessHistoryRetrieve(info, i);
	if (step->type == CHESS_STEP_NORMAL)
	    fprintf(fp, "[%3d]%s => %c%-4d", i + 1, bw_chess[step->color],
		    ColName[step->loc.c], 19 - step->loc.r);
	else if (step->type == CHESS_STEP_PASS)
	    fprintf(fp, "[%3d]%s => 虛手 ", i + 1, bw_chess[(i + 1) % 2]);
	else
	    break;
	if (i % 5 == 4)
	    fputc('\n', fp);
    }

    fprintf(fp,
	    "\n\n《以下為 sgf 格式棋譜》\n<golog>\n(;GM[1]"
	    "GN[%s-%s(W) Ptt]\n"
	    "SZ[19]HA[%d]PB[%s]PW[%s]\n"
	    "PC[FPG BBS/Ptt BBS: ptt.cc]\n",
	    info->user1.userid, info->user2.userid,
	    sethand,
	    info->user1.userid, info->user2.userid);

    if (sethand) {
	const int lower = sethand * (sethand - 1) / 2;
	const int upper = lower + sethand;
	int j;
	fputs("AB", fp);
	for (j = lower; j < upper; ++j)
	    fprintf(fp, "[%c%c]",
		    SetHandPoints[j].c + 'a',
		    SetHandPoints[j].r + 'a');
	fputc('\n', fp);
    }

    for (i = (sethand ? 1 : 0); i < nStep; ++i) {
	const go_step_t* const step =
	    (const go_step_t*) ChessHistoryRetrieve(info, i);
	if (step->type == CHESS_STEP_NORMAL)
	    fprintf(fp, ";%c[%c%c]",
		    step->color == BWHITE ? 'W' : 'B',
		    step->loc.c + 'a',
		    step->loc.r + 'a');
	else if (step->type == CHESS_STEP_PASS)
	    fprintf(fp, ";%c[]  ", i % 2 ? 'W' : 'B');
	else
	    break;
	if (i % 10 == 9)
	    fputc('\n', fp);
    }
    fprintf(fp, ";)\n<golog>\n\n");
}

void
gochess(int s, ChessGameMode mode)
{
    ChessInfo* info = NewChessInfo(&go_actions, &go_constants, s, mode);
    board_t    board;
    go_tag_t   tag;

    go_init_board(board);
    go_init_tag(&tag);

    info->board = board;
    info->tag   = &tag;

    info->cursor.r = 9;
    info->cursor.c = 9;

    // copied from gomo.c
    if (info->mode == CHESS_MODE_VERSUS) {
	/* Assume that info->user1 is me. */
	info->user1.lose++;
	pwcuChessResult(SIG_GO, CHESS_RESULT_LOST);
    }

    if (mode == CHESS_MODE_WATCH)
	setutmpmode(CHESSWATCHING);
    else
	setutmpmode(UMODE_GO);
    currutmp->sig = SIG_GO;

    ChessPlay(info);

    DeleteChessInfo(info);
}

int
gochess_main(void)
{
    return ChessStartGame('g', SIG_GO, "圍棋");
}

int
gochess_personal(void)
{
    gochess(0, CHESS_MODE_PERSONAL);
    return 0;
}

int
gochess_watch(void)
{
    return ChessWatchGame(&gochess, UMODE_GO, "圍棋");
}

static int
mygetc(FILE* fp, char* buf, int* idx, int len)
{
    for (;;) {
	while (buf[*idx] && isspace(buf[*idx])) ++*idx;

	if (buf[*idx]) {
	    ++*idx;
	    return buf[*idx - 1];
	}

	if (fgets(buf, len, fp) == NULL)
	    return EOF;

	if (strcmp(buf, "<golog>\n") == 0)
	    return EOF;

	*idx = 0;
    }
}

ChessInfo*
gochess_replay(FILE* fp)
{
    ChessInfo *info;
    int        ch;
    char       userid[2][IDLEN + 1] = { "", "" };
    char       sethand_str[4] = "";
    char      *recording = NULL;
    char      *record_end = NULL;
    go_step_t  step;

    /* for mygetc */
    char buf[512] = "";
    int  idx = 0;

#define GETC() mygetc(fp, buf, &idx, sizeof(buf))

    /* sgf file started with "(;" */
    if (GETC() != '(' || GETC() != ';')
	return NULL;

    /* header info */
    while ((ch = GETC()) != EOF && ch != ';') {
	if (ch == '[') {
	    if (recording) {
		while ((ch = GETC()) != EOF && ch != ']')
		    if (recording < record_end)
			*recording++ = ch;
		*recording = 0;
		recording = NULL;
	    } else
		while ((ch = GETC()) != EOF && ch != ']')
		    continue;

	    if (ch == EOF)
		break;
	} else if (ch == ';') /* next stage */
	    break;
	else {
	    int ch2 = GETC();

	    if (ch2 == EOF) {
		ch = EOF;
		break;
	    }

	    if (ch == 'P') {
		if (ch2 == 'B') {
		    recording  = userid[BBLACK];
		    record_end = userid[BBLACK] + IDLEN;
		} else if (ch2 == 'W') {
		    recording  = userid[BWHITE];
		    record_end = userid[BWHITE] + IDLEN;
		}
	    } else if (ch == 'H') {
		if (ch2 == 'A') {
		    recording  = sethand_str;
		    record_end = sethand_str + sizeof(sethand_str) - 1;
		}
	    }
	}
    }

    if (ch == EOF)
	return NULL;

    info = NewChessInfo(&go_actions, &go_constants,
	    0, CHESS_MODE_REPLAY);

    /* filling header information to info */
    if (userid[BBLANK][0]) {
	userec_t rec;
	if (getuser(userid[BBLANK], &rec))
	    go_init_user_userec(&rec, &info->user1);
    }

    if (userid[BWHITE][0]) {
	userec_t rec;
	if (getuser(userid[BWHITE], &rec))
	    go_init_user_userec(&rec, &info->user2);
    }

    if (sethand_str[0]) {
	int sethand = atoi(sethand_str);
	if (sethand >= 2 && sethand <= 9) {
	    step.type  = CHESS_STEP_NORMAL;
	    step.color = SETHAND;
	    step.loc.r = sethand;
	    ChessHistoryAppend(info, &step);
	}
    }

    /* steps, ends with ")" */
    while ((ch = GETC()) != EOF && ch != ')') {
	if (ch == ';')
	    ChessHistoryAppend(info, &step);
	else if (ch == 'B')
	    step.color = BBLACK;
	else if (ch == 'W')
	    step.color = BWHITE;
	else if (ch == '[') {
	    ch = GETC();
	    if (ch == EOF)
		break;
	    else if (ch == ']') {
		step.type = CHESS_STEP_PASS;
		continue;
	    } else
		step.loc.c = ch - 'a';

	    ch = GETC();
	    if (ch == EOF)
		break;
	    else if (ch == ']') {
		step.type = CHESS_STEP_PASS;
		continue;
	    } else
		step.loc.r = ch - 'a';

	    while ((ch = GETC()) != EOF && ch != ']');

	    if (step.loc.r < 0 || step.loc.r >= BRDSIZ ||
		    step.loc.c < 0 || step.loc.c >= BRDSIZ)
		step.type = CHESS_STEP_PASS;
	    else
		step.type = CHESS_STEP_NORMAL;
	}
    }

    info->board = malloc(sizeof(board_t));
    info->tag   = malloc(sizeof(go_tag_t));

    go_init_board(info->board);
    go_init_tag(info->tag);

    return info;

#undef GETC
}
