/* $Id$ */
/*-------------------------------------------------------*/
/* go.c                         ( NTU FPG BBS Ver 1.00 ) */
/*-------------------------------------------------------*/
/* target : 圍棋                                         */
/* create : 01/09/00                                     */
/*-------------------------------------------------------*/

#include "bbs.h"
#include <sys/socket.h> 

#define BBLANK 	(0)  	/* 空白 */
#define BBLACK 	(1)  	/* 黑子 */
#define BWHITE 	(2)  	/* 白子 */

#define redoln() redoscr()

#define BRDSIZ 	(19) 	/* 棋盤單邊大小 */

/* FIXME 當游標在棋盤邊緣, 再繼續移, cursor 會跑掉 */
#define move(y,x)	move(y, (x) + ((y) < 2 || (y) > 20 ? 0 : \
			(x) > 43 ? 11 : 8))
#define iBGOTO(x,y)	move(20 - (y) , (x) * 2 + 4 - 8),do_move(20-(y),(x)*2+4)  // really dirty ><
#define BGOTO(x,y)	move(20 - (y) , (x) * 2 + 4),do_move(20-(y),(x)*2+4)

struct GOData {
    unsigned char	go[BRDSIZ][BRDSIZ];
    unsigned char	l[BRDSIZ][BRDSIZ];
    unsigned char	ml[BRDSIZ][BRDSIZ];
    Horder_t pool[500];
    int	lib, mik, mjk, hik, hjk, mk, hk;
    float	win;
    char	AB[41];

    unsigned char 	me, he, hand;
};

//extern 	char	*bw_chess[];	/* 和五子棋共用 */

static char	* const locE = "ABCDEFGHJKLMNOPQRST";


static Horder_t	*v;

static void
GO_init(struct GOData *gd)
{
    memset(gd, 0, sizeof(struct GOData));
    v = gd->pool;
}

static void
GO_add(struct GOData *gd, Horder_t *mv)
{
    if (v < gd->pool + 500) 
	*v++ = *mv;
}

static int
GO_sethand(struct GOData *gd, int i)
{
    int	j;
    char	tmp[5];
    unsigned char (*go)[BRDSIZ]=gd->go;

    if (i > 0 && i < 10)
    {
	move(1, 71);
	prints("讓子：%d", i);   

	gd->win = 0;
	go[3][3] = BBLACK;    
	if (i > 1)
	{
	    go[15][15] = BBLACK;
	    if (i > 2)
	    {
		go[3][15] = BBLACK;
		if (i > 3)
		{
		    go[15][3] = BBLACK;
		    if (i == 5)
			go[9][9] = BBLACK;
		    else
			if (i > 5)
			{
			    go[9][15] = BBLACK;
			    go[9][3] = BBLACK;
			    if (i == 7)
				go[9][9] = BBLACK;
			    else
				if (i > 7)
				{
				    go[15][9] = BBLACK;
				    go[3][9] = BBLACK;
				    if (i > 8) 
					go[9][9] = BBLACK;
				}
			}
		}
	    }
	}
    }
    else
	return 0;

    gd->hand = i;
    *gd->AB = 0;
    for (i = 0;i < 19;i++)
	for (j = 0;j < 19;j++)
	    if (go[i][j])
	    {
		BGOTO(i, j);
		outs(bw_chess[go[i][j] - 1]);
		redoln(); 
		sprintf(tmp, "[%c%c]", 'a' + i, 's' - j);
		strcat(gd->AB, tmp);
	    }
    return 1;
}


static void
GO_count(struct GOData *gd, int x, int y, int color)
{
    unsigned char (*go)[BRDSIZ]=gd->go;
    unsigned char (*ml)[BRDSIZ]=gd->ml;
    ml[x][y] = 0;

    if (x != 0)
    {
	if ((go[x - 1][y] == BBLANK) && ml[x - 1][y])
	{
	    ++gd->lib;
	    ml[x - 1][y] = 0;
	}
	else
	    if ((go[x - 1][y] == color) && ml[x - 1][y])
		GO_count(gd, x - 1, y, color);
    }
    if (x != 18)
    {
	if ((go[x + 1][y] == BBLANK) && ml[x + 1][y])
	{
	    ++gd->lib;
	    ml[x + 1][y] = 0;
	}
	else
	    if ((go[x + 1][y] == color) && ml[x + 1][y])
		GO_count(gd, x + 1, y, color);
    }
    if (y != 0)
    {
	if ((go[x][y - 1] == BBLANK) && ml[x][y - 1])
	{
	    ++gd->lib;
	    ml[x][y - 1] = 0; 
	}
	else
	    if ((go[x][y - 1] == color) && ml[x][y - 1])
		GO_count(gd, x, y - 1, color);
    }
    if (y != 18)
    {
	if ((go[x][y + 1] == BBLANK) && ml[x][y + 1])
	{
	    ++gd->lib; 
	    ml[x][y + 1] = 0;
	}
	else
	    if ((go[x][y + 1] == color) && ml[x][y + 1])
		GO_count(gd, x, y + 1, color);
    }
}

static void
GO_countlib(struct GOData *gd, int x, int y, char color)
{
    int i, j;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    gd->ml[i][j] = 1;

    GO_count(gd, x, y, color);
}

static void 
GO_eval(struct GOData *gd, char color)
{
    int i, j;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    if (gd->go[i][j] == color)
	    {
		gd->lib = 0;
		GO_countlib(gd, i, j, color);
		gd->l[i][j] = gd->lib;
	    }
}

