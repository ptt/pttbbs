/* $Id$ */
#include "bbs.h"
extern SHM_t   *SHM;

void usage(void)
{
    fprintf(stderr,
	    "usage: hotbard [-t topn]\n");
}

struct bs {
    int     nusers;
    boardheader_t *b;
} *brd;

int main(int argc, char **argv)
{
    int     ch, topn = 20, i, nbrds, j, k, nusers;

    chdir(BBSHOME);
    while( (ch = getopt(argc, argv, "t:h")) != -1 )
	switch( ch ){
	case 't':
	    topn = atoi(optarg);
	    if( topn <= 0 ){
		usage();
		return 1;
	    }
	    break;
	case 'h':
	default:
	    usage();
	    return 1;
	}

    attach_SHM();
    brd = (struct bs *)malloc(sizeof(struct bs) * topn);
    brd[0].b = &SHM->bcache[0];
    brd[0].nusers = brd[0].b->brdname[0] ? brd[0].b->nuser : 0;
    nbrds = 1;

    for( i = 1 ; i < MAX_BOARD ; ++i )
	if( nbrds != topn ||
	    (SHM->bcache[i].brdname[0] &&
	     SHM->bcache[i].nuser > brd[nbrds - 1].nusers) ){

	    nusers = SHM->bcache[i].nuser; // no race ?
	    for( k = nbrds - 2 ; k >= 0 ; --k )
		if( brd[k].nusers > nusers )
		    break;

	    if( (k + 1) < nbrds && (k + 2) < topn )
		for( j = nbrds - 1 ; j >= k + 1 ; --j )
		    brd[j] = brd[j - 1];
	    brd[k + 1].nusers = nusers;
	    brd[k + 1].b = &SHM->bcache[i];

	    if( nbrds < topn )
		++nbrds;
	}

    for( i = 0 ; i < nbrds ; ++i )
	printf("%05d|%-12s|%s\n",
	       brd[i].nusers, brd[i].b->brdname, brd[i].b->title);
    return 0;
}
