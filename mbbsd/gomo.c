/* $Id$ */
#include "bbs.h"
#include "gomo.h"

#define QCAST   int (*)(const void *, const void *)
#define BOARD_LINE_ON_SCREEN(X) ((X) + 2)

static const char* turn_color[] = { ANSI_COLOR(37;43), ANSI_COLOR(30;43) };

enum Turn {
    WHT = 0,
    BLK
};

typedef struct {
    ChessStepType type;  /* necessary one */
    int           color;
    rc_t          loc;
} gomo_step_t;

typedef char board_t[BRDSIZ][BRDSIZ];
typedef char (*board_p)[BRDSIZ];

static void gomo_init_user(const userinfo_t* uinfo, ChessUser* user);
static void gomo_init_user_userec(const userec_t* urec, ChessUser* user);
static void gomo_init_board(board_t board);
static void gomo_drawline(const ChessInfo* info, int line);
static void gomo_movecur(int r, int c);
static int  gomo_prepare_play(ChessInfo* info);
static int  gomo_select(ChessInfo* info, rc_t location,
	ChessGameResult* result);
static void gomo_prepare_step(ChessInfo* info, const gomo_step_t* step);
static ChessGameResult gomo_apply_step(board_t board, const gomo_step_t* step);
static void gomo_drawstep(ChessInfo* info, const gomo_step_t* step);
static void gomo_gameend(ChessInfo* info, ChessGameResult result);
static void gomo_genlog(ChessInfo* info, FILE* fp, ChessGameResult result);

const static ChessActions gomo_actions = {
    &gomo_init_user,
    &gomo_init_user_userec,
    (void (*)(void*)) &gomo_init_board,
    &gomo_drawline,
    &gomo_movecur,
    &gomo_prepare_play,
    NULL, /* process_key */
    &gomo_select,
    (void (*)(ChessInfo*, const void*)) &gomo_prepare_step,
    (ChessGameResult (*)(void*, const void*)) &gomo_apply_step,
    (void (*)(ChessInfo*, const void*)) &gomo_drawstep,
    NULL, /* post_game */
    &gomo_gameend,
    &gomo_genlog
};

const static ChessConstants gomo_constants = {
    sizeof(gomo_step_t),
    MAX_TIME,
    BRDSIZ,
    BRDSIZ,
    0,
    "五子棋",
    "photo_fivechess",
#ifdef GLOBAL_FIVECHESS_LOG
    GLOBAL_FIVECHESS_LOG,
#else
    NULL,
#endif
    { ANSI_COLOR(37;43), ANSI_COLOR(30;43) },
    { "白棋", "黑棋, 有禁手" },
};

/* pattern and advance map */

static int
intrevcmp(const void *a, const void *b)
{
    return (*(int *)b - *(int *)a);
}

// 以 (x,y) 為起點, 方向 (dx,dy), 傳回以 bit 表示相鄰哪幾格有子
// 如 10111 表示該方向相鄰 1,2,3 有子, 4 空地
// 最高位 1 表示對方的子, 或是牆
/* x,y: 0..BRDSIZ-1 ; color: CBLACK,CWHITE ; dx,dy: -1,0,+1 */
static int
gomo_getindex(board_t ku, int x, int y, int color, int dx, int dy)
{
    int             i, k, n;
    for (n = -1, i = 0, k = 1; i < 5; i++, k*=2) {
	x += dx;
	y += dy;

	if ((x < 0) || (x >= BRDSIZ) || (y < 0) || (y >= BRDSIZ)) {
	    n += k;
	    break;
	} else if (ku[x][y] != BBLANK) {
	    n += k;
	    if (ku[x][y] != color)
		break;
	}
    }

    if (i >= 5)
	n += k;

    return n;
}

ChessGameResult
chkwin(int style, int limit)
{
    if (style == 0x0c)
	return CHESS_RESULT_WIN;
    else if (limit == 0) {
	if (style == 0x0b)
	    return CHESS_RESULT_WIN;
	return CHESS_RESULT_CONTINUE;
    }
    if ((style < 0x0c) && (style > 0x07))
	return CHESS_RESULT_LOST;
    return CHESS_RESULT_CONTINUE;
}

