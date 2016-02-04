#include "bbs.h"

enum {
    BWHITE = 0,  /* 白子 (後手) */
    BBLACK = 1,  /* 黑子 (先手) */
    BEMPTY = 2,  /* 空白 */
    MAX_TIME = 300,  /* idle 秒數 */
};

#define BRDSIZ (19)  /* 棋盤單邊大小 */
#define BOARD_LINE_ON_SCREEN(X) ((X) + 2)
#define INVALID_ROW(R) ((R) < 0 || (R) >= BRDSIZ)
#define INVALID_COL(C) ((C) < 0 || (C) >= BRDSIZ)

static const char* turn_color[] = { ANSI_COLOR(37;43), ANSI_COLOR(30;43) };

typedef struct {
    ChessStepType type;  /* necessary one */
    int           color;
    rc_t          loc1, loc2;
} conn6_step_t;

typedef struct {
    int  valid;
    rc_t loc1;
} conn6_tag_data_t;

typedef char board_t[BRDSIZ][BRDSIZ];
typedef char (*board_p)[BRDSIZ];

static void conn6_init_user(const userinfo_t* uinfo, ChessUser* user);
static void conn6_init_user_userec(const userec_t* urec, ChessUser* user);
static void conn6_init_board(board_t board);
static void conn6_drawline(const ChessInfo* info, int line);
static void conn6_movecur(int r, int c);
static int  conn6_prepare_play(ChessInfo* info);
static int  conn6_select(ChessInfo* info, rc_t location,
			 ChessGameResult* result);
static void conn6_prepare_step(ChessInfo* info, const conn6_step_t* step);
static ChessGameResult conn6_apply_step(board_t board, const conn6_step_t* step);
static void conn6_drawstep(ChessInfo* info, const conn6_step_t* step);
static void conn6_gameend(ChessInfo* info, ChessGameResult result);
static void conn6_genlog(ChessInfo* info, FILE* fp, ChessGameResult result);

static const ChessActions conn6_actions = {
    &conn6_init_user,
    &conn6_init_user_userec,
    (void (*)(void*)) &conn6_init_board,
    &conn6_drawline,
    &conn6_movecur,
    &conn6_prepare_play,
    NULL, /* process_key */
    &conn6_select,
    (void (*)(ChessInfo*, const void*)) &conn6_prepare_step,
    (ChessGameResult (*)(void*, const void*)) &conn6_apply_step,
    (void (*)(ChessInfo*, const void*)) &conn6_drawstep,
    NULL, /* post_game */
    &conn6_gameend,
    &conn6_genlog,
};

static const ChessConstants conn6_constants = {
    sizeof(conn6_step_t),
    MAX_TIME,
    BRDSIZ,
    BRDSIZ,
    0,
    "六子棋",
    "photo_connect6",
#ifdef BN_CONNECT6_LOG
    BN_CONNECT6_LOG,
#else
    NULL,
#endif
    { ANSI_COLOR(37;43), ANSI_COLOR(30;43) },
    { "白棋", "黑棋" },
};

static int
conn6_check_win(board_t b, int color)
{
    int x, y;
    char pattern[6];
    char temp[BRDSIZ * 2];
    int win_num = 6;

    memset(pattern, color, win_num);
    /* check all rows */
    for (y = 0; y < BRDSIZ; y++)
	if (memmem(b[y], BRDSIZ, pattern, win_num) != 0)
	    return 1;

    /* check all columns */
    for (x = 0; x < BRDSIZ; x++) {
	for (y = 0; y < BRDSIZ; y++)
	    temp[y] = b[y][x];
	if (memmem(temp, BRDSIZ, pattern, win_num) != 0)
	    return 1;
    }

    for (x = 0; x < BRDSIZ - win_num + 1; x++) {
	for (y = 0; y + x < BRDSIZ; y++) {
	    temp[y] = b[y][y + x];
	}
	if (memmem(temp, y, pattern, win_num) != 0)
	    return 1;

	for (y = 0; y + x < BRDSIZ; y++) {
	    temp[y] = b[BRDSIZ - 1 - y][y + x];
	}
	if (memmem(temp, y, pattern, win_num) != 0)
	    return 1;

	if (x == 0)
	    continue;

	for (y = 0; y + x < BRDSIZ; y++) {
	    temp[y] = b[y + x][y];
	}
	if (memmem(temp, y, pattern, win_num) != 0)
	    return 1;

	for (y = 0; y + x < BRDSIZ; y++) {
	    temp[y] = b[BRDSIZ - 1 - y - x][y];
	}
	if (memmem(temp, y, pattern, win_num) != 0)
	    return 1;
    }
    return 0;
}

