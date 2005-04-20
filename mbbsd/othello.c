/* $Id$ */
#include "bbs.h"

#define LOGFILE "etc/othello.log"
#define SECRET "etc/othello.secret"
#define NR_TABLE 2

#define true 1
#define false 0
#define STARTX 3
#define STARTY 20
#define NONE_CHESS "  "
#define WHITE_CHESS "●"
#define BLACK_CHESS "○"
#define HINT_CHESS "＃"
#define NONE  0
#define HINT  1
#define BLACK 2
#define WHITE 3

#define INVERT(COLOR) (((COLOR))==WHITE?BLACK:WHITE)

struct OthelloData {
	char     nowx, nowy;
	char     number[2];

	char     pass;
	char     if_hint;
	int      think, which_table;

	char     nowboard[10][10];
	char     evaltable[NR_TABLE + 1][10][10];
};

static const char    *CHESS_TYPE[] = {NONE_CHESS, HINT_CHESS, BLACK_CHESS, WHITE_CHESS};
static const char     DIRX[] = {-1, -1, -1, 0, 1, 1, 1, 0};
static const char     DIRY[] = {-1, 0, 1, 1, 1, 0, -1, -1};
static const char     init_table[NR_TABLE + 1][5][5] = {
    {{0, 0, 0, 0, 0},
    {0, 30, -3, 2, 2},
    {0, -3, -3, -1, -1},
    {0, 2, -1, 1, 1},
    {0, 2, -1, 1, 0}},

    {{0, 0, 0, 0, 0},
    {0, 70, 5, 20, 30},
    {0, 5, -5, 3, 3},
    {0, 20, 3, 5, 5},
    {0, 30, 3, 5, 5}},

    {{0, 0, 0, 0, 0},
    {0, 5, 2, 2, 2},
    {0, 2, 1, 1, 1},
    {0, 2, 1, 1, 1},
    {0, 2, 1, 1, 1}}
};

static void
print_chess(struct OthelloData *od, int x, int y, char chess)
{
    move(STARTX - 1 + x * 2, STARTY - 2 + y * 4);
    if (chess != HINT || od->if_hint == 1)
	outs(CHESS_TYPE[(int)chess]);
    else
	outs(CHESS_TYPE[NONE]);
    refresh();
}

static void
printboard(struct OthelloData *od)
{
    int             i;

    move(STARTX, STARTY);
    outs("┌─┬─┬─┬─┬─┬─┬─┬─┐");
    for (i = 0; i < 7; i++) {
	move(STARTX + 1 + i * 2, STARTY);
	outs("│  │  │  │  │  │  │  │  │");
	move(STARTX + 2 + i * 2, STARTY);
	outs("├─┼─┼─┼─┼─┼─┼─┼─┤");
    }
    move(STARTX + 1 + i * 2, STARTY);
    outs("│  │  │  │  │  │  │  │  │");
    move(STARTX + 2 + i * 2, STARTY);
    outs("└─┴─┴─┴─┴─┴─┴─┴─┘");
    print_chess(od, 4, 4, WHITE);
    print_chess(od, 5, 5, WHITE);
    print_chess(od, 4, 5, BLACK);
    print_chess(od, 5, 4, BLACK);
    move(3, 56);
    prints("(黑)%s", cuser.userid);
    move(3, 72);
    outs(": 02");
    move(4, 56);
    outs("(白)電腦        : 02");
    move(6, 56);
    outs("＃  可以下之處");
    move(7, 56);
    outs("[q] 退出");
    move(8, 56);
    outs("[h] 開啟/關閉 提示");
    move(9, 56);
    outs("[Enter][Space] 下棋");
    move(10, 56);
    outs("上:↑, i");
    move(11, 56);
    outs("下:↓, k");
    move(12, 56);
    outs("左:←, j");
    move(13, 56);
    outs("右:→, l");
}

static int
get_key(struct OthelloData *od, int x, int y)
{
    int             ch;

    move(STARTX - 1 + x * 2, STARTY - 1 + y * 4);
    ch = igetch();
    move(STARTX - 1 + x * 2, STARTY - 2 + y * 4);
    if (od->nowboard[x][y] != HINT || od->if_hint == 1)
	outs(CHESS_TYPE[(int)od->nowboard[x][y]]);
    else
	outs(CHESS_TYPE[NONE]);
    return ch;
}