static int 
GO_check(struct GOData *gd, Horder_t *mv)
{
    int m, n, k;
    unsigned char (*go)[BRDSIZ]=gd->go;
    unsigned char (*l)[BRDSIZ]=gd->l;

    gd->lib = 0;
    GO_countlib(gd, mv->x, mv->y, gd->me);

    if (gd->lib == 0)
    {
	go[(int)mv->x][(int)mv->y] = gd->me;

	GO_eval(gd, gd->he);
	k = 0;

	for (m = 0; m < 19; m++)
	    for (n = 0; n < 19; n++)
		if ((go[m][n] == gd->he) && !l[m][n]) ++k;

	if ((k == 0) || (k == 1 && ((mv->x == gd->mik) && (mv->y == gd->mjk))))
	{
	    go[(int)mv->x][(int)mv->y] = BBLANK;   /* restore to open */
	    return 0;
	}
	else
	    return 1;
    }
    else
	return 1;
}

static void
GO_blank(char x, char y)
{
    char *str = "┌┬┐├┼┤└┴┘";
    int n1, n2, loc;

    BGOTO(x, y);
    n1 = (x == 0) ? 0 : (x == 18) ? 2 : 1;
    n2 = (y == 18) ? 0 : (y == 0) ? 2 : 1;
    loc= 2 * (n2 * 3 + n1);
    prints("%.2s", str + loc);
    //redoln();
} 

static void 
GO_examboard(struct GOData *gd, char color)
{
    int i, j, n;
    unsigned char (*go)[BRDSIZ]=gd->go;
    unsigned char (*l)[BRDSIZ]=gd->l;

    GO_eval(gd, color);

    if (color == gd->he)
    {
	gd->hik = -1;
	gd->hjk = -1;
    }
    else
    {
	gd->mik = -1;
	gd->mjk = -1;
    }
    n = 0;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    if ((go[i][j] == color) && (l[i][j] == 0))
	    {
		go[i][j] = BBLANK;
		GO_blank(i, j);
		if (color == gd->he)
		{
		    gd->hik = i;
		    gd->hjk = j;
		    ++gd->hk;
		}
		else
		{
		    gd->mik = i;
		    gd->mjk = j;
		    ++gd->mk;
		}
		++n;
	    }

    if (color == gd->he && n > 1)
    {
	gd->hik = -1;
	gd->hjk = -1;
    }
    else if ( n > 1 )
    {
	gd->mik = -1;
	gd->mjk = -1;
    }
}

static void
GO_clean(struct GOData *gd, int fd, int x, int y, int color)
{
    Horder_t tmp;
    unsigned char (*go)[BRDSIZ]=gd->go;
    unsigned char (*ml)[BRDSIZ]=gd->ml;

    ml[x][y] = 0;

    go[x][y] = BBLANK;
    GO_blank(x, y);

    tmp.x = x;
    tmp.y = y;

    if (send(fd, &tmp, sizeof(Horder_t), 0) != sizeof(Horder_t))
	return;

    if (x != 0)
    {
	if ((go[x - 1][y] == color) && ml[x - 1][y])
	    GO_clean(gd, fd, x - 1, y, color);
    }
    if (x != 18)
    {
	if ((go[x + 1][y] == color) && ml[x + 1][y])
	    GO_clean(gd, fd, x + 1, y, color);
    }
    if (y != 0)
    {
	if ((go[x][y - 1] == color) && ml[x][y - 1])
	    GO_clean(gd, fd, x, y - 1, color);
    }
    if (y != 18)
    { 
	if ((go[x][y + 1] == color) && ml[x][y + 1])
	    GO_clean(gd, fd, x, y + 1, color);
    }
}

static void
GO_cleandead(struct GOData *gd, int fd, char x, char y, char color)
{
    int i, j;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    gd->ml[i][j] = 1;

    GO_clean(gd, fd, x, y, color);
}

static void
GO_log(struct GOData *gd, char *userid)
{
    fileheader_t mymail;
    char         title[128], buf[80];
    int          i = 0;
    FILE        *fp;
    Horder_t *ptr = gd->pool;
    //extern screenline *big_picture;

    sethomepath(buf, cuser.userid);
    stampfile(buf, &mymail);

    fp = fopen(buf, "w");
    for(i = 1; i < 21; i++)
	fprintf(fp, "%.*s\n", big_picture[i].len, big_picture[i].data + 1);

    i = 0;

    fprintf(fp, "\n");

    do
    {
	if (ptr->x == -1 || ptr->y == -1)
	    fprintf(fp, "[%3d]%s =>    %c", i + 1, gd->win ? bw_chess[i % 2] : bw_chess[(i + 1) % 2],
		    (i % 5) == 4 ? '\n' : '\t');
	else
	    fprintf(fp, "[%3d]%s => %.1s%d%c", i + 1, gd->win ? bw_chess[i % 2] : bw_chess[(i + 1) % 2],
		    locE + ptr->x, ptr->y + 1, (i % 5) == 4 ? '\n' : '\t');
	i++;

    } while (++ptr < v);

    fprintf(fp, "\n\n《以下為 sgf 格式棋譜》\n<golog>\n(;GM[1]");
    if (userid == NULL)
	fprintf(fp, "GN[Gobot-Gobot FPG]\n");
    else
	fprintf(fp, "GN[%s-%s(%c) FPG]\n", userid, cuser.userid, gd->me == BBLACK ? 'B' : 'W');
    fprintf(fp, "SZ[19]HA[%d]", gd->hand);
    if (userid == NULL)
    {
	fprintf(fp, "PB[Gobot]PW[Gobot]\n");
    }
    else
    {
	if (gd->me == BBLACK)
	    fprintf(fp, "PB[%s]PW[%s]\n", cuser.userid, userid);
	else
	    fprintf(fp, "PB[%s]PW[%s]\n", userid, cuser.userid);
    }
    fprintf(fp, "PC[FPG BBS/Ptt BBS: ptt.cc]\n");

    if (gd->win)
	i = 0;
    else
    {
	i = 1;
	fprintf(fp, "AB%s\n", gd->AB);
    }
    ptr = gd->pool;
    do
    {
	if (ptr->x == -1 || ptr->y == -1)
	    fprintf(fp, ";%c[]%c", i % 2 ? 'W' : 'B', (i % 10) == 9 ? '\n' : ' ');
	else
	    fprintf(fp, ";%c[%c%c]%c", i % 2 ? 'W' : 'B', 'a' + ptr->x, 's' - ptr->y, (i % 10) == 9 ? '\n' : ' ');

	i++;
    } while (++ptr < v);

    fprintf(fp, ";)\n<golog>\n\n");
    fclose(fp);

    mymail.filemode = FILE_READ;

    strcpy(mymail.owner, "[備.忘.錄]");
    if (userid == NULL)
    {
	strcpy(mymail.title, "圍棋打譜");
    }
    else
	snprintf(mymail.title, sizeof(mymail.title),
		ANSI_COLOR(37;41) "棋譜" ANSI_RESET " %s VS %s", cuser.userid, userid);

    sethomedir(title, cuser.userid);
    append_record(title, &mymail, sizeof(mymail));
}

