/* $Id: merge_board.c 1096 2003-08-15 06:13:29Z victor $ */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"

void usage() {
    fprintf(stderr, "Usage:\n\n"
	    "merge_dir <dir1> <dir2> \n");
}
int
dir_cmp(fileheader_t *a, fileheader_t *b)
{
    if(!a->filename[0] || !b->filename[0]) return 0;
    return (strcasecmp(a->filename+1, b->filename+1));
}


fileheader_t *fh;

int main(int argc, char **argv) {
    int pn, sn, i;
    
    if(argc < 2) {
	usage();
	return 1;
    }
    
    pn=get_num_records(argv[1], sizeof(fileheader_t)); 
    sn=get_num_records(argv[2], sizeof(fileheader_t)); 
    if(sn <=0 || pn<=0) 
      {
        usage();
        return 1;
      }
    fh = (fileheader_t *)malloc( (pn+sn)*sizeof(fileheader_t));
    get_records(argv[1], fh, sizeof(fileheader_t), 1, pn);
    get_records(argv[2], fh+pn, sizeof(fileheader_t), 1, sn);
    qsort(fh, pn+sn, sizeof(boardheader_t), dir_cmp);
    
    for(i=0; i<pn+sn; i++)
      {
         printf("%s %s %s\n",fh[i].date, fh[i].title, fh[i].filename);
      }
    
    return 0;
}