static int
eatline(int i, int j, char color, int dir, char chessboard[][10])
{
    int             tmpx, tmpy;
    char            tmpchess;

    tmpx = i + DIRX[dir];
    tmpy = j + DIRY[dir];
    tmpchess = chessboard[tmpx][tmpy];
    if (tmpchess == -1)
	return false;
    if (tmpchess != INVERT(color))
	return false;

    tmpx += DIRX[dir];
    tmpy += DIRY[dir];
    tmpchess = chessboard[tmpx][tmpy];
    while (tmpchess != -1) {
	if (tmpchess < BLACK)
	    return false;
	if (tmpchess == color) {
	    while (i != tmpx || j != tmpy) {
		chessboard[i][j] = color;
		i += DIRX[dir];
		j += DIRY[dir];
	    }
	    return true;
	}
	tmpx += DIRX[dir];
	tmpy += DIRY[dir];
	tmpchess = chessboard[tmpx][tmpy];
    }
    return false;
}

static int
if_can_put(int x, int y, char color, char chessboard[][10])
{
    int             i, temp, checkx, checky;

    if (chessboard[x][y] < BLACK)
	for (i = 0; i < 8; i++) {
	    checkx = x + DIRX[i];
	    checky = y + DIRY[i];
	    temp = chessboard[checkx][checky];
	    if (temp < BLACK)
		continue;
	    if (temp != color)
		while (chessboard[checkx += DIRX[i]][checky += DIRY[i]] > HINT)
		    if (chessboard[checkx][checky] == color)
			return true;
	}
    return false;
}

static int
get_hint(struct OthelloData *od, char color)
{
    int             i, j, temp = 0;

    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    if (od->nowboard[i][j] == HINT)
		od->nowboard[i][j] = NONE;
	    if (if_can_put(i, j, color, od->nowboard)) {
		od->nowboard[i][j] = HINT;
		temp++;
	    }
	    print_chess(od, i, j, od->nowboard[i][j]);
	}
    return temp;
}

static void
eat(int x, int y, int color, char chessboard[][10])
{
    int             k;

    for (k = 0; k < 8; k++)
	eatline(x, y, color, k, chessboard);
}

static void
end_of_game(struct OthelloData *od, int quit)
{
    FILE           *fp, *fp1;
    char           *opponent[] = {"", "CD-65", "", "嬰兒", "小孩", "", "大人", "專家"};

    move(STARTX - 1, 30);
    outs("                         ");
    move(22, 35);
    fp = fopen(LOGFILE, "a");
    if (!quit) {
	fp1 = fopen(SECRET, "a");
	if (fp1) {
	    fprintf(fp1, "%d,%d,%s,%02d,%02d\n", od->think, od->which_table,
		    cuser.userid, od->number[0], od->number[1]);
	    fclose(fp1);
	}
    }
    if (quit) {
	if (od->number[0] == 2 && od->number[1] == 2) {
	    if (fp)
		fclose(fp);
	    return;
	}
	fprintf(fp, "在%s級中, %s臨陣脫逃\n", opponent[od->think], cuser.userid);
	if (fp)
	    fclose(fp);
	return;
    }
    if (od->number[0] > od->number[1]) {
	prints("你贏了電腦%02d子", od->number[0] - od->number[1]);
	if (od->think == 6 && od->number[0] - od->number[1] >= 50)
	    demoney(200);
	if (od->think == 7 && od->number[0] - od->number[1] >= 40)
	    demoney(200);
	if (fp)
	    fprintf(fp, "在%s級中, %s以 %02d:%02d 贏了電腦%02d子\n",
		    opponent[od->think], cuser.userid, od->number[0], od->number[1],
		    od->number[0] - od->number[1]);
    } else if (od->number[1] > od->number[0]) {
	prints("電腦贏了你%02d子", od->number[1] - od->number[0]);
	if (fp) {
	    fprintf(fp, "在%s級中, ", opponent[od->think]);
	    if (od->number[1] - od->number[0] > 20)
		fprintf(fp, "電腦以 %02d:%02d 慘電%s %02d子\n", od->number[1],
			od->number[0], cuser.userid, od->number[1] - od->number[0]);
	    else
		fprintf(fp, "電腦以 %02d:%02d 贏了%s %02d子\n", od->number[1],
			od->number[0], cuser.userid, od->number[1] - od->number[0]);
	}
    } else {
	outs("你和電腦打成平手!!");
	if (fp)
	    fprintf(fp, "在%s級中, %s和電腦以 %02d:%02d 打成了平手\n",
		    opponent[od->think], cuser.userid, od->number[1], od->number[0]);
    }
    if (fp)
	fclose(fp);
    move(1, 1);
    igetch();
}

