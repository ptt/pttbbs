/* $Id$ */

#include "bbs.h"

#define MAX_TIME (300)
#define BRDSIZ   (8) 	/* 棋盤單邊大小 */

#define NONE_CHESS "  "
#define WHITE_CHESS "●"
#define BLACK_CHESS "○"
#define HINT_CHESS "＃"
#define NONE  0
#define HINT  1
#define BLACK 2
#define WHITE 3

#define STARTY 10

#define INVERT(COLOR) (((COLOR))==WHITE?BLACK:WHITE)

#define IS_BLANK(COLOR) ((COLOR) < BLACK)  /* NONE or HINT */
#define IS_CHESS(COLOR) ((COLOR) >= BLACK)
#define TURN_TO_COLOR(TURN)  (WHITE - (TURN))
#define COLOR_TO_TURN(COLOR) (WHITE - (COLOR))

typedef char      color_t;
typedef color_t   board_t[BRDSIZ + 2][BRDSIZ + 2];
typedef color_t (*board_p)[BRDSIZ + 2];
/* [0] & [9] are dummy */

typedef struct {
    ChessStepType type;  /* necessary one */
    color_t       color;
    rc_t          loc;
} reversi_step_t;

typedef struct {
    int number[2];
} reversi_tag_t;

/* chess framework action functions */
static void reversi_init_user(const userinfo_t *uinfo, ChessUser *user);
static void reversi_init_user_userec(const userec_t *urec, ChessUser *user);
static void reversi_init_board(board_t board);
static void reversi_drawline(const ChessInfo* info, int line);
static void reversi_movecur(int r, int c);
static int  reversi_prepare_play(ChessInfo* info);
static int  reversi_select(ChessInfo* info, rc_t scrloc, ChessGameResult* result);
static void reversi_prepare_step(ChessInfo* info, const reversi_step_t* step);
static ChessGameResult reversi_apply_step(board_t board, const reversi_step_t* step);
static void reversi_drawstep(ChessInfo* info, const void* move);
static ChessGameResult reversi_post_game(ChessInfo* info);
static void reversi_gameend(ChessInfo* info, ChessGameResult result);
static void reversi_genlog(ChessInfo* info, FILE* fp, ChessGameResult result);

static const char    *CHESS_TYPE[] = {NONE_CHESS, HINT_CHESS, BLACK_CHESS, WHITE_CHESS};
static const char     DIRX[] = {-1, -1, -1, 0, 1, 1, 1, 0};
static const char     DIRY[] = {-1, 0, 1, 1, 1, 0, -1, -1};

static const ChessActions reversi_actions = {
    &reversi_init_user,
    &reversi_init_user_userec,
    (void (*) (void*)) &reversi_init_board,
    &reversi_drawline,
    &reversi_movecur,
    &reversi_prepare_play,
    NULL, /* process_key */
    &reversi_select,
    (void (*)(ChessInfo*, const void*)) &reversi_prepare_step,
    (ChessGameResult (*)(void*, const void*)) &reversi_apply_step,
    &reversi_drawstep,
    &reversi_post_game,
    &reversi_gameend,
    &reversi_genlog
};

static const ChessConstants reversi_constants = {
    sizeof(reversi_step_t),
    MAX_TIME,
    BRDSIZ,
    BRDSIZ,
    0,
    "黑白棋",
    "photo_reversi",
#ifdef BN_REVERSI_LOG
    BN_REVERSI_LOG,
#else
    NULL,
#endif
    { "", "" },
    { "白棋",  "黑棋" },
};

static int
can_put(board_t board, color_t who, int x, int y)
{
    int i, temp, checkx, checky;

    if (IS_BLANK(board[x][y]))
	for (i = 0; i < 8; ++i) {
	    checkx = x + DIRX[i];
	    checky = y + DIRY[i];
	    temp = board[checkx][checky];
	    if (IS_BLANK(temp))
		continue;
	    if (temp != who) {
		while (board[checkx += DIRX[i]][checky += DIRY[i]] == temp);
		if (board[checkx][checky] == who)
		    return 1;
	    }
	}
    return 0;
}

