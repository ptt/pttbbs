/* $Id$ */

/* 'mandex -h' to help */

#include "bbs.h"
#ifndef MAXPATHLEN
#define MAXPATHLEN  1024
#endif

typedef struct
{
    char bname[40];
    int ndir, nfile, k;
} boardinfo;

extern  int     numboards;
extern  boardheader_t *bcache;
boardinfo       board[MAX_BOARD], *biptr;
char    color[4][10] = {"[1;33m", "[1;32m", "[1;36m", "[1;37m"};
char    fn_index[] = ".index";
char    fn_new[] = ".index.new";
char    index_title[] = "¡· ºëµØ°Ï¥Ø¿ý¯Á¤Þ";
char    topdir[128], pgem[512], pndx[512];
FILE    *fndx;
int     ndir, nfile, index_pos, k;
int     nSorted, nb;

int sortbyname(const void *a, const void *b)
{
    return strcmp(((boardinfo*)b)->bname, ((boardinfo*)a)->bname);
}

int k_cmp(b, a)
    boardinfo *b, *a;
{
    return ((a->k / 100 + a->ndir + a->nfile) - (b->k / 100 + b->ndir + b->nfile));
}

/* visit the hierarchy recursively */

void
mandex(level, num_header, fpath)
    int level;
    char *fpath, *num_header;
{
    FILE *fgem;
    char *fname, buf[256];
    struct stat st;
    int count;
    fileheader_t fhdr;

    fgem = fopen(fpath, "r+");
    if (fgem == NULL)
	return;

    fname = strrchr(fpath, '.');
    if (!level){
        printf("%s\r\n",fpath);
	strcpy(pgem, fpath);

	strcpy(fname, fn_new);
	fndx = fopen(fpath, "w");
	if (fndx == NULL){
	    fclose(fgem);
	    return;
	}
	fprintf(fndx, "[1;32m§Ç¸¹\t\t\tºëµØ°Ï¥DÃD[m\n"
		"[36m¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w"
		"¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w[m\n");
	strcpy(pndx, fpath);
	ndir = nfile = 0;
	index_pos = -1;
    }

    count = 0;
    while (fread(&fhdr, sizeof(fhdr), 1, fgem) == 1){
	strcpy(fname, fhdr.filename);
        if (!fname[0]) continue;
	if (!level && !strncmp(fhdr.title, index_title, strlen(index_title))
	    && index_pos < 0){
	    index_pos = count;
	    unlink(fpath);
	}
	st.st_size = 0;
	stat(fpath, &st);

	sprintf(buf, "%.*s%s%3d. %s [m\n",
		11 * level, num_header, color[level % 4], ++count, fhdr.title);
	                                                              /* Ptt */
	fputs(buf, fndx);
	if (dashd(fpath)){
	    ++ndir;
	    /* I can't find the code to change title? */
	    if (*fhdr.title != '#' && level < 10 && 
		(fhdr.filemode&(FILE_BM|FILE_HIDE))==0){
		strcat(fpath, "/.DIR");
		mandex(level + 1, buf, fpath);
	    }
	}
	else
	    ++nfile;
    }

    if (!level){
	char lpath[MAXPATHLEN];

	fclose(fndx);
	strcpy(fname, fn_index);
	rename(pndx, fpath);
	strcpy(pndx, fpath);

	sprintf(buf, "%s.new", pgem);
	if (index_pos >= 0 || (fndx = fopen(buf, "w"))){
	    fname[-1] = 0;
	    stamplink(fpath, &fhdr);
	    unlink(fpath);
	    strcpy(fhdr.owner, "¨C¤Ñ¦Û°Ê§ó·s");
	    sprintf(lpath, "%s/%s", topdir, pndx);
	    st.st_size = 0;
	    stat(lpath, &st);
	    sprintf(fhdr.title, "%s (%.1fk)", index_title, st.st_size / 1024.);
	    k = st.st_size;	/* Ptt */
	    printf("(%s)[%dK]", fpath, k);
	    symlink(lpath, fpath);
	    if (index_pos < 0){
		fwrite(&fhdr, sizeof(fhdr), 1, fndx);
		rewind(fgem);
		while (fread(&fhdr, sizeof(fhdr), 1, fgem) == 1)
		    fwrite(&fhdr, sizeof(fhdr), 1, fndx);
		fclose(fndx);
		fclose(fgem);
		rename(buf, pgem);
	    }
	    else{
		fseek(fgem, index_pos * sizeof(fhdr), 0);
		fwrite(&fhdr, sizeof(fhdr), 1, fgem);
		fclose(fgem);
	    }
	    return;
	}
    }
    fclose(fgem);
}


