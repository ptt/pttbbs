/* $Id: topsong.c,v 1.2 2002/06/04 13:08:34 in2 Exp $ */
#include "bbs.h"

#define MAX_SONGS 300
#define QCAST     int (*)(const void *, const void *)

typedef struct songcmp_t {
    char name[100];
    char cname[100];
    long int count;
} songcmp_t;

static long int totalcount=0;

static int count_cmp(songcmp_t *b, songcmp_t *a) {
    return (a->count - b->count);
}

int topsong() {
    more(FN_TOPSONG,YEA);
    return 0;
}
     
static int strip_blank(char *cbuf, char *buf) {
    for(; *buf; buf++)
	if(*buf != ' ')
	    *cbuf++ = *buf;	  
    *cbuf = 0;
    return 0;
}

void sortsong() {
    FILE *fo, *fp = fopen(BBSHOME "/" FN_USSONG, "r");
    songcmp_t songs[MAX_SONGS + 1];
    int n;
    char buf[256], cbuf[256];

    memset(songs , 0, sizeof(songs));
    if(!fp) return;
    if(!(fo = fopen(FN_TOPSONG,"w"))) {
	fclose(fp);
	return;
    }
    
    totalcount = 0;
    while(fgets(buf, 200, fp)) {
	strtok(buf, "\n\r");
	strip_blank(cbuf, buf);
	if(!cbuf[0] || !isprint2(cbuf[0]))
	    continue;
	
	for(n = 0; n < MAX_SONGS && songs[n].name[0]; n++)
	    if(!strcmp(songs[n].cname,cbuf))
		break;
	strcpy(songs[n].name, buf);
	strcpy(songs[n].cname, cbuf);
	songs[n].count++;
	totalcount++;
    }
    qsort(songs, MAX_SONGS, sizeof(songcmp_t), (QCAST)count_cmp);
    fprintf(fo,
	    "    \033[36m──\033[37m名次\033[36m──────\033[37m歌"
	    "  名\033[36m───────────\033[37m次數\033[36m"
	    "──\033[32m共%ld次\033[36m──\033[m\n", totalcount);
    for(n = 0; n < 100 && songs[n].name[0]; n++) {
        fprintf(fo, "      %5d. %-38.38s %4ld \033[32m[%.2f]\033[m\n", n + 1,
		songs[n].name, songs[n].count,
		(float)songs[n].count/totalcount);
    }
    fclose(fp);
    fclose(fo);
}