static int
caculate_hint(board_t board, color_t who)
{
    int i, j, count = 0;

    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    if (board[i][j] == HINT)
		board[i][j] = NONE;
	    if (can_put(board, who, i, j)) {
		board[i][j] = HINT;
		++count;
	    }
	}
    return count;
}

static void
reversi_init_user(const userinfo_t* uinfo, ChessUser* user)
{
    strlcpy(user->userid, uinfo->userid, sizeof(user->userid));
    user->win  = 
    user->lose = 
    user->tie  = 0;
}

static void
reversi_init_user_userec(const userec_t* urec, ChessUser* user)
{
    strlcpy(user->userid, urec->userid, sizeof(user->userid));
    user->win  =
    user->lose =
    user->tie  = 0;
}

static void
reversi_init_board(board_t board)
{
    memset(board, NONE, sizeof(board_t));
    board[4][4] = board[5][5] = WHITE;
    board[4][5] = board[5][4] = BLACK;

    caculate_hint(board, BLACK);
}

static void
reversi_drawline(const ChessInfo* info, int line){
    static const char* num_str[] =
    {"", "１", "２", "３", "４", "５", "６", "７", "８"};
    if(line)
	move(line, STARTY);

    if (line == 0) {
	prints(ANSI_COLOR(1;46) "  黑白棋對戰  " ANSI_COLOR(45)
		"%30s VS %-20s%10s" ANSI_RESET,
	       info->user1.userid, info->user2.userid,
	       info->mode == CHESS_MODE_WATCH ? "[觀棋模式]" : "");
    } else if (line == 2)
	outs("  Ａ  Ｂ  Ｃ  Ｄ  Ｅ  Ｆ  Ｇ  Ｈ");
    else if (line == 3)
	outs("┌─┬─┬─┬─┬─┬─┬─┬─┐");
    else if (line == 19)
	outs("└─┴─┴─┴─┴─┴─┴─┴─┘");
    else if (line == 20)
	prints("  (" BLACK_CHESS ") %-15s%2d%*s",
		info->myturn ? info->user1.userid : info->user2.userid,
		((reversi_tag_t*)info->tag)->number[COLOR_TO_TURN(BLACK)],
		34 - 24, "");
    else if (line == 21)
	prints("  (" WHITE_CHESS ") %-15s%2d%*s",
		info->myturn ? info->user2.userid : info->user1.userid,
		((reversi_tag_t*)info->tag)->number[COLOR_TO_TURN(WHITE)],
		34 - 24, "");
    else if (line > 3 && line < 19) {
	if ((line & 1) == 1)
	    outs("├─┼─┼─┼─┼─┼─┼─┼─┤");
	else {
	    int x = line / 2 - 1;
	    int y;
	    board_p board = (board_p) info->board;

	    move(line, STARTY - 2);
	prints("%s│", num_str[x]);
	    for(y = 1; y <= 8; ++y)
		prints("%s│", CHESS_TYPE[(int) board[x][y]]);
	}
    }

    ChessDrawExtraInfo(info, line, 4);
}

static void
reversi_movecur(int r, int c)
{
    move(r * 2 + 4, c * 4 + STARTY + 2);
}

static int
reversi_prepare_play(ChessInfo* info)
{
    int x, y;
    int result;
    board_p board = (board_p) info->board;
    reversi_tag_t* tag = (reversi_tag_t*) info->tag;

    tag->number[0] = tag->number[1] = 0;
    for(x = 1; x <= 8; ++x)
	for(y = 1; y <= 8; ++y)
	    if (IS_CHESS(board[x][y]))
		++tag->number[COLOR_TO_TURN(board[x][y])];

    result = !caculate_hint(board, TURN_TO_COLOR(info->turn));
    if (result) {
	reversi_step_t step = { .type = CHESS_STEP_SPECIAL, .color = TURN_TO_COLOR(info->turn) };
	if (info->turn == info->myturn) {
	    ChessStepSend(info, &step);
	    ChessHistoryAppend(info, &step);
	    strcpy(info->last_movestr, "你必須放棄這一步!!");
	} else {
	    ChessStepReceive(info, &step);
	    strcpy(info->last_movestr, "對方必須放棄這一步!!");
	}
    }

    ChessRedraw(info);
    return result;
}