static int getstyle(board_t ku, int x, int y, int color, int limit);
/* x,y: 0..BRDSIZ-1 ; color: CBLACK,CWHITE ; limit:1,0 ; dx,dy: 0,1 */
static int
dirchk(board_t ku, int x, int y, int color, int limit, int dx, int dy)
{
    int             le, ri, loc, style = 0;

    le = gomo_getindex(ku, x, y, color, -dx, -dy);
    ri = gomo_getindex(ku, x, y, color, dx, dy);

    loc = (le > ri) ? (((le * (le + 1)) >> 1) + ri) :
	(((ri * (ri + 1)) >> 1) + le);

    style = pat_gomoku[loc];

    if (limit == 0)
	return (style & 0x0f);

    style >>= 4;

    if ((style == 3) || (style == 2)) {
	int             i, n = 0, tmp, nx, ny;

	n = adv_gomoku[loc / 2];

	if(loc%2==0)
	    n/=16;
	else
	    n%=16;

	ku[x][y] = color;

	for (i = 0; i < 2; i++) {
	    if ((tmp = (i == 0) ? (-(n >> 2)) : (n & 3)) != 0) {
		nx = x + (le > ri ? 1 : -1) * tmp * dx;
		ny = y + (le > ri ? 1 : -1) * tmp * dy;

		if ((dirchk(ku, nx, ny, color, 0, dx, dy) == 0x06) &&
		    (chkwin(getstyle(ku, nx, ny, color, limit), limit) >= 0))
		    break;
	    }
	}
	if (i >= 2)
	    style = 0;
	ku[x][y] = BBLANK;
    }
    return style;
}

/* 例外=F 錯誤=E 有子=D 連五=C 連六=B 雙四=A 四四=9 三三=8 */
/* 四三=7 活四=6 斷四=5 死四=4 活三=3 斷三=2 保留=1 無效=0 */

/* x,y: 0..BRDSIZ-1 ; color: CBLACK,CWHITE ; limit: 1,0 */
static int
getstyle(board_t ku, int x, int y, int color, int limit)
{
    int             i, j, dir[4], style;

    if ((x < 0) || (x >= BRDSIZ) || (y < 0) || (y >= BRDSIZ))
	return 0x0f;
    if (ku[x][y] != BBLANK)
	return 0x0d;

    // (-1,1), (0,1), (1,0), (1,1)
    for (i = 0; i < 4; i++)
	dir[i] = dirchk(ku, x, y, color, limit, i ? (i >> 1) : -1, i ? (i & 1) : 1);

    qsort(dir, 4, sizeof(int), (QCAST)intrevcmp);

    if ((style = dir[0]) >= 2) {
	for (i = 1, j = 6 + (limit ? 1 : 0); i < 4; i++) {
	    if ((style > j) || (dir[i] < 2))
		break;
	    if (dir[i] > 3)
		style = 9;
	    else if ((style < 7) && (style > 3))
		style = 7;
	    else
		style = 8;
	}
    }
    return style;
}

static char*
gomo_move_warn(int style, char buf[])
{
    char *xtype[] = {
	ANSI_COLOR(1;31) "跳三" ANSI_RESET,
	ANSI_COLOR(1;31) "活三" ANSI_RESET,
	ANSI_COLOR(1;31) "死四" ANSI_RESET,
	ANSI_COLOR(1;31) "跳四" ANSI_RESET,
	ANSI_COLOR(1;31) "活四" ANSI_RESET,
	ANSI_COLOR(1;31) "四三" ANSI_RESET,
	ANSI_COLOR(1;31) "雙三" ANSI_RESET,
	ANSI_COLOR(1;31) "雙四" ANSI_RESET,
	ANSI_COLOR(1;31) "雙四" ANSI_RESET,
	ANSI_COLOR(1;31) "連六" ANSI_RESET,
	ANSI_COLOR(1;31) "連五" ANSI_RESET
    };
    if (style > 1 && style < 13)
	return strcpy(buf, xtype[style - 2]);
    else
	return NULL;
}

static void
gomoku_usr_put(userec_t* userec, const ChessUser* user)
{
    userec->five_win = user->win;
    userec->five_lose = user->lose;
    userec->five_tie = user->tie;
}

static char*
gomo_getstep(const gomo_step_t* step, char buf[])
{
    const static char* const ColName = "ＡＢＣＤＥＦＧＨＩＪＫＬＭＮ";
    const static char* const RawName = "151413121110９８７６５４３２１";
    const static int ansi_length     = sizeof(ANSI_COLOR(30;43)) - 1;

    strcpy(buf, turn_color[step->color]);
    buf[ansi_length    ] = ColName[step->loc.c * 2];
    buf[ansi_length + 1] = ColName[step->loc.c * 2 + 1];
    buf[ansi_length + 2] = RawName[step->loc.r * 2];
    buf[ansi_length + 3] = RawName[step->loc.r * 2 + 1];
    strcpy(buf + ansi_length + 4, ANSI_RESET);

    return buf;
}

static void
gomo_init_user(const userinfo_t* uinfo, ChessUser* user)
{
    strlcpy(user->userid, uinfo->userid, sizeof(user->userid));
    user->win  = uinfo->five_win;
    user->lose = uinfo->five_lose;
    user->tie  = uinfo->five_tie;
}

