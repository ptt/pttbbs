/* $Id: horoscope.c,v 1.2 2002/06/06 21:34:14 in2 Exp $ */
#include "bbs.h"

struct userec_t cuser;

int main() {
    int i, j, k;
    FILE *fp;
    int max, item, maxhoroscope;

    int act[12];

    char *name[13] =
    {"¨d¦Ï",
     "ª÷¤û",
     "Âù¤l",
     "¥¨ÃÉ",
     "·à¤l",
     "³B¤k",
     "¤Ñ¯¯",
     "¤ÑÃÈ",
     "®g¤â",
     "¼¯½~",
     "¤ô²~",
     "Âù³½",
     ""
    };
    char *blk[10] =
    {
	"  ", "¢j", "¢k", "¢l", "¢m",
	"¢n", "¢o", "¢p", "¢i", "¢i",
    };

    memset(act, 0, sizeof(act));
    if(passwd_mmap())
	exit(1);
    for(k = 1; k <= MAX_USERS; k++) {
	passwd_query(k, &cuser);
	if(!cuser.userid[0])
	    continue;
	switch (cuser.month)
	{
	case 1:
	    if (cuser.day <= 19)
		act[9]++;
	    else
		act[10]++;
	    break;
	case 2:
	    if (cuser.day <= 18)
		act[10]++;
	    else
		act[11]++;
	    break;
	case 3:
	    if (cuser.day <= 20)
		act[11]++;
	    else
		act[0]++;
	    break;
	case 4:
	    if (cuser.day <= 19)
		act[0]++;
	    else
		act[1]++;
	    break;
	case 5:
	    if (cuser.day <= 20)
		act[1]++;
	    else
		act[2]++;
	    break;
	case 6:
	    if (cuser.day <= 21)
		act[2]++;
	    else
		act[3]++;
	    break;
	case 7:
	    if (cuser.day <= 22)
		act[3]++;
	    else
		act[4]++;
	    break;
	case 8:
	    if (cuser.day <= 22)
		act[4]++;
	    else
		act[5]++;
	    break;
	case 9:
	    if (cuser.day <= 22)
		act[5]++;
	    else
		act[6]++;
	    break;
	case 10:
	    if (cuser.day <= 23)
		act[6]++;
	    else
		act[7]++;
	    break;
	case 11:
	    if (cuser.day <= 22)
		act[7]++;
	    else
		act[8]++;
	    break;
	case 12:
	    if (cuser.day <= 21)
		act[8]++;
	    else
		act[9]++;
	    break;
	}
    }

    for (i = max = maxhoroscope = 0; i < 12; i++)
    {
	if (act[i] > max)
	{
	    max = act[i];
	    maxhoroscope = i;
	}
    }

    item = max / 30 + 1;

    if ((fp = fopen(BBSHOME"/etc/horoscope", "w")) == NULL)
    {
	printf("cann't open etc/horoscope\n");
	return 1;
    }

    for (i = 0; i < 12; i++)
    {
	fprintf(fp, " [1;37m%s®y [0;36m", name[i]);
	for (j = 0; j < act[i] / item; j++)
	{
	    fprintf(fp, "%2s", blk[9]);
	}
    /* ¬°¤F­è¦n¤@­¶ */
	if (i != 11)
	    fprintf(fp, "%2s [1;37m%d[m\n\n", blk[(act[i] % item) * 10 / item],
		    act[i]);
	else
	    fprintf(fp, "%2s [1;37m%d[m\n", blk[(act[i] % item) * 10 / item],
		    act[i]);
    }
    fclose(fp);
    return 0;
}