static char*
conn6_getstep(const conn6_step_t* step, char buf[])
{
    static const char* const ColName = "ＡＢＣＤＥＦＧＨＩＪＫＬＭＮ O P Q R S";
    static const char* const RowName = "19181716151413121110９８７６５４３２１";

    sprintf(buf, "%s%2.2s%2.2s,%2.2s%2.2s" ANSI_RESET,
	    turn_color[step->color],
	    ColName + step->loc1.c * 2,
	    RowName + step->loc1.r * 2,
	    ColName + step->loc2.c * 2,
	    RowName + step->loc2.r * 2);
    return buf;
}

static void
conn6_init_user(const userinfo_t* uinfo, ChessUser* user)
{
    strlcpy(user->userid, uinfo->userid, sizeof(user->userid));
    user->win  = uinfo->conn6_win;
    user->lose = uinfo->conn6_lose;
    user->tie  = uinfo->conn6_tie;
}

static void
conn6_init_user_userec(const userec_t* urec, ChessUser* user)
{
    strlcpy(user->userid, urec->userid, sizeof(user->userid));
    user->win  = urec->conn6_win;
    user->lose = urec->conn6_lose;
    user->tie  = urec->conn6_tie;
}

static void
conn6_init_board(board_t board)
{
    memset(board, BEMPTY, sizeof(board_t));
}

static void
conn6_drawline(const ChessInfo* info, int line)
{
    static const char* const BoardPic[] = {
	"╔", "╤", "╤", "╗",
	"╟", "┼", "┼", "╢",
	"╟", "┼", "＋", "╢",
	"╚", "╧", "╧", "╝",
    };
    static const int BoardPicIndex[] =
    { 0, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 3 };

    board_p board = (board_p)info->board;

    if (line == 0) {
	prints(ANSI_COLOR(1;46) "  六子棋對戰  " ANSI_COLOR(45)
		"%30s VS %-20s%10s" ANSI_RESET,
	       info->user1.userid, info->user2.userid,
	       info->mode == CHESS_MODE_WATCH ? "[觀棋模式]" : "");
    } else if (line == 1) {
	outs("   A B C D E F G H I J K L M N O P Q R S");
    } else if (line >= 2 && line <= BRDSIZ + 1) {
	const int board_line = line - 2;
	const char* const* const pics =
	    &BoardPic[BoardPicIndex[board_line] * 4];
	int i;

	prints("%3d" ANSI_COLOR(30;43), BRDSIZ + 2 - line);

	for (i = 0; i < BRDSIZ; ++i)
	    if (board[board_line][i] == BEMPTY)
		outs(pics[BoardPicIndex[i]]);
	    else
		outs(bw_chess[(int) board[board_line][i]]);

	outs(ANSI_RESET);
    } else if (line >= (BRDSIZ + 2) && line < b_lines)
	prints("%33s", "");

    ChessDrawExtraInfo(info, line, 8);

    // 13 = CHESS_DRAWING_REAL_WARN_ROW
    // 17 = CHESS_DRAWING_MYWIN_ROW
    if (line > 13 &&
	line < 17) {
	switch (line - 13) {
	    case 1: outs("首回外每回合下兩子。");
		    break;
	    case 2: outs("詳細規則見 http://connect6.org");
		    break;
#ifdef CONN6_BETA
	    case 3: outs("系統測試中，戰績不列入記錄");
		    break;
#endif
	}
    }
}

static void
conn6_movecur(int r, int c)
{
    move(r + 2, c * 2 + 3);
}

static int
conn6_prepare_play(ChessInfo* info GCC_UNUSED)
{
    return 0;
}