static int
go_key(struct GOData *gd, int fd, int ch, Horder_t *mv)
{
    if (ch >= 'a' && ch <= 's' && ch != 'i')
    {
	char pbuf[4];
	int  vx, vy;

	pbuf[0] = ch;
	pbuf[1] = 0;

	if (fd) add_io(0, 0);
	getdata(21, 9, "直接指定位置(1 - 19)：", pbuf, 4, DOECHO);
	if (fd) add_io(fd, 0);
	move(21, 9);
	clrtoeol();
	//outs("                               ");

	vx = ch - 'a';
	if (ch > 'i')
	    vx--;
	vy = atoi(pbuf) - 1;
	if( vx >= 0 && vx < BRDSIZ && vy >= 0 && vy < BRDSIZ && gd->go[vx][vy] == BBLANK)
	{
	    mv->x = vx;
	    mv->y = vy;
	    return 1;
	}
    }
    else
    {
	switch(ch)
	{
	    case KEY_RIGHT:
		mv->x = (mv->x == BRDSIZ - 1) ? mv->x : mv->x + 1;
		break;
	    case KEY_LEFT:
		mv->x = (mv->x == 0 ) ? 0 : mv->x - 1;
		break;
	    case KEY_UP:
		mv->y = (mv->y == BRDSIZ - 1) ? mv->y : mv->y + 1;
		break;
	    case KEY_DOWN:
		mv->y = (mv->y == 0 ) ? 0 : mv->y - 1;
		break;
	    case ' ':
	    case '\r':
	    case '\n':
		if (gd->go[(int)mv->x][(int)mv->y] == BBLANK && GO_check(gd, mv))
		    return 1;
	}
    }
    return 0;
}

static unsigned char 
GO_findcolor(struct GOData *gd, int x, int y)
{
    int k, result = 0, color[4];
    unsigned char (*go)[BRDSIZ]=gd->go;

    if (go[x][y] != BBLANK)
	return 0;

    if (x > 0)
    {
	k = x;
	do --k;
	while ((go[k][y] == BBLANK) && (k > 0));
	color[0] = go[k][y];
    }
    else 
	color[0] = go[x][y];

    if (x < 18)
    {
	k = x;
	do ++k;
	while ((go[k][y] == BBLANK) && (k < 18));
	color[1] = go[k][y];
    }
    else
	color[1] = go[x][y];

    if (y > 0)
    {
	k = y;
	do --k;
	while ((go[x][k] == BBLANK) && (k > 0));
	color[2] = go[x][k];
    }
    else color[2] = go[x][y];

    if (y < 18)
    {
	k = y;
	do ++k;
	while ((go[x][k] == BBLANK) && (k < 18));
	color[3] = go[x][k];
    }
    else
	color[3] = go[x][y];

    for (k = 0;k < 4;k++)
    {
	if (color[k] == BBLANK)
	    continue;
	else
	{
	    result = color[k];
	    break;
	}
    }

    for (k = 0;k < 4;k++)
    {
	if ((color[k] != BBLANK) && (color[k] != result))
	    return 0;
    }

    return result;
}

static int
GO_result(struct GOData *gd)
{
    int i, j, result;
    float count[2];
    char    *chessresult[] = { "‧", "。" }; 
    unsigned char (*go)[BRDSIZ]=gd->go;

    count[0] = count[1] = 0;

    for (i = 0; i < 19; i++)
	for (j = 0; j < 19; j++)
	    if (go[i][j] == BBLANK)
	    {
		result = GO_findcolor(gd, i, j);
		BGOTO(i, j);
		if (result)
		{
		    outs(chessresult[result - 1]);
		    count[result - 1]++;
		}
		else
		{
		    outs("×");
		    gd->win -= 0.5;	  
		}
	    }
	    else
		count[go[i][j] - 1]++;
    redoscr();

    if (gd->me == BBLACK)
    {
	move(5, 46);
	prints("%s 方目數：%-3.1f       ", bw_chess[gd->me - 1], count[0]);
	move(6, 46);
	prints("%s 方目數：%-3.1f       ", bw_chess[gd->he - 1], count[1]);
	move(21, 46);
	clrtoeol();
	move(8, 46);

	if (count[0] > 181 + gd->win)
	    return 1;
    }
    else
    {
	move(5, 46);
	prints("%s 方目數：%-3.1f       ", bw_chess[gd->me - 1], count[1]);
	move(6, 46);
	prints("%s 方目數：%-3.1f       ", bw_chess[gd->he - 1], count[0]);
	move(21, 46);
	clrtoeol();
	move(8, 46);

	if (count[0] <= 181 + gd->win)
	    return 1;
    }
    return 0;
}

