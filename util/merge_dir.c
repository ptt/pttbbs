/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

void usage() {
    fprintf(stderr, "Usage:\n\n"
	    "merge_dir <dir1> <dir2> \n");
}


fileheader_t *fh;
int dir_cmp(const void *a, const void *b)
{
    return (atoi( &((fileheader_t *)a)->filename[2] ) -
            atoi( &((fileheader_t *)b)->filename[2] ));
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
    append_record("DIR.merge", &fh[i-1], sizeof(fileheader_t)); 
    
    return 0;
}
