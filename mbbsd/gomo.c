/* $Id$ */
#include "bbs.h"
#include "gomo.h"

#define QCAST   int (*)(const void *, const void *)

static int      tick, lastcount, mylasttick, hislasttick;
//static char     ku[BRDSIZ][BRDSIZ];

static Horder_t *v;
static int  draw_photo;

#define move(y,x)	move(y, (x) + ((y) < 2 || (y) > 16 ? 0 : \
			(x) > 35 ? 11 : 8))

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
gomo_getindex(char ku[][BRDSIZ], int x, int y, int color, int dx, int dy)
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

int
chkwin(int style, int limit)
{
    if (style == 0x0c)
	return 1 /* style */ ;
    else if (limit == 0) {
	if (style == 0x0b)
	    return 1 /* style */ ;
	return 0;
    }
    if ((style < 0x0c) && (style > 0x07))
	return -1 /* -style */ ;
    return 0;
}

static int getstyle(char ku[][BRDSIZ], int x, int y, int color, int limit);
/* x,y: 0..BRDSIZ-1 ; color: CBLACK,CWHITE ; limit:1,0 ; dx,dy: 0,1 */
static int
dirchk(char ku[][BRDSIZ], int x, int y, int color, int limit, int dx, int dy)
{
    int             le, ri, loc, style = 0;

    le = gomo_getindex(ku, x, y, color, -dx, -dy);
    ri = gomo_getindex(ku, x, y, color, dx, dy);

    loc = (le > ri) ? (((le * (le + 1)) >> 1) + ri) :
	(((ri * (ri + 1)) >> 1) + le);

    style = pat[loc];

    if (limit == 0)
	return (style & 0x0f);

    style >>= 4;

    if ((style == 3) || (style == 2)) {
	int             i, n = 0, tmp, nx, ny;

	n = adv[loc / 2];

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
getstyle(char ku[][BRDSIZ], int x, int y, int color, int limit)
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

static void
HO_init(char ku[][BRDSIZ], Horder_t *pool)
{
    memset(pool, 0, sizeof(Horder_t)*BRDSIZ*BRDSIZ);
    v = pool;
    pat = pat_gomoku;
    adv = adv_gomoku;
    memset(ku, 0, (BRDSIZ*BRDSIZ));
}

static void
HO_add(Horder_t * mv)
{
    *v++ = *mv;
}

static void
HO_undo(char ku[][BRDSIZ], Horder_t * mv)
{
    char           *str = "┌┬┐├┼┤└┴┘";
    int             n1, n2, loc;

    *mv = *(--v);
    ku[(int)mv->x][(int)mv->y] = BBLANK;
    BGOTO(mv->x, mv->y);
    n1 = (mv->x == 0) ? 0 : (mv->x == 14) ? 2 : 1;
    n2 = (mv->y == 14) ? 0 : (mv->y == 0) ? 2 : 1;
    loc = 2 * (n2 * 3 + n1);
    prints("%.2s", str + loc);
    redoln();
}

static void
HO_log(Horder_t *pool, char *user)
{
    int             i;
    FILE           *log;
    char            buf[80];
    char            buf1[80];
    char            title[80];
    Horder_t       *ptr = pool;
    fileheader_t    mymail;

    snprintf(buf, sizeof(buf), "home/%c/%s/F.%d",
	     cuser.userid[0], cuser.userid,  rand() & 65535);
    log = fopen(buf, "w");
    assert(log);

    for (i = 1; i < 17; i++)
	fprintf(log, "%.*s\n", big_picture[i].len, big_picture[i].data);

    i = 0;
    do {
	fprintf(log, "[%2d]%s ==> %c%d%c", i + 1, bw_chess[i % 2],
		'A' + ptr->x, ptr->y + 1, (i % 2) ? '\n' : '\t');
	i++;
    } while (++ptr < v);
    fclose(log);

    sethomepath(buf1, cuser.userid);
    stampfile(buf1, &mymail);

    mymail.filemode = FILE_READ ;
    strlcpy(mymail.owner, "[備.忘.錄]", sizeof(mymail.owner));
    snprintf(mymail.title, sizeof(mymail.title),
	     "\033[37;41m棋譜\033[m %s VS %s", cuser.userid, user);
    sethomedir(title, cuser.userid);
    Rename(buf, buf1);
    append_record(title, &mymail, sizeof(mymail));

    unlink(buf);
}

static int
countgomo(Horder_t *pool)
{
    return v-pool;
}

static int
chkmv(char ku[][BRDSIZ], Horder_t * mv, int color, int limit)
{
    char           *xtype[] = {"\033[1;31m跳三\033[m", "\033[1;31m活三\033[m",
	"\033[1;31m死四\033[m", "\033[1;31m跳四\033[m",
	"\033[1;31m活四\033[m", "\033[1;31m四三\033[m",
	"\033[1;31m雙三\033[m", "\033[1;31m雙四\033[m",
	"\033[1;31m雙四\033[m", "\033[1;31m連六\033[m",
    "\033[1;31m連五\033[m"};
    int             rule = getstyle(ku, mv->x, mv->y, color, limit);
    if (rule > 1 && rule < 13) {
	move(draw_photo ? 19 : 15, 40);
	outs(xtype[rule - 2]);
	bell();
    }
    return chkwin(rule, limit);
}

static int
gomo_key(char ku[][BRDSIZ], int fd, int ch, Horder_t * mv)
{
    if (ch >= 'a' && ch <= 'o') {
	char            pbuf[4], vx, vy;

	pbuf[0] = ch;
	pbuf[1] = '\0';

	if (fd)
	    add_io(0, 0);
	oldgetdata(17, 0, "直接指定位置 :", pbuf, sizeof(pbuf), DOECHO);
	if (fd)
	    add_io(fd, 0);
	vx = pbuf[0] - 'a';
	vy = atoi(pbuf + 1) - 1;
	if (vx >= 0 && vx < 15 && vy >= 0 && vy < 15 &&
	    ku[(int)vx][(int)vy] == BBLANK) {
	    mv->x = vx;
	    mv->y = vy;
	    return 1;
	}
    } else {
	switch (ch) {
	case KEY_RIGHT:
	    if(mv->x<BRDSIZ-1)
		mv->x++;
	    break;
	case KEY_LEFT:
	    if(mv->x>0)
		mv->x--;
	    break;
	case KEY_UP:
	    if(mv->y<BRDSIZ-1)
		mv->y++;
	    break;
	case KEY_DOWN:
	    if(mv->y>0)
		mv->y--;
	    break;
	case ' ':
	case '\r':
	    if (ku[(int)mv->x][(int)mv->y] == BBLANK)
		return 1;
	}
    }
    return 0;
}

int
gomoku(int fd)
{
    Horder_t        mv;
    int             me, he, ch;
    char            hewantpass, iwantpass, passrejected;
    userinfo_t     *my = currutmp;
    Horder_t        pool[BRDSIZ*BRDSIZ];
    int  scr_need_redraw;
    char ku[BRDSIZ][BRDSIZ];
    char genbuf[200];

    HO_init(ku, pool);
    me = !(my->turn) + 1;
    he = my->turn + 1;
    tick = now + MAX_TIME;
    lastcount = MAX_TIME;
    setutmpmode(M_FIVE);
    clear();

    prints("\033[1;46m  五子棋對戰  \033[45m%30s VS %-30s\033[m",
	   cuser.userid, my->mateid);
    //show_file("etc/@five", 1, -1, ONLY_COLOR);
    move(1, 0);
    outs(
	    "    A B C D E F G H I J K L M N\n"
	    " 15\033[30;43m┌┬┬┬┬┬┬┬┬┬┬┬┬┬┐\033[m\n"
	    " 14\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    " 13\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    " 12\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    " 11\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    " 10\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  9\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  8\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  7\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  6\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  5\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  4\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  3\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  2\033[30;43m├┼┼┼┼┼┼┼┼┼┼┼┼┼┤\033[m\n"
	    "  1\033[30;43m└┴┴┴┴┴┴┴┴┴┴┴┴┴┘\033[m\n"
	);

    draw_photo = 0;
    setuserfile(genbuf, "photo_fivechess");
    if (dashf(genbuf))
	draw_photo = 1;
    else {
	sethomefile(genbuf, my->mateid, "photo_fivechess");
	if (dashf(genbuf))
	    draw_photo = 1;
    }

    getuser(my->mateid);
    if (draw_photo) {
	int line;
	FILE* fp;
	const static char * const blank_photo[6] = {
	    "┌──────┐",
	    "│ 空         │",
	    "│    白      │",
	    "│       照   │",
	    "│          片│",
	    "└──────┘" 
	};

	setuserfile(genbuf, "photo_fivechess");
	fp = fopen(genbuf, "r");
	for (line = 2; line < 8; ++line) {
	    move(line, 37);
	    if (fp != NULL) {
		if (fgets(genbuf, 200, fp)) {
		    genbuf[strlen(genbuf) - 1] = 0;
		    prints("%s  ", genbuf);
		} else
		    outs("                  ");
	    } else
		outs(blank_photo[line - 2]);

	    switch (line - 2) {
		case 0: prints("<代號> %s", cuser.userid);      break;
		case 1: prints("<暱稱> %.16s", cuser.username); break;
		case 2: prints("<上站> %d", cuser.numlogins);   break;
		case 3: prints("<文章> %d", cuser.numposts);    break;
		case 4: prints("<財產> %d", cuser.money);       break;
		case 5: prints("<來源> %.16s", cuser.lasthost); break;
	    }
	}
	if (fp != NULL)
	    fclose(fp);

	move(8, 43);
	prints("\033[7m%s\033[m", me == BBLACK ? "黑棋" : "白棋");
	move(9, 43);
	outs("           Ｖ.Ｓ           ");
	move(10, 68);
	prints("\033[7m%s\033[m", me == BBLACK ? "白棋" : "黑棋");

	sethomefile(genbuf, my->mateid, "photo_fivechess");
	fp = fopen(genbuf, "r");
	for (line = 11; line < 17; ++line) {
	    move(line, 37);
	    switch (line - 11) {
		case 0: prints("<代號> %-16.16s ", xuser.userid);   break;
		case 1: prints("<暱稱> %-16.16s ", xuser.username); break;
		case 2: prints("<上站> %-16d ", xuser.numlogins);   break;
		case 3: prints("<文章> %-16d ", xuser.numposts);    break;
		case 4: prints("<財產> %-16d ", xuser.money);       break;
		case 5: prints("<來源> %-16.16s ", xuser.lasthost); break;
	    }

	    if (fp != NULL) {
		if (fgets(genbuf, 200, fp)) {
		    genbuf[strlen(genbuf) - 1] = 0;
		    outs(genbuf);
		} else
		    outs("                ");
	    } else
		outs(blank_photo[line - 11]);
	}
	if (fp != NULL)
	    fclose(fp);

	move(18, 4);
	prints("我是 %s", me == BBLACK ? "先手 ●，有禁手" : "後手 ○");
    } else {
	move(3, 40); outs("[q] 認輸離開");
	move(4, 40); outs("[u] 悔棋");
	move(5, 40); outs("[p] 要求和棋");
	move(9, 39); outs("[歡迎到five_chess討論五子棋喔]");

	move(11, 40);
	prints("我是 %s", me == BBLACK ? "先手 ●, 有禁手" : "後手 ○");
	move(16, 40);
	prints("\033[1;33m%s", cuser.userid);
	move(17, 40);
	prints("\033[1;33m%s", my->mateid);

	move(16, 60);
	prints("\033[1;31m%d\033[37m勝 \033[34m%d\033[37m敗 \033[36m%d\033[37m和"
		"\033[m", cuser.five_win, cuser.five_lose, cuser.five_tie);

	move(17, 60);
	prints("\033[1;31m%d\033[37m勝 \033[34m%d\033[37m敗 \033[36m%d\033[37m"
		"和\033[m", xuser.five_win, xuser.five_lose, xuser.five_tie);

	move(18, 40);
	prints("%s時間還剩%d:%02d\n", my->turn ? "你的" : "對方",
		MAX_TIME / 60, MAX_TIME % 60);
    }
    outmsg("\033[1;33;42m 下五子棋 \033[;31;47m (←↑↓→)\033[30m移動 \033[31m(空白鍵/ENTER)\033[30m下子 \033[31m(q)\033[30m投降 \033[31m(p)\033[30m和棋 \033[31m(u)\033[30m悔棋          \033[m");

    cuser.five_lose++;
    /* 一進來先加一場敗場, 贏了後再扣回去, 避免快輸了惡意斷線 */
    passwd_update(usernum, &cuser);

    add_io(fd, 0);

    hewantpass = iwantpass = passrejected = 0;
    mv.x = mv.y = 7;
    scr_need_redraw = 1;
    for (;;) {
	if (scr_need_redraw){
	    if (draw_photo)
		move(19, 4);
	    else
		move(13, 40);
	    outs(my->turn ? "輪到自己下了!" : "等待對方下子..");
	    redoln();
	    scr_need_redraw = 0;
	}
	if (lastcount != tick - now) {
	    lastcount = tick - now;
	    move(18, 40);
	    prints("%s時間還剩%d:%02d\n", my->turn ? "你的" : "對方",
		   lastcount / 60, lastcount % 60);
	    if (lastcount <= 0 && my->turn) {
		move(19, 40);
		outs("時間已到, 你輸了");
		my->five_lose++;
		send(fd, '\0', 1, 0);
		break;
	    }
	    if (lastcount <= -5 && !my->turn) {
		move(19, 40);
		outs("對手太久沒下, 你贏了!");
		cuser.five_lose--;
		cuser.five_win++;
		my->five_win++;
                passwd_update(usernum, &cuser);
		mv.x = mv.y = -2;
		send(fd, &mv, sizeof(Horder_t), 0);
		mv = *(v - 1);
		break;
	    }
	}
	move(draw_photo ? 20 : 14, 40);
	clrtoeol();
	if (hewantpass) {
	    outs("\033[1;32m和棋要求!\033[m");
	    bell();
	} else if (iwantpass)
	    outs("\033[1;32m提出和棋要求!\033[m");
	else if (passrejected) {
	    outs("\033[1;32m要求被拒!\033[m");
	    passrejected = 0;
	}
	BGOTOCUR(mv.x, mv.y);
	ch = igetch();
	if ((iwantpass || hewantpass) && ch != 'p' && ch != I_OTHERDATA) {
	    mv.x = mv.y = -3;
	    send(fd , &mv, sizeof(Horder_t), 0);
	    mv = *(v - 1);
	    iwantpass = 0;
	    hewantpass = 0;
	    continue;
	}
	if (ch == 'q') {
	    if (countgomo(pool) < 10) {
		cuser.five_lose--;
                passwd_update(usernum, &cuser);
	    }
	    send(fd, "", 1, 0);
	    break;
	} else if (ch == 'u' && !my->turn && v > pool) {
	    mv.x = mv.y = -1;
	    ch = send(fd, &mv, sizeof(Horder_t), 0);
	    if (ch == sizeof(Horder_t)) {
		HO_undo(ku, &mv);
		tick = mylasttick;
		my->turn = 1;
		scr_need_redraw = 1;
		continue;
	    } else
		break;
	}
	if (ch == 'p') {
	    if (my->turn) {
		if (iwantpass == 0) {
		    iwantpass = 1;
		    mv.x = mv.y = -2;
		    send(fd, &mv, sizeof(Horder_t), 0);
		    mv = *(v - 1);
		}
		continue;
	    } else if (hewantpass) {
		cuser.five_lose--;
		cuser.five_tie++;
		my->five_tie++;
		passwd_update(usernum, &cuser);
		mv.x = mv.y = -2;
		send(fd, &mv, sizeof(Horder_t), 0);
		mv = *(v - 1);
		break;
	    }
	}
	if (ch == I_OTHERDATA) {
	    ch = recv(fd, &mv, sizeof(Horder_t), 0);
	    if (ch != sizeof(Horder_t)) {
		lastcount = tick - now;
		if (lastcount >= 0) {
		    cuser.five_lose--;
		    if (countgomo(pool) >= 10) {
			cuser.five_win++;
			my->five_win++;
		    }
		    passwd_update(usernum, &cuser);
		    outmsg("對方認輸了!!");
		    break;
		} else {
		    outmsg("你超過時間未下子, 輸了!");
		    my->five_lose++;
		    break;
		}
	    } else if (mv.x == -2 && mv.y == -2) {
		if (iwantpass == 1) {
		    cuser.five_lose--;
		    cuser.five_tie++;
		    my->five_tie++;
		    passwd_update(usernum, &cuser);
		    break;
		} else {
		    hewantpass = 1;
		    mv = *(v - 1);
		    continue;
		}
	    } else if (mv.x == -3 && mv.y == -3) {
		if (iwantpass)
		    passrejected = 1;
		iwantpass = 0;
		hewantpass = 0;
		mv = *(v - 1);
		continue;
	    }
	    if (my->turn && mv.x == -1 && mv.y == -1) {
		outmsg("對方悔棋");
		tick = hislasttick;
		HO_undo(ku, &mv);
		my->turn = 0;
		scr_need_redraw = 1;
		continue;
	    }
	    if (!my->turn) {
		int win;
		win = chkmv(ku, &mv, he, he == BBLACK);
		HO_add(&mv);
		hislasttick = tick;
		tick = now + MAX_TIME;
		ku[(int)mv.x][(int)mv.y] = he;
		bell();
		BGOTO(mv.x, mv.y);
		outs(bw_chess[he - 1]);
		redoln();

		if (win) {
		    outmsg(win == 1 ? "對方贏了!" : "對方禁手");
		    if (win != 1) {
			cuser.five_lose--;
			cuser.five_win++;
			my->five_win++;
			passwd_update(usernum, &cuser);
		    } else
			my->five_lose++;
		    break;
		}
		my->turn = 1;
	    }
	    scr_need_redraw = 1;
	    continue;
	}
	if (my->turn) {
	    if (gomo_key(ku, fd, ch, &mv))
		my->turn = 0;
	    else
		continue;

	    if (!my->turn) {
		int win;
		HO_add(&mv);
		BGOTO(mv.x, mv.y);
		outs(bw_chess[me - 1]);
		redoln();
		win = chkmv(ku, &mv, me, me == BBLACK);
		ku[(int)mv.x][(int)mv.y] = me;
		mylasttick = tick;
		tick = now + MAX_TIME;	/* 倒數 */
		lastcount = MAX_TIME;
		if (send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		    break;
		if (win) {
		    outmsg(win == 1 ? "我贏囉~~" : "禁手輸了");
		    if (win == 1) {
			cuser.five_lose--;
			cuser.five_win++;
			my->five_win++;
			passwd_update(usernum, &cuser);
		    } else
			my->five_lose++;
		    break;
		}
		move(draw_photo ? 19 : 15, 40);
		clrtoeol();
	    }
	    scr_need_redraw = 1;
	}
    }
    add_io(0, 0);
    close(fd);

    igetch();
    if (v > pool) {
	char            ans[4];

	getdata(19, 0, "要保留本局成棋譜嗎?(y/N)", ans, sizeof(ans), LCECHO);
	if (*ans == 'y')
	    HO_log(pool, my->mateid);
    }
    return 0;
}
