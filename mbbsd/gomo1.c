/* $Id: gomo1.c,v 1.4 2002/07/21 09:26:02 in2 Exp $ */
#include "bbs.h"

#define QCAST   int (*)(const void *, const void *)

/* pattern and advance map */

static int
intrevcmp(const void *a, const void *b)
{
    return (*(int *)b - *(int *)a);
}

/* x,y: 0..BRDSIZ-1 ; color: CBLACK,CWHITE ; dx,dy: -1,0,+1 */
static int
gomo_getindex(int x, int y, int color, int dx, int dy)
{
    int             i, k, n;
    for (n = -1, i = 0, k = 1; i < 5; i++, k <<= 1) {
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

/* x,y: 0..BRDSIZ-1 ; color: CBLACK,CWHITE ; limit:1,0 ; dx,dy: 0,1 */
static int
dirchk(int x, int y, int color, int limit, int dx, int dy)
{
    int             le, ri, loc, style = 0;

    le = gomo_getindex(x, y, color, -dx, -dy);
    ri = gomo_getindex(x, y, color, dx, dy);

    loc = (le > ri) ? (((le * (le + 1)) >> 1) + ri) :
	(((ri * (ri + 1)) >> 1) + le);

    style = pat[loc];

    if (limit == 0)
	return (style & 0x0f);

    style >>= 4;

    if ((style == 3) || (style == 2)) {
	int             i, n = 0, tmp, nx, ny;

	n = adv[loc >> 1];

	((loc & 1) == 0) ? (n >>= 4) : (n &= 0x0f);

	ku[x][y] = color;

	for (i = 0; i < 2; i++) {
	    if ((tmp = (i == 0) ? (-(n >> 2)) : (n & 3)) != 0) {
		nx = x + (le > ri ? 1 : -1) * tmp * dx;
		ny = y + (le > ri ? 1 : -1) * tmp * dy;

		if ((dirchk(nx, ny, color, 0, dx, dy) == 0x06) &&
		    (chkwin(getstyle(nx, ny, color, limit), limit) >= 0))
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
int
getstyle(int x, int y, int color, int limit)
{
    int             i, j, dir[4], style;

    if ((x < 0) || (x >= BRDSIZ) || (y < 0) || (y >= BRDSIZ))
	return 0x0f;
    if (ku[x][y] != BBLANK)
	return 0x0d;

    for (i = 0; i < 4; i++)
	dir[i] = dirchk(x, y, color, limit, i ? (i >> 1) : -1, i ? (i & 1) : 1);

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
