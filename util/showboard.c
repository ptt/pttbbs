/* $Id$ */
/* 看板一覽表(sorted) */
#include "bbs.h"

boardheader_t allbrd[MAX_BOARD];

int board_cmp(boardheader_t *a, boardheader_t *b)
{
    return (strcasecmp(a->brdname, b->brdname));
}


int main(int argc, char *argv[])
{
    int inf, i = 0, detail_i = 0, count;

    if (argc < 2) {
	printf("Usage:\t%s .BRD [bid]\n", argv[0]);
	exit(1);
    }


    inf = open(argv[1], O_RDONLY);
    if (inf == -1) {
	printf("error open file\n");
	exit(1);
    }

/* read in all boards */

    i = 0;
    memset(allbrd, 0, MAX_BOARD * sizeof(boardheader_t));
    while (read(inf, &allbrd[i], sizeof(boardheader_t)) == sizeof(boardheader_t)) {
	if (allbrd[i].brdname[0] ) {
	    i++;
	}
    }
    close(inf);

/* sort them by name */
    count = i;
#if 0
    qsort(allbrd, count, sizeof(boardheader_t), board_cmp);
#endif

/* write out the target file */

    if (argc > 2) {
	detail_i = atoi(argv[2]);
	if (detail_i < 1 || detail_i > count)
	    detail_i = -1;
    } else {
	detail_i = -1;
    }

    if (detail_i < 0) {
	printf(
		"     看板名稱     板主                     類別   中文敘述\n"
		"     -----------------------------------------------------------------\n");
	for (i = 0; i < count; i++) {
	    printf("%4d %-13s%-25.25s%s\n", i+1, allbrd[i].brdname, allbrd[i].BM, allbrd[i].title);
	}
    } else {
	/* print details */
	boardheader_t b = allbrd[detail_i - 1];
	printf("brdname(bid):\t%s\n", b.brdname);
	printf("title:\t%s\n", b.title);
	printf("BM:\t%s\n", b.BM);
	printf("brdattr:\t%08x\n", b.brdattr);
	printf("post_limit_posts:\t%d\n", b.post_limit_posts);
	printf("post_limit_regtime:\t%d\n", b.post_limit_regtime);
	printf("level:\t%d\n", b.level);
	printf("gid:\t%d\n", b.gid);
	printf("parent:\t%d\n", b.parent);
	printf("childcount:\t%d\n", b.childcount);
	printf("nuser:\t%d\n", b.nuser);

	printf("next[0]:\t%d\n", b.next[0]);
	printf("next[1]:\t%d\n", b.next[1]);
	printf("firstchild[0]:\t%d\n", b.firstchild[0]);
	printf("firstchild[1]:\t%d\n", b.firstchild[1]);
	/* traverse to find my children */
	printf("---- children: ---- \n");
	for (i = 0; i < count; i++) {
	    if(allbrd[i].gid == detail_i)
		printf("%4d %-13s%-25.25s%s\n", 
			i+1, allbrd[i].brdname, 
			allbrd[i].BM, allbrd[i].title);
	}
    }
    return 0;
}
