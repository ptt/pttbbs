/* $Id$ */
#include "bbs.h"
#include <err.h>

extern SHM_t *SHM;

int main(int argc, char **argv)
{
    int     sfd, index, i;
    attach_SHM();
    if( (sfd = toconnect(UTMPD_ADDR)) < 0 ) {
	printf("connect fail\n");
	return 1;
    }

    index = -1;
    towrite(sfd, &index, sizeof(index));
    for( i = 0 ; i < USHM_SIZE ; ++i )
	if( towrite(sfd, &SHM->uinfo[i].uid, sizeof(SHM->uinfo[i].uid)) < 0 ||
	    towrite(sfd, SHM->uinfo[i].myfriend,
		    sizeof(SHM->uinfo[i].myfriend)) < 0                       ||
	    towrite(sfd, SHM->uinfo[i].reject,
		    sizeof(SHM->uinfo[i].reject)) < 0                       ){
	    fprintf(stderr, "sync error %d\n", i);
	}
    return 0;
}