static int
conn6_select(ChessInfo* info, rc_t location, ChessGameResult* result)
{
    board_p board = (board_p)info->board;
    conn6_tag_data_t *tag = (conn6_tag_data_t*)info->tag;
    conn6_step_t step;
    int first_step = 0;

    if(board[location.r][location.c] != BEMPTY)
	return 0;

    /* TODO(piaip) improve this. */
    if (memchr(board, BBLACK, BRDSIZ * BRDSIZ) == NULL &&
	memchr(board, BWHITE, BRDSIZ * BRDSIZ) == NULL) {
	first_step = 1;
    }

    board[location.r][location.c] = info->turn;
    if (conn6_check_win(board, info->turn)) {
	if (info->turn == info->myturn)
	    *result = CHESS_RESULT_WIN;
	else
	    *result = CHESS_RESULT_LOST;
    } else {
	*result = CHESS_RESULT_CONTINUE;

	/* Are we able to finish this step now? */
	if (!first_step && !tag->valid) {
	    tag->loc1 = location;
	    tag->valid = 1;
	    ChessDrawLine(info, BOARD_LINE_ON_SCREEN(location.r));
	    return 0;
	}
    }



    step.type  = CHESS_STEP_NORMAL;
    step.color = info->turn;
    if (tag->valid)
	step.loc1 = tag->loc1;
    else
	step.loc1 = location;
    step.loc2 = location;
    tag->valid = 0;
    conn6_getstep(&step, info->last_movestr);
    ChessHistoryAppend(info, &step);
    ChessStepSend(info, &step);
    conn6_drawstep(info, &step);

    return 1;
}

static void
conn6_prepare_step(ChessInfo* info, const conn6_step_t* step)
{
    conn6_tag_data_t *tag = (conn6_tag_data_t*)info->tag;

    if (step->type == CHESS_STEP_NORMAL) {
	conn6_getstep(step, info->last_movestr);
	info->cursor = step->loc2;
    }
    tag->valid = 0;
}

static ChessGameResult
conn6_apply_step(board_t board, const conn6_step_t* step)
{
    board[step->loc1.r][step->loc1.c] = step->color;
    board[step->loc2.r][step->loc2.c] = step->color;
    return conn6_check_win(board, step->color);
}

static void
conn6_drawstep(ChessInfo* info, const conn6_step_t* step)
{
    ChessDrawLine(info, BOARD_LINE_ON_SCREEN(step->loc1.r));
    if (step->loc1.r != step->loc2.r)
	ChessDrawLine(info, BOARD_LINE_ON_SCREEN(step->loc2.r));
}

static void
conn6_gameend(ChessInfo* info, ChessGameResult result GCC_UNUSED)
{
    if (info->mode == CHESS_MODE_VERSUS) {

#ifndef CONN6_BETA
	// lost was already initialized
	if (result != CHESS_RESULT_LOST)
	    pwcuChessResult(SIG_CONN6, result);
#endif

    } else if (info->mode == CHESS_MODE_REPLAY) {
	free(info->board);
	free(info->tag);
    }
}

static void
conn6_genlog(ChessInfo* info, FILE* fp, ChessGameResult result GCC_UNUSED)
{
    char buf[ANSILINELEN] = "";
    const int nStep = info->history.used;
    int   i;
    VREFCUR cur;

    cur = vcur_save();
    for (i = 1; i <= 18; i++)
    {
	move(i, 0);
	inansistr(buf, sizeof(buf)-1);
	fprintf(fp, "%s\n", buf);
    }
    vcur_restore(cur);

    fprintf(fp, "\n");
    fprintf(fp, "按 z 可進入打譜模式\n");
    fprintf(fp, "\n");

    fprintf(fp, "<connect6log>\nblack:%s\nwhite:%s\n",
	    info->myturn ? info->user1.userid : info->user2.userid,
	    info->myturn ? info->user2.userid : info->user1.userid);

    for (i = 0; i < nStep; ++i) {
	const conn6_step_t* const step =
	    (const conn6_step_t*) ChessHistoryRetrieve(info, i);
	if (step->type == CHESS_STEP_NORMAL)
	    fprintf(fp, "[%2d]%s ==> %c%-2d %c%-5d", i + 1,
		    bw_chess[step->color],
		    'A' + step->loc1.c, BRDSIZ - step->loc1.r,
		    'A' + step->loc2.c, BRDSIZ - step->loc2.r);
	else
	    break;
	if (i % 2)
	    fputc('\n', fp);
    }

    if (i % 2)
	fputc('\n', fp);
    fputs("</connect6log>\n", fp);
}