void
GO_cleantable(void)
{
    move(1, 0);
#define AC ANSI_COLOR(30;43)
#define AR ANSI_RESET
    outs(
    "     A B C D E F G H J K L M N O P Q R S T\n"
    " 19" AC " ┌┬┬┬┬┬┬┬┬┬┬┬┬┬┬┬┬┬┐ " AR "\n"
    " 18" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 17" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 16" AC " ├┼┼＋┼┼┼┼┼＋┼┼┼┼┼＋┼┼┤ " AR "\n"
    " 15" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 14" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 13" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 12" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 11" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    " 10" AC " ├┼┼＋┼┼┼┼┼＋┼┼┼┼┼＋┼┼┤ " AR "\n"
    "  9" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  8" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  7" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  6" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  5" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  4" AC " ├┼┼＋┼┼┼┼┼＋┼┼┼┼┼＋┼┼┤ " AR "\n"
    "  3" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  2" AC " ├┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┤ " AR "\n"
    "  1" AC " └┴┴┴┴┴┴┴┴┴┴┴┴┴┴┴┴┴┘ " AR "\n"
    );
}

int
gochess(int fd)
{
    Horder_t    mv;
    userinfo_t *my = currutmp;
    time4_t mtime, htime, btime;
    int     i, j, ch = 0, passflag, endflag, totalgo, timeflag, is_view;
    unsigned char    mhand, hhand;
    int scr_need_redraw = 1;

    struct GOData gd;
    unsigned char	(*go)[BRDSIZ]=gd.go;
    unsigned char	(*l)[BRDSIZ]=gd.l;
    Horder_t *pool=gd.pool;

    GO_init(&gd);

    gd.hk = gd.mk = 0;
    totalgo = 0;
    gd.hik = gd.hjk = -1;
    gd.mik = gd.mjk = -1;
    mhand = hhand = 250;	/* 不太可能一分鐘內下這麼多手 :p */
    mtime = htime = 60;		/* 一開始的一分鐘內不計手 */
    gd.win = 3.5;

    gd.me = !(my->turn) + 1;
    gd.he = my->turn + 1;
    if (gd.me > 2) gd.me = 2;
    if (gd.he > 2) gd.he = 2;

    endflag = passflag = timeflag = 0;
    is_view = 1;

    setutmpmode(GO);

    clear();

    /* 觀戰用程式碼, 與 ptt 不相容
    if (gd.me == BWHITE)
    {
	VIEWCHESS	vs;

	memset(&vs, 0, sizeof(VIEWCHESS));     
	strcpy(vs.userid[0], cuser.userid);
	strcpy(vs.userid[1], my->mateid);
	if (cshm_new(&vs) == -1)
	    is_view = 0;
    }
    */

    prints(ANSI_COLOR(1;46) "  圍棋對戰  " ANSI_COLOR(45) "%31s VS %-31s" ANSI_RESET,
	    cuser.userid, my->mateid);
    GO_cleantable();

    /* film_out(FILM_GO, 1); */

    add_io(fd, 0);

    mv.x = mv.y = 9;
    btime = now;

    for(;;)
    {
	if(scr_need_redraw){
	    move(2, 46);
	    prints("%s 方提子數：%3d", bw_chess[gd.me - 1], gd.hk);
	    move(3, 46);
	    prints("%s 方提子數：%3d", bw_chess[gd.he - 1], gd.mk);

	    move(8, 46);
	    clrtoeol();
	    if (endflag)
		outs("請清除死子，以便計算勝負");
	    else if (my->turn)
		prints("輪到自己下了.... 我是 %s", bw_chess[gd.me - 1]);
	    else
		outs("等待對方下子....");


	    move(10, 46);
	    clrtoeol();
	    if (totalgo > 0)
	    {
		if (pool[totalgo - 1].x == -1 || pool[totalgo - 1].y == -1)
		    prints("%s #%-3d PASS 上一手    ", gd.win ? bw_chess[(totalgo - 1) & 1] : bw_chess[totalgo & 1], totalgo);
		else
		    prints("%s #%-3d %.1s%-2d  上一手    ", gd.win ? bw_chess[(totalgo - 1) & 1] : bw_chess[totalgo & 1], totalgo, locE + pool[totalgo - 1].x, pool[totalgo - 1].y + 1);
	    }

	    for (i = totalgo - 1;i > 0 && totalgo - i <= 10;i--)
	    {
		move(10 + totalgo - i, 46);
		clrtoeol();
		if (pool[i - 1].x == -1 || pool[i - 1].y == -1)
		    prints("%s #%-3d PASS", gd.win ? bw_chess[(i - 1) & 1] : bw_chess[i & 1], i);
		else
		    prints("%s #%-3d %.1s%-2d ", gd.win ? bw_chess[(i - 1) & 1] : bw_chess[i & 1], i, locE + pool[i - 1].x, pool[i - 1].y + 1);
	    }

	    move(21, 46);

	    if (v == pool)
	    {
		if (gd.me == BWHITE && gd.win != 0)
		    outs(ANSI_COLOR(1;33) "按 x 讓子 y 不限時 Ctrl-C 中止棋局" ANSI_RESET);
		else
		    outs(ANSI_COLOR(1;33) "按 Ctrl-C 中止棋局" ANSI_RESET);
	    }
	    else if (passflag && my->turn)
	    {
		if (endflag)
		    outs(ANSI_COLOR(1;33) "對方 DONE，己方 DONE 就計算結果" ANSI_RESET);
		else
		    outs(ANSI_COLOR(1;33) "對方 PASS，己方 PASS 就結束棋局" ANSI_RESET);
	    }
	    else if (v > pool)
		clrtoeol();

	    if (endflag)
		outmsg(ANSI_COLOR(1;33;42) " 下棋 " ANSI_COLOR(;31;47) " (←↑↓→)" ANSI_COLOR(30) "移動 " ANSI_COLOR(31) "(空白鍵/ENTER)" ANSI_COLOR(30) "下子 " ANSI_COLOR(31) "(v)" ANSI_COLOR(30) "傳訊 " ANSI_COLOR(31) "(z)" ANSI_COLOR(30) "投降 " ANSI_COLOR(31) "(w)" ANSI_COLOR(30) "DONE " ANSI_COLOR(31) "(u)" ANSI_COLOR(30) "回復      " ANSI_RESET);
	    else
		outmsg(ANSI_COLOR(1;33;42) " 下棋 " ANSI_COLOR(;31;47) " (←↑↓→)" ANSI_COLOR(30) "移動 " ANSI_COLOR(31) "(空白鍵/ENTER)" ANSI_COLOR(30) "下子 " ANSI_COLOR(31) "(v)" ANSI_COLOR(30) "傳訊 " ANSI_COLOR(31) "(z)" ANSI_COLOR(30) "投降 " ANSI_COLOR(31) "(w)" ANSI_COLOR(30) "PASS              " ANSI_RESET);

	    redoscr();
	    scr_need_redraw = 0;
	}

	if (endflag || timeflag)
	{
		char buf[128];
		int n;
		//move(5, 46);
		n = sprintf(buf, ANSI_MOVETO(6,47) "%s 方時間：----- --", bw_chess[gd.me - 1]);
		output(buf, n);
		//move(6, 46);
		n = sprintf(buf, ANSI_MOVETO(7,47) "%s 方時間：----- --", bw_chess[gd.he - 1]);
		output(buf, n);
	}
	else
	{
	    if (my->turn)
	    {
		mtime -= now - btime;
		if (mtime < 0)
		{
		    if (mhand > 25)		/* 第一分鐘過後，要在 12 分鐘內下 25 手 */
		    {
			mtime = 12 * 60;
			mhand = 25;
		    }
		    else
		    {
			mv.x = mv.y = -8;
			if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
			    break;
			move(8, 46);
			outs("未在時間內完成 25 手，裁定敗");
			break;
		    }
		}
	    }
	    else
	    {
		htime -= now - btime;
		if (htime < 0)
		{
		    if (hhand > 25)         /* 第一分鐘過後，要在 12 分鐘內下 25 手 */
		    {
			htime = 12 * 60;
			hhand = 25;
		    }
		    else
			htime = 0;
		}
	    }
	    move(5, 46);
	    clrtoeol();
	    /* move() only move pointer in screen.c, doesn't move
	     * curses correctly */
	    //do_move(5, 35);
	    {
		char buf[128];
		int n;
		//move(5, 46);
		n = sprintf(buf, ANSI_MOVETO(6,47) "%s 方時間：%02d:%02d ",
			bw_chess[gd.me - 1], (int)mtime / 60, (int)mtime % 60);
		if (mhand <= 25)
		    n += sprintf(buf + n, "%2d 手", 25 - mhand);
		output(buf, n);
		//move(6, 46);
		n = sprintf(buf, ANSI_MOVETO(7,47) "%s 方時間：%02d:%02d ",
			bw_chess[gd.he - 1], (int)htime / 60, (int)htime % 60);
		if (hhand <= 25)
		    n += sprintf(buf + n, "%2d 手", 25 - hhand); 
		output(buf, n);
	    }
	}
	btime = now;

	iBGOTO(mv.x, mv.y);

	/*
	if (ch == -1 && is_view)
	    cshm_update(mv.x, mv.y);
	*/

	ch = igetch();

	if (ch == 'v')
	{
	    screen_backup_t old_screen;

	    screen_backup(&old_screen);
	    add_io(0, 0);
	    if (ch == 'v')
	    {
		//extern char   watermode;

		//watermode = 2;
		my_write(0, "丟水球過去：", my->mateid, WATERBALL_GENERAL, &SHM->uinfo[my->destuip]);
		//watermode = 0;
	    }
	    /* ??? scw: when will these code be executed?
	    else
	    {
		show_last_call_in();
		my_write(currutmp->msgs[0].last_pid, "水球丟回去：");
	    }
	    */
	    screen_restore(&old_screen);
	    add_io(fd, 0);
	    scr_need_redraw = 1;
	    continue;
	} else if (ch == 'x') {
	    if (v == pool && gd.me == BWHITE && gd.win != 0)
	    {
		char buf[4];

		add_io(0, 0);
		getdata(21, 46, "要讓多少子呢(1 - 9)？ ", buf, 4, DOECHO);
		add_io(fd, 0);

		if (GO_sethand(&gd, atoi(buf)))
		{
		    mv.x = -9;
		    mv.y = atoi(buf);
		    if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
			break;
		    mv.x = mv.y = 9; 
		    my->turn = 1;
		    ch = -1;
		}
		continue;
	    }
	}
	else if (ch == 'y')
	{
	    if (v == pool && gd.me == BWHITE)
	    {
		timeflag = 1;
		mv.x = mv.y = -10;
		if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		    break;
		mv.x = mv.y = 9;
		ch = -1; 
	    }
	}
	else if (ch == 'w')
	{
	    if (endflag)
	    {
		Horder_t tmp;

		if (passflag && my->turn)
		{
		    tmp.x = tmp.y = -3;
		    if (send(fd, &tmp, sizeof(Horder_t), 0) != sizeof(Horder_t))
			break;

		    if (GO_result(&gd))
			outs("恭禧，您獲得勝利....      ");
		    else
			outs("輸了，再接再勵呦....      ");

		    if (gd.me == BWHITE)
			ch = -1;

		    break;
		}
		else
		{
		    Horder_t tmp;

		    passflag = 1;
		    tmp.x = tmp.y = -6;
		    if (send(fd, &tmp, sizeof(Horder_t), 0) != sizeof(Horder_t))
			break;
		    my->turn = 0;

		    if (gd.me == BWHITE)
			ch = -1;

		    continue;
		}
	    }
	    else
	    {
		if (my->turn)
		{
		    if (passflag)
		    { 
			endflag = 1;
			passflag = 0;
			mv.x = mv.y = -2;
			if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
			    break;
			mv.x = mv.y = 9;
			memcpy(l, go, sizeof(gd.l));	/* 備份 */

			if (gd.me == BWHITE)
			    ch = -1;
			continue;
		    }
		    else
		    {
			passflag = 1;
			mv.x = mv.y = -1;
			if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
			    break;
			GO_add(&gd, &mv);
			totalgo++;
			mv.x = mv.y = 9; 
			my->turn = 0;

			if (gd.me == BWHITE)
			    ch = -1;
			continue;
		    }
		}
	    }
	}
	else if (ch == Ctrl('C')/* && v == pool */)
	{
	    mv.x = mv.y = -5;
	    if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		break;
	    break;
	}
	else if (ch == 'u')
	{
	    if (endflag)
	    {
		memcpy(go, l, sizeof(gd.go));

		for(i = 0;i < 19;i++)
		    for(j = 0;j < 19;j++)
		    {
			BGOTO(i, j);
			if (go[i][j])
			{
			    outs(bw_chess[go[i][j] - 1]);
			}
			else
			    GO_blank(i, j);
		    }
		redoscr();   	      
		mv.x = mv.y = -7;
		if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		    break;
		mv = *(v - 1);

		if (gd.me == BWHITE)
		    ch = -1;
	    }	
	}
	else if (ch == 'z')
	{
	    mv.x = mv.y = -4;
	    if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		break;
	    move(8, 46);
	    outs("您認輸了....             ");
	    break;
	}

	if (ch == I_OTHERDATA)
	{
	    ch = recv(fd, &mv, sizeof(Horder_t), 0);
	    scr_need_redraw = 1;

	    if (ch <= 0)
	    {
		move(8, 46);
		outs("對方斷線....            ");
		break;
	    }

	    if (ch != sizeof(Horder_t))
	    {
		continue;
	    }

	    if (mv.x == -9 && mv.y > 0)
	    {
		GO_sethand(&gd, mv.y);
		mv.x = mv.y = 9;
		my->turn = 0;
		continue;
	    }

	    if (mv.x == -10 && mv.y == -10)
	    {
		timeflag = 1;
		mv.x = mv.y = 9;
		continue;
	    }

	    if (mv.x == -5 && mv.y == -5){
		move(8, 46);
		outs("對方斷線....      ");
		break;
	    }

	    if (mv.x == -4 && mv.y == -4)
	    {
		move(8, 46);
		outs("對方認輸....      ");
		break;
	    }

	    if (mv.x == -8 && mv.y == -8)
	    {
		move(8, 46);
		outs("對方未在時間內完成 25 手，裁定勝");
		break;
	    } 

	    if (mv.x == -3 && mv.y == -3)
	    {
		if (GO_result(&gd))
		    outs("恭禧，您獲得勝利....      ");
		else
		    outs("輸了，再接再勵呦....      "); 
		break;

		if (gd.me == BWHITE)
		    ch = -1;
	    }

	    if (mv.x == -2 && mv.y == -2)
	    {
		endflag = 1;
		passflag = 0;
		mv.x = mv.y = 9;
		my->turn = 1;
		memcpy(l, go, sizeof(gd.l));	/* 備份 */

		if (gd.me == BWHITE)
		    ch = -1;
		continue;
	    } 

	    if (mv.x == -7 && mv.y == -7)
	    {
		memcpy(go, l, sizeof(gd.go));

		for(i = 0;i < 19;i++)
		    for(j = 0;j < 19;j++)
		    {
			BGOTO(i, j);
			if (go[i][j])
			{
			    outs(bw_chess[go[i][j] - 1]);
			    redoln();
			}
			else
			    GO_blank(i, j);
		    }
		mv = *(v - 1);

		if (gd.me == BWHITE)
		    ch = -1;

		continue;
	    }

	    if (mv.x == -6 && mv.y == -6)
	    {
		passflag = 1;
		mv = *(v - 1);
		my->turn = 1;

		if (gd.me == BWHITE)
		    ch = -1;

		continue;
	    }

	    if (mv.x == -1 && mv.y == -1)
	    {
		GO_add(&gd, &mv);
		totalgo++;
		mv.x = mv.y = 9;

		passflag = 1;
		my->turn = 1;

		if (gd.me == BWHITE)
		    ch = -1;

		continue;
	    }

	    if (endflag)
	    {
		int y, x;

		getyx(&y, &x);

		go[(int)mv.x][(int)mv.y] = BBLANK;
		GO_blank(mv.x, mv.y);
		/*
		   gd.hk++;
		   */
		mv.x = (x - 4) / 2;
		mv.y = 20 - y;
		iBGOTO(mv.x, mv.y);

		if (gd.me == BWHITE)
		    ch = -1;	
		continue;
	    }

	    if (!my->turn)
	    {
		GO_add(&gd, &mv);

		totalgo++;
		if (--hhand <= 0)
		{
		    htime = 12 * 60;
		    hhand = 25;
		}
		go[(int)mv.x][(int)mv.y] = gd.he;
		BGOTO(mv.x, mv.y);
		outs(bw_chess[gd.he - 1]);

		GO_examboard(&gd, gd.me);
		my->turn = 1;
		htime -= now - btime;
		btime = now;
		passflag = 0;

		if (gd.me == BWHITE)
		    ch = -1;
	    }
	    continue;
	}

	if (endflag == 1 && (ch == ' ' || ch == '\r' || ch == '\n')) {
	    if (go[(int)mv.x][(int)mv.y] == gd.me)
		GO_cleandead(&gd, fd, mv.x, mv.y, gd.me);

	    if (gd.me == BWHITE)
		ch = -1;
	    continue;
	}

	if (my->turn)
	{
	    if (go_key(&gd, fd, ch, &mv))
		my->turn = 0;
	    else
		continue;

	    if (!my->turn)
	    {
		GO_add(&gd, &mv);

		totalgo++;
		mtime -= now - btime;
		btime = now;
		if (--mhand <= 0)
		{
		    mtime = 12 * 60;
		    mhand = 25; 
		}
		BGOTO(mv.x, mv.y);
		outs(bw_chess[gd.me - 1]);
		go[(int)mv.x][(int)mv.y] = gd.me;
		GO_examboard(&gd, gd.he);
		if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		    break;
		passflag = 0;

		if (gd.me == BWHITE)
		    ch = -1;
		scr_need_redraw = 1;
	    }
	}
    }

    add_io(0, 0);
    close(fd);
    redoscr();

    igetch();

    /*
    if (gd.me == BWHITE && is_view)
	cshm_free();
    */

    if (endflag)
    {
	for(i = 0;i < 19;i++)
	    for(j = 0;j < 19;j++)
	    {
		BGOTO(i, j);
		if (l[i][j])
		{
		    outs(bw_chess[l[i][j] - 1]);
		}
		else
		    GO_blank(i, j);
	    } 
    }
    redoscr();

    if (v > pool + 10)
    {
	char ans[3];

	getdata(b_lines - 1, 0, "要存成棋譜嗎(Y/N)？[Y] ", ans, 3, LCECHO);

	if (*ans != 'n')
	    GO_log(&gd, my->mateid);
    }

    return 0;
}

