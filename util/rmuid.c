/* $Id$ */
#include <stdio.h>

#if 0
#include "bbs.h"

extern int numboards;
extern boardheader_t *bcache;

int getbidofuid(int uid)
{
  register int n; boardheader_t *bh;
 if(!uid) return 1;
 for (n=0;n<numboards;n++)
    { 
	bh = &bcache[n]; 
	/* unused == uid!? don't use a variable claimed as 'unused!' */
	if(bh->unused == uid)
		 return n+1;
    }
 return 1;
}
#endif

int main(int argc, char* argv[])
{
    fprintf(stderr, "This utility is marked as broken.\n");

#if 0
    //struct stat  st;
    int n;
    boardheader_t bh;
    //char pathname[1024];

    attach_SHM();
    resolve_boards();
    for (n=0;n<numboards;n++){
	memcpy( &bh, &bcache[n], sizeof(bh));
	bh.gid=getbidofuid(bh.gid);
	//printf("%14.14s%14.14s \r\n",bh.brdname, bh.title);
	substitute_record("BOARDS.bid", &bh, sizeof(bh), n+1);
    }
#endif
    return 0;
}







