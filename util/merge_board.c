/* $Id$ */
#include "bbs.h"

typedef struct hash_t {
    char *brdname;
    struct hash_t *next;
} hash_t;

FILE *fout;
hash_t *hash_tbl[65536];
int counter;

void usage() {
    fprintf(stderr, "Usage:\n\n"
	    "merge_board <output file> [input file1] [input file2] ...\n");
}

unsigned int string_hash(unsigned char *s) {
    unsigned int v=0;
    
    while(*s) {
	v = (v << 8) | (v >> 24);
	v ^= toupper(*s++);	/* note this is case insensitive */
    }
    return (v * 2654435769U) >> (32 - 16);
}

int is_exist(char *brdname) {
    int i;
    hash_t *n;

    i = string_hash(brdname);
    for(n = hash_tbl[i]; n != NULL; n = n->next)
	if(strcasecmp(brdname, n->brdname) == 0)
	    return 1;
    return 0;
}

void add_hash(char *brdname) {
    int i;
    hash_t *n;
    
    i = string_hash(brdname);
    
    n = malloc(sizeof(*n));
    n->brdname = strdup(brdname);
    n->next = hash_tbl[i];
    hash_tbl[i] = n;
}

void merge_board(boardheader_t *b) {
    if(!is_exist(b->brdname)) {
	fwrite(b, sizeof(*b), 1, fout);
	add_hash(b->brdname);
	++counter;
    }
}

void merge_file(char *fname) {
    FILE *fin;
    boardheader_t b;
    
    if((fin = fopen(fname, "r")) == NULL) {
	perror(fname);
	return;
    }
    
    counter = 0;
    while(fread(&b, sizeof(b), 1, fin) == 1)
	if(b.brdname[0])
	    merge_board(&b);
    
    printf("merge from %s: %d boards\n", fname, counter);
    
    fclose(fin);
}

int main(int argc, char **argv) {
    int i;
    
    if(argc < 2) {
	usage();
	return 1;
    }
    
    bzero(hash_tbl, sizeof(hash_tbl));
    
    if((fout = fopen(argv[1], "w")) == NULL) {
	perror(argv[1]);
	return 2;
    }
    
    for(i = 2; i < argc; ++i)
	merge_file(argv[i]);
    
    fclose(fout);
    printf("Done\n");
    
    return 0;
}