static void
othello_redraw(struct OthelloData *od)
{
    int             i, j;

    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++)
	    print_chess(od, i, j, od->nowboard[i][j]);
}

static int
player(struct OthelloData *od, char color)
{
    int             ch;

    if (get_hint(od, color)) {
	while (true) {
	    ch = get_key(od, od->nowx, od->nowy);
	    switch (ch) {
	    case 'J':
	    case 'j':
	    case KEY_LEFT:
		od->nowy--;
		break;
	    case 'L':
	    case 'l':
	    case KEY_RIGHT:
		od->nowy++;
		break;
	    case 'I':
	    case 'i':
	    case KEY_UP:
		od->nowx--;
		break;
	    case 'K':
	    case 'k':
	    case KEY_DOWN:
		od->nowx++;
		break;
	    case ' ':
	    case '\r':
		if (od->nowboard[(int)od->nowx][(int)od->nowy] != HINT)
		    break;
		od->pass = 0;
		od->nowboard[(int)od->nowx][(int)od->nowy] = color;
		eat(od->nowx, od->nowy, color, od->nowboard);
		print_chess(od, od->nowx, od->nowy, color);
		return true;
	    case 'q':
		end_of_game(od, 1);
		return false;
	    case 'H':
	    case 'h':
		od->if_hint = od->if_hint ^ 1;
		othello_redraw(od);
		break;
	    }
	    if (od->nowx == 9)
		od->nowx = 1;
	    if (od->nowx == 0)
		od->nowx = 8;
	    if (od->nowy == 9)
		od->nowy = 1;
	    if (od->nowy == 0)
		od->nowy = 8;
	}
    } else {
	od->pass++;
	if (od->pass == 1) {
	    move(23, 34);
	    outs("你必需放棄這一步!!");
	    igetch();
	    move(28, 23);
	    outs("                             ");
	} else {
	    end_of_game(od,0);
	    return false;
	}
    }
    return 0;
}

static void
init(struct OthelloData *od)
{
    int             i, j, i1, j1;

    memset(od, 0, sizeof(struct OthelloData));
    od->nowx = 4;
    od->nowy = 4;
    od->number[0] = od->number[1] = 2;
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    i1 = 4.5 - abs(4.5 - i);
	    j1 = 4.5 - abs(4.5 - j);
	    od->evaltable[0][i][j] = init_table[0][i1][j1];
	    od->evaltable[1][i][j] = init_table[1][i1][j1];
	}
    memset(od->nowboard, NONE, sizeof(od->nowboard));
    for(i=0;i<10;i++)
	od->nowboard[i][0]=od->nowboard[0][i]=od->nowboard[i][9]=od->nowboard[9][i]=-1;
    od->nowboard[4][4] = od->nowboard[5][5] = WHITE;
    od->nowboard[4][5] = od->nowboard[5][4] = BLACK;
}

static void
report(struct OthelloData *od)
{
    int             i, j;

    od->number[0] = od->number[1] = 0;
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++)
	    if (od->nowboard[i][j] == BLACK)
		od->number[0]++;
	    else if (od->nowboard[i][j] == WHITE)
		od->number[1]++;
    move(3, 60);
    outs(cuser.userid);
    move(3, 72);
    prints(": %02d", od->number[0]);
    move(4, 60);
    prints("電腦        : %02d", od->number[1]);
}

static int
EVL(struct OthelloData *od, char chessboard[][10], int color, int table_number)
{
    int             points = 0, a, b;
    for (a = 1; a <= 8; a++)
	for (b = 1; b <= 8; b++)
	    if (chessboard[a][b] > HINT) {
		if (chessboard[a][b] == BLACK)
		    points += od->evaltable[table_number][a][b];
		else
		    points -= od->evaltable[table_number][a][b];
	    }
    return ((color == BLACK) ? points : -points);
}