static void
gomo_init_user_userec(const userec_t* urec, ChessUser* user)
{
    strlcpy(user->userid, urec->userid, sizeof(user->userid));
    user->win  = urec->five_win;
    user->lose = urec->five_lose;
    user->tie  = urec->five_tie;
}

static void
gomo_init_board(board_t board)
{
    memset(board, BBLANK, sizeof(board_t));
}

static void
gomo_drawline(const ChessInfo* info, int line)
{
    const static char* const BoardPic[] = {
	"╔", "╤", "╤", "╗",
	"╟", "┼", "┼", "╢",
	"╟", "┼", "＋", "╢",
	"╚", "╧", "╧", "╝",
    };
    const static int BoardPicIndex[] =
    { 0, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 3 };

    board_p board = (board_p) info->board;

    if (line == 0) {
	prints(ANSI_COLOR(1;46) "  五子棋對戰  " ANSI_COLOR(45)
		"%30s VS %-20s%10s" ANSI_RESET,
	       info->user1.userid, info->user2.userid,
	       info->mode == CHESS_MODE_WATCH ? "[觀棋模式]" : "");
    } else if (line == 1) {
	outs("    A B C D E F G H I J K L M N");
    } else if (line >= 2 && line <= 16) {
	const int board_line = line - 2;
	const char* const* const pics =
	    &BoardPic[BoardPicIndex[board_line] * 4];
	int i;

	prints("%3d" ANSI_COLOR(30;43), 17 - line);

	for (i = 0; i < BRDSIZ; ++i)
	    if (board[board_line][i] == -1)
		outs(pics[BoardPicIndex[i]]);
	    else
		outs(bw_chess[(int) board[board_line][i]]);

	outs(ANSI_RESET);
    } else if (line >= 17 && line < b_lines)
	prints("%33s", "");

    ChessDrawExtraInfo(info, line, 8);
}

static void
gomo_movecur(int r, int c)
{
    move(r + 2, c * 2 + 3);
}

static int
gomo_prepare_play(ChessInfo* info)
{
    if (!gomo_move_warn(*(int*) info->tag, info->warnmsg))
	info->warnmsg[0] = 0;

    return 0;
}

static int
gomo_select(ChessInfo* info, rc_t location, ChessGameResult* result)
{
    board_p     board = (board_p) info->board;
    gomo_step_t step;

    if(board[location.r][location.c] != BBLANK)
	return 0;

    *(int*) info->tag = getstyle(board, location.r, location.c,
	    info->turn, info->turn == BLK);
    *result   = chkwin(*(int*) info->tag, info->turn == BLK);

    board[location.r][location.c] = info->turn;

    step.type  = CHESS_STEP_NORMAL;
    step.color = info->turn;
    step.loc   = location;
    gomo_getstep(&step, info->last_movestr);

    ChessHistoryAppend(info, &step);
    ChessStepSend(info, &step);
    gomo_drawstep(info, &step);

    return 1;
}

static void
gomo_prepare_step(ChessInfo* info, const gomo_step_t* step)
{
    if (step->type == CHESS_STEP_NORMAL) {
	gomo_getstep(step, info->last_movestr);
	*(int*) info->tag = getstyle(info->board, step->loc.r, step->loc.c,
		step->color, step->color == BLK);
	info->cursor = step->loc;
    }
}

static ChessGameResult
gomo_apply_step(board_t board, const gomo_step_t* step)
{
    int style;

    style = getstyle(board, step->loc.r, step->loc.c,
	    step->color, step->color == BLK);
    board[step->loc.r][step->loc.c] = step->color;
    return chkwin(style, step->color == BLK);
}

static void
gomo_drawstep(ChessInfo* info, const gomo_step_t* step)
{
    ChessDrawLine(info, BOARD_LINE_ON_SCREEN(step->loc.r));
}

static void
gomo_gameend(ChessInfo* info, ChessGameResult result)
{
    if (info->mode == CHESS_MODE_VERSUS) {
	ChessUser* const user1 = &info->user1;
	/* ChessUser* const user2 = &info->user2; */

	user1->lose--;
	if (result == CHESS_RESULT_WIN) {
	    user1->win++;
	    currutmp->five_win++;
	} else if (result == CHESS_RESULT_LOST) {
	    user1->lose++;
	    currutmp->five_lose++;
	} else {
	    user1->tie++;
	    currutmp->five_tie++;
	}

	cuser.five_win  = user1->win;
	cuser.five_lose = user1->lose;
	cuser.five_tie  = user1->tie;

	passwd_update(usernum, &cuser);
    } else if (info->mode == CHESS_MODE_REPLAY) {
	free(info->board);
	free(info->tag);
    }
}

