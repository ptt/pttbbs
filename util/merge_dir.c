/* $Id: merge_dir.c 1096 2003-08-15 06:13:29Z victor $ */
#define _UTIL_C_
#include "bbs.h"

void usage() {
    fprintf(stderr, "Usage:\n\n"
	    "merge_dir <dir1> <dir2> \n");
}


fileheader_t *fh;
int dir_cmp(fileheader_t *a, fileheader_t *b)
{
 return  strncasecmp(a->filename, b->filename, 12);
}

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
    qsort(fh, pn+sn, sizeof(fileheader_t), dir_cmp);
    for(i=1; i<pn+sn; i++ )
      {
         if(!fh[i].title[0] || !fh[i].filename[0]) continue;
         if( strcmp(fh[i-1].filename, fh[i].filename))
           append_record("DIR.merge", &fh[i-1], sizeof(fileheader_t)); 
         else
           fh[i].filemode |= fh[i-1].filemode;
      }
    
    return 0;
}
