/* $Id$ */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"

typedef struct hash_t {
    char *userid;
    struct hash_t *next;
} hash_t;

FILE *fout;
hash_t *hash_tbl[65536];
int counter;

void usage() {
    fprintf(stderr, "Usage:\n\n"
	    "merge_passwd <output file> [input file1] [input file2] ...\n");
}

unsigned int string_hash(unsigned char *s) {
    unsigned int v=0;
    
    while(*s) {
	v = (v << 8) | (v >> 24);
	v ^= toupper(*s++);	/* note this is case insensitive */
    }
    return (v * 2654435769UL) >> (32 - 16);
}

int is_exist(char *userid) {
    int i;
    hash_t *n;

    i = string_hash(userid);
    for(n = hash_tbl[i]; n != NULL; n = n->next)
	if(strcasecmp(userid, n->userid) == 0)
	    return 1;
    return 0;
}

void add_hash(char *userid) {
    int i;
    hash_t *n;
    
    i = string_hash(userid);
    
    n = malloc(sizeof(*n));
    n->userid = strdup(userid);
    n->next = hash_tbl[i];
    hash_tbl[i] = n;
}

void merge_user(userec_t *u) {
    if(!is_exist(u->userid)) {
	fwrite(u, sizeof(*u), 1, fout);
	add_hash(u->userid);
	++counter;
    }
}

void merge_file(char *fname) {
    FILE *fin;
    userec_t u;
    
    if((fin = fopen(fname, "r")) == NULL) {
	perror(fname);
	return;
    }
    
    counter = 0;
    while(fread(&u, sizeof(u), 1, fin) == 1)
	if(u.userid[0])
	    merge_user(&u);
    
    printf("merge from %s: %d users\n", fname, counter);
    
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