static int
conn6_loadlog(FILE *fp, ChessInfo *info)
{
    char buf[256];

    while (fgets(buf, sizeof(buf), fp)) {
	if (strcmp("</connect6log>\n", buf) == 0)
	    return 1;
	else if (strncmp("black:", buf, 6) == 0 ||
		strncmp("white:", buf, 6) == 0) {
	    /* /(black|white):([a-zA-Z0-9]+)/; $2 */
	    userec_t   rec;
	    ChessUser *user = (buf[0] == 'b' ? &info->user1 : &info->user2);

	    chomp(buf);
	    if (getuser(buf + 6, &rec))
		conn6_init_user_userec(&rec, user);
	} else if (buf[0] == '[') {
	    /* "[ 1]● ==> H8 H8    [ 2]○ ==> J9 K9    "  */
	    conn6_step_t step = { .type = CHESS_STEP_NORMAL };
	    int         c, r;
	    const char *p = buf;
	    int i;

	    for(i=0; i<2; i++) {
		p = strchr(p, '>');

		if (p == NULL) break;

		++p; /* skip '>' */
		while (*p && isspace(*p)) ++p;
		if (!*p) break;

		/* i=0, p -> "H8 H8..." */
		/* i=1, p -> "J9 K9    \n" */
		c = p[0] - 'A';
		r = BRDSIZ - atoi(p + 1);

		if (INVALID_COL(c) || INVALID_ROW(r))
		    break;

		step.color = i ^ 1;
		step.loc1.r = r;
		step.loc1.c = c;

		while (*p && !isspace(*p)) ++p;
		while (*p && isspace(*p)) ++p;
		if (!*p) break;

		/* i = 0, p-> "H8..." */
		/* i = 1, p-> "K8    \n" */
		c = p[0] - 'A';
		r = BRDSIZ - atoi(p + 1);
		if (INVALID_COL(c) || INVALID_ROW(r))
		    break;
		step.loc2.r = r;
		step.loc2.c = c;

		ChessHistoryAppend(info, &step);
	    }
	}
    }
#undef INVALID_ROW
#undef INVALID_COL
    return 0;
}


void
connect6(int s, ChessGameMode mode)
{
    ChessInfo* info = NewChessInfo(&conn6_actions, &conn6_constants, s, mode);
    board_t    board;
    conn6_tag_data_t tag = {0};

    conn6_init_board(board);

    info->board = board;
    info->tag   = &tag;

    info->cursor.r = 9;
    info->cursor.c = 9;

    if (info->mode == CHESS_MODE_VERSUS) {
	/* Assume that info->user1 is me. */
	info->user1.lose++;
#ifndef CONN6_BETA
	pwcuChessResult(SIG_CONN6, CHESS_RESULT_LOST);
#endif
    }

    if (mode == CHESS_MODE_WATCH)
	setutmpmode(CHESSWATCHING);
    else
	setutmpmode(M_CONN6);
    currutmp->sig = SIG_CONN6;

    ChessPlay(info);
    DeleteChessInfo(info);
}

int
conn6_main(void)
{
    return ChessStartGame('f', SIG_CONN6, "六子棋");
}

int
conn6_personal(void)
{
    connect6(0, CHESS_MODE_PERSONAL);
    return 0;
}

int
conn6_watch(void)
{
    return ChessWatchGame(&connect6, M_CONN6, "六子棋");
}

ChessInfo*
conn6_replay(FILE* fp)
{
    ChessInfo *info;

    info = NewChessInfo(&conn6_actions, &conn6_constants,
			0, CHESS_MODE_REPLAY);

    if(!conn6_loadlog(fp, info)) {
	DeleteChessInfo(info);
	return NULL;
    }

    info->board = malloc(sizeof(board_t));
    info->tag   = malloc(sizeof(conn6_tag_data_t));
    ((conn6_tag_data_t*)info->tag)->valid = 0;

    conn6_init_board(info->board);
    return info;
}

