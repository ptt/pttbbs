/* $Id$ */
#include "bbs.h"

#define RED   1
#define BLACK 0
typedef short int sint;

typedef struct item {
    short int       color, value, die, out;
}               item;

typedef struct cur {
    short int       y, x, end;
}               cur;

struct DarkData {
    item     brd[4][8];
    cur      curr;
    sint     rcount, bcount, cont, fix;	/* cont:是否可連吃 */
    sint     my, mx, mly, mlx;		/* 移動的座標 標 */

    sint     cur_eaty, cur_eatx;	/* 吃掉對方其子的秀出座標 */
};

static char    * const rname[] = {"兵", "炮", "傌", "車", "相", "仕", "帥"};
static char    * const bname[] = {"卒", "包", "馬", "車", "象", "士", "將"};

static const sint     cury[] = {3, 5, 7, 9}, curx[] = {5, 9, 13, 17, 21, 25, 29, 33};

static void
brdswap(struct DarkData *dd, sint y, sint x, sint ly, sint lx)
{
    memcpy(&dd->brd[y][x], &dd->brd[ly][lx], sizeof(item));
    dd->brd[ly][lx].die = 1;
    dd->brd[ly][lx].color = -1;	/* 沒這個color */
    dd->brd[ly][lx].value = -1;
}

static          sint
Is_win(struct DarkData *dd, item att, item det, sint y, sint x, sint ly, sint lx)
{
    sint            i, c = 0, min, max;
    if (att.value == 1) {	/* 砲 */
	if (y != ly && x != lx)
	    return 0;
	if ((abs(ly - y) == 1 && dd->brd[y][x].die == 0) ||
	    (abs(lx - x) == 1 && dd->brd[y][x].die == 0))
	    return 0;
	if (y == ly) {
	    if (x > lx) {
		max = x;
		min = lx;
	    } else {
		max = lx;
		min = x;
	    }
	    for (i = min + 1; i < max; i++)
		if (dd->brd[y][i].die == 0)
		    c++;
	} else if (x == lx) {
	    if (y > ly) {
		max = y;
		min = ly;
	    } else {
		max = ly;
		min = y;
	    }
	    for (i = min + 1; i < max; i++)
		if (dd->brd[i][x].die == 0)
		    c++;
	}
	if (c != 1)
	    return 0;
	if (det.die == 1)
	    return 0;
	return 1;
    }
    /* 非砲 */
    if (((abs(ly - y) == 1 && x == lx) || (abs(lx - x) == 1 && ly == y)) && dd->brd[y][x].out == 1) {
	if (att.value == 0 && det.value == 6)
	    return 1;
	else if (att.value == 6 && det.value == 0)
	    return 0;
	else if (att.value >= det.value)
	    return 1;
	else
	    return 0;
    }
    return 0;
}

static          sint
Is_move(struct DarkData *dd, sint y, sint x, sint ly, sint lx)
{
    if (dd->brd[y][x].die == 1 && ((abs(ly - y) == 1 && x == lx) || (abs(lx - x) == 1 && ly == y)))
	return 1;
    return 0;
}

static void
brd_rand(struct DarkData *dd)
{
    sint            y, x, index;
    sint            tem[32];
    sint            value[32] = {
	0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
	0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6
    };

    bzero(dd->brd, sizeof(dd->brd));
    bzero(tem, sizeof(tem));
    bzero(&dd->curr, sizeof(dd->curr));
    for (y = 0; y < 4; y++)
	for (x = 0; x < 8; x++)
	    while (1) {
		index = random() % 32;
		if (tem[index])
		    continue;
		dd->brd[y][x].color = (index > 15) ? 0 : 1;
		dd->brd[y][x].value = value[index];
		tem[index] = 1;
		break;
	    }
}

static void
brd_prints(void)
{
    clear();
    move(1, 0);
    outs("\n"
	 "   " ANSI_COLOR(43;30) "╭─┬─┬─┬─┬─┬─┬─┬─╮" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "│●│●│●│●│●│●│●│●│" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "├─┼─┼─┼─┼─┼─┼─┼─┤" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "│●│●│●│●│●│●│●│●│" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "├─┼─┼─┼─┼─┼─┼─┼─┤" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "│●│●│●│●│●│●│●│●│" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "├─┼─┼─┼─┼─┼─┼─┼─┤" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "│●│●│●│●│●│●│●│●│" ANSI_RESET "\n"
	 "   " ANSI_COLOR(43;30) "╰─┴─┴─┴─┴─┴─┴─┴─╯" ANSI_RESET "\n"
	 "   ");
}