int
GoBot(void)
{
    Horder_t mv;
    int i, ch, totalgo;
    unsigned char  tmp_go[2][BRDSIZ][BRDSIZ];
    unsigned char  tmp_l[2][BRDSIZ][BRDSIZ];
    unsigned char  tmp_ml[2][BRDSIZ][BRDSIZ];
    int     tmp_lib[2], tmp_mik[2], tmp_mjk[2], tmp_hik[2], tmp_hjk[2];
    int scr_need_redraw = 1;

    struct GOData gd;
    unsigned char	(*go)[BRDSIZ]=gd.go;
    unsigned char	(*l)[BRDSIZ]=gd.l;
    unsigned char	(*ml)[BRDSIZ]=gd.ml;
    Horder_t *pool=gd.pool;

    GO_init(&gd);

    memset(&tmp_go, 0, sizeof(tmp_go));
    memset(&tmp_l, 0, sizeof(tmp_l));
    memset(&tmp_ml, 0, sizeof(tmp_ml));
    for (i = 0;i < 2;i++)
    {
	tmp_lib[i] = 0;
	tmp_mik[i] = tmp_mjk[i] = tmp_hik[i] = tmp_hjk[i] = -1;
    }


    gd.me = BBLACK;
    gd.he = BWHITE;
    gd.hand = 0;
    totalgo = 0;
    gd.hik = gd.hjk = -1;
    gd.mik = gd.mjk = -1; 

    setutmpmode(GO);

    clear();

    prints(ANSI_COLOR(1;46) "  圍棋打譜  " ANSI_COLOR(45) "%66s" ANSI_RESET, " ");
    GO_cleantable();

    /* film_out(FILM_GO, 1); */

    mv.x = mv.y = 9;

    for(;;)
    {
	if(scr_need_redraw){
	    move(8, 46);
	    clrtoeol();
	    prints("輪到 %s 方下了....", bw_chess[gd.me - 1]);

	    move(10, 46);
	    clrtoeol();
	    if (totalgo > 0)
	    {
		if (pool[totalgo - 1].x == -1 || pool[totalgo - 1].y == -1)
		    prints("%s #%-3d PASS 上一手    ", bw_chess[(totalgo - 1) & 1], totalgo);
		else
		    prints("%s #%-3d %.1s%-2d  上一手    ", bw_chess[(totalgo - 1) & 1], totalgo, locE + pool[totalgo - 1].x, pool[totalgo - 1].y + 1);
	    }

	    for (i = totalgo - 1;i > 0 && totalgo - i <= 10;i--)
	    {
		move(10 + totalgo - i, 46);
		clrtoeol();
		if (pool[i - 1].x == -1 || pool[i - 1].y == -1)
		    prints("%s #%-3d PASS", bw_chess[(i - 1) & 1], i);
		else
		    prints("%s #%-3d %.1s%-2d ", bw_chess[(i - 1) & 1], i, locE + pool[i - 1].x, pool[i - 1].y + 1);
	    }

	    outmsg(ANSI_COLOR(1;33;42)" 打譜 "
		    ANSI_COLOR(;31;47)" (←↑↓→)"
		    ANSI_COLOR(30)"移動 "
		    ANSI_COLOR(31)"(空白鍵/ENTER)"
		    ANSI_COLOR(30)"下子 "
		    ANSI_COLOR(31)"(u)"
		    ANSI_COLOR(30)"回上一步 " 
		    ANSI_COLOR(31) "(z)" 
		    ANSI_COLOR(30) "離開                  "
		    ANSI_RESET);    
	    redoscr();
	    scr_need_redraw = 0;
	}

	iBGOTO(mv.x, mv.y);

	ch = igetch();

	if (ch == 'z')
	    break;
	else if (ch == 'u')
	{
	    int j;
	    if (!totalgo)
		continue;
	    memset(go, 0, sizeof(gd.go));
	    memset( l, 0, sizeof(gd.go));
	    memset(ml, 0, sizeof(gd.go));
	    memset(&tmp_go, 0, sizeof(tmp_go));
	    memset(&tmp_l, 0, sizeof(tmp_l));
	    memset(&tmp_ml, 0, sizeof(tmp_ml));
	    for (i = 0;i < 2;i++)
	    {
		tmp_lib[i] = 0;
		tmp_mik[i] = tmp_mjk[i] = tmp_hik[i] = tmp_hjk[i] = -1;
	    }
	    gd.me = BBLACK;
	    gd.he = BWHITE; 
	    gd.hik = gd.hjk = -1;
	    gd.mik = gd.mjk = -1; 

	    /* film_out(FILM_GO, 1); */
	    totalgo--;
	    for (i = 0;i < totalgo;i++)
	    {
		go[(int)pool[i].x][(int)pool[i].y] = gd.me;
		GO_examboard(&gd, gd.he);
		memcpy(&tmp_go[gd.me - 1], go, sizeof(gd.l));
		memcpy(&tmp_l[gd.me - 1], l, sizeof(gd.l));
		memcpy(&tmp_ml[gd.me - 1], ml, sizeof(gd.ml));
		tmp_lib[gd.me - 1] = gd.lib;
		tmp_mik[gd.me - 1] = gd.mik;
		tmp_mjk[gd.me - 1] = gd.mjk;
		tmp_hik[gd.me - 1] = gd.hik;
		tmp_hjk[gd.me - 1] = gd.hjk;
		gd.me = gd.he;
		gd.he = 3 - gd.me;
		memcpy(go, &tmp_go[gd.me - 1], sizeof(gd.l));
		memcpy(l, &tmp_l[gd.me - 1], sizeof(gd.l));
		memcpy(ml, &tmp_ml[gd.me - 1], sizeof(gd.ml));
		gd.lib = tmp_lib[gd.me - 1];
		gd.mik = tmp_mik[gd.me - 1];
		gd.mjk = tmp_mjk[gd.me - 1];
		gd.hik = tmp_hik[gd.me - 1];
		gd.hjk = tmp_hjk[gd.me - 1];
		go[(int)pool[i].x][(int)pool[i].y] = gd.he;
		GO_examboard(&gd, gd.me); 
	    }	
	    GO_cleantable();
	    for (i = 0; i < BRDSIZ; ++i)
		for(j = 0; j < BRDSIZ; ++j)
		    if (go[i][j]) {
			BGOTO(i, j);
			outs(bw_chess[go[i][j] - 1]);
		    }
	    scr_need_redraw = 1;
	    mv = *(--v);
	}
	else 
	{
	    if (go_key(&gd, 0, ch, &mv))
	    {
		GO_add(&gd, &mv);
		totalgo++;
		BGOTO(mv.x, mv.y);
		outs(bw_chess[gd.me - 1]); 
		go[(int)mv.x][(int)mv.y] = gd.me;
		GO_examboard(&gd, gd.he);
		memcpy(&tmp_go[gd.me - 1], go, sizeof(gd.l));
		memcpy(&tmp_l[gd.me - 1], l, sizeof(gd.l));
		memcpy(&tmp_ml[gd.me - 1], ml, sizeof(gd.ml));
		tmp_lib[gd.me - 1] = gd.lib;
		tmp_mik[gd.me - 1] = gd.mik;
		tmp_mjk[gd.me - 1] = gd.mjk;
		tmp_hik[gd.me - 1] = gd.hik;
		tmp_hjk[gd.me - 1] = gd.hjk;
		gd.me = gd.he;
		gd.he = 3 - gd.me;
		memcpy(go, &tmp_go[gd.me - 1], sizeof(gd.l));
		memcpy(l, &tmp_l[gd.me - 1], sizeof(gd.l));
		memcpy(ml, &tmp_ml[gd.me - 1], sizeof(gd.ml));
		gd.lib = tmp_lib[gd.me - 1];
		gd.mik = tmp_mik[gd.me - 1];
		gd.mjk = tmp_mjk[gd.me - 1];
		gd.hik = tmp_hik[gd.me - 1];
		gd.hjk = tmp_hjk[gd.me - 1];
		go[(int)mv.x][(int)mv.y] = gd.he;
		GO_examboard(&gd, gd.me);
		scr_need_redraw = 1;
	    }
	}
    }

    if (v > pool)
    {
	char ans[3];

	getdata(b_lines - 1, 0, "要存成棋譜嗎(Y/N)？[Y] ", ans, 3, LCECHO);

	if (*ans != 'n')
	    GO_log(&gd, NULL);
    } 

    return 0;
}
