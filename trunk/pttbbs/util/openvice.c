/* $Id: openvice.c,v 1.1 2002/03/07 15:13:46 in2 Exp $ */
/* µo²¼¶}¼ú¤pµ{¦¡ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "util.h"

#define VICE_SHOW  BBSHOME "/etc/vice.show1"
#define VICE_BINGO BBSHOME "/etc/vice.bingo"
#define VICE_NEW   "vice.new"
#define VICE_DATA  "vice.data"
#define MAX_BINGO  99999999

int main()
{
    char TABLE[5][3] =
    {"¤@", "¤G", "¤T", "¥|", "¤­"};

    int i = 0, bingo, base = 0;


    FILE *fp = fopen(VICE_SHOW, "w"), *fb = fopen(VICE_BINGO, "w");

    extern struct utmpfile_t *utmpshm;
    resolve_utmp();

    srand(utmpshm->number);

    if (!fp || !fb )
	perror("error open file");


    bingo = rand() % MAX_BINGO;
    fprintf(fp, "%1c[1;33m²Î¤@µo²¼¤¤¼ú¸¹½X[m\n", ' ');
    fprintf(fp, "%1c[1;37m================[m\n", ' ');
    fprintf(fp, "%1c[1;31m¯S§O¼ú[m: [1;31m%08d[m\n\n", ' ', bingo);
    fprintf(fb, "%d\n", bingo);

    while (i < 5)
    {
	bingo = (base + rand()) % MAX_BINGO;
	fprintf(fp, "%1c[1;36m²Ä%s¼ú[m: [1;37m%08d[m\n", ' ', TABLE[i], bingo);
	fprintf(fb, "%08d\n", bingo);
	i++;
    }
    fclose(fp);
    fclose(fb);
    return 0;
}
