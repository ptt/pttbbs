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

static item     brd[4][8];
static cur      curr;		/* 6 ­Ó bytes */

static char    * const rname[] = {"§L", "¬¶", "ØX", "¨®", "¬Û", "¥K", "«Ó"};
static char    * const bname[] = {"¨ò", "¥]", "°¨", "¨®", "¶H", "¤h", "±N"};

const static sint     cury[] = {3, 5, 7, 9}, curx[] = {5, 9, 13, 17, 21, 25, 29, 33};
static sint     rcount, bcount, cont, fix;	/* cont:¬O§_¥i³s¦Y */
static sint     my = 0, mx = 0, mly = -1, mlx = -1;	/* ²¾°Êªº®y¼Ð ¼Ð */

static sint     cur_eaty, cur_eatx;	/* ¦Y±¼¹ï¤è¨ä¤lªº¨q¥X®y¼Ð */
static void
brdswap(sint y, sint x, sint ly, sint lx)
{
    memcpy(&brd[y][x], &brd[ly][lx], sizeof(item));
    brd[ly][lx].die = 1;
    brd[ly][lx].color = -1;	/* ¨S³o­Ócolor */
    brd[ly][lx].value = -1;
}

static          sint
Is_win(item att, item det, sint y, sint x, sint ly, sint lx)
{
    sint            i, c = 0, min, max;
    if (att.value == 1) {	/* ¯¥ */
	if (y != ly && x != lx)
	    return 0;
	if ((abs(ly - y) == 1 && brd[y][x].die == 0) ||
	    (abs(lx - x) == 1 && brd[y][x].die == 0))
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
		if (brd[y][i].die == 0)
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
		if (brd[i][x].die == 0)
		    c++;
	}
	if (c != 1)
	    return 0;
	if (det.die == 1)
	    return 0;
	return 1;
    }
    /* «D¯¥ */
    if (((abs(ly - y) == 1 && x == lx) || (abs(lx - x) == 1 && ly == y)) && brd[y][x].out == 1) {
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
Is_move(sint y, sint x, sint ly, sint lx)
{
    if (brd[y][x].die == 1 && ((abs(ly - y) == 1 && x == lx) || (abs(lx - x) == 1 && ly == y)))
	return 1;
    return 0;
}

static void
brd_rand()
{
    sint            y, x, index;
    sint            tem[32];
    sint            value[32] = {0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6};

    bzero(brd, sizeof(brd));
    bzero(tem, sizeof(tem));
    bzero(&curr, sizeof(curr));
    for (y = 0; y < 4; y++)
	for (x = 0; x < 8; x++)
	    while (1) {
		index = rand() % 32;
		if (tem[index])
		    continue;
		brd[y][x].color = (index > 15) ? 0 : 1;
		brd[y][x].value = value[index];
		tem[index] = 1;
		break;
	    }
}

static void
brd_prints()
{
    clear();
    move(1, 0);
    outs("\n"
	 "   [43;30m¢~¢w¢s¢w¢s¢w¢s¢w¢s¢w¢s¢w¢s¢w¢s¢w¢¡[m\n"
	 "   [43;30m¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x[m\n"
	 "   [43;30m¢u¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢t[m\n"
	 "   [43;30m¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x[m\n"
	 "   [43;30m¢u¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢t[m\n"
	 "   [43;30m¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x[m\n"
	 "   [43;30m¢u¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢q¢w¢t[m\n"
	 "   [43;30m¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x¡´¢x[m\n"
	 "   [43;30m¢¢¢w¢r¢w¢r¢w¢r¢w¢r¢w¢r¢w¢r¢w¢r¢w¢£[m\n"
	 "   ");
}

static void
draw_line(sint y, sint f)
{
    sint            i;
    char            buf[1024], tmp[256];

    *buf = 0;
    *tmp = 0;
    strlcpy(buf, "\033[43;30m", sizeof(buf));
    for (i = 0; i < 8; i++) {
	if (brd[y][i].die == 1)
	    snprintf(tmp, sizeof(tmp), "¢x  ");
	else if (brd[y][i].out == 0)
	    snprintf(tmp, sizeof(tmp), "¢x¡´");
	else {
	    snprintf(tmp, sizeof(tmp), "¢x\033[%s1;%dm%s\033[m\033[43;30m",
		     (f == i) ? "1;47;" : "", (brd[y][i].color) ? 31 : 34,
		     (brd[y][i].color) ? rname[brd[y][i].value] :
		     bname[brd[y][i].value]);
	}
	strcat(buf, tmp);
    }
    strcat(buf, "¢x\033[m");

    move(cury[y], 3);
    clrtoeol();
    outs(buf);
}

static void
redraw()
{
    sint            i = 0;
    for (; i < 4; i++)
	draw_line(i, -1);
}

static          sint
playing(sint fd, sint color, sint ch, sint * b, userinfo_t * uin)
{
    curr.end = 0;
    move(cury[my], curx[mx]);

    if (fix) {
	if (ch == 's') {
	    fix = 0;
	    *b = 0;
	    return 0;
	} else {
	    draw_line(mly, -1);
	}
    }
    switch (ch) {
    case KEY_LEFT:
	if (mx == 0)
	    mx = 7;
	else
	    mx--;
	move(cury[my], curx[mx]);
	*b = -1;
	break;
    case KEY_RIGHT:
	if (mx == 7)
	    mx = 0;
	else
	    mx++;
	move(cury[my], curx[mx]);
	*b = -1;
	break;
    case KEY_UP:
	if (my == 0)
	    my = 3;
	else
	    my--;
	move(cury[my], curx[mx]);
	*b = -1;
	break;
    case KEY_DOWN:
	if (my == 3)
	    my = 0;
	else
	    my++;
	move(cury[my], curx[mx]);
	*b = -1;
	break;
    case 'q':
    case 'Q':
	if (!color)
	    bcount = 0;
	else
	    rcount = 0;
	*b = 0;
	return -2;
    case 'p':
    case 'P':
	return -3;
    case 'c':
	return -4;
    case 'g':
	return -5;
    case 's':			/* Â½¶}´Ñ¤l ©Î¬O¿ï¾Ü´Ñ¤l */
	/* ¿ï¾Ü´Ñ¤l */
	if (brd[my][mx].out == 1) {
	    if (brd[my][mx].color != color) {
		*b = -1;
		break;
	    }
	    if (mly < 0) {	/* ¥i¥H¿ï¾Ü */
		mly = my;
		mlx = mx;
		draw_line(my, mx);
		*b = -1;
		break;
	    } else if (mly == my && mlx == mx) {	/* ¤£¿ï¤F */
		mly = -1;
		mlx = -1;
		draw_line(my, -1);
	    } else {
		draw_line(mly, -1);
		mly = my;
		mlx = mx;
		if (brd[mly][mlx].value == 1)
		    fix = 1;
		draw_line(my, mx);
	    }
	    *b = -1;
	    break;
	}
	/* Â½¶}´Ñ¤l */
	if (mly >= 0) {
	    *b = -1;
	    break;
	}			/* ¥»¨Ó´N¬OÂ½¶}ªº */
	/* ¨M©w¤@¶}©lªºÃC¦â */
	if (currutmp->color == '.') {
	    if (uin->color != '1' && uin->color != '0')
		currutmp->color = (brd[my][mx].color) ? '1' : '0';
	    else
		currutmp->color = (uin->color == '0') ? '1' : '0';
	}
	brd[my][mx].out = 1;
	draw_line(my, -1);
	move(cury[my], curx[mx]);
	*b = 0;
	break;
    case 'u':
	move(0, 0);
	clrtoeol();
	prints("%s¦â%s cont=%d", (brd[my][mx].color == RED) ? "¬õ" : "¶Â", rname[brd[my][mx].value], cont);
	*b = -1;
	break;
    case '\r':			/* ¦Y or ²¾°Ê  ly¸òlx¥²¶·¤j©ó0 */
    case '\n':
	if (
	    mly >= 0		/* ­n¥ý¿ï¤l */
	    &&
	    brd[mly][mlx].color != brd[my][mx].color	/* ¦P¦â¤£¯à²¾°Ê¤]¤£¯à¦Y */
	    &&
	    (Is_move(my, mx, mly, mlx) || Is_win(brd[mly][mlx], brd[my][mx], my, mx, mly, mlx))
	    ) {
	    if (fix && brd[my][mx].value < 0) {
		*b = -1;
		return 0;
	    }
	    if (brd[my][mx].value >= 0 && brd[my][mx].die == 0) {
		if (!color)
		    bcount--;
		else
		    rcount--;
		move(cur_eaty, cur_eatx);
		outs((color) ? bname[brd[my][mx].value] : rname[brd[my][mx].value]);
		if (cur_eatx >= 26) {
		    cur_eatx = 5;
		    cur_eaty++;
		} else
		    cur_eatx += 3;
	    }
	    brdswap(my, mx, mly, mlx);
	    draw_line(mly, -1);
	    draw_line(my, -1);
	    if (fix == 1)
		*b = -1;
	    else {
		mly = -1;
		mlx = -1;
		*b = 0;
	    }
	} else
	    *b = -1;
	break;
    default:
	*b = -1;
    }

    if (!rcount)
	return -1;
    else if (!bcount)
	return -1;
    if (*b == -1)
	return 0;
    curr.y = my;
    curr.x = mx;
    curr.end = (!*b) ? 1 : 0;
    send(fd, &curr, sizeof(curr), 0);
    send(fd, &brd, sizeof(brd), 0);
    return 0;
}

int
main_dark(int fd, userinfo_t * uin)
{
    sint            end = 0, ch = 1, i = 0, cont = 0;
    char            buf[16];
    *buf = 0;
    fix = 0;
    currutmp->color = '.';
    /* '.' ªí¥ÜÁÙ¨S¨M©wÃC¦â */
    rcount = 16;
    bcount = 16;
    //initialize
    cur_eaty = 18, cur_eatx = 5;
    setutmpmode(DARK);
    brd_prints();
    if (currutmp->turn) {
	brd_rand();
	send(fd, &brd, sizeof(brd), 0);
	mouts(21, 0, "   [1;37m[1;33m¡»[1;37m§A¬O¥ý¤â[m");
	mouts(22, 0, "   [1;33m¡»[5;35m½ü¨ì§A¤U¤F[m");
    } else {
	recv(fd, &brd, sizeof(brd), 0);
	mouts(21, 0, "   [1;33m¡»[1;37m§A¬O«á¤â[m");
    }
    move(12, 3);
    prints("%s[0³Ó0±Ñ][5;31m¢þ¢û[1;37m.[m%s[0³Ó0±Ñ]", currutmp->userid, currutmp->mateid);
    outs("\n"
	 "                                                [1;36m¢®¢¬[1;31m¥\\¯àªí[1;36m¢­¢®¢­¢¬¢®¢­[m\n"
	 "                                                [1;36m¢¬[1;33m ¡ô¡ö¡õ¡÷[1;37m: [1;35m²¾°Ê[m\n"
	 "                                                [1;36m¢®[1;33m ¢û[1;37m: [1;35m      ¿ï¤l,Â½¤l[m\n"
	 "                                                [1;36m¢¬[1;33m enter[1;37m: [1;35m   ¦Y´Ñ,©ñ´Ñ[m\n"
	 "¡@[1;33m¤w¸g¸Ñ¨Mªº[1;37m:[1;36m¡@¡@                               ¢®[1;33m ¢ø[1;37m: [1;35m      ¦X´Ñ[m\n"
	 "                                       ¡@¡@     [1;36m¢¬[1;33m ¢ù[1;37m: [1;35m      »{¿é[m\n"
	 "                                                [1;36m¢®[1;33m ¢ë[1;37m: [1;35m      ´«Ãä[m");

    if (currutmp->turn)
	move(cury[0], curx[0]);

    add_io(fd, 0);
    while (end <= 0) {
	if (uin->turn == 'w' || currutmp->turn == 'w') {
	    end = -1;
	    break;
	}
	ch = igetch();
	if (ch == I_OTHERDATA) {
	    ch = recv(fd, &curr, sizeof(curr), 0);
	    if (ch != sizeof(curr)) {
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
	    if (curr.end == -3)
		mouts(23, 30, "\033[33m­n¨D¦X´Ñ\033[m");
	    else if (curr.end == -4)
		mouts(23, 30, "\033[33m­n¨D´«Ãä\033[m");
	    else if (curr.end == -5)
		mouts(23, 30, "\033[33m­n¨D³s¦Y\033[m");
	    else
		mouts(23, 30, "");

	    recv(fd, &brd, sizeof(brd), 0);
	    my = curr.y;
	    mx = curr.x;
	    redraw();
	    if (curr.end)
		mouts(22, 0, "   [1;33m¡»[5;35m½ü¨ì§A¤U¤F[m");
	    move(cury[my], curx[mx]);
	} else {
	    if (currutmp->turn == 'p') {
		if (ch == 'y') {
		    end = -3;
		    currutmp->turn = 'e';
		    break;
		} else {
		    mouts(23, 30, "");
		    *buf = 0;
		    currutmp->turn = (uin->turn) ? 0 : 1;
		}
	    } else if (currutmp->turn == 'c') {
		if (ch == 'y') {
		    currutmp->color = (currutmp->color == '1') ? '0' : '1';
		    uin->color = (uin->color == '1') ? '0' : '1';
		    mouts(21, 0, (currutmp->color == '1') ? "   \033[1;33m¡»[1;31m§A«ù¬õ¦â´Ñ\033[m" : "   \033[1;33m¡»[1;36m§A«ù¶Â¦â´Ñ\033[m");
		} else {
		    mouts(23, 30, "");
		    currutmp->turn = (uin->turn) ? 0 : 1;
		}
	    } else if (currutmp->turn == 'g') {
		if (ch == 'y') {
		    cont = 1;
		    mouts(21, 0, "   \033[1;33m¡»[1;31m§A«ù¬õ¦â´Ñ\033[m ¥i³s¦Y");
		} else {
		    mouts(23, 30, "");
		    currutmp->turn = (uin->turn) ? 0 : 1;
		}
	    }
	    if (currutmp->turn == 1) {
	        sint go_on;
		if (uin->turn == 'g') {
		    cont = 1;
		    uin->turn = (currutmp->turn) ? 0 : 1;
		    mouts(21, 10, "¥i³s¦Y");
		}
		end = playing(fd, currutmp->color - '0', ch, &go_on, uin);

		if (end == -1) {
		    currutmp->turn = 'w';
		    break;
		} else if (end == -2) {
		    uin->turn = 'w';
		    break;
		} else if (end == -3) {
		    uin->turn = 'p';
		    curr.end = -3;
		    send(fd, &curr, sizeof(curr), 0);
		    send(fd, &brd, sizeof(buf), 0);
		    continue;
		} else if (end == -4) {
		    if (currutmp->color != '1' && currutmp->color != '0')
			continue;
		    uin->turn = 'c';
		    i = 0;
		    curr.end = -4;
		    send(fd, &curr, sizeof(curr), 0);
		    send(fd, &brd, sizeof(buf), 0);
		    continue;
		} else if (end == -5) {
		    uin->turn = 'g';
		    curr.end = -5;
		    send(fd, &curr, sizeof(curr), 0);
		    send(fd, &brd, sizeof(buf), 0);
		    continue;
		}
		if (!i && currutmp->color == '1') {
		    mouts(21, 0, "   \033[1;33m¡»[1;31m§A«ù¬õ¦â´Ñ\033[m");
		    i++;
		    move(cury[my], curx[mx]);
		}
		if (!i && currutmp->color == '0') {
		    mouts(21, 0, "   \033[1;33m¡»[1;36m§A«ù¶Â¦â´Ñ\033[m");
		    i++;
		    move(cury[my], curx[mx]);
		}
		if (uin->turn == 'e') {
		    end = -3;
		    break;
		}
		if (go_on < 0)
		    continue;

		move(22, 0);
		clrtoeol();
		prints("   [1;33m¡»[1;37m½ü¨ì%s¤U §O©È§O©È ¥LºâÔ£¦Ì[m", currutmp->mateid);
		currutmp->turn = 0;
		uin->turn = 1;
	    } else {
		if (ch == 'q') {
		    uin->turn = 'w';
		    break;
		}
		move(22, 0);
		clrtoeol();
		prints("   [1;33m¡»[1;37m½ü¨ì%s¤U §O©È§O©È ¥LºâÔ£¦Ì[m", currutmp->mateid);
	    }
	}
    }

    switch (end) {
    case -1:
    case -2:
	if (currutmp->turn == 'w') {
	    move(22, 0);
	    clrtoeol();
	    outs("[1;31m§AÄ¹¤F.. ¯u¬O®¥³ß~~[m");
	} else {
	    move(22, 0);
	    clrtoeol();
	    outs("[1;31m¿é±¼¤F°Õ.....¤U¦¸Åý¥L¦n¬Ý!![m");
	}
	break;
    case -3:
	mouts(22, 0, "[1;31m¦X´Ñ­ò!! ¤U¦¸¦b¤À°ª¤U§a ^_^[m");
	break;
    default:
	add_io(0, 0);
	close(fd);
	pressanykey();
	return 0;
    }
    add_io(0, 0);
    close(fd);
    pressanykey();
    return 0;
}
