/* $Id: othello.c,v 1.5 2002/07/21 09:26:02 in2 Exp $ */
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

static char     nowx = 3, nowy = 3;
static char    *CHESS_TYPE[] = {NONE_CHESS, HINT_CHESS, BLACK_CHESS, WHITE_CHESS};
static char     DIRX[] = {-1, -1, -1, 0, 1, 1, 1, 0};
static char     DIRY[] = {-1, 0, 1, 1, 1, 0, -1, -1};
static char     number[2];

static char     pass = 0;
static char     if_hint = 0;
static int      think, which_table;

static char     nowboard[10][10] =
{{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, -1},
{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
static char     init_table[NR_TABLE + 1][5][5] = {
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

static char     table[NR_TABLE + 1][10][10];
static void
print_chess(int x, int y, char chess)
{
    move(STARTX - 1 + x * 2, STARTY - 2 + y * 4);
    if (chess != HINT || if_hint == 1)
	prints(CHESS_TYPE[(int)chess]);
    else
	prints(CHESS_TYPE[NONE]);
    refresh();
}

static void
printboard()
{
    int             i;

    move(STARTX, STARTY);
    prints("┌─┬─┬─┬─┬─┬─┬─┬─┐");
    for (i = 0; i < 7; i++) {
	move(STARTX + 1 + i * 2, STARTY);
	prints("│  │  │  │  │  │  │  │  │");
	move(STARTX + 2 + i * 2, STARTY);
	prints("├─┼─┼─┼─┼─┼─┼─┼─┤");
    }
    move(STARTX + 1 + i * 2, STARTY);
    prints("│  │  │  │  │  │  │  │  │");
    move(STARTX + 2 + i * 2, STARTY);
    prints("└─┴─┴─┴─┴─┴─┴─┴─┘");
    print_chess(4, 4, WHITE);
    print_chess(5, 5, WHITE);
    print_chess(4, 5, BLACK);
    print_chess(5, 4, BLACK);
    move(3, 56);
    prints("(黑)%s", cuser.userid);
    move(3, 72);
    prints(": 02");
    move(4, 56);
    prints("(白)電腦        : 02");
    move(6, 56);
    prints("＃  可以下之處");
    move(7, 56);
    prints("[q] 退出");
    move(8, 56);
    prints("[h] 開啟/關閉 提示");
    move(9, 56);
    prints("[Enter][Space] 下棋");
    move(10, 56);
    prints("上:↑, i");
    move(11, 56);
    prints("下:↓, k");
    move(12, 56);
    prints("左:←, j");
    move(13, 56);
    prints("右:→, l");
}

static int
get_key(char nowx, char nowy)
{
    int             ch;

    move(STARTX - 1 + nowx * 2, STARTY - 1 + nowy * 4);
    ch = igetkey();
    move(STARTX - 1 + nowx * 2, STARTY - 2 + nowy * 4);
    if (nowboard[(int)nowx][(int)nowy] != HINT || if_hint == 1)
	outs(CHESS_TYPE[(int)nowboard[(int)nowx][(int)nowy]]);
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
get_hint(char color)
{
    int             i, j, temp = 0;

    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    if (nowboard[i][j] == HINT)
		nowboard[i][j] = NONE;
	    if (if_can_put(i, j, color, nowboard)) {
		nowboard[i][j] = HINT;
		temp++;
	    }
	    print_chess(i, j, nowboard[i][j]);
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
end_of_game(int quit)
{
    FILE           *fp, *fp1;
    char           *opponent[] = {"", "CD-65", "", "嬰兒", "小孩", "", "大人", "專家"};

    move(STARTX - 1, 30);
    prints("                         ");
    move(22, 35);
    fp = fopen(LOGFILE, "a");
    if (!quit) {
	fp1 = fopen(SECRET, "a");
	if (fp1) {
	    fprintf(fp1, "%d,%d,%s,%02d,%02d\n", think, which_table,
		    cuser.userid, number[0], number[1]);
	    fclose(fp1);
	}
    }
    if (quit) {
	if (number[0] == 2 && number[1] == 2) {
	    if (fp)
		fclose(fp);
	    return;
	}
	fprintf(fp, "在%s級中, %s臨陣脫逃\n", opponent[think], cuser.userid);
	if (fp)
	    fclose(fp);
	return;
    }
    if (number[0] > number[1]) {
	prints("你贏了電腦%02d子", number[0] - number[1]);
	if (think == 6 && number[0] - number[1] >= 50)
	    demoney(200);
	if (think == 7 && number[0] - number[1] >= 40)
	    demoney(200);
	if (fp)
	    fprintf(fp, "在%s級中, %s以 %02d:%02d 贏了電腦%02d子\n",
		    opponent[think], cuser.userid, number[0], number[1],
		    number[0] - number[1]);
    } else if (number[1] > number[0]) {
	prints("電腦贏了你%02d子", number[1] - number[0]);
	if (fp) {
	    fprintf(fp, "在%s級中, ", opponent[think]);
	    if (number[1] - number[0] > 20)
		fprintf(fp, "電腦以 %02d:%02d 慘電%s %02d子\n", number[1],
			number[0], cuser.userid, number[1] - number[0]);
	    else
		fprintf(fp, "電腦以 %02d:%02d 贏了%s %02d子\n", number[1],
			number[0], cuser.userid, number[1] - number[0]);
	}
    } else {
	prints("你和電腦打成平手!!");
	if (fp)
	    fprintf(fp, "在%s級中, %s和電腦以 %02d:%02d 打成了平手\n",
		    opponent[think], cuser.userid, number[1], number[0]);
    }
    if (fp)
	fclose(fp);
    move(1, 1);
    igetkey();
}

static void
othello_redraw()
{
    int             i, j;

    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++)
	    print_chess(i, j, nowboard[i][j]);
}

static int
player(char color)
{
    int             ch;

    if (get_hint(color)) {
	while (true) {
	    ch = get_key(nowx, nowy);
	    switch (ch) {
	    case 'J':
	    case 'j':
	    case KEY_LEFT:
		nowy--;
		break;
	    case 'L':
	    case 'l':
	    case KEY_RIGHT:
		nowy++;
		break;
	    case 'I':
	    case 'i':
	    case KEY_UP:
		nowx--;
		break;
	    case 'K':
	    case 'k':
	    case KEY_DOWN:
		nowx++;
		break;
	    case ' ':
	    case '\r':
		if (nowboard[(int)nowx][(int)nowy] != HINT)
		    break;
		pass = 0;
		nowboard[(int)nowx][(int)nowy] = color;
		eat(nowx, nowy, color, nowboard);
		print_chess(nowx, nowy, color);
		return true;
	    case 'q':
		end_of_game(1);
		return false;
	    case 'H':
	    case 'h':
		if_hint = if_hint ^ 1;
		othello_redraw();
		break;
	    }
	    if (nowx == 9)
		nowx = 1;
	    if (nowx == 0)
		nowx = 8;
	    if (nowy == 9)
		nowy = 1;
	    if (nowy == 0)
		nowy = 8;
	}
    } else {
	pass++;
	if (pass == 1) {
	    move(23, 34);
	    prints("你必需放棄這一步!!");
	    igetch();
	    move(28, 23);
	    prints("                             ");
	} else {
	    end_of_game(0);
	    return false;
	}
    }
    return 0;
}

static void
init()
{
    int             i, j, i1, j1;

    nowx = 4;
    nowy = 4;
    number[0] = number[1] = 2;
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    i1 = 4.5 - abs(4.5 - i);
	    j1 = 4.5 - abs(4.5 - j);
	    table[0][i][j] = init_table[0][i1][j1];
	    table[1][i][j] = init_table[1][i1][j1];
	}
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++)
	    nowboard[i][j] = NONE;
    nowboard[4][4] = nowboard[5][5] = WHITE;
    nowboard[4][5] = nowboard[5][4] = BLACK;
}

static void
report()
{
    int             i, j;

    number[0] = number[1] = 0;
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++)
	    if (nowboard[i][j] == BLACK)
		number[0]++;
	    else if (nowboard[i][j] == WHITE)
		number[1]++;
    move(3, 60);
    prints("%s", cuser.userid);
    move(3, 72);
    prints(": %02d", number[0]);
    move(4, 60);
    prints("電腦        : %02d", number[1]);
}

static int
EVL(char chessboard[][10], int color, int table_number)
{
    int             points = 0, a, b;
    for (a = 1; a <= 8; a++)
	for (b = 1; b <= 8; b++)
	    if (chessboard[a][b] > HINT) {
		if (chessboard[a][b] == BLACK)
		    points += table[table_number][a][b];
		else
		    points -= table[table_number][a][b];
	    }
    return ((color == BLACK) ? points : -points);
}

static int
alphabeta(int alpha, int beta, int level, char chessboard[][10],
	  int thinkstep, int color, int table)
{
    int             i, j, k, flag = 1;
    char            tempboard[10][10];
    if (level == thinkstep + 1)
	return EVL(chessboard, (level & 1 ? color : ((color - 2) ^ 1) + 2),
		   table);
    for (i = 1; i <= 8; i++) {
	for (j = 1; j <= 8; j++) {
	    if (if_can_put(i, j, color, chessboard)) {
		flag = 0;
		memcpy(tempboard, chessboard, sizeof(char) * 100);
		eat(i, j, color, tempboard);

		k = alphabeta(alpha, beta, level + 1, tempboard, thinkstep,
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
	return EVL(chessboard, color, table);
    return ((level & 1) ? alpha : beta);
}

static int
Computer(int thinkstep, int table)
{
    int             i, j, maxi = 0, maxj = 0, level = 1;
    char            chessboard[10][10];
    int             alpha = -10000, k;
    if ((number[0] + number[1]) > 44)
	table = NR_TABLE;
    for (i = 1; i <= 8; i++)
	for (j = 1; j <= 8; j++) {
	    if (if_can_put(i, j, WHITE, nowboard)) {
		memcpy(chessboard, nowboard, sizeof(char) * 100);
		eat(i, j, WHITE, chessboard);
		k = alphabeta(alpha, 10000, level + 1, chessboard, thinkstep,
			      BLACK, table);
		if (k > alpha) {
		    alpha = k;
		    maxi = i;
		    maxj = j;
		}
	    }
	}
    if (alpha != -10000) {
	eat(maxi, maxj, WHITE, nowboard);
	pass = 0;
	nowx = maxi;
	nowy = maxj;
    } else {
	move(23, 30);
	prints("電腦放棄這一步棋!!");
	pass++;
	if (pass == 2) {
	    move(23, 24);
	    prints("                               ");
	    end_of_game(0);
	    return false;
	}
	igetch();
	move(23, 24);
	prints("                                  ");
    }
    return true;
}

static int
choose()
{
    char            thinkstep[2];

    move(2, 0);
    prints("請選擇難度:");
    move(5, 0);
    prints("(1) CD-65\n");	/* 想 1 步 */
    prints("(2) 嬰兒\n");	/* 想 3 步 */
    prints("(3) 小孩\n");	/* 想 4 步 */
    do {
	getdata(4, 0, "請選擇一個對象和您對打:(1~5)",
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
othello_main()
{
    lockreturn0(OTHELLO, LOCK_MULTI);
    clear();
    init();
    think = choose();
    showtitle("黑白棋", BBSName);
    printboard();
    which_table = rand() % NR_TABLE;
    while (true) {
	move(STARTX - 1, 30);
	prints("輪到你下了...");
	if (!player(BLACK))
	    break;
	report();
	othello_redraw();
	if (number[0] + number[1] == 64) {
	    end_of_game(0);
	    break;
	}
	move(STARTX - 1, 30);
	prints("電腦思考中...");
	refresh();
	if (!Computer(think, which_table))
	    break;
	report();
	othello_redraw();
	if (number[0] + number[1] == 64) {
	    end_of_game(0);
	    break;
	}
    }
    more(LOGFILE, YEA);
    unlockutmpmode();
    return 1;
}