static int
reversi_select(ChessInfo* info, rc_t loc, ChessGameResult* result GCC_UNUSED)
{
    board_p board = (board_p) info->board;

    ++loc.r; ++loc.c;
    if (can_put(board, TURN_TO_COLOR(info->turn), loc.r, loc.c)) {
	reversi_step_t step = { CHESS_STEP_NORMAL,
	    TURN_TO_COLOR(info->turn), loc };
	reversi_apply_step(board, &step);

	snprintf(info->last_movestr, sizeof(info->last_movestr),
		"%c%d", step.loc.c - 1 + 'A', step.loc.r);

	ChessStepSend(info, &step);
	ChessHistoryAppend(info, &step);

	return 1;
    } else
	return 0;
}

static ChessGameResult
reversi_apply_step(board_t board, const reversi_step_t* step)
{
    int i;
    color_t opposite = INVERT(step->color);

    if (step->type != CHESS_STEP_NORMAL)
	return CHESS_RESULT_CONTINUE;

    for (i = 0; i < 8; ++i) {
	int x = step->loc.r;
	int y = step->loc.c;

	while (board[x += DIRX[i]][y += DIRY[i]] == opposite);

	if (board[x][y] == step->color) {
	    x = step->loc.r;
	    y = step->loc.c;

	    while (board[x += DIRX[i]][y += DIRY[i]] == opposite)
		board[x][y] = step->color;
	}
    }
    board[step->loc.r][step->loc.c] = step->color;

    return CHESS_RESULT_CONTINUE;
}

static void
reversi_prepare_step(ChessInfo* info, const reversi_step_t* step)
{
    if (step->type == CHESS_STEP_NORMAL)
	snprintf(info->last_movestr, sizeof(info->last_movestr),
		"%c%d", step->loc.c - 1 + 'A', step->loc.r);
    else if (step->color == TURN_TO_COLOR(info->myturn))
	strcpy(info->last_movestr, "你必須放棄這一步!!");
    else
	strcpy(info->last_movestr, "對方必須放棄這一步!!");
}

static void
reversi_drawstep(ChessInfo* info, const void* move GCC_UNUSED)
{
    ChessRedraw(info);
}

static ChessGameResult
reversi_post_game(ChessInfo* info)
{
    int x, y;
    board_p board = (board_p) info->board;
    reversi_tag_t* tag = (reversi_tag_t*) info->tag;

    tag->number[0] = tag->number[1] = 0;
    for(x = 1; x <= 8; ++x)
	for(y = 1; y <= 8; ++y)
	    if (board[x][y] == HINT)
		board[x][y] = NONE;
	    else if (IS_CHESS(board[x][y]))
		++tag->number[COLOR_TO_TURN(board[x][y])];

    ChessRedraw(info);

    if (tag->number[0] == tag->number[1])
	return CHESS_RESULT_TIE;
    else if (tag->number[(int) info->myturn] < tag->number[info->myturn ^ 1])
	return CHESS_RESULT_LOST;
    else
	return CHESS_RESULT_WIN;
}

static void
reversi_gameend(ChessInfo* info GCC_UNUSED, ChessGameResult result GCC_UNUSED)
{
    /* nothing to do now 
     * TODO game record */
}

static void
reversi_genlog(ChessInfo* info, FILE* fp, ChessGameResult result GCC_UNUSED)
{
    char buf[ANSILINELEN] = "";
    const int nStep = info->history.used;
    int   i;
    VREFCUR cur;

    cur = vcur_save();
    for (i = 2; i <= 21; i++)
    {
	move(i, 0);
	inansistr(buf, sizeof(buf)-1);
	fprintf(fp, "%s\n", buf);
    }
    vcur_restore(cur);

    fprintf(fp, "\n");
    fprintf(fp, "按 z 可進入打譜模式\n");
    fprintf(fp, "\n");

    fprintf(fp, "<reversilog>\nblack:%s\nwhite:%s\n",
	    info->myturn ? info->user1.userid : info->user2.userid,
	    info->myturn ? info->user2.userid : info->user1.userid);

    for (i = 0; i < nStep; ++i) {
	const reversi_step_t* const step =
	    (const reversi_step_t*) ChessHistoryRetrieve(info, i);
	if (step->type == CHESS_STEP_NORMAL)
	    fprintf(fp, "[%2d]%s ==> %c%-5d", i + 1,
		    CHESS_TYPE[(int) step->color],
		    'A' + step->loc.c - 1, step->loc.r);
	else
	    fprintf(fp, "[%2d]%s ==> pass  ", i + 1,
		    CHESS_TYPE[(int) step->color]);
	if (i % 2)
	    fputc('\n', fp);
    }

    if (i % 2)
	fputc('\n', fp);
    fputs("</reversilog>\n", fp);
}