int main(int argc, char* argv[])
{
    boardheader_t *bptr = NULL;
    DIR     *dirp;
    struct  dirent *de;
    int     ch, n, place = 0, fd, i;
    char    checkrebuild = 0, *fname, fpath[MAXPATHLEN];
    char    dirs[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		      'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
		      'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
		      'z', 'x', 'c', 'v', 'b', 'n', 'm',
		      'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
		      'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
		      'Z', 'X', 'C', 'V', 'B', 'N', 'M', NULL};

    nice(10);
    while( (ch = getopt(argc, argv, "xh")) != -1 ){
	switch( ch ){
	case 'x':
	    checkrebuild = 1;
	    break;
	case 'h':
	    printf("NAME                                    \n"
		   "     mandex - ºëµØ°Ï¯Á¤Þµ{¦¡ (man index)\n\n"
		   "SYNOPSIS                                \n"
		   "     mandex [-x] [board]                \n\n"
		   "DESCRIPTION                             \n"
		   "ºëµØ°Ï¯Á¤Þ (man index)                  \n\n"
		   "-x    ¥u¦³§t¦³ .rebuildªº¥Ø¿ý¤~­«»s     \n"
		   "board ¥þ³¡ªºªO (default to all)         \n\n");
	    return 0;
	}
    }

    argc -= optind;
    argv += optind;

    attach_SHM();
    resolve_boards();
/*
    if( argc == 0 ){
	puts("Creating the whole index...");
        chdir(strcpy(topdir, BBSHOME));
        strcpy(fpath, "man/.DIR");
        mandex(0, "", fpath);
    }
*/
    chdir(strcpy(topdir, BBSHOME "/man/boards"));

    if( argc == 1 ){
        sprintf(fpath, "%c/%s/.DIR", argv[0][0], argv[0]);
        mandex(0, "", fpath);
	return 0;
    }

    /* process all boards */
    if( checkrebuild &&
	(fd = open(BBSHOME "/man/.rank.cache", O_RDONLY)) >= 0 ){
	read(fd, board, sizeof(board));
	close(fd);
	qsort(board, MAX_BOARD, sizeof(boardinfo), sortbyname);
	for( nb = 0 ; board[nb].bname[0] != 0 ; ++nb )
	    ;
	nSorted = nb;
    }
    else{
	memset(board, 0, sizeof(board));
	checkrebuild = 0;
	nSorted = nb = 0;
    }

    for( i = 0 ; dirs[i] != NULL ; ++i ){
	sprintf(topdir, BBSHOME "/man/boards/%c", dirs[i]);
	chdir(topdir);
	if(!(dirp = opendir(topdir))) {
	    printf("## unable to enter [man/boards]\n");
	    exit(-1);
	}
    
	while((de = readdir(dirp))){
	    fname = de->d_name;
	    ch = fname[0];
	    if (ch != '.'){
		k = 0;
		if( checkrebuild ){
		    sprintf(fpath, "%s/.rebuild", fname);
		    if( access(fpath, 0) < 0 ){
			printf("skip no modify board %s\n", fname);
			continue;
		    }
		    unlink(fpath);
		}
		sprintf(fpath, "%s/.DIR", fname);
		mandex(0, "", fpath);
		printf("%-14sd: %d\tf: %d\n", fname, ndir, nfile); /* report */
		if( k ){
		    if( !(biptr = bsearch(fname, board, nSorted,
					  sizeof(boardinfo), sortbyname))){
			biptr = &board[nb];
			++nb;
		    }
		    strcpy(biptr->bname, fname);
		    biptr->ndir = ndir;
		    biptr->nfile = nfile;
		    biptr->k = k;
		}
	    }
	}
	closedir(dirp);
    }
    
    qsort(board, nb, sizeof(boardinfo), k_cmp);
    unlink(BBSHOME "/man/.rank.cache");
    if( (fd = open(BBSHOME "/man/.rank.cache",
		   O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0660)) >= 0 ){
	write(fd, board, sizeof(board));       
	close(fd);
    }
    if (!(fndx = fopen(BBSHOME "/etc/topboardman", "w")))
	exit(0);
    
    fprintf(fndx, "[1;44m±Æ¦W[47;30m        ¬Ý ªO  ¥Ø¿ý¼Æ   ÀÉ®×¼Æ"
	    "     byte¼Æ [30m  Á` ¤À     ªO   ¥D          [m\n");
    
    for (ch = 0; ch < nb; ch++){
	for (n = 0; n < numboards; n++){
	    bptr = &bcache[n];
	    if (!strcmp(bptr->brdname, board[ch].bname))
		break;
	}
	if (n >= numboards ||
	    (bptr->brdattr & (BRD_BAD | BRD_NOCOUNT))) 
	    continue;

	/* ªO¥D³]©w¤£¦C¤J°O¿ý */
	if (bptr->brdattr & BRD_HIDE && !(bptr->brdattr & BRD_BMCOUNT))
	    continue;

	if (board[ch].ndir + board[ch].nfile < 5)
	    break;
	fprintf(fndx, "%3d.[33m%15s[m %5d %7d %10d   %6d [31m%-24.24s[m\n",
		++place,
		board[ch].bname,
		board[ch].ndir, board[ch].nfile, board[ch].k
		,board[ch].k / 100 + board[ch].nfile + board[ch].ndir
		,bptr->BM);
    }
    fclose(fndx);
    return 0;
}
