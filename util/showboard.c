/* $Id$ */
/* 看板一覽表(sorted) */
#include "bbs.h"

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
	printf("Usage:\t%s .BRD [MAXUSERS]\n", argv[0]);
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
