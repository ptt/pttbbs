/* $Id$ */
#include "bbs.h"

#define VICE_PLAY   BBSHOME "/etc/vice/vice.play"
#define VICE_DATA   "vice.new"
#define VICE_BINGO  BBSHOME "/etc/vice.bingo"
#define VICE_SHOW   BBSHOME "/etc/vice.show"
#define VICE_LOST   BBSHOME "/etc/vice/vice.lost"
#define VICE_WIN    BBSHOME "/etc/vice/vice.win"
#define VICE_END    BBSHOME "/etc/vice/vice.end"
#define VICE_NO     BBSHOME "/etc/vice/vice.no"
#define MAX_NO_PICTURE   2
#define MAX_WIN_PICTURE  2
#define MAX_LOST_PICTURE 3
#define MAX_END_PICTURE  5

static int
vice_load(char tbingo[6][15])
{
    FILE           *fb = fopen(VICE_BINGO, "r");
    char            buf[16], *ptr;
    int             i = 0;
    if (!fb)
	return -1;
    bzero((char *)tbingo, 6*15);
    while (i < 6 && fgets(buf, 15, fb)) {
	if ((ptr = strchr(buf, '\n')))
	    *ptr = 0;
	strcpy(tbingo[i++], buf);
    }
    fclose(fb);
    return 0;
}

static int
check(char tbingo[6][15], char *data)
{
    int             i = 0, j;

    if (!strcmp(data, tbingo[0]))
	return 8;

    for (j = 8; j > 0; j--)
	for (i = 1; i < 6; i++)
	    if (!strncmp(data + 8 - j, tbingo[i] + 8 - j, j))
		return j - 1;
    return 0;
}
/* TODO Ptt:showfile ran_showfile more 三者要合 */
static int
ran_showfile(int y, int x, char *filename, int maxnum)
{
    FILE           *fs;
    char            buf[512];

    bzero(buf, sizeof(buf));
    snprintf(buf, sizeof(buf), "%s%d", filename, rand() % maxnum + 1);
    if (!(fs = fopen(buf, "r"))) {
	move(10, 10);
	prints("can't open file: %s", buf);
	return 0;
    }
    move(y, x);

    while (fgets(buf, sizeof(buf), fs))
	outs(buf);

    fclose(fs);
    return 1;
}

static int
ran_showmfile(char *filename, int maxnum)
{
    char            buf[256];

    snprintf(buf, sizeof(buf), "%s%d", filename, rand() % maxnum + 1);
    return more(buf, YEA);
}


int
vice_main()
{
    FILE           *fd;
    char            tbingo[6][15];
    char            buf_data[256],
                    serial[16], ch[2], *ptr;
    int             TABLE[] = {0, 10, 200, 1000, 4000, 10000, 40000, 100000, 200000};
    int             total = 0, money, i = 4, j = 0;

    setuserfile(buf_data, VICE_DATA);
    if (!dashf(buf_data)) {
	ran_showmfile(VICE_NO, MAX_NO_PICTURE);
	return 0;
    }
    if (vice_load(tbingo) < 0)
	return -1;
    clear();
    ran_showfile(0, 0, VICE_PLAY, 1);
    ran_showfile(10, 0, VICE_SHOW, 1);

    if (!(fd = fopen(buf_data, "r")))
	return 0;
    j = 0;
    i = 0;
    move(10, 24);
    clrtoeol();
    outs("這一期的發票號碼");
    // FIXME 當發票多於一行, 開獎的號碼就會被蓋掉
    while (fgets(serial, 15, fd)) {
	if ((ptr = strchr(serial, '\r')))
	    *ptr = 0;
	if (j == 0)
	    i++;
	if( i >= 14 )
	    break;
	move(10 + i, 24 + j);
	outs(serial);
	j += 9;
	j %= 45;
    }
    getdata(8, 0, "按'c'開始對獎了(或是任意鍵離開)): ",
	    ch, sizeof(ch), LCECHO);
    if (ch[0] != 'c' || lockutmpmode(VICE, LOCK_MULTI)) {
	fclose(fd);
	return 0;
    }
    showtitle("發票對獎", BBSNAME);
    rewind(fd);
    while (fgets(serial, 15, fd)) {
	if ((ptr = strchr(serial, '\n')))
	    *ptr = 0;
	money = TABLE[check(tbingo, serial)];
	total += money;
	prints("%s 中了 %d\n", serial, money);
    }
    pressanykey();
    if (total > 0) {
	ran_showmfile(VICE_WIN, MAX_WIN_PICTURE);
	move(22, 0);
	clrtoeol();
	prints("全部的發票中了 %d 塊錢\n", total);
	demoney(total);
    } else
	ran_showmfile(VICE_LOST, MAX_LOST_PICTURE);

    fclose(fd);
    unlink(buf_data);
    pressanykey();
    unlockutmpmode();
    return 0;
}