static void
draw_line(struct DarkData *dd, sint y, sint f)
{
    sint            i;
    char            buf[1024], tmp[256];

    *buf = 0;
    *tmp = 0;
    strlcpy(buf, ANSI_COLOR(43;30), sizeof(buf));
    for (i = 0; i < 8; i++) {
	if (dd->brd[y][i].die == 1)
	    snprintf(tmp, sizeof(tmp), "│  ");
	else if (dd->brd[y][i].out == 0)
	    snprintf(tmp, sizeof(tmp), "│●");
	else {
	    snprintf(tmp, sizeof(tmp), "│" ANSI_COLOR(%s1;%d) "%s" ANSI_RESET ANSI_COLOR(43;30) "",
		     (f == i) ? "1;47;" : "", (dd->brd[y][i].color) ? 31 : 34,
		     (dd->brd[y][i].color) ? rname[dd->brd[y][i].value] :
		     bname[dd->brd[y][i].value]);
	}
	strcat(buf, tmp);
    }
    strcat(buf, "│" ANSI_RESET);

    move(cury[y], 3);
    clrtoeol();
    outs(buf);
}

static void
redraw(struct DarkData *dd)
{
    sint            i = 0;
    for (; i < 4; i++)
	draw_line(dd, i, -1);
}

static          sint
playing(struct DarkData *dd, sint fd, sint color, sint ch, sint * b, userinfo_t * uin)
{
    dd->curr.end = 0;
    move(cury[dd->my], curx[dd->mx]);

    if (dd->fix) {
	if (ch == 's') {
	    dd->fix = 0;
	    *b = 0;
	    return 0;
	} else {
	    draw_line(dd, dd->mly, -1);
	}
    }
    switch (ch) {
    case KEY_LEFT:
	if (dd->mx == 0)
	    dd->mx = 7;
	else
	    dd->mx--;
	move(cury[dd->my], curx[dd->mx]);
	*b = -1;
	break;
    case KEY_RIGHT:
	if (dd->mx == 7)
	    dd->mx = 0;
	else
	    dd->mx++;
	move(cury[dd->my], curx[dd->mx]);
	*b = -1;
	break;
    case KEY_UP:
	if (dd->my == 0)
	    dd->my = 3;
	else
	    dd->my--;
	move(cury[dd->my], curx[dd->mx]);
	*b = -1;
	break;
    case KEY_DOWN:
	if (dd->my == 3)
	    dd->my = 0;
	else
	    dd->my++;
	move(cury[dd->my], curx[dd->mx]);
	*b = -1;
	break;
    case 'q':
    case 'Q':
	if (!color)
	    dd->bcount = 0;
	else
	    dd->rcount = 0;
	*b = 0;
	return -2;
    case 'p':
    case 'P':
	return -3;
    case 'c':
	return -4;
    case 'g':
	return -5;
    case 's':			/* 翻開棋子 或是選擇棋子 */
	/* 選擇棋子 */
	if (dd->brd[dd->my][dd->mx].out == 1) {
	    if (dd->brd[dd->my][dd->mx].color != color) {
		*b = -1;
		break;
	    }
	    if (dd->mly < 0) {	/* 可以選擇 */
		dd->mly = dd->my;
		dd->mlx = dd->mx;
		draw_line(dd, dd->my, dd->mx);
		*b = -1;
		break;
	    } else if (dd->mly == dd->my && dd->mlx == dd->mx) {	/* 不選了 */
		dd->mly = -1;
		dd->mlx = -1;
		draw_line(dd, dd->my, -1);
	    } else {
		draw_line(dd, dd->mly, -1);
		dd->mly = dd->my;
		dd->mlx = dd->mx;
		if (dd->brd[dd->mly][dd->mlx].value == 1)
		    dd->fix = 1;
		draw_line(dd, dd->my, dd->mx);
	    }
	    *b = -1;
	    break;
	}
	/* 翻開棋子 */
	if (dd->mly >= 0) {
	    *b = -1;
	    break;
	}			/* 本來就是翻開的 */
	/* 決定一開始的顏色 */
	if (currutmp->color == '.') {
	    if (uin->color != '1' && uin->color != '0')
		currutmp->color = (dd->brd[dd->my][dd->mx].color) ? '1' : '0';
	    else
		currutmp->color = (uin->color == '0') ? '1' : '0';
	}
	dd->brd[dd->my][dd->mx].out = 1;
	draw_line(dd, dd->my, -1);
	move(cury[dd->my], curx[dd->mx]);
	*b = 0;
	break;
    case 'u':
	move(0, 0);
	clrtoeol();
	prints("%s色%s cont=%d", 
		(dd->brd[dd->my][dd->mx].color == RED) ? "紅" : "黑", 
		rname[dd->brd[dd->my][dd->mx].value], dd->cont);
	*b = -1;
	break;
    case KEY_ENTER:			/* 吃 or 移動  ly跟lx必須大於0 */
	if (
	    dd->mly >= 0		/* 要先選子 */
	    &&
	    dd->brd[dd->mly][dd->mlx].color != dd->brd[dd->my][dd->mx].color	/* 同色不能移動也不能吃 */
	    &&
	    (Is_move(dd, dd->my, dd->mx, dd->mly, dd->mlx) || 
	     Is_win(dd, dd->brd[dd->mly][dd->mlx], dd->brd[dd->my][dd->mx], dd->my, dd->mx, dd->mly, dd->mlx))
	    ) {
	    if (dd->fix && dd->brd[dd->my][dd->mx].value < 0) {
		*b = -1;
		return 0;
	    }
	    if (dd->brd[dd->my][dd->mx].value >= 0 && dd->brd[dd->my][dd->mx].die == 0) {
		if (!color)
		    dd->bcount--;
		else
		    dd->rcount--;
		move(dd->cur_eaty, dd->cur_eatx);
		if(color)
		    outs(bname[dd->brd[dd->my][dd->mx].value]);
		else
		    outs(rname[dd->brd[dd->my][dd->mx].value]);
		if (dd->cur_eatx >= 26) {
		    dd->cur_eatx = 5;
		    dd->cur_eaty++;
		} else
		    dd->cur_eatx += 3;
	    }
	    brdswap(dd, dd->my, dd->mx, dd->mly, dd->mlx);
	    draw_line(dd, dd->mly, -1);
	    draw_line(dd, dd->my, -1);
	    if (dd->fix == 1)
		*b = -1;
	    else {
		dd->mly = -1;
		dd->mlx = -1;
		*b = 0;
	    }
	} else
	    *b = -1;
	break;
    default:
	*b = -1;
    }

    if (!dd->rcount)
	return -1;
    else if (!dd->bcount)
	return -1;
    if (*b == -1)
	return 0;
    dd->curr.y = dd->my;
    dd->curr.x = dd->mx;
    dd->curr.end = (!*b) ? 1 : 0;
    send(fd, &dd->curr, sizeof(dd->curr), 0);
    send(fd, &dd->brd, sizeof(dd->brd), 0);
    return 0;
}

