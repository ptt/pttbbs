/* $Id: showboard.c,v 1.1 2002/03/07 15:13:46 in2 Exp $ */
/* 看板一覽表(sorted) */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "config.h"
#include "pttstruct.h"

boardheader_t allbrd[MAX_BOARD];

int
board_cmp(a, b)
    boardheader_t *a, *b;
{
    return (strcasecmp(a->brdname, b->brdname));
}


int main(argc, argv)
    int argc;
    char *argv[];
{
    int inf, i, count;

    if (argc < 2)
    {
	printf("Usage:\t%s .BOARDS [MAXUSERS]\n", argv[0]);
	exit(1);
    }


    inf = open(argv[1], O_RDONLY);
    if (inf == -1)
    {
	printf("error open file\n");
	exit(1);
    }

/* read in all boards */

    i = 0;
    memset(allbrd, 0, MAX_BOARD * sizeof(boardheader_t));
    while (read(inf, &allbrd[i], sizeof(boardheader_t)) == sizeof(boardheader_t))
    {
	if (allbrd[i].brdname[0] )
	{
	    i++;
	}
    }
    close(inf);

/* sort them by name */
    count = i;
    qsort(allbrd, count, sizeof(boardheader_t), board_cmp);

/* write out the target file */

    printf(
	"看板名稱     板主                     類別   中文敘述\n"
	"----------------------------------------------------------------------\n");
    for (i = 0; i < count; i++)
    {
	printf("%-13s%-25.25s%s\n", allbrd[i].brdname, allbrd[i].BM, allbrd[i].title);
    }
    return 0;
}
