/* $Id: post.c,v 1.4 2002/06/06 21:34:14 in2 Exp $ */
#include "bbs.h"

void keeplog(FILE *fin, char *fpath, char *board, char *title, char *owner) {
    fileheader_t fhdr;
    char genbuf[256], buf[512];
    FILE *fout;
    int bid;
    
    sprintf(genbuf, BBSHOME "/boards/%c/%s", board[0], board);
    stampfile(genbuf, &fhdr);
    
    if(!(fout = fopen(genbuf, "w"))) {
	perror(genbuf);
	return;
    }
    
    while(fgets(buf, 512, fin))
	fputs(buf, fout);
    
    fclose(fin);
    fclose(fout);
    
    strncpy(fhdr.title, title, sizeof(fhdr.title) - 1);
    fhdr.title[sizeof(fhdr.title) - 1] = '\0';
    
    strcpy(fhdr.owner, owner);
    sprintf(genbuf, BBSHOME "/boards/%c/%s/.DIR", board[0], board);
    append_record(genbuf, &fhdr, sizeof(fhdr));
    if((bid = getbnum(board)) > 0)
	touchbtotal(bid);

}

int main(int argc, char **argv) {
    FILE *fp;
    
    resolve_boards();
    if(argc != 5) {
	printf("usage: %s <board name> <title> <owner> <file>\n", argv[0]);
	return 0;
    }
    
    if(strcmp(argv[4], "-") == 0)
	fp = stdin;
    else {
	fp = fopen(argv[4], "r");
	if(!fp) {
	    perror(argv[4]);
	    return 1;
	}
    }
    keeplog(fp, argv[4], argv[1], argv[2], argv[3]);
    return 0;
}