static int
reversi_loadlog(FILE *fp, ChessInfo *info)
{
    char       buf[256];

#define INVALID_ROW(R) ((R) <= 0 || (R) > 8)
#define INVALID_COL(C) ((C) <= 0 || (C) > 8)
    while (fgets(buf, sizeof(buf), fp)) {
	if (strcmp("</reversilog>\n", buf) == 0)
	    return 1;
	else if (strncmp("black:", buf, 6) == 0 || 
		strncmp("white:", buf, 6) == 0) {
	    /* /(black|white):([a-zA-Z0-9]+)/; $2 */
	    userec_t   rec;
	    ChessUser *user = (buf[0] == 'b' ? &info->user1 : &info->user2);

	    chomp(buf);
	    if (getuser(buf + 6, &rec))
		reversi_init_user_userec(&rec, user);
	} else if (buf[0] == '[') {
	    /* "[ 1]● ==> C4    [ 2]○ ==> C5"  */
	    reversi_step_t step = { .type = CHESS_STEP_NORMAL };
	    int         c, r;
	    const char *p = buf;
	    int i;
	    
	    for(i=0; i<2; i++) {
		p = strchr(p, '>');

		if (p == NULL) break;

		++p; /* skip '>' */
		while (*p && isspace(*p)) ++p;
		if (!*p) break;

		/* i=0, p -> "C4 ..." */
		/* i=1, p -> "C5\n" */

		if (strncmp(p, "pass", 4) == 0)
		    /* [..] .. => pass */
		    step.type = CHESS_STEP_SPECIAL;
		else {
		    c = p[0] - 'A' + 1;
		    r = atoi(p + 1);

		    if (INVALID_COL(c) || INVALID_ROW(r))
			break;

		    step.loc.r = r;
		    step.loc.c = c;
		}

		step.color = i==0 ? BLACK : WHITE;
		ChessHistoryAppend(info, &step);
	    }
	}
    }
#undef INVALID_ROW
#undef INVALID_COL
    return 0;
}

void
reversi(int s, ChessGameMode mode)
{
    ChessInfo*    info = NewChessInfo(&reversi_actions, &reversi_constants, s, mode);
    board_t       board;
    reversi_tag_t tag = { { 2, 2 } }; /* will be overridden */

    reversi_init_board(board);

    info->board = board;
    info->tag   = &tag;

    info->cursor.r = 3;
    info->cursor.c = 3;

    if (mode == CHESS_MODE_WATCH)
	setutmpmode(CHESSWATCHING);
    else
	setutmpmode(REVERSI);
    currutmp->sig = SIG_REVERSI;

    ChessPlay(info);

    DeleteChessInfo(info);
}

int
reversi_main(void)
{
    return ChessStartGame('r', SIG_REVERSI, "黑白棋");
}

int
reversi_personal(void)
{
    reversi(0, CHESS_MODE_PERSONAL);
    return 0;
}

int
reversi_watch(void)
{
    return ChessWatchGame(&reversi, REVERSI, "黑白棋");
}

ChessInfo*
reversi_replay(FILE* fp)
{
    ChessInfo *info;

    info = NewChessInfo(&reversi_actions, &reversi_constants,
	    0, CHESS_MODE_REPLAY);

    if(!reversi_loadlog(fp, info)) {
	DeleteChessInfo(info);
	return NULL;
    }

    info->board = malloc(sizeof(board_t));
    info->tag   = malloc(sizeof(reversi_tag_t));

    reversi_init_board(info->board);
    /* tag will be initialized later */

    return info;
}