static void
gomo_genlog(ChessInfo* info, FILE* fp, ChessGameResult result)
{
    const int nStep = info->history.used;
    int       i;

    if(big_picture)
    for (i = 1; i <= 18; i++)
	fprintf(fp, "%.*s\n", big_picture[i].len, big_picture[i].data);

    fprintf(fp, "\n");
    fprintf(fp, "按 z 可進入打譜模式\n");
    fprintf(fp, "\n");

    fprintf(fp, "<gomokulog>\nblack:%s\nwhite:%s\n",
	    info->myturn ? info->user1.userid : info->user2.userid,
	    info->myturn ? info->user2.userid : info->user1.userid);

    for (i = 0; i < nStep; ++i) {
	const gomo_step_t* const step =
	    (const gomo_step_t*) ChessHistoryRetrieve(info, i);
	if (step->type == CHESS_STEP_NORMAL)
	    fprintf(fp, "[%2d]%s ==> %c%-5d", i + 1, bw_chess[step->color],
		    'A' + step->loc.c, 15 - step->loc.r);
	else
	    break;
	if (i % 2)
	    fputc('\n', fp);
    }

    if (i % 2)
	fputc('\n', fp);
    fputs("</gomokulog>\n", fp);
}

static int gomo_loadlog(FILE *fp, ChessInfo *info)
{
    char       buf[256];

#define INVALID_ROW(R) ((R) < 0 || (R) >= BRDSIZ)
#define INVALID_COL(C) ((C) < 0 || (C) >= BRDSIZ)
    while (fgets(buf, sizeof(buf), fp)) {
	if (strcmp("</gomokulog>\n", buf) == 0)
	    return 1;
	else if (strncmp("black:", buf, 6) == 0 || 
		strncmp("white:", buf, 6) == 0) {
	    /* /(black|white):([a-zA-Z0-9]+)/; $2 */
	    userec_t   rec;
	    ChessUser *user = (buf[0] == 'b' ? &info->user1 : &info->user2);

	    chomp(buf);
	    if (getuser(buf + 6, &rec))
		gomo_init_user_userec(&rec, user);
	} else if (buf[0] == '[') {
	    /* "[ 1]● ==> H8    [ 2]○ ==> H9"  */
	    gomo_step_t step = { CHESS_STEP_NORMAL };
	    int         c, r;
	    const char *p = buf;
	    int i;
	    
	    for(i=0; i<2; i++) {
		p = strchr(p, '>');

		if (p == NULL) break;

		++p; /* skip '>' */
		while (*p && isspace(*p)) ++p;
		if (!*p) break;

		/* i=0, p -> "H8 ..." */
		/* i=1, p -> "H9\n" */
		c = p[0] - 'A';
		r = BRDSIZ - atoi(p + 1);

		if (INVALID_COL(c) || INVALID_ROW(r))
		    break;

		step.color = i==0? BLK : WHT;
		step.loc.r = r;
		step.loc.c = c;
		ChessHistoryAppend(info, &step);
	    }
	}
    }
#undef INVALID_ROW
#undef INVALID_COL
    return 0;
}


void
gomoku(int s, ChessGameMode mode)
{
    ChessInfo* info = NewChessInfo(&gomo_actions, &gomo_constants, s, mode);
    board_t    board;
    int        tag;

    gomo_init_board(board);
    tag = 0;

    info->board = board;
    info->tag   = &tag;

    info->cursor.r = 7;
    info->cursor.c = 7;

    if (info->mode == CHESS_MODE_VERSUS) {
	/* Assume that info->user1 is me. */
	info->user1.lose++;
	passwd_query(usernum, &cuser);
	gomoku_usr_put(&cuser, &info->user1);
	passwd_update(usernum, &cuser);
    }

    if (mode == CHESS_MODE_WATCH)
	setutmpmode(CHESSWATCHING);
    else
	setutmpmode(M_FIVE);
    currutmp->sig = SIG_GOMO;

    ChessPlay(info);

    DeleteChessInfo(info);
}

int
gomoku_main(void)
{
    return ChessStartGame('f', SIG_GOMO, "五子棋");
}

int
gomoku_personal(void)
{
    gomoku(0, CHESS_MODE_PERSONAL);
    return 0;
}

int
gomoku_watch(void)
{
    return ChessWatchGame(&gomoku, M_FIVE, "五子棋");
}

ChessInfo*
gomoku_replay(FILE* fp)
{
    ChessInfo *info;

    info = NewChessInfo(&gomo_actions, &gomo_constants,
	    0, CHESS_MODE_REPLAY);

    if(!gomo_loadlog(fp, info)) {
	DeleteChessInfo(info);
	return NULL;
    }

    info->board = malloc(sizeof(board_t));
    info->tag   = malloc(sizeof(int));

    gomo_init_board(info->board);
    *(int*)(info->tag) = 0;

    return info;
}