static int
alphabeta(struct OthelloData *od, int alpha, int beta, int level, char chessboard[][10],
	  int thinkstep, int color, int table)
{
    int             i, j, k, flag = 1;
    char            tempboard[10][10];
    if (level == thinkstep + 1)
	return EVL(od, chessboard, (level & 1 ? color : ((color - 2) ^ 1) + 2),
		   table);
    for (i = 1; i <= 8; i++) {
	for (j = 1; j <= 8; j++) {
	    if (if_can_put(i, j, color, chessboard)) {
		flag = 0;
		memcpy(tempboard, chessboard, sizeof(char) * 100);
		eat(i, j, color, tempboard);

		k = alphabeta(od, alpha, beta, level + 1, tempboard, thinkstep,
			      ((color - 2) ^ 1) + 2, table);
		if (((level & 1) && k > alpha))
		    alpha = k;
		else if (!(level & 1) && k < beta)
		    beta = k;
		if (alpha >= beta)
		    break;
	    }
	}
    }
    if (flag)
	return EVL(od, chessboard, color, table);
    return ((level & 1) ? alpha : beta);
}

static int
Computer(struct OthelloData *od, int thinkstep, int table)
{
    int             i, j, maxi = 0, maxj = 0, level = 1;
    char            chessboard[10][10];
    int             alpha = -10000, k;
    if ((od->number[0] + od->number[1]) > 44)
	table = NR_TABLE;
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    if (if_can_put(i, j, WHITE, od->nowboard)) {
		memcpy(chessboard, od->nowboard, sizeof(char) * 100);
		eat(i, j, WHITE, chessboard);
		k = alphabeta(od, alpha, 10000, level + 1, chessboard, thinkstep,
			      BLACK, table);
		if (k > alpha) {
		    alpha = k;
		    maxi = i;
		    maxj = j;
		}
	    }
	}
    if (alpha != -10000) {
	eat(maxi, maxj, WHITE, od->nowboard);
	od->pass = 0;
	od->nowx = maxi;
	od->nowy = maxj;
    } else {
	move(23, 30);
	outs("電腦放棄這一步棋!!");
	od->pass++;
	if (od->pass == 2) {
	    move(23, 24);
	    outs("                               ");
	    end_of_game(od, 0);
	    return false;
	}
	igetch();
	move(23, 24);
	outs("                                  ");
    }
    return true;
}

static int
choose(void)
{
    char            thinkstep[2];

    move(2, 0);
    outs("請選擇難度:");
    move(5, 0);
    outs("(1) CD-65\n");	/* 想 1 步 */
    outs("(2) 嬰兒\n");	/* 想 3 步 */
    outs("(3) 小孩\n");	/* 想 4 步 */
    do {
	getdata(4, 0, "請選擇一個對象和您對打:(1~3)",
		thinkstep, sizeof(thinkstep), LCECHO);
    } while (thinkstep[0] < '1' || thinkstep[0] > '3');
    clear();
    switch (thinkstep[0]) {
    case '2':
	thinkstep[0] = '3';
	break;
    case '3':
	thinkstep[0] = '4';
	break;
    default:
	thinkstep[0] = '1';
	break;
    }
    return atoi(thinkstep);
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0

int
othello_main(void)
{
    struct OthelloData *od;

    lockreturn0(OTHELLO, LOCK_MULTI);

    od=(struct OthelloData*)malloc(sizeof(struct OthelloData));
    if(od==NULL) {
	unlockutmpmode();
	return 0;
    }

    clear();
    init(od);
    od->think = choose();
    showtitle("黑白棋", BBSName);
    printboard(od);
    od->which_table = random() % NR_TABLE;
    while (true) {
	move(STARTX - 1, 30);
	outs("輪到你下了...");
	if (!player(od, BLACK))
	    break;
	report(od);
	othello_redraw(od);
	if (od->number[0] + od->number[1] == 64) {
	    end_of_game(od, 0);
	    break;
	}
	move(STARTX - 1, 30);
	outs("電腦思考中...");
	refresh();
	if (!Computer(od, od->think, od->which_table))
	    break;
	report(od);
	othello_redraw(od);
	if (od->number[0] + od->number[1] == 64) {
	    end_of_game(od, 0);
	    break;
	}
    }
    more(LOGFILE, YEA);
    unlockutmpmode();
    free(od);
    return 1;
}