int
main_dark(int fd, userinfo_t * uin)
{
    sint            end = 0, ch = 1, i = 0;
    char            buf[16];
    struct DarkData dd;

    memset(&dd, 0, sizeof(dd));
    dd.my=dd.mx=0;
    dd.mly=dd.mlx=-1;

    *buf = 0;
    dd.fix = 0;
    currutmp->color = '.';
    /* '.' 表示還沒決定顏色 */
    dd.rcount = 16;
    dd.bcount = 16;
    //initialize
    dd.cur_eaty = 18, dd.cur_eatx = 5;
    setutmpmode(DARK);
    brd_prints();
    if (currutmp->turn) {
	brd_rand(&dd);
	send(fd, &dd.brd, sizeof(dd.brd), 0);
	mvouts(21, 0, "   " ANSI_COLOR(1;37) ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;37) "你是先手" ANSI_RESET);
	mvouts(22, 0, "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(5;35) "輪到你下了" ANSI_RESET);
    } else {
	recv(fd, &dd.brd, sizeof(dd.brd), 0);
	mvouts(21, 0, "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;37) "你是後手" ANSI_RESET);
    }
    move(12, 3);
    prints("%s[0勝0敗]" ANSI_COLOR(5;31) "ｖｓ" ANSI_COLOR(1;37) "." ANSI_RESET "%s[0勝0敗]", currutmp->userid, currutmp->mateid);
    outs("\n"
	 "                                                " ANSI_COLOR(1;36) "╳╱" ANSI_COLOR(1;31) "功\能表" ANSI_COLOR(1;36) "╲╳╲╱╳╲" ANSI_RESET "\n"
	 "                                                " ANSI_COLOR(1;36) "╱" ANSI_COLOR(1;33) " ↑←↓→" ANSI_COLOR(1;37) ": " ANSI_COLOR(1;35) "移動" ANSI_RESET "\n"
	 "                                                " ANSI_COLOR(1;36) "╳" ANSI_COLOR(1;33) " ｓ" ANSI_COLOR(1;37) ": " ANSI_COLOR(1;35) "      選子,翻子" ANSI_RESET "\n"
	 "                                                " ANSI_COLOR(1;36) "╱" ANSI_COLOR(1;33) " enter" ANSI_COLOR(1;37) ": " ANSI_COLOR(1;35) "   吃棋,放棋" ANSI_RESET "\n"
	 "　" ANSI_COLOR(1;33) "已經解決的" ANSI_COLOR(1;37) ":" ANSI_COLOR(1;36) "　　                               ╳" ANSI_COLOR(1;33) " ｐ" ANSI_COLOR(1;37) ": " ANSI_COLOR(1;35) "      合棋" ANSI_RESET "\n"
	 "                                       　　     " ANSI_COLOR(1;36) "╱" ANSI_COLOR(1;33) " ｑ" ANSI_COLOR(1;37) ": " ANSI_COLOR(1;35) "      認輸" ANSI_RESET "\n"
	 "                                                " ANSI_COLOR(1;36) "╳" ANSI_COLOR(1;33) " ｃ" ANSI_COLOR(1;37) ": " ANSI_COLOR(1;35) "      換邊" ANSI_RESET);

    if (currutmp->turn)
	move(cury[0], curx[0]);

    vkey_attach(fd);
    while (end <= 0) {
	if (uin->turn == 'w' || currutmp->turn == 'w') {
	    end = -1;
	    break;
	}
	ch = vkey();
	if (ch == I_OTHERDATA) {
	    ch = recv(fd, &dd.curr, sizeof(dd.curr), 0);
	    if (ch != sizeof(dd.curr)) {
		if (uin->turn == 'e') {
		    end = -3;
		    break;
		} else if (uin->turn != 'w') {
		    end = -1;
		    currutmp->turn = 'w';
		    break;
		}
		end = -1;
		break;
	    }
	    if (dd.curr.end == -3)
		mvouts(23, 30, ANSI_COLOR(33) "要求合棋" ANSI_RESET);
	    else if (dd.curr.end == -4)
		mvouts(23, 30, ANSI_COLOR(33) "要求換邊" ANSI_RESET);
	    else if (dd.curr.end == -5)
		mvouts(23, 30, ANSI_COLOR(33) "要求連吃" ANSI_RESET);
	    else
		mvouts(23, 30, "");

	    recv(fd, &dd.brd, sizeof(dd.brd), 0);
	    dd.my = dd.curr.y;
	    dd.mx = dd.curr.x;
	    redraw(&dd);
	    if (dd.curr.end)
		mvouts(22, 0, "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(5;35) "輪到你下了" ANSI_RESET);
	    move(cury[dd.my], curx[dd.mx]);
	} else {
	    if (currutmp->turn == 'p') {
		if (ch == 'y') {
		    end = -3;
		    currutmp->turn = 'e';
		    break;
		} else {
		    mvouts(23, 30, "");
		    *buf = 0;
		    currutmp->turn = (uin->turn) ? 0 : 1;
		}
	    } else if (currutmp->turn == 'c') {
		if (ch == 'y') {
		    currutmp->color = (currutmp->color == '1') ? '0' : '1';
		    uin->color = (uin->color == '1') ? '0' : '1';
		    mvouts(21, 0, (currutmp->color == '1') ? "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;31) "你持紅色棋" ANSI_RESET : "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;36) "你持黑色棋" ANSI_RESET);
		} else {
		    mvouts(23, 30, "");
		    currutmp->turn = (uin->turn) ? 0 : 1;
		}
	    } else if (currutmp->turn == 'g') {
		if (ch == 'y') {
		    dd.cont = 1;
		    mvouts(21, 0, "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;31) "你持紅色棋" ANSI_RESET " 可連吃");
		} else {
		    mvouts(23, 30, "");
		    currutmp->turn = (uin->turn) ? 0 : 1;
		}
	    }
	    if (currutmp->turn == 1) {
	        sint go_on = 0;
		if (uin->turn == 'g') {
		    dd.cont = 1;
		    uin->turn = (currutmp->turn) ? 0 : 1;
		    mvouts(21, 10, "可連吃");
		}
		end = playing(&dd, fd, currutmp->color - '0', ch, &go_on, uin);

		if (end == -1) {
		    currutmp->turn = 'w';
		    break;
		} else if (end == -2) {
		    uin->turn = 'w';
		    break;
		} else if (end == -3) {
		    uin->turn = 'p';
		    dd.curr.end = -3;
		    send(fd, &dd.curr, sizeof(dd.curr), 0);
		    send(fd, &dd.brd, sizeof(buf), 0);
		    continue;
		} else if (end == -4) {
		    if (currutmp->color != '1' && currutmp->color != '0')
			continue;
		    uin->turn = 'c';
		    i = 0;
		    dd.curr.end = -4;
		    send(fd, &dd.curr, sizeof(dd.curr), 0);
		    send(fd, &dd.brd, sizeof(buf), 0);
		    continue;
		} else if (end == -5) {
		    uin->turn = 'g';
		    dd.curr.end = -5;
		    send(fd, &dd.curr, sizeof(dd.curr), 0);
		    send(fd, &dd.brd, sizeof(buf), 0);
		    continue;
		}
		if (!i && currutmp->color == '1') {
		    mvouts(21, 0, "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;31) "你持紅色棋" ANSI_RESET);
		    i++;
		    move(cury[dd.my], curx[dd.mx]);
		}
		if (!i && currutmp->color == '0') {
		    mvouts(21, 0, "   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;36) "你持黑色棋" ANSI_RESET);
		    i++;
		    move(cury[dd.my], curx[dd.mx]);
		}
		if (uin->turn == 'e') {
		    end = -3;
		    break;
		}
		if (go_on < 0)
		    continue;

		move(22, 0);
		clrtoeol();
		prints("   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;37) "輪到%s下 別怕別怕 他算啥米" ANSI_RESET, currutmp->mateid);
		currutmp->turn = 0;
		uin->turn = 1;
	    } else {
		if (ch == 'q') {
		    uin->turn = 'w';
		    break;
		}
		move(22, 0);
		clrtoeol();
		prints("   " ANSI_COLOR(1;33) "◆" ANSI_COLOR(1;37) "輪到%s下 別怕別怕 他算啥米" ANSI_RESET, currutmp->mateid);
	    }
	}
    }

    switch (end) {
    case -1:
    case -2:
	if (currutmp->turn == 'w') {
	    move(22, 0);
	    clrtoeol();
	    outs(ANSI_COLOR(1;31) "你贏了.. 真是恭喜~~" ANSI_RESET);
	} else {
	    move(22, 0);
	    clrtoeol();
	    outs(ANSI_COLOR(1;31) "輸掉了啦.....下次讓他好看!!" ANSI_RESET);
	}
	break;
    case -3:
	mvouts(22, 0, ANSI_COLOR(1;31) "合棋唷!! 下次在分高下吧 ^_^" ANSI_RESET);
	break;
    default:
	vkey_detach();
	close(fd);
	pressanykey();
	return 0;
    }
    vkey_detach();
    close(fd);
    pressanykey();
    return 0;
}
